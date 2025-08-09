#include "scriptProfiler.hpp"
#include <intercept.hpp>
#include <numeric>
#include <random>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#ifndef __linux__
#include <Windows.h>
#else
#include <signal.h>
#include <limits.h>
#include <unistd.h>
#include <link.h>
#endif
#include "ProfilerAdapter.hpp"
#include "AdapterArmaDiag.hpp"
#ifdef WITH_BROFILER
#include "AdapterBrofiler.hpp"
#include "Event.h"
#endif
#include "AdapterChrome.hpp"
#include "AdapterTracy.hpp"
#include <memory>
#include <string>
#include "NetworkProfiler.hpp"
#include <pointers.hpp>


using namespace intercept;
using namespace std::chrono_literals;
std::chrono::high_resolution_clock::time_point startTime;
static sqf_script_type* GameDataProfileScope_type;
static nlohmann::json json;

scriptProfiler profiler{};
bool instructionLevelProfiling = false;

void diag_log(r_string msg) {
    GProfilerAdapter->addLog(msg);
    sqf::diag_log(msg);
}


class GameDataProfileScope : public game_data {

public:
    class scopeData {
    public:
        scopeData(r_string _name, game_value thisArgs, std::shared_ptr<ScopeInfo> scopeInfo) : name(std::move(_name)) {
            if (!scopeInfo) return;

            scopeTempStorage = GProfilerAdapter->enterScope(scopeInfo);
#ifdef WITH_BROFILER
            GProfilerAdapter->setThisArgs(scopeTempStorage, std::move(thisArgs));
#endif
        }

        scopeData(r_string _name, game_value thisArgs, std::shared_ptr<ScopeInfo> scopeInfo, uint64_t threadID) : name(std::move(_name)) {
            if (!scopeInfo) return;

            scopeTempStorage = GProfilerAdapter->enterScope(scopeInfo, threadID);
#ifdef WITH_BROFILER
            GProfilerAdapter->setThisArgs(scopeTempStorage, std::move(thisArgs));
#endif
        }
        ~scopeData() {
            //static bool stop = false;
            //game_state* gs = (game_state*)0x00007ff73083e450;
            if (scopeTempStorage) //no need to if its nullptr
                GProfilerAdapter->leaveScope(scopeTempStorage);
            //if (stop && name.find("handleStateDefault"sv) != -1) {
            //    std::this_thread::sleep_for(500ms);
            //    __debugbreak();
            //}
        }
        std::shared_ptr<ScopeTempStorage> scopeTempStorage;
        r_string name;
        //sourcedocpos createPos;
    };

    GameDataProfileScope() = default;
    GameDataProfileScope(std::shared_ptr<scopeData>&& _data) noexcept : data(std::move(_data)) {}
    void lastRefDeleted() const override { delete this; }
    const sqf_script_type& type() const override { return *GameDataProfileScope_type; }
    ~GameDataProfileScope() override = default;
    bool get_as_bool() const override { return true; }
    float get_as_number() const override { return 0.f; }
    const r_string& get_as_string() const override { return data->name; }
    game_data* copy() const override { return new GameDataProfileScope(*this); } //#TODO is copying scopes even allowed?!
    r_string to_string() const override { return data->name; }
    //virtual bool equals(const game_data*) const override;
    const char* type_as_string() const override { return "profileScope"; }
    bool is_nil() const override { return false; }
    bool can_serialize() override { return true; }//Setting this to false causes a fail in scheduled and global vars

    serialization_return serialize(param_archive& ar) override {
        game_data::serialize(ar); //This is fake. We can't be serialized. But I don't want errors.
        return serialization_return::no_error;
    }

    std::shared_ptr<scopeData> data;
};

game_data* createGameDataProfileScope(param_archive* ar) {
    //#TODO use armaAlloc
    auto x = new GameDataProfileScope();
    if (ar)
        x->serialize(*ar);
    return x;
}

class GameInstructionProfileScopeStart final : public game_instruction {
public:
    r_string name;
    std::shared_ptr<ScopeInfo> scopeInfo;
    bool cbaCompile = false;

    GameInstructionProfileScopeStart(r_string name_) {  
        if (name_ == "CBA_fnc_compileFunction") cbaCompile = true;
        name = std::move(name_);

    }

    void lastRefDeleted() const override {
        rv_allocator<GameInstructionProfileScopeStart>::destroy_deallocate(const_cast<GameInstructionProfileScopeStart *>(this), 1);
    }

    bool exec(game_state& state, vm_context& ctx) override {
        if (!GProfilerAdapter->isScheduledSupported() &&/*ctx.scheduled || */sqf::can_suspend()) return false;

       
        auto ev = state.get_evaluator();

        //if (!ev->local->variables.is_null(ev->local->variables.get("1scp"sv)))
        //    return false;

        auto data = std::make_shared<GameDataProfileScope::scopeData>(name,
#ifdef WITH_BROFILER
            state.get_local_variable("_this"),
#else
            game_value(),
#endif
            scopeInfo);

        //data->createPos = ctx.get_current_position();
#ifdef WITH_CHROME
        if (GProfilerAdapter->isScheduledSupported() && sqf::can_suspend()) {
            if (auto chromeStorage = std::dynamic_pointer_cast<ScopeTempStorageChrome>(data->scopeTempStorage))
                chromeStorage->threadID = reinterpret_cast<uint64_t>(&ctx);
        }
#endif
        if (cbaCompile)
            GProfilerAdapter->setDescription(data->scopeTempStorage, state.get_local_variable("_this")[1]);


        //if (name == "CBA_fnc_compileFunction") {
        //    diag_log("preinitNow"sv);
        //
        //
        //    if (!ev->local->variables.is_null(ev->local->variables.get("1scp"sv)))
        //        diag_log("scopeAlreadyExists"sv);
        //
        //    diag_log("curP " + ctx.get_current_position().sourcefile + ":" + std::to_string(ctx.get_current_position().sourceline));
        //
        //    auto vsp = ev->local;
        //    r_string spacing(""sv);
        //    while (vsp) {
        //        for (auto& it : vsp->variables) {
        //            diag_log(spacing+it.get_map_key());
        //            if (it.get_map_key() == "1scp") {
        //                auto gd1 = it.value.get_as<GameDataProfileScope>();
        //                diag_log(spacing + "-" + gd1->data->name);
        //                //diag_log(spacing + "+" + gd1->data->createPos.sourcefile+":"+std::to_string(gd1->data->createPos.sourceline));
        //            }
        //
        //
        //        }
        //
        //        vsp = vsp->parent;
        //        spacing += " ";
        //    }
        //}

        static r_string scp("1scp"sv); //#TODO this crashes game at shutdown because it tries to deallocate
        state.set_local_variable(scp, game_value(new GameDataProfileScope(std::move(data))), false);

        //if (name == "CBA_fnc_compileFunction") {
        //    diag_log("preinitPush"sv);
        //    for (auto& it : ev->local->variables) {
        //        diag_log(it.get_map_key());
        //    }
        //}

        return false;
    }
    int stack_size(void* t) const override { return 0; }
    r_string get_name() const override { return "GameInstructionProfileScopeStart"sv; }
    ~GameInstructionProfileScopeStart() override = default;
};

game_value createProfileScope(game_state&, game_value_parameter name) {
    if (sqf::can_suspend()) return {};
    static r_string profName("scriptProfiler.cpp");


    auto data = std::make_shared<GameDataProfileScope::scopeData>(name,
    //sqf::str(state.eval->local->variables.get("_this").value), //#TODO remove this. We don't want this
    game_value(),
    GProfilerAdapter->createScope(static_cast<r_string>(name), profName, __LINE__)
    );
    //#TODO retrieve line from callstack
    return game_value(new GameDataProfileScope(std::move(data)));
}

game_value profilerSleep(game_state&) {
    std::this_thread::sleep_for(17ms);
    return {};
}

game_value profilerCaptureFrame(game_state&) {
    auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
    if (!armaDiagProf) return {};
    armaDiagProf->captureFrame();
    return {};
}

game_value profilerCaptureFrames(game_state&, game_value_parameter count) {
    auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
    if (!armaDiagProf) return {};
    armaDiagProf->captureFrames(static_cast<uint32_t>(static_cast<float>(count)));
    return {};
}

game_value profilerCaptureSlowFrame(game_state&, game_value_parameter threshold) {
    auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
    if (!armaDiagProf) return {};
    armaDiagProf->captureSlowFrame(chrono::milliseconds(static_cast<float>(threshold)));
    return {};
}

game_value profilerCaptureTrigger(game_state&) {
    auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
    if (!armaDiagProf) return {};
    armaDiagProf->captureTrigger();
    return {};
}

game_value profilerTrigger(game_state&) {
    auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
    if (!armaDiagProf) return {};
    armaDiagProf->profilerTrigger();
    return {};
}

game_value profilerLog(game_state&, game_value_parameter message) {
    GProfilerAdapter->addLog(message);
    return {};
}

game_value profilerSetOutputFile(game_state& state, game_value_parameter file) {
#ifdef WITH_CHROME
    auto chromeAdapter = std::dynamic_pointer_cast<AdapterChrome>(GProfilerAdapter);
    if (!chromeAdapter) {
        state.set_script_error(game_state::game_evaluator::evaluator_error_type::bad_var, "not using ChromeAdapter"sv);
        return {};
    }

    chromeAdapter->setTargetFile(static_cast<std::string_view>(static_cast<r_string>(file)));
#endif
    return {};
}

game_value profilerSetAdapter(game_state&, game_value_parameter file) {
    
    r_string adap = file;

    profiler.waitForAdapter = adap;
    return {};
}

game_value profilerSetCounter(game_state&, game_value_parameter name, game_value_parameter value) {
    GProfilerAdapter->setCounter(name,value);
    return {};
}

game_value profilerTime(game_state&) {
    //We want time to be as close as possible to when command returns, needs some tricks.

    game_value result({0.0f, 0.0f, 0.0f});

    auto& resultArr = result.to_array();

    auto& fseconds = resultArr[0].get_as<game_data_number>()->number;
    auto& fmicroseconds = resultArr[1].get_as<game_data_number>()->number;
    auto& fnanoseconds = resultArr[2].get_as<game_data_number>()->number;

    const auto elapsedTime = std::chrono::high_resolution_clock::now() - startTime;

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsedTime);
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(elapsedTime - seconds);
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            elapsedTime
            - seconds
            - microseconds
        );

    fseconds = seconds.count();
    fmicroseconds = microseconds.count();
    fnanoseconds = nanoseconds.count();

    return result;
}

//profiles script like diag_codePerformance
game_value profileScript(game_state& state, game_value_parameter par) {
    code _code = par[0];
    int runs = par.get(2).value_or(10000);

    auto _emptyCode = sqf::compile("");

    //CBA fastForEach

    if (par.get(1) && !par[1].is_nil()) {
        //#TODO we want to create a subscope
        state.set_local_variable("_this"sv, par[1]);
    }

    //prep for action
    for (int i = 0; i < 1000; ++i) {
        sqf::call(_emptyCode);
    }

    auto emptyMeasStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        sqf::call(_emptyCode);
    }
    auto emptyMeasEnd = std::chrono::high_resolution_clock::now();

    auto cycleLoss = std::chrono::duration_cast<std::chrono::nanoseconds>(emptyMeasEnd - emptyMeasStart) / 1000;


    //prepare
    for (int i = 0; i < 500; ++i) {
        sqf::call(_code);
    }

    auto measStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < runs; ++i) {
        sqf::call(_code);
    }
    auto measEnd = std::chrono::high_resolution_clock::now();

    auto measTime = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(measEnd - measStart) / runs;
    measTime -= cycleLoss;

    return { static_cast<float>(measTime.count()), std::to_string(measTime.count()) , runs };
}


std::regex getScriptName_acefncRegex(R"(\\?(?:[xz]|idi)\\([^\\]*)\\addons\\([^\\]*)\\(?:[^\\]*\\)*fnc?_([^.]*)\.sqf)", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
std::regex getScriptName_aceMiscRegex(R"(\\?[xz]\\([^\\]*)\\addons\\([^\\]*)\\(?:[^\\]*\\)*([^.]*)\.sqf)", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
std::regex getScriptName_LinePreprocRegex(R"(#line [0-9]* (?:"|')([^"']*))", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
std::regex getScriptName_bisfncRegex(R"(\\?A3\\(?:[^.]*\\)+fn_([^.]*).sqf)", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
std::regex getScriptName_pathScriptNameRegex(R"(\[([^\]]*)\]$)", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
std::regex getScriptName_scriptNameCmdRegex(R"(scriptName (?:"|')([^"']*))", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
std::regex getScriptName_scriptNameVarRegex(R"(scriptName = (?:"|')([^"']*))", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);



std::string getScriptName(const r_string& str, const r_string& filePath, uint32_t returnFirstLineIfNoName = 0) {
    if (str.empty() || (str.front() == '[' && *(str.begin() + str.length() - 1) == ']')) return "<unknown>";
    
    if (!filePath.empty()) {
        std::match_results<compact_array<char>::const_iterator> pathMatch;
        //A3\functions_f\Animation\Math\fn_deltaTime.sqf [BIS_fnc_deltaTime]
        if (std::regex_search(filePath.begin(), filePath.end(), pathMatch, getScriptName_pathScriptNameRegex)) {
            return std::string(pathMatch[1]); //BIS_fnc_deltaTime
        }
        //\x\cba\addons\common\fnc_currentUnit.sqf
        if (std::regex_search(filePath.begin(), filePath.end(), pathMatch, getScriptName_acefncRegex)) {
            return std::string(pathMatch[1]) + "_" + std::string(pathMatch[2]) + "_fnc_" + std::string(pathMatch[3]); //CBA_common_fnc_currentUnit
        }
        //A3\functions_f\Animation\Math\fn_deltaTime.sqf
        if (std::regex_search(filePath.begin(), filePath.end(), pathMatch, getScriptName_bisfncRegex)) {
            return "BIS_fnc_" + std::string(pathMatch[1]); //BIS_fnc_deltaTime
        }
    }

    std::match_results<compact_array<char>::const_iterator> scriptNameMatch;
    //scriptName "cba_events_fnc_playerEH_EachFrame";
    if (std::regex_search(str.begin(), str.end(), scriptNameMatch, getScriptName_scriptNameCmdRegex)) {
        auto mt = scriptNameMatch[1].str();
        if (mt != "%1") //Skip "%1" from initFunctions
            return mt; //cba_events_fnc_playerEH_EachFrame
    }
    //private _fnc_scriptName = 'CBA_fnc_currentUnit';
    if (std::regex_search(str.begin(), str.end(), scriptNameMatch, getScriptName_scriptNameVarRegex)) {
        auto mt = scriptNameMatch[1].str();
        if (mt != "%1") //Skip "%1" from initFunctions
            return mt; //CBA_fnc_currentUnit
    }

    std::match_results<compact_array<char>::const_iterator> filePathFindMatch;
    //#line 1337 "\x\cba\addons\common\fnc_currentUnit.sqf [CBA_fnc_currentUnit]"
    if (std::regex_search(str.begin(), str.end(), filePathFindMatch, getScriptName_LinePreprocRegex)) {
        const auto filePathFromLine = std::string(filePathFindMatch[1]); //\x\cba\addons\common\fnc_currentUnit.sqf [CBA_fnc_currentUnit]

        std::smatch pathMatchFromline;
        //A3\functions_f\Animation\Math\fn_deltaTime.sqf [BIS_fnc_deltaTime]
        if (!filePathFromLine.empty() && std::regex_search(filePathFromLine, pathMatchFromline, getScriptName_pathScriptNameRegex)) {
            return std::string(pathMatchFromline[1]); //BIS_fnc_deltaTime
        }
        //\x\cba\addons\common\fnc_currentUnit.sqf
        if (!filePathFromLine.empty() && std::regex_search(filePathFromLine, pathMatchFromline, getScriptName_acefncRegex)) {
            return std::string(pathMatchFromline[1]) + "_" + std::string(pathMatchFromline[2]) + "_fnc_" + std::string(pathMatchFromline[3]); //CBA_common_fnc_currentUnit
        }
        //\x\cba\addons\common\XEH_preStart.sqf
        if (!filePathFromLine.empty() && std::regex_search(filePathFromLine, pathMatchFromline, getScriptName_aceMiscRegex)) {
            return std::string(pathMatchFromline[1]) + "_" + std::string(pathMatchFromline[2]) + "_" + std::string(pathMatchFromline[3]); //CBA_common_XEH_preStart
        }
        //A3\functions_f\Animation\Math\fn_deltaTime.sqf
        if (!filePathFromLine.empty() && std::regex_search(filePathFromLine, pathMatchFromline, getScriptName_bisfncRegex)) {
            return "BIS_fnc_" + std::string(pathMatchFromline[1]); //BIS_fnc_deltaTime
        }
    }


    //if (str.find("createProfileScope", 0) != -1) return "<unknown>"; //Don't remember why I did this :D

    if (returnFirstLineIfNoName) {
        auto linebreak = str.find("\n", 0);
        if (linebreak < 20) {
            auto linebreak2 = str.find("\n", linebreak + 1);
            if (linebreak2 > linebreak) linebreak = linebreak2;
        }
        if (linebreak != -1) {
            auto name = std::string(str.data(), linebreak);
            std::transform(name.begin(), name.end(), name.begin(), [](char ch) {
                if (ch == '"') return '\'';
                if (ch == '\n') return ' ';
                return ch;
            });
            return name;
        } else if (str.length() < returnFirstLineIfNoName) {
            auto name = std::string(str.data());
            std::transform(name.begin(), name.end(), name.begin(), [](char ch) {
                if (ch == '"') return '\'';
                return ch;
            });
            return name;
        }
        //OutputDebugStringA(str.data());
        //OutputDebugStringA("\n\n#######\n");
    }

    return "<unknown>";
}

//game_value compileRedirect(uintptr_t st, game_value_parameter message) {
//    game_state* state = reinterpret_cast<game_state*>(st);
//    r_string str = message;
//
//    std::string scriptName = getScriptName(str);
//
//    if (!scriptName.empty())
//        str = r_string("private _scoooope = createProfileScope \""sv) + scriptName + "\"; "sv + str;
//    return sqf::compile(str);
//}


std::string getScriptFromFirstLine(sourcedocpos& pos, bool compact) {//https://github.com/dedmen/ArmaDebugEngine/blob/master/BIDebugEngine/BIDebugEngine/Script.cpp
    if (pos.content.empty()) return pos.content.data();
    auto needSourceFile = pos.sourcefile.empty();
    int line = pos.sourceline + 1;
    auto start = pos.content.begin();
    auto end = pos.content.end();
    std::string filename(needSourceFile ? "" : pos.sourcefile.data());
    std::transform(filename.begin(), filename.end(), filename.begin(), tolower);
    auto curPos = start;
    auto curLine = 1U;
    std::string output;
    bool inWantedFile = needSourceFile;
    output.reserve(end - start);

    auto removeEmptyLines = [&](size_t count) {
        for (size_t i = 0; i < count; i++) {
            auto found = output.find("\n\n");
            if (found != std::string::npos)
                output.replace(found, 2, 1, '\n');
            else if (output.front() == '\n') {
                output.erase(0, 1);
            } else {
                output.replace(0, output.find('\n'), 1, '\n');
            }
        }
    };

    auto readLineMacro = [&]() {
        curPos += 6;
        auto numberEnd = std::find(curPos, end, ' ');
        auto number = std::stoul(std::string(static_cast<const char*>(curPos), numberEnd - curPos));
        curPos = numberEnd + 2;
        auto nameEnd = std::find(curPos, end, '"');
        std::string name(static_cast<const char*>(curPos), nameEnd - curPos);
        std::transform(name.begin(), name.end(), name.begin(), tolower);
        if (needSourceFile) {
            needSourceFile = false;
            filename = name;
        }
        bool wasInWantedFile = inWantedFile;
        inWantedFile = (name == filename);
        if (inWantedFile) {
            if (number < curLine) removeEmptyLines((curLine - number));
            curLine = number;
        }
        if (*(nameEnd + 1) == '\r') ++nameEnd;
        curPos = nameEnd + 2;
        //if (inWantedFile && *curPos == '\n') {
        //    curPos++;
        //}//after each #include there is a newline which we also don't want


        if (wasInWantedFile) {
            output.append("#include \"");
            output.append(name);
            output.append("\"\n");
            curLine++;
        }


        return curPos <= end;
    };
    auto readLine = [&]() {
        if (curPos > end) return false;
        if (*curPos == '#' && *(curPos+1) == 'l') return readLineMacro();
        auto lineEnd = std::find(curPos, end, '\n') + 1;
        if (inWantedFile) {
            output.append(static_cast<const char*>(curPos), lineEnd - curPos);
            curLine++;
        }
        //line is curPos -> lineEnd
        curPos = lineEnd;
        return curPos <= end;
    };
    while (readLine()) {};
    if (compact) {
        //http://stackoverflow.com/a/24315631
        size_t start_pos;
        while ((start_pos = output.find("\n\n\n", 0)) != std::string::npos) {
            output.replace(start_pos, 3, "\n");
        }
    }
    return output;
}





static struct vtables {
    void** vt_GameInstructionNewExpression;
    void** vt_GameInstructionConst;
    void** vt_GameInstructionFunction;
    void** vt_GameInstructionOperator;
    void** vt_GameInstructionAssignment;
    void** vt_GameInstructionVariable;
    void** vt_GameInstructionArray;
} GVt;

static struct {
    void* vt_GameInstructionNewExpression;
    void* vt_GameInstructionConst;
    void* vt_GameInstructionFunction;
    void* vt_GameInstructionOperator;
    void* vt_GameInstructionAssignment;
    void* vt_GameInstructionVariable;
    void* vt_GameInstructionArray;
} oldFunc;

class GameInstructionConst : public game_instruction {
public:
    game_value value;

    bool exec(game_state& state, vm_context& t) override {
        //static const r_string InstrName = "I_Const"sv;
        //static Brofiler::EventDescription* autogenerated_description = ::Brofiler::EventDescription::Create(InstrName, __FILE__, __LINE__);
        //Brofiler::Event autogenerated_event_639(*autogenerated_description);
        //
        //if (autogenerated_event_639.data)
        //    autogenerated_event_639.data->thisArgs = value;

        typedef bool(
#ifndef __linux__
            __thiscall 
#endif
            *OrigEx)(game_instruction*, game_state&, vm_context&);
        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionConst)(this, state, t);
    }

    int stack_size(void* t) const override { return 0; }
    r_string get_name() const override { return ""sv; }
};

bool codeHasScopeInstruction(const auto_array<ref<game_instruction>>& bodyCode)
{
    if (bodyCode.empty()) return false;

    auto lt = typeid(*bodyCode.front().getRef()).hash_code();
    auto rt = typeid(GameInstructionProfileScopeStart).hash_code();

    return lt == rt;
}

void addScopeInstruction(auto_array<ref<game_instruction>>& bodyCode, const r_string& scriptName) {
#ifndef __linux__
#ifndef _WIN64
#error "no x64 hash codes yet"
#endif
#endif
    if (bodyCode.empty()) return;
    if (codeHasScopeInstruction(bodyCode)) return;
    if (bodyCode.size() < 4) return;


    auto& funcPath = bodyCode.front()->sdp->sourcefile;
    //r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);



    //Insert instruction to set _x
    ref<GameInstructionProfileScopeStart> curElInstruction = rv_allocator<GameInstructionProfileScopeStart>::create_single(scriptName);
    curElInstruction->sdp = bodyCode.front()->sdp;
    curElInstruction->scopeInfo = GProfilerAdapter->createScope(curElInstruction->name,
        funcPath.empty() ? curElInstruction->name : funcPath,
        curElInstruction->sdp->sourceline);
    //if (scriptName == "<unknown>")
    //curElInstruction->eventDescription->source = src;

    //Add instruction at start
    bodyCode.insert(bodyCode.begin(), curElInstruction);

#ifdef __linux__
    static const size_t ConstTypeIDHash = 600831349;
#else
    static const size_t ConstTypeIDHash = 0x0a56f03038a03360ull;
#endif
    for (auto& it : bodyCode) {

        auto instC = dynamic_cast<GameInstructionProfileScopeStart*>(it.get());
        if (instC) {
            break;
        }

        auto typeHash = typeid(it.get()).hash_code();

        //linux
        //auto typeN = typeid(*it.get()).name();
        //std::string stuff = std::to_string(typeHash) + " " + typeN;
        //sqf::diag_log(stuff);


        if (typeHash != ConstTypeIDHash) continue;
        auto inst = static_cast<GameInstructionConst*>(it.get());
        if (inst->value.type_enum() != game_data_type::CODE) continue;

        auto bodyCodeNext = static_cast<game_data_code*>(inst->value.data.get());
        if (bodyCodeNext->instructions.size() > 20)
           addScopeInstruction(bodyCodeNext->instructions, scriptName);
    }
}

std::optional<r_string> tryGetNameFromInitFunctions(game_state& state) {
    if (state.get_vm_context()->get_current_position().sourcefile != R"(A3\functions_f\initFunctions.sqf)"sv
        ||
        state.get_vm_context()->get_current_position().sourceline > 200
        ) return {};

    r_string fncname = state.get_local_variable("_fncvar"sv);
    return fncname;
}

r_string getCallerSourceFile(const game_state& state) {
    //return state.get_vm_context()->get_current_position().sourcefile; // This used to work, but now doesn't, just returns "CBA_fnc_preInit" even though we are in CBA_fnc_compileFinal

    auto& cs = state.get_vm_context()->callstack;
    if (cs.empty()) return {};
    auto x1 = typeid(*cs.back().get()).raw_name();
    if (typeid(*cs.back().get()).hash_code() != 0x6a5a9847820cfc77) return {}; // callstackitemdata
    const auto& code = static_cast<vm_context::callstack_item_data*>(cs.back().get())->_code;
    if (!code) return {};
    if (code->instructions.empty()) return {};
    return code->instructions.front()->sdp->sourcefile;
}

std::optional<r_string> tryGetNameFromCBACompile(game_state& state) {
    const std::string cbaCompile_Path(R"(\x\cba\addons\xeh\fnc_compileFunction.sqf)");
    const std::string cbaCompile_PathB(R"(/x/cba/addons/xeh/fnc_compileFunction.sqf)"); // ArmaScriptCompiler does forward slashes instead (to be fixed there soonish)
    const auto sFilePath = getCallerSourceFile(state);
    if (sFilePath.empty()) return {};

    if (getCallerSourceFile(state).find(sFilePath.front() == '/' ? cbaCompile_PathB : cbaCompile_Path) != 0) return {};
    r_string fncname = state.get_local_variable("_funcname"sv);
    return fncname;
}

bool hasCBA = false;

std::optional<r_string> tryGetNameFromCBACompileFinal(game_state& state) {
    const std::string cbaCompile_Path(R"(\x\cba\addons\common\fnc_compileFinal.sqf)");
    const std::string cbaCompile_PathB(R"(/x/cba/addons/common/fnc_compileFinal.sqf)"); // ArmaScriptCompiler does forward slashes instead (to be fixed there soonish)
    const auto sFilePath = getCallerSourceFile(state);
    if (sFilePath.empty()) return {};

    if (getCallerSourceFile(state).find(sFilePath.front() == '/' ? cbaCompile_PathB : cbaCompile_Path) != 0) return {};
    hasCBA = true; // I only need this for OnFrame handler, and that one always goes through here
    r_string fncname = state.get_local_variable("_name"sv);
    return fncname;
}

unary_function compileFinal214 = nullptr; // #TODO get rid of this at 2.14 release

game_value compileRedirect2(game_state& state, game_value_parameter message) {
    if (!profiler.compileScope) {
        static r_string compileEventText("compile");
        static r_string profName("scriptProfiler.cpp");
        profiler.compileScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
    }

    auto tempData = GProfilerAdapter->enterScope(profiler.compileScope);

    r_string str = message;

    auto comp = sqf::compile(str);
    auto bodyCode = static_cast<game_data_code*>(comp.data.get());
    if (bodyCode->instructions.empty()) {
        GProfilerAdapter->leaveScope(tempData);
        return comp;
    }

#ifdef WITH_BROFILER
    if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
        r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);
        brofilerData->evtDt->sourceCode = src;
    }
#endif

    GProfilerAdapter->leaveScope(tempData);

    auto& funcPath = bodyCode->instructions.front()->sdp->sourcefile;
    //#TODO pass instructions to getScriptName and check if there is a "scriptName" or "scopeName" unary command call
    r_string scriptName(getScriptName(str, funcPath, 32));

    //if (scriptName.empty()) scriptName = "<unknown>";

    if (bodyCode->instructions.size() > 4 && !scriptName.empty())// && scriptName != "<unknown>"
        addScopeInstruction(bodyCode->instructions, scriptName);

    // HACK: CBA's OnFrame function is not a standalone function that has "compile" called on it, it is just a member variable in a function.
    // So we use this hack here to find it
    // Cheap size check first, string compare on every compile is meh
    //if (funcPath.capacity() == 45 && funcPath == R"(x\cba\addons\common\init_perFrameHandler.sqf)")
    //{
    //    // Find the first constant instruction that pushes a piece of code, that's our OnFrame code.
    //
    //    for (auto& game_instruction : bodyCode->instructions)
    //    {
    //        if (typeid(*game_instruction.getRef()).hash_code() != 0x0a56f03038a03360)
    //            continue;
    //        auto* constant = static_cast<game_instruction_constant*>(game_instruction.getRef());
    //        if (constant->value.type_enum() != game_data_type::CODE)
    //            continue;
    //
    //        // First code constant, this should be it
    //
    //        //{
    //        //    auto subBodyCode = static_cast<game_data_code*>(constant->value.data.get());
    //        //    r_string funcName = "CBA_PFH_OnFrame";
    //        //    addScopeInstruction(subBodyCode->instructions, funcName);
    //        //
    //        //    // HACK2, because old CBA (pre 2.14) recompiles this string while stripping all file location info (and our profiling scope), we also need to inject a manual scripted scope into it.
    //        //
    //        //    subBodyCode->code_string = "scriptName \"CBA_PFH_OnFrame\";" + subBodyCode->code_string;
    //        //    __nop();
    //        //}
    //    }
    //
    //}


    return comp;
}

game_value compileRedirectFinal(game_state& state, game_value_parameter message) {
    if (!profiler.compileScope) {
        static r_string compileEventText("compile");
        static r_string profName("scriptProfiler.cpp");
        profiler.compileScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
    }

    auto tempData = GProfilerAdapter->enterScope(profiler.compileScope);

    r_string bodyString;
    game_data_code* bodyCode;
    game_value resultValue = message;
    if (message.type_enum() == game_data_type::STRING) {

        bodyString = message;

        resultValue = sqf::compile_final(bodyString);
        bodyCode = static_cast<game_data_code*>(resultValue.data.get());
        if (bodyCode->instructions.empty()) {
            GProfilerAdapter->leaveScope(tempData);
            return resultValue;
        }

#ifdef WITH_BROFILER
        if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
            r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);
            brofilerData->evtDt->sourceCode = src;
        }
#endif

        GProfilerAdapter->leaveScope(tempData);

    } else if (message.type_enum() == game_data_type::CODE) {
        //#TODO call the function directly once this was fixed with 2.14 release
        resultValue = host::functions.invoke_raw_unary(compileFinal214, message);

        bodyCode = static_cast<game_data_code*>(resultValue.data.get());
        bodyString = bodyCode->code_string;
        if (bodyCode->instructions.empty()) {
            GProfilerAdapter->leaveScope(tempData); //#TODO do we even need to leave this, or is it RAII?
            return resultValue;
        }
    } else {
        //#TODO call the function directly once this was fixed with 2.14 release
        return host::functions.invoke_raw_unary(compileFinal214, message);
    }


    auto& funcPath = bodyCode->instructions.front()->sdp->sourcefile;

    auto scriptName = tryGetNameFromInitFunctions(state);
    if (!scriptName) scriptName = tryGetNameFromCBACompileFinal(state);
    if (!scriptName) scriptName = getScriptName(bodyString, funcPath, 32);
    //if (scriptName.empty()) scriptName = "<unknown>";

    if (bodyCode->instructions.size() > 3 && scriptName && !scriptName->empty())// && scriptName != "<unknown>"
        addScopeInstruction(bodyCode->instructions, *scriptName);

    return resultValue;
}

game_value callExtensionRedirect(game_state&, game_value_parameter ext, game_value_parameter msg) {
    if (!profiler.callExtScope) {
        static r_string compileEventText("callExtension");
        static r_string profName("scriptProfiler.cpp");
        profiler.callExtScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
    }

    auto tempData = GProfilerAdapter->enterScope(profiler.callExtScope);

    GProfilerAdapter->setName(tempData, ext);
    GProfilerAdapter->setDescription(tempData, msg);

    auto res = sqf::call_extension(ext,msg);

#ifdef WITH_BROFILER
    if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
        brofilerData->evtDt->thisArgs = ext;
        brofilerData->evtDt->sourceCode = msg;
    }
#endif

    GProfilerAdapter->leaveScope(tempData);

    return res;
}

game_value callExtensionArgsRedirect(game_state&, game_value_parameter ext, game_value_parameter msg) {
    if (!profiler.callExtScope) {
        static r_string compileEventText("callExtension");
        static r_string profName("scriptProfiler.cpp");
        profiler.callExtScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
    }

    auto tempData = GProfilerAdapter->enterScope(profiler.callExtScope);

    //GProfilerAdapter->setDescription(tempData, msg);

    auto res = host::functions.invoke_raw_binary(__sqf::binary__callextension__string__array__ret__array, ext, msg);


#ifdef WITH_BROFILER
    if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
        brofilerData->evtDt->thisArgs = ext;
        brofilerData->evtDt->sourceCode = msg;
    }
#endif

    GProfilerAdapter->leaveScope(tempData);

    return res;
}

static unary_function compileScriptFunc;

game_value compileScriptRedirect(game_state& state, game_value_parameter message) {

    if (!profiler.compileScriptScope) {
        static r_string compileEventText("compileScript");
        static r_string profName("scriptProfiler.cpp");
        profiler.compileScriptScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
    }

    auto tempData = GProfilerAdapter->enterScope(profiler.compileScriptScope);
    GProfilerAdapter->setName(tempData, "compileScript " + static_cast<r_string>(message[0]));

    auto& args = message.to_array();

    auto comp = host::functions.invoke_raw_unary(compileScriptFunc, message);
    auto bodyCode = static_cast<game_data_code*>(comp.data.get());
    if (bodyCode->instructions.empty()) {
        GProfilerAdapter->leaveScope(tempData);
        return comp;
    }

#ifdef WITH_BROFILER
    if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
        r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);
        brofilerData->evtDt->sourceCode = src;
    }
#endif

    GProfilerAdapter->leaveScope(tempData);

    auto& funcPath = args[0];
    //#TODO pass instructions to getScriptName and check if there is a "scriptName" or "scopeName" unary command call

    std::optional<r_string> scriptName;
    if (args.size() > 2) {
        auto scrNamePrefixHeader = getScriptName(args[2], funcPath, 32);
        if (scrNamePrefixHeader != "<unknown>")
            scriptName = scrNamePrefixHeader;
    }


    if (!scriptName) scriptName = tryGetNameFromCBACompile(state);

    if (!scriptName)
    {
        // Load file contents and see if we can grab name from there
        auto scriptContents = sqf::preprocess_file_line_numbers(args[0]);
        scriptName = getScriptName(scriptContents, funcPath, 32);
    };

    //if (scriptName.empty()) scriptName = "<unknown>";

    if (bodyCode->instructions.size() > 3 && scriptName)// && scriptName != "<unknown>"
        addScopeInstruction(bodyCode->instructions, *scriptName);

    return comp;
}


game_value diag_logRedirect(game_state&, game_value_parameter msg) {
    r_string str = static_cast<r_string>(msg);

    GProfilerAdapter->addLog(str);
    sqf::diag_log(msg);
    return {};
}

std::string get_command_line() {
#if __linux__
    std::ifstream cmdline("/proc/self/cmdline");
    std::string file_contents;
    std::string line;
    while (std::getline(cmdline, line, '\0')) {
        file_contents += line;
        file_contents.push_back(' '); //#TODO can linux even have more than one line?
    }
    return file_contents;
#else
    return GetCommandLineA();
#endif
}

std::optional<std::string> getCommandLineParam(std::string_view needle) {
    std::string commandLineF = get_command_line();
    std::string_view commandLine(commandLineF);
    const auto found = commandLine.find(needle);
    if (found != std::string::npos) {
        auto parameter = commandLine.substr(found);

        // Find end of parameter
        parameter = parameter.substr(0, parameter.find(' '));
        auto adapterStr = parameter.substr(std::min(parameter.length(), needle.length() + 1));
        //if (adapterStr.back() == '"') // Filtering away quotes, but we don't do that anymore
        //    adapterStr = adapterStr.substr(0, adapterStr.length() - 1);
        return std::string(adapterStr);
    }

    if (!json.empty() && json.contains(needle.substr(1))) {
        switch (json[needle.substr(1)].type()) {
            case nlohmann::json::value_t::boolean:
            {
                if (json[needle.substr(1)].get<bool>()) {
                    return "true";
                } else {
                    return {};
                }
            }
            case nlohmann::json::value_t::string:
            {
                return json[needle.substr(1)].get<std::string>();
            }
            default:
                return {};
        }
    }
    return {};
}

scriptProfiler::scriptProfiler() {
    startTime = std::chrono::high_resolution_clock::now();
}

auto_array<std::pair<r_string, uint32_t>>(*getCallstackRaw)(game_state* gs) = nullptr;
bool InstructionCallstack = false;

#ifndef __linux__
#pragma region Instructions
namespace intercept::__internal {

    class gsFuncBase {
    public:
        r_string _name;
        void copyPH(const gsFuncBase* other) noexcept {
            securityStuff = other->securityStuff;
            //std::copy(std::begin(other->securityStuff), std::end(other->securityStuff), std::begin(securityStuff));
        }
    private:
        std::array<size_t,
#if _WIN64 || __X86_64__
            9
#else
#ifdef __linux__
            8
#else
            11
#endif
#endif
        > securityStuff{};  //Will scale with x64
                            //size_t securityStuff[11];
    };
    class gsFunction : public gsFuncBase {
        void* placeholder12{ nullptr };//0x30  //jni function
    public:
        r_string _name2;//0x34 this is (tolower name)
        unary_operator * _operator;//0x38
#ifndef __linux__
        r_string _rightType;//0x3c RString to something
        r_string _description;//0x38
        r_string _example;
        r_string _example2;
        r_string placeholder_11;
        r_string placeholder_12;
        r_string _category{ "intercept"sv }; //0x48
#endif
                                             //const rv_string* placeholder13;
    };
    class gsOperator : public gsFuncBase {
        void* placeholder12{ nullptr };//0x30  JNI function
    public:
        r_string _name2;//0x34 this is (tolower name)
        int32_t placeholder_10{ 4 }; //0x38 Small int 0-5  priority
        binary_operator * _operator;//0x3c
#ifndef __linux__
        r_string _leftType;//0x40 Description of left hand side parameter
        r_string _rightType;//0x44 Description of right hand side parameter
        r_string _description;//0x48
        r_string _example;//0x4c
        r_string placeholder_11;//0x60
        r_string _version;//0x64 some version number
        r_string placeholder_12;//0x68
        r_string _category{ "intercept"sv }; //0x6c
#endif
    };
    class gsNular : public gsFuncBase {
    public:
        r_string _name2;//0x30 this is (tolower name)
        nular_operator * _operator;//0x34
#ifndef __linux__
        r_string _description;//0x38
        r_string _example;
        r_string _example2;
        r_string _version;//0x44 some version number
        r_string placeholder_10;
        r_string _category; //0x4d
#endif
        void* placeholder11{ nullptr };//0x50 JNI probably
        const char *get_map_key() const noexcept { return _name2.data(); }
    };



    class game_functions : public auto_array<gsFunction>, public gsFuncBase {
    public:
        game_functions(r_string name) : _name(std::move(name)) {}
        r_string _name;
        game_functions() noexcept {}
        const char *get_map_key() const noexcept { return _name.data(); }
    };

    class game_operators : public auto_array<gsOperator>, public gsFuncBase {
    public:
        game_operators(r_string name) : _name(std::move(name)) {}
        r_string _name;
        int32_t placeholder10{ 4 }; //0x2C Small int 0-5  priority
        game_operators() noexcept {}
        const char *get_map_key() const noexcept { return _name.data(); }
    };
}


class GameInstructionVariable : public game_instruction {
public:
    r_string name;
    static inline std::unordered_map<size_t, std::shared_ptr<ScopeInfo>> descriptions;
    virtual bool exec(game_state& state, vm_context& t) {
        typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);
        
        if (instructionLevelProfiling) {
            static r_string setVar("getVar");

            auto found = descriptions.find(reinterpret_cast<size_t>(name.data()));
            if (found == descriptions.end()) {
                
                auto newStuff = GProfilerAdapter->createScope(setVar,name,0);
                auto ret = descriptions.insert({reinterpret_cast<size_t>(name.data()), newStuff});
                found = ret.first;
            }

            auto temp = GProfilerAdapter->enterScope(found->second, ScopeWithCallstack{ getCallstackRaw != nullptr });
            if (getCallstackRaw && InstructionCallstack) AdapterTracy::sendCallstack(getCallstackRaw(&state));

            auto res = reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionVariable)(this, state, t);
            GProfilerAdapter->leaveScope(temp);
            return res;
        }

        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionVariable)(this, state, t);
    }
    virtual int stack_size(void* t) const { return 0; }
    virtual r_string get_name() const { return ""sv; }
};

class GameInstructionOperator : public game_instruction {
public:
    const intercept::__internal::game_operators *_operators;

    static inline std::unordered_map<size_t, std::shared_ptr<ScopeInfo>> descriptions;

    virtual bool exec(game_state& state, vm_context& t) {
        /*
            if (false && !sqf::can_suspend() && !state.eval->local->variables.has_key("1scp")) {
                auto found = descriptions.find(sdp.content.hash());
                if (found == descriptions.end()) {
                    found = descriptions.insert({ sdp.content.hash(),::Brofiler::EventDescription::Create(_operators->_name, sdp.sourcefile, sdp.sourceline) }).first;
                }
                auto data = std::make_shared<GameDataProfileScope::scopeData>(found->second->name,
#ifdef BROFILER_ONLY
                    std::chrono::high_resolution_clock::time_point(), 0u,
#else
                    std::chrono::high_resolution_clock::now(), profiler.startNewScope(),
#endif
                    state.eval->local->variables.get("_this").value,
                    (found->second));
                auto newScope = new GameDataProfileScope(std::move(data));
                state.eval->local->variables.insert(
                    game_variable("1scp"sv, game_value(newScope), false)
                );


                //static const r_string InstrName = "I_Operator"sv;
                //static Brofiler::EventDescription* autogenerated_description = ::Brofiler::EventDescription::Create(InstrName, __FILE__, __LINE__);
                //Brofiler::Event autogenerated_event_639(*(found->second));

                if (newScope->data->evtDt) {
                    //autogenerated_event_639.data->altName = _operators->_name;
                    newScope->data->evtDt->sourceCode = sdp.content;
                }

            }
        */

        typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);

        if (instructionLevelProfiling) {
            auto instructionName = _operators->gsFuncBase::_name;

            auto found = descriptions.find(reinterpret_cast<size_t>(instructionName.data()));
            if (found == descriptions.end()) {
                
                auto newStuff = GProfilerAdapter->createScope(instructionName,{},0);
                auto ret = descriptions.insert({reinterpret_cast<size_t>(instructionName.data()), newStuff});
                found = ret.first;
            }

            auto temp = GProfilerAdapter->enterScope(found->second);
            auto res = reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionOperator)(this, state, t);
            GProfilerAdapter->leaveScope(temp);
            return res;
        }

        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionOperator)(this, state, t);
    }
    virtual int stack_size(void* t) const { return 0; }
    virtual r_string get_name() const { return ""sv; }
};

class GameInstructionFunction : public game_instruction {
public:
    const intercept::__internal::game_functions *_functions;

    static inline std::unordered_map<size_t, std::shared_ptr<ScopeInfo>> descriptions;

    virtual bool exec(game_state& state, vm_context& t) {
        typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);
        
        if (instructionLevelProfiling) {
            auto instructionName = _functions->gsFuncBase::_name;

            auto found = descriptions.find(reinterpret_cast<size_t>(instructionName.data()));
            if (found == descriptions.end()) {
                
                auto newStuff = GProfilerAdapter->createScope(instructionName,{},0);
                auto ret = descriptions.insert({reinterpret_cast<size_t>(instructionName.data()), newStuff});
                found = ret.first;
            }

            auto temp = GProfilerAdapter->enterScope(found->second);
            auto res = reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionFunction)(this, state, t);
            GProfilerAdapter->leaveScope(temp);
            return res;
        }


        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionFunction)(this, state, t);
    }
    virtual int stack_size(void* t) const { return 0; }
    virtual r_string get_name() const { return ""sv; }
};

class GameInstructionArray : public game_instruction {
public:
    int size;
    virtual bool exec(game_state& state, vm_context& t) {
        //static const r_string InstrName = "I_Array"sv;
        //static Brofiler::EventDescription* autogenerated_description = ::Brofiler::EventDescription::Create(InstrName, __FILE__, __LINE__);
        //Brofiler::Event autogenerated_event_639(*autogenerated_description);

        typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);
        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionArray)(this, state, t);
    }
    virtual int stack_size(void* t) const { return 0; }
    virtual r_string get_name() const { return ""sv; }
};

class GameInstructionAssignment : public game_instruction {
public:
    r_string name;
    bool forceLocal;
    static inline std::unordered_map<size_t, std::shared_ptr<ScopeInfo>> descriptions;
    virtual bool exec(game_state& state, vm_context& t) {
        typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);

        if (instructionLevelProfiling) {
            static r_string setVar("assignVar");

            auto found = descriptions.find(reinterpret_cast<size_t>(name.data()));
            if (found == descriptions.end()) {
                
                auto newStuff = GProfilerAdapter->createScope(setVar,name,0);
                auto ret = descriptions.insert({reinterpret_cast<size_t>(name.data()), newStuff});
                found = ret.first;
            }

            auto temp = GProfilerAdapter->enterScope(found->second);
            auto res = reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionAssignment)(this, state, t);
            GProfilerAdapter->leaveScope(temp);
            return res;
        }

        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionAssignment)(this, state, t);
    }
    virtual int stack_size(void* t) const { return 0; }
    virtual r_string get_name() const { return ""sv; }
};

class GameInstructionNewExpression : public game_instruction {
public:
    int beg{ 0 };
    int end{ 0 };
    //static inline std::map<size_t, Brofiler::EventDescription*> descriptions;

    virtual bool exec(game_state& state, vm_context& t) {
        //static const r_string InstrName = "I_NewExpression"sv;
        //static Brofiler::EventDescription* autogenerated_description = ::Brofiler::EventDescription::Create(InstrName, __FILE__, __LINE__);
        //Brofiler::Event autogenerated_event_639(*autogenerated_description);
        //if (scopeLevelCounter == 0) {
        //    auto srcHash = t.sdoc._content.hash();
        //    auto found = descriptions.find(srcHash);
        //    if (found == descriptions.end()) {
        //        found = descriptions.insert({ srcHash,::Brofiler::EventDescription::Create(r_string("unknown"), __FILE__, __LINE__) }).first;
        //    }
        //
        //    //static const r_string InstrName = "I_Function"sv;
        //    //static Brofiler::EventDescription* autogenerated_description = ::Brofiler::EventDescription::Create(InstrName, __FILE__, __LINE__);
        //    Brofiler::Event autogenerated_event_639(*(found->second));
        //
        //    if (autogenerated_event_639.data)
        //        autogenerated_event_639.data->sourceCode = t.sdoc._content;
        //
        //
        //    typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);
        //    return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionNewExpression)(this, state, t);
        //}


        typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);
        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionNewExpression)(this, state, t);
    }
    virtual int stack_size(void* t) const { return 0; }
    virtual r_string get_name() const { return ""sv; }
};

#pragma endregion Instructions
#endif


#if __linux__
extern "C" int iterateCallback(struct dl_phdr_info* info, size_t size, void* data) {
    std::filesystem::path sharedPath = info->dlpi_name;
    if (sharedPath.filename() == "ArmaScriptProfiler_x64.so") {
        *static_cast<std::filesystem::path*>(data) = info->dlpi_name;
        return 0;
    }
    return 0;
}
#endif

std::filesystem::path getSharedObjectPath() {
#if __linux__
    std::filesystem::path path;
    dl_iterate_phdr(iterateCallback, &path);
    return path;

#else
    wchar_t buffer[MAX_PATH] = { 0 };
    HMODULE handle = nullptr;
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&getSharedObjectPath), &handle) == 0) {
        sqf::diag_log("getSharedObjectPath: GetModuleHandle failed");
        return std::filesystem::path{};
    }

    DWORD len = GetModuleFileNameW(handle, buffer, MAX_PATH);
    if (len == 0) {
        return std::filesystem::path{};
    }
    return std::filesystem::path{buffer};
#endif
}

std::filesystem::path findConfigFilePath() {

    std::filesystem::path path = getSharedObjectPath();
    path = path.parent_path().parent_path();

    std::filesystem::path dirName{ "config"sv };
    std::filesystem::path fileName{ "parameters.json"sv };
    path = path / dirName / fileName;

    return path;
}

void scriptProfiler::preStart() {
    sqf::diag_log("Arma Script Profiler preStart");

    std::filesystem::path filePath = findConfigFilePath();
    
    if (!filePath.empty() && std::filesystem::exists(filePath)) {
        sqf::diag_log("ASP: Found a Configuration File"sv);
        std::ifstream file(filePath);
        try {
            json = nlohmann::json::parse(file);
        }
        catch (nlohmann::json::exception& error) {
            // log error, set parameterFile false, and continue by using getCommandLineParam
            sqf::diag_log(error.what());
             
        }
    }

    auto startAdapter = getCommandLineParam("-profilerAdapter"sv);

    if (startAdapter) {
        if (false) {
#ifdef WITH_CHROME
        }
        else if (*startAdapter == "Chrome"sv) {
            auto chromeAdapter = std::make_shared<AdapterChrome>();
            GProfilerAdapter = chromeAdapter;
            sqf::diag_log("ASP: Selected Chrome Adapter"sv);
            auto chromeOutput = getCommandLineParam("-profilerOutput"sv);
            if (chromeOutput)
                chromeAdapter->setTargetFile(*chromeOutput);
#endif
#ifdef WITH_BROFILER
        }
        else if (*startAdapter == "Brofiler"sv) {
            GProfilerAdapter = std::make_shared<AdapterBrofiler>();
            sqf::diag_log("ASP: Selected Brofiler Adapter"sv);
#endif
        }
        else if (*startAdapter == "Arma"sv) {
            GProfilerAdapter = std::make_shared<AdapterArmaDiag>();
            sqf::diag_log("ASP: Selected ArmaDiag Adapter"sv);
        }
        else if (*startAdapter == "Tracy"sv) {
            GProfilerAdapter = std::make_shared<AdapterTracy>();
            sqf::diag_log("ASP: Selected Tracy Adapter"sv);
        }
    } else {
        GProfilerAdapter = std::make_shared<AdapterTracy>();
        sqf::diag_log("ASP: Selected Tracy Adapter"sv);
    }

    if (getCommandLineParam("-profilerEnableInstruction"sv)) {
        sqf::diag_log("ASP: Instruction Level profiling enabled"sv);
        instructionLevelProfiling = true;
    }

    if (auto tracyAdapter = std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter)) {

        auto iface = client::host::request_plugin_interface("BIDebugEngine_getCallstack", 1);
        if (iface)
            getCallstackRaw = reinterpret_cast<decltype(getCallstackRaw)>(*iface);

        tracyAdapter->addParameter(TP_InstructionProfilingEnabled, "InstructionProfiling", true, getCommandLineParam("-profilerEnableInstruction"sv) ? 1 : 0);
        if (getCallstackRaw)
            tracyAdapter->addParameter(TP_InstructionGetVarCallstackEnabled, "InstrProfGetVarCallstack", true, 0);
    }


    if (getCommandLineParam("-profilerEnableEngine"sv)) {
        diag_log("ASP: Enable Engine Profiling"sv);
        if (intercept::sqf::product_version().branch != "Profile"sv) {
            intercept::sqf::diag_log("ERROR ArmaScriptProfiler: Cannot enable engine profiling without Profiling build of Arma"sv);
        } else {
            engineProf = std::make_shared<EngineProfiling>();

            //if (!getCommandLineParam("-profilerEngineThreads"sv)) {
            //    engineProf->setMainThreadOnly();
            //    diag_log("ASP: Engine profiler main thread only mode"sv);
            //}

            if (!getCommandLineParam("-profilerEngineDoFile"sv)) {
                engineProf->setNoFile();
                diag_log("ASP: Engine profiler NoFile mode"sv);
            }

            if (!getCommandLineParam("-profilerEngineDoMem"sv)) {
                engineProf->setNoMem();
                diag_log("ASP: Engine profiler NoMem mode"sv);
            }

            engineFrameEnd = true;
            engineProf->init();
        }
    }

    if (getCommandLineParam("-profilerEnableFAlloc")) {
        allocHook = std::make_shared<FAllocHook>();
        diag_log("ASP: FAlloc instrumentation enabled"sv);
        allocHook->init();
    }

    if (getCommandLineParam("-profilerNoPaths"sv)) {
        diag_log("ASP: Omitting file paths"sv);
        GProfilerAdapter->setOmitFilePaths();
    }

    if (auto tracyAdapter = std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter)) {
        tracyAdapter->addParameter(TP_OmitFilePath, "OmitFilePaths", true, getCommandLineParam("-profilerNoPaths"sv) ? 1 : 0);
    }

    static auto codeType = client::host::register_sqf_type("ProfileScope"sv, "ProfileScope"sv, "Dis is a profile scope. It profiles things."sv, "ProfileScope"sv, createGameDataProfileScope);
    GameDataProfileScope_type = codeType.second;
    static auto _createProfileScope = client::host::register_sqf_command("createProfileScope", "Creates a ProfileScope", createProfileScope, codeType.first, game_data_type::STRING);
    static auto _profilerSleep = client::host::register_sqf_command("profilerBlockingSleep", "Pauses the engine for 17ms. Used for testing.", profilerSleep, game_data_type::NOTHING);
    static auto _profilerCaptureFrame = client::host::register_sqf_command("profilerCaptureFrame", "Captures the next frame", profilerCaptureFrame, game_data_type::NOTHING);
    static auto _profilerCaptureFrames = client::host::register_sqf_command("profilerCaptureFrames", "Captures the next frame", profilerCaptureFrames, game_data_type::NOTHING, game_data_type::SCALAR);
    static auto _profilerCaptureSlowFrame = client::host::register_sqf_command("profilerCaptureSlowFrame", "Captures the first frame that hits the threshold in ms", profilerCaptureSlowFrame, game_data_type::NOTHING, game_data_type::SCALAR);
    static auto _profilerCaptureTrigger = client::host::register_sqf_command("profilerCaptureTrigger", "Starts recording and captures the frame that contains a trigger", profilerCaptureTrigger, game_data_type::NOTHING);
    static auto _profilerTrigger = client::host::register_sqf_command("profilerTrigger", "Trigger", profilerTrigger, game_data_type::NOTHING);
    static auto _profilerLog = client::host::register_sqf_command("profilerLog", "Logs message to capture", profilerLog, game_data_type::NOTHING, game_data_type::STRING);
    static auto _profilerSetOutputFile = client::host::register_sqf_command("profilerSetOutputFile", "Set's output file for ChromeAdapter", profilerSetOutputFile, game_data_type::NOTHING, game_data_type::STRING);
    static auto _profilerSetAdapter = client::host::register_sqf_command("profilerSetAdapter", "Set's profiler Adapter", profilerSetAdapter, game_data_type::NOTHING, game_data_type::STRING);
    static auto _profilerSetCounter = client::host::register_sqf_command("profilerSetCounter", "Set's a counter value", profilerSetCounter, game_data_type::NOTHING, game_data_type::STRING, game_data_type::SCALAR);
    static auto _profilerTime = client::host::register_sqf_command("profilerTime", "Returns the time since gamestart as [seconds, microseconds, nanoseconds]"sv, profilerTime, game_data_type::ARRAY);

    compileFinal214 = (unary_function)host::functions.get_unary_function_typed("compilefinal"sv, "CODE"sv); //#TODO get rid at 2.14 release

    auto compHookDisabled = client::host::request_plugin_interface("ProfilerNoCompile", 1); //ASM will call us via interface instead

    if (!compHookDisabled && !getCommandLineParam("-profilerNoInstrumentation"sv)) {
        static auto _profilerCompile = client::host::register_sqf_command("compile", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
        //static auto _profilerCompile2 = client::host::register_sqf_command("compile2", "Profiler redirect", compileRedirect, game_data_type::CODE, game_data_type::STRING);
        //static auto _profilerCompile3 = client::host::register_sqf_command("compile3", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
        static auto _profilerCompileF = client::host::register_sqf_command("compileFinal", "Profiler redirect", compileRedirectFinal, game_data_type::CODE, game_data_type::CODE);


        compileScriptFunc = (unary_function)host::functions.get_unary_function_typed("compilescript"sv, "ARRAY"sv);
        if (compileScriptFunc)
            static auto _profilerCompileScript = client::host::register_sqf_command("compileScript", "Profiler redirect", compileScriptRedirect, game_data_type::CODE, game_data_type::ARRAY);

    };
    static auto _profilerCallExt = client::host::register_sqf_command("callExtension", "Profiler redirect", callExtensionRedirect, game_data_type::STRING, game_data_type::STRING, game_data_type::STRING);
    static auto _profilerCallExtArgs = client::host::register_sqf_command("callExtension", "Profiler redirect", callExtensionArgsRedirect, game_data_type::STRING, game_data_type::STRING, game_data_type::ARRAY);
    static auto _profilerDiagLog = client::host::register_sqf_command("diag_log", "Profiler redirect", diag_logRedirect, game_data_type::NOTHING, game_data_type::ANY);
    static auto _profilerProfScript = client::host::register_sqf_command("profileScript", "Profiler redirect", profileScript, game_data_type::ARRAY, game_data_type::ARRAY);
    if (!compHookDisabled) {
        static auto _profilerPrepFile = client::host::register_sqf_command("preprocessFile", "Profiler redirect", [](game_state&, game_value_parameter arg) -> game_value {
            if (!profiler.preprocFileScope) {
                static r_string compileEventText("preprocessFile");
                static r_string profName("scriptProfiler.cpp");
                profiler.preprocFileScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
            }

            auto tempData = GProfilerAdapter->enterScope(profiler.preprocFileScope);

            GProfilerAdapter->setName(tempData, "preprocessFile "+static_cast<r_string>(arg));

            auto res = sqf::preprocess_file_line_numbers(arg);

            GProfilerAdapter->leaveScope(tempData);

            return res;
        }, game_data_type::STRING, game_data_type::STRING);
        static auto _profilerPrepFileLN = client::host::register_sqf_command("preprocessFileLineNumbers", "Profiler redirect", [](game_state&, game_value_parameter arg) -> game_value {
            if (!profiler.preprocFileScope) {
                static r_string compileEventText("preprocessFileLineNumbers");
                static r_string profName("scriptProfiler.cpp");
                profiler.preprocFileScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
            }

            auto tempData = GProfilerAdapter->enterScope(profiler.preprocFileScope);

            GProfilerAdapter->setName(tempData, "preprocessFile "+static_cast<r_string>(arg));

            auto res = sqf::preprocess_file_line_numbers(arg);

            GProfilerAdapter->leaveScope(tempData);

            return res;
        }, game_data_type::STRING, game_data_type::STRING);
    }

    if (getCommandLineParam("-profilerEnableNetwork"sv)) {
        if (std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter)) {
            diag_log("ASP: Network statistics enabled"sv);
            GNetworkProfiler.init();
        } else {
            diag_log("ASP: Network statistics could NOT be enabled because it requires Tracy mode"sv);
        }
        
    }



#ifndef __linux__
    auto iface = client::host::request_plugin_interface("sqf_asm_devIf", 1);
    if (iface) {
        GVt = *static_cast<vtables*>(*iface);
        DWORD dwVirtualProtectBackup;

        //#TODO only do if instruction profiling startparameter is present
        game_instruction* ins;
        //ins = new GameInstructionConst();
        //VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionConst), 14u, 0x40u, &dwVirtualProtectBackup);
        //oldFunc.vt_GameInstructionConst = GVt.vt_GameInstructionConst[3];
        //GVt.vt_GameInstructionConst[3] = (*reinterpret_cast<void***>(ins))[3];
        //VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionConst), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        //delete ins;
        //
        ins = new GameInstructionVariable();
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionVariable), 14u, 0x40u, &dwVirtualProtectBackup);
        oldFunc.vt_GameInstructionVariable = GVt.vt_GameInstructionVariable[3];
        GVt.vt_GameInstructionVariable[3] = (*reinterpret_cast<void***>(ins))[3];
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionVariable), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        delete ins;
        
        ins = new GameInstructionOperator();
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionOperator), 14u, 0x40u, &dwVirtualProtectBackup);
        oldFunc.vt_GameInstructionOperator = GVt.vt_GameInstructionOperator[3];
        GVt.vt_GameInstructionOperator[3] = (*reinterpret_cast<void***>(ins))[3];
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionOperator), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        delete ins;

        ins = new GameInstructionFunction();
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionFunction), 14u, 0x40u, &dwVirtualProtectBackup);
        oldFunc.vt_GameInstructionFunction = GVt.vt_GameInstructionFunction[3];
        GVt.vt_GameInstructionFunction[3] = (*reinterpret_cast<void***>(ins))[3];
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionFunction), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        delete ins;

        //ins = new GameInstructionArray();
        //VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionArray), 14u, 0x40u, &dwVirtualProtectBackup);
        //oldFunc.vt_GameInstructionArray = GVt.vt_GameInstructionArray[3];
        //GVt.vt_GameInstructionArray[3] = (*reinterpret_cast<void***>(ins))[3];
        //VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionArray), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        //delete ins;
        //
        ins = new GameInstructionAssignment();
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionAssignment), 14u, 0x40u, &dwVirtualProtectBackup);
        oldFunc.vt_GameInstructionAssignment = GVt.vt_GameInstructionAssignment[3];
        GVt.vt_GameInstructionAssignment[3] = (*reinterpret_cast<void***>(ins))[3];
        VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionAssignment), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        delete ins;

        //ins = new GameInstructionNewExpression();
        //VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionNewExpression), 14u, 0x40u, &dwVirtualProtectBackup);
        //oldFunc.vt_GameInstructionNewExpression = GVt.vt_GameInstructionNewExpression[3];
        //GVt.vt_GameInstructionNewExpression[3] = (*reinterpret_cast<void***>(ins))[3];
        //VirtualProtect(reinterpret_cast<LPVOID>(GVt.vt_GameInstructionNewExpression), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        //delete ins;
    }
#endif
}


client::EHIdentifierHandle endFrameHandle;


void scriptProfiler::perFrame() {
    if (!engineFrameEnd)
        GProfilerAdapter->perFrame();

    if (!waitForAdapter.empty()) {
        compileScope.reset();
        callExtScope.reset();
        preprocFileScope.reset();

        if (false) {
#ifdef WITH_CHROME    
        } else if (waitForAdapter == "Chrome") {
            auto chromeAdapter = std::dynamic_pointer_cast<AdapterChrome>(GProfilerAdapter);
            if (!chromeAdapter) {
                std::shared_ptr<ProfilerAdapter> newAdapter = std::make_shared<AdapterChrome>();
                GProfilerAdapter.swap(newAdapter);
                newAdapter->cleanup();
            }
#endif
#ifdef WITH_BROFILER
        } else if (waitForAdapter == "Brofiler") {
            auto brofilerAdapter = std::dynamic_pointer_cast<AdapterBrofiler>(GProfilerAdapter);
            if (!brofilerAdapter) {
                std::shared_ptr<ProfilerAdapter> newAdapter = std::make_shared<AdapterBrofiler>();
                GProfilerAdapter.swap(newAdapter);
                newAdapter->cleanup();
            }
#endif
        } else if (waitForAdapter == "Arma") {
            auto armaAdapter = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
            if (!armaAdapter) {
                std::shared_ptr<ProfilerAdapter> newAdapter = std::make_shared<AdapterArmaDiag>();
                GProfilerAdapter.swap(newAdapter);
                newAdapter->cleanup();
            }
        } else if (waitForAdapter == "Tracy") {
            auto tracyAdapter = std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter);
            if (!tracyAdapter) {
                std::shared_ptr<ProfilerAdapter> newAdapter = std::make_shared<AdapterTracy>();
                GProfilerAdapter.swap(newAdapter);
                newAdapter->cleanup();
            }
        }
        waitForAdapter.clear();
    }


    // Try to keep CBA PFH's updated

    if (hasCBA)
    {
        for (auto pfhEntry : intercept::sqf::get_variable(intercept::sqf::mission_namespace(), "cba_common_perFrameHandlerArray").to_array())
        {
            auto pfhEntryArray = pfhEntry.to_array();
            // https://github.com/CBATeam/CBA_A3/blob/263286f95453697bfb296937cb1a896c7885e682/addons/common/init_perFrameHandler.sqf#L32
            // _x params ["_function", "_delay", "_delta", "", "_args", "_handle"];

            if (pfhEntryArray.front().type_enum() != game_data_type::CODE)
            {
                // Something is fucked
                hasCBA = false;
                break;
            }

            auto bodyCode = static_cast<game_data_code*>(pfhEntryArray.front().data.get());
            if (bodyCode->instructions.empty()) {
                continue; // lol someone added a empty PFH?
            }
            if (codeHasScopeInstruction(bodyCode->instructions))
                continue;

            auto& funcPath = bodyCode->instructions.front()->sdp->sourcefile;
            auto& str = bodyCode->code_string;
            //#TODO pass instructions to getScriptName and check if there is a "scriptName" or "scopeName" unary command call
            r_string scriptName(getScriptName(str, funcPath, 32));

            //if (scriptName.empty()) scriptName = "<unknown>";

            if (bodyCode->instructions.size() > 3 && !scriptName.empty())// && scriptName != "<unknown>"
                addScopeInstruction(bodyCode->instructions, scriptName);
        }


        for (auto pfhEntry : intercept::sqf::get_variable(intercept::sqf::mission_namespace(), "cba_common_waitAndExecArray").to_array())
        {
            auto pfhEntryArray = pfhEntry.to_array();
            // [time, func, args]

            if (pfhEntryArray.size() != 3 || pfhEntryArray[1].type_enum() != game_data_type::CODE)
            {
                // Something is fucked
                hasCBA = false;
                break;
            }

            auto bodyCode = static_cast<game_data_code*>(pfhEntryArray[1].data.get());
            if (bodyCode->instructions.empty()) {
                continue; // lol someone added a empty PFH?
            }
            if (codeHasScopeInstruction(bodyCode->instructions))
                continue;

            auto& funcPath = bodyCode->instructions.front()->sdp->sourcefile;
            auto& str = bodyCode->code_string;
            //#TODO pass instructions to getScriptName and check if there is a "scriptName" or "scopeName" unary command call
            r_string scriptName(getScriptName(str, funcPath, 32));

            //if (scriptName.empty()) scriptName = "<unknown>";

            if (bodyCode->instructions.size() > 3 && !scriptName.empty())// && scriptName != "<unknown>"
                addScopeInstruction(bodyCode->instructions, scriptName);
        }


        for (auto pfhEntry : intercept::sqf::get_variable(intercept::sqf::mission_namespace(), "cba_common_nextFrameBufferA").to_array())
        {
            auto pfhEntryArray = pfhEntry.to_array();
            // [args, code]

            if (pfhEntryArray.size() != 2 || pfhEntryArray[1].type_enum() != game_data_type::CODE)
            {
                // Something is fucked
                hasCBA = false;
                break;
            }

            auto bodyCode = static_cast<game_data_code*>(pfhEntryArray[1].data.get());
            if (bodyCode->instructions.empty()) {
                continue; // lol someone added a empty PFH?
            }
            if (codeHasScopeInstruction(bodyCode->instructions))
                continue;

            auto& funcPath = bodyCode->instructions.front()->sdp->sourcefile;
            auto& str = bodyCode->code_string;
            //#TODO pass instructions to getScriptName and check if there is a "scriptName" or "scopeName" unary command call
            r_string scriptName(getScriptName(str, funcPath, 32));

            //if (scriptName.empty()) scriptName = "<unknown>";

            if (bodyCode->instructions.size() > 3 && !scriptName.empty())// && scriptName != "<unknown>"
                addScopeInstruction(bodyCode->instructions, scriptName);
        }

        for (auto pfhEntry : intercept::sqf::get_variable(intercept::sqf::mission_namespace(), "cba_common_waitUntilAndExecArray").to_array())
        {
            auto pfhEntryArray = pfhEntry.to_array();
            // [condcode, actioncode, args]

            if (pfhEntryArray.size() != 3 || pfhEntryArray[1].type_enum() != game_data_type::CODE || pfhEntryArray[0].type_enum() != game_data_type::CODE)
            {
                // Something is fucked
                hasCBA = false;
                break;
            }

            for(auto& cd : {pfhEntryArray[0], pfhEntryArray[1]})
            {
                auto bodyCode = static_cast<game_data_code*>(cd.data.get());
                if (bodyCode->instructions.empty()) {
                    continue; // lol someone added a empty PFH?
                }
                if (codeHasScopeInstruction(bodyCode->instructions))
                    continue;

                auto& funcPath = bodyCode->instructions.front()->sdp->sourcefile;
                auto& str = bodyCode->code_string;
                //#TODO pass instructions to getScriptName and check if there is a "scriptName" or "scopeName" unary command call
                r_string scriptName(getScriptName(str, funcPath, 32));

                //if (scriptName.empty()) scriptName = "<unknown>";

                if (bodyCode->instructions.size() > 3 && !scriptName.empty())// && scriptName != "<unknown>"
                    addScopeInstruction(bodyCode->instructions, scriptName);
            }
        }

    }


}

void scriptProfiler::preInit() {

    //auto updateFunc = [this]() {
    //    //See scriptProfiler::perFrame();
    //};

    //if (!sqf::has_interface())
    //    endFrameHandle = client::addMissionEventHandler<client::eventhandlers_mission::EachFrame>(updateFunc);
    //else
    //    endFrameHandle = client::addMissionEventHandler<client::eventhandlers_mission::Draw3D>(updateFunc);
}

void copyToClipboard(r_string txt) {
#ifndef __linux__
    if (OpenClipboard(NULL) == 0) {
    } else {
        if (EmptyClipboard() == 0) {
        } else {
            // GPTR = GMEM_FIXED + GMEM_ZEROINIT, returns a ptr, no need for GlobalLock/GlobalUnlock
            char *pClipboardData = (char *) GlobalAlloc(GPTR, txt.length());

            if (pClipboardData == NULL) {
                return;
            }
            strncpy_s(pClipboardData, txt.length(), txt.data(), _TRUNCATE);

            // if success, system owns the memory, if fail, free it from the heap
            if (SetClipboardData(CF_TEXT, pClipboardData) == NULL) {
                GlobalFree(pClipboardData);
            } else {
                if (CloseClipboard() == 0) {
                }
            }
        }
    }
#endif
}

class ArmaScriptProfiler_ProfInterface {
public:
    virtual game_value createScope(r_string name) {
        auto data = std::make_shared<GameDataProfileScope::scopeData>(name, game_value(),
            GProfilerAdapter->createScope(name, name, 0));

        return game_value(new GameDataProfileScope(std::move(data)));
    }

    //v2
    virtual game_value createScopeCustomThread(r_string name, uint64_t threadID) {
        auto data = std::make_shared<GameDataProfileScope::scopeData>(name, game_value(),
            GProfilerAdapter->createScope(name, name, 0), threadID);

        return game_value(new GameDataProfileScope(std::move(data)));
    }

    //v3
    virtual void ASM_createScopeInstr(game_state& state, game_data_code* bodyCode) {
        if (bodyCode->instructions.empty()) {
            return;
        }

#ifdef WITH_BROFILER
        if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
            r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);
            brofilerData->evtDt->sourceCode = src;
        }
#endif

        auto& funcPath = bodyCode->instructions.front()->sdp->sourcefile;

        auto scriptName = tryGetNameFromInitFunctions(state);
        if (!scriptName) scriptName = tryGetNameFromCBACompile(state);
        if (!scriptName) scriptName = getScriptName(bodyCode->code_string, funcPath, 32);
        //if (scriptName.empty()) scriptName = "<unknown>";

        if (bodyCode->instructions.size() > 4 && scriptName && !scriptName->empty())// && scriptName != "<unknown>"
            addScopeInstruction(bodyCode->instructions, *scriptName);

        return;
    }

    virtual game_value compile(game_state& state, game_value_parameter code, bool final) {
        if (final) return compileRedirectFinal(state, code);
        return compileRedirect2(state, code);
    }

};

static ArmaScriptProfiler_ProfInterface profIface;


void scriptProfiler::registerInterfaces() {
    client::host::register_plugin_interface("ArmaScriptProfilerProfIFace"sv, 1, &profIface);
    client::host::register_plugin_interface("ArmaScriptProfilerProfIFace"sv, 2, &profIface);
    if (!getCommandLineParam("-profilerNoInstrumentation"sv)) //Don't offer ourselves to ASM
    client::host::register_plugin_interface("ArmaScriptProfilerProfIFace"sv, 3, &profIface);
}
