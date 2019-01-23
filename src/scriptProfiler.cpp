#include "scriptProfiler.hpp"
#include <intercept.hpp>
#include <numeric>
#include <random>
#include <Windows.h>
#include "ProfilerAdapter.hpp"
#include "AdapterArmaDiag.hpp"
#include "AdapterBrofiler.hpp"
#include "Event.h"
#include "AdapterChrome.hpp"
#include "AdapterTracy.hpp"

using namespace intercept;
using namespace std::chrono_literals;
static sqf_script_type GameDataProfileScope_type;
std::shared_ptr<ProfilerAdapter> GProfilerAdapter; //Needs to be above!! profiler
scriptProfiler profiler{};
bool instructionLevelProfiling = false;

class GameDataProfileScope : public game_data {

public:
    class scopeData {
    public:
        scopeData(r_string _name, game_value thisArgs, std::shared_ptr<ScopeInfo> scopeInfo) : name(std::move(_name)) {
			if (!scopeInfo) return;

			scopeTempStorage = GProfilerAdapter->enterScope(scopeInfo);
			GProfilerAdapter->setThisArgs(scopeTempStorage, thisArgs);
        }
        ~scopeData() {
			GProfilerAdapter->leaveScope(scopeTempStorage);
        }
		std::shared_ptr<ScopeTempStorage> scopeTempStorage;
		r_string name;
    };

    GameDataProfileScope() {}
    GameDataProfileScope(std::shared_ptr<scopeData>&& _data) : data(std::move(_data)) {}
    void lastRefDeleted() const override { delete this; }
    const sqf_script_type& type() const override { return GameDataProfileScope_type; }
    ~GameDataProfileScope() override {

    }

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

class GameInstructionProfileScopeStart : public game_instruction {
public:
    r_string name;
   std::shared_ptr<ScopeInfo> scopeInfo;

    void lastRefDeleted() const override {
        rv_allocator<GameInstructionProfileScopeStart>::destroy_deallocate(const_cast<GameInstructionProfileScopeStart *>(this), 1);
    }

    bool exec(game_state& state, vm_context& ctx) override {
        static r_string lastScopeStart = "";
	    if ((!GProfilerAdapter->IsScheduledSupported() &&/*ctx.scheduled || */sqf::can_suspend()) || (lastScopeStart.length() && lastScopeStart == name)) return false;

        auto data = std::make_shared<GameDataProfileScope::scopeData>(name,
            state.eval->local->variables.get("_this").value,
			scopeInfo);

        state.eval->local->variables.insert(
            game_variable("1scp"sv, game_value(new GameDataProfileScope(std::move(data))), false)
        );
        lastScopeStart = name;


		if (GProfilerAdapter->IsScheduledSupported() && sqf::can_suspend()) {
			if (auto chromeStorage = std::dynamic_pointer_cast<ScopeTempStorageChrome>(data->scopeTempStorage))
				chromeStorage->threadID = reinterpret_cast<uint64_t>(&ctx);
		}



        return false;
    }
    int stack_size(void* t) const override { return 0; }
    r_string get_name() const override { return "GameInstructionProfileScopeStart"sv; }
    ~GameInstructionProfileScopeStart() override {}
};

game_value createProfileScope(uintptr_t st, game_value_parameter name) {
    if (sqf::can_suspend()) return {};
    static r_string profName("scriptProfiler.cpp");

    game_state* state = (game_state*) st;

    auto data = std::make_shared<GameDataProfileScope::scopeData>(name,
    sqf::str(state->eval->local->variables.get("_this").value), //#TODO remove this. We don't want this
	GProfilerAdapter->createScope((r_string)name, profName, __LINE__)
    );
	//#TODO retrieve line from callstack
    return game_value(new GameDataProfileScope(std::move(data)));
}

game_value profilerSleep(uintptr_t) {
    std::this_thread::sleep_for(17ms);
    return {};
}

game_value profilerCaptureFrame(uintptr_t) {
	auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
	if (!armaDiagProf) return {};
	armaDiagProf->captureFrame();
    return {};
}

game_value profilerCaptureFrames(uintptr_t, game_value_parameter count) {
	auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
	if (!armaDiagProf) return {};
	armaDiagProf->captureFrames(static_cast<float>(count));
    return {};
}

game_value profilerCaptureSlowFrame(uintptr_t, game_value_parameter threshold) {
	auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
	if (!armaDiagProf) return {};
	armaDiagProf->captureSlowFrame(chrono::milliseconds(static_cast<float>(threshold)));
    return {};
}

game_value profilerCaptureTrigger(uintptr_t) {
	auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
	if (!armaDiagProf) return {};
	armaDiagProf->captureTrigger();
    return {};
}

game_value profilerTrigger(uintptr_t) {
 auto armaDiagProf = std::dynamic_pointer_cast<AdapterArmaDiag>(GProfilerAdapter);
	if (!armaDiagProf) return {};
	armaDiagProf->profilerTrigger();
    return {};
}

game_value profilerLog(uintptr_t, game_value_parameter message) {
	GProfilerAdapter->addLog(message);
    return {};
}

game_value profilerSetOutputFile(uintptr_t st, game_value_parameter file) {
	
	auto chromeAdapter = std::dynamic_pointer_cast<AdapterChrome>(GProfilerAdapter);
	if (!chromeAdapter) {
		game_state* state = reinterpret_cast<game_state*>(st);
		state->eval->_errorMessage = "not using ChromeAdapter";
		state->eval->_errorType = game_state::game_evaluator::evaluator_error_type::tg90; //No idea what tg90 is..

		return {};
	}

	chromeAdapter->setTargetFile(static_cast<std::string_view>(static_cast<r_string>(file)));
	return {};
}

game_value profilerSetAdapter(uintptr_t st, game_value_parameter file) {
	
	r_string adap = file;

	profiler.waitForAdapter = adap;
	return {};
}

game_value profilerSetCounter(uintptr_t st, game_value_parameter name, game_value_parameter value) {
	GProfilerAdapter->setCounter(name,value);
	return {};
}

//profiles script like diag_codePerformance
game_value profileScript(uintptr_t stat, game_value_parameter par) {
	game_state* state = (game_state*) stat;
	code _code = par[0];
	int runs = par.get(2).value_or(10000);

	auto _emptyCode = sqf::compile("");

	//CBA fastForEach

	if (par.get(1) && !par[1].is_nil()) {
		state->eval->local->variables.insert({ "_this"sv,  par[1] });
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


std::regex getScriptName_acefncRegex(R"(\\?[xz]\\([^\\]*)\\addons\\([^\\]*)\\(?:functions\\)?fnc?_([^.]*)\.sqf)", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
std::regex getScriptName_LinePreprocRegex(R"(#line [0-9]* "([^"]*))", std::regex_constants::ECMAScript | std::regex_constants::optimize | std::regex_constants::icase);
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
        return std::string(scriptNameMatch[1]); //cba_events_fnc_playerEH_EachFrame
    }
    //private _fnc_scriptName = 'CBA_fnc_currentUnit';
    if (std::regex_search(str.begin(), str.end(), scriptNameMatch, getScriptName_scriptNameVarRegex)) {
        return std::string(scriptNameMatch[1]); //CBA_fnc_currentUnit
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
        //A3\functions_f\Animation\Math\fn_deltaTime.sqf
        if (!filePathFromLine.empty() && std::regex_search(filePathFromLine, pathMatchFromline, getScriptName_bisfncRegex)) {
            return "BIS_fnc_" + std::string(pathMatchFromline[1]); //BIS_fnc_deltaTime
        }
    }


    if (str.find("createProfileScope", 0) != -1) return "<unknown>"; //Don't remember why I did this :D

    if (returnFirstLineIfNoName) {
        auto linebreak = str.find("\n", 0);
        if (linebreak < 20) {
            auto linebreak2 = str.find("\n", linebreak);
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

	auto removeEmptyLines = [&](int count) {
		for (size_t i = 0; i < count; i++) {
			auto found = output.find("\n\n");
			if (found != std::string::npos)
				output.replace(found, 2, 1, '\n');
			else if (output.front() == '\n') {
				output.erase(0, 1);
			} else {
				output.replace(0, output.find("\n"), 1, '\n');
			}
		}
	};

	auto readLineMacro = [&]() {
		curPos += 6;
		auto numberEnd = std::find(curPos, end, ' ');
		auto number = std::stoi(std::string(static_cast<const char*>(curPos), numberEnd - curPos));
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
		if (*(nameEnd + 1) == '\r') nameEnd++;
		curPos = nameEnd + 2;
		//if (inWantedFile && *curPos == '\n') {
		//	curPos++;
		//}//after each #include there is a newline which we also don't want


		if (wasInWantedFile) {
			output.append("#include \"");
			output.append(name);
			output.append("\"\n");
			curLine++;
		}


		if (curPos > end) return false;
		return true;
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
		size_t start_pos = 0;
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
    virtual bool exec(game_state& state, vm_context& t) {
        //static const r_string InstrName = "I_Const"sv;
        //static Brofiler::EventDescription* autogenerated_description = ::Brofiler::EventDescription::Create(InstrName, __FILE__, __LINE__);
        //Brofiler::Event autogenerated_event_639(*autogenerated_description);
        //
        //if (autogenerated_event_639.data)
        //    autogenerated_event_639.data->thisArgs = value;

        typedef bool(__thiscall *OrigEx)(game_instruction*, game_state&, vm_context&);
        return reinterpret_cast<OrigEx>(oldFunc.vt_GameInstructionConst)(this, state, t);
    }
    virtual int stack_size(void* t) const { return 0; }
    virtual r_string get_name() const { return ""sv; }
};




void addScopeInstruction(game_data_code* bodyCode, std::string scriptName) {
#ifndef _WIN64
#error "no x64 hash codes yet"
#endif
    auto lt = typeid(bodyCode->instructions->data()[0]).hash_code();
    auto rt = typeid(GameInstructionProfileScopeStart).hash_code();

    if (lt == rt) return;



    auto& funcPath = bodyCode->instructions->front()->sdp.sourcefile;
    r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);



    //Insert instruction to set _x
    ref<GameInstructionProfileScopeStart> curElInstruction = rv_allocator<GameInstructionProfileScopeStart>::create_single();
    curElInstruction->name = scriptName;
    curElInstruction->sdp = bodyCode->instructions->front()->sdp;
    curElInstruction->scopeInfo = GProfilerAdapter->createScope(curElInstruction->name,
        funcPath.empty() ? curElInstruction->name : funcPath,
        curElInstruction->sdp.sourceline);
    //if (scriptName == "<unknown>")
    //curElInstruction->eventDescription->source = src;


    auto oldInstructions = bodyCode->instructions;
    ref<compact_array<ref<game_instruction>>> newInstr = compact_array<ref<game_instruction>>::create(*oldInstructions, oldInstructions->size() + 1);

    std::copy(oldInstructions->begin(), oldInstructions->begin() + oldInstructions->size(), newInstr->begin() + 1);
    newInstr->data()[0] = curElInstruction;
    bodyCode->instructions = newInstr;



    static const size_t ConstTypeIDHash = 0x0a56f03038a03360;
    for (auto& it : *bodyCode->instructions) {
        auto typeHash = typeid(*it.get()).hash_code();
        auto typeN = typeid(*it.get()).raw_name();
        if (typeHash != ConstTypeIDHash) continue;
        GameInstructionConst* inst = static_cast<GameInstructionConst*>(it.get());
        if (inst->value.type_enum() != GameDataType::CODE) continue;

        auto bodyCode = static_cast<game_data_code*>(inst->value.data.get());
        if (bodyCode->instructions && bodyCode->instructions->size() > 20)
           addScopeInstruction(bodyCode, scriptName);
    }
}


game_value compileRedirect2(uintptr_t st, game_value_parameter message) {
	if (!profiler.compileScope) {
		static r_string compileEventText("compile");
		static r_string profName("scriptProfiler.cpp");
		profiler.compileScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
	}

	auto tempData = GProfilerAdapter->enterScope(profiler.compileScope);

    game_state* state = reinterpret_cast<game_state*>(st);
    r_string str = message;

	auto comp = sqf::compile(str);
	auto bodyCode = static_cast<game_data_code*>(comp.data.get());
	if (!bodyCode->instructions) {
		GProfilerAdapter->leaveScope(tempData);
		return comp;
	}

	if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
		r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);
		brofilerData->evtDt->sourceCode = src;
	}

	GProfilerAdapter->leaveScope(tempData);

    auto& funcPath = bodyCode->instructions->front()->sdp.sourcefile;
    std::string scriptName = getScriptName(str, funcPath, 32);
    //if (scriptName.empty()) scriptName = "<unknown>";

    if (bodyCode->instructions && !scriptName.empty() && scriptName != "<unknown>")
        addScopeInstruction(bodyCode, scriptName);

    return comp;
}

game_value callExtensionRedirect(uintptr_t st, game_value_parameter ext, game_value_parameter msg) {
	if (!profiler.callExtScope) {
		static r_string compileEventText("callExtension");
		static r_string profName("scriptProfiler.cpp");
		profiler.callExtScope = GProfilerAdapter->createScope(compileEventText, profName, __LINE__);
	}

	auto tempData = GProfilerAdapter->enterScope(profiler.callExtScope);

    auto res = sqf::call_extension(ext,msg);

	if (auto brofilerData = std::dynamic_pointer_cast<ScopeTempStorageBrofiler>(tempData)) {
		brofilerData->evtDt->thisArgs = ext;
		brofilerData->evtDt->sourceCode = msg;
	}

	GProfilerAdapter->leaveScope(tempData);

	return res;
}


std::optional<std::string> getCommandLineParam(std::string_view needle) {
	std::string commandLine = GetCommandLineA();
    auto found = commandLine.find(needle);
    if (found != std::string::npos) {
        auto spacePos = commandLine.find(' ', found + needle.length() + 1);
        auto valueLength = spacePos - (found + needle.length() + 1);
        auto adapterStr = commandLine.substr(found + needle.length() + 1, valueLength);
        if (adapterStr.back() == '"')
            adapterStr = adapterStr.substr(0, adapterStr.length() - 1);
		return adapterStr;
    }
	return {};
}

scriptProfiler::scriptProfiler() {
	std::string commandLine = GetCommandLineA();

	if (getCommandLineParam("-profilerEnableInstruction"sv)) {
		instructionLevelProfiling = true;
	}

	auto startAdapter = getCommandLineParam("-profilerAdapter"sv);

    if (startAdapter) {
		if (*startAdapter == "Chrome"sv) {
			auto chromeAdapter = std::make_shared<AdapterChrome>();
			GProfilerAdapter = chromeAdapter;

			auto chromeOutput = getCommandLineParam("-profilerOutput"sv);
			if (chromeOutput)
				chromeAdapter->setTargetFile(*chromeOutput);
		} else if (*startAdapter == "Brofiler"sv) {
			GProfilerAdapter = std::make_shared<AdapterBrofiler>();
		} else if (*startAdapter == "Arma"sv) {
			GProfilerAdapter = std::make_shared<AdapterArmaDiag>();
		} else if (*startAdapter == "Tracy"sv) {
            GProfilerAdapter = std::make_shared<AdapterTracy>();
        }
    } else {
	    GProfilerAdapter = std::make_shared<AdapterBrofiler>();
    }
}


scriptProfiler::~scriptProfiler() {}


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
			10
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
		game_functions(std::string name) : _name(name.c_str()) {}
		r_string _name;
		game_functions() noexcept {}
		const char *get_map_key() const noexcept { return _name.data(); }
	};

	class game_operators : public auto_array<gsOperator>, public gsFuncBase {
	public:
		game_operators(std::string name) : _name(name.c_str()) {}
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

			auto temp = GProfilerAdapter->enterScope(found->second);
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
			auto instructionName = _operators->_name;

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
			auto instructionName = _functions->_name;

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



void scriptProfiler::preStart() {
    static Brofiler::ThreadScope mainThreadScope("Frame");

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

	
	static auto _profilerCompile = client::host::register_sqf_command("compile", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
    //static auto _profilerCompile2 = client::host::register_sqf_command("compile2", "Profiler redirect", compileRedirect, game_data_type::CODE, game_data_type::STRING);
    //static auto _profilerCompile3 = client::host::register_sqf_command("compile3", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
    static auto _profilerCompileF = client::host::register_sqf_command("compileFinal", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
	static auto _profilerCallExt = client::host::register_sqf_command("callExtension", "Profiler redirect", callExtensionRedirect, game_data_type::STRING, game_data_type::STRING, game_data_type::STRING);
	static auto _profilerProfScript = client::host::register_sqf_command("profileScript", "Profiler redirect", profileScript, game_data_type::ARRAY, game_data_type::ARRAY);


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

}


client::EHIdentifierHandle endFrameHandle;


void scriptProfiler::perFrame() {
	GProfilerAdapter->perFrame();

	if (!waitForAdapter.empty()) {
		if (waitForAdapter == "Chrome") {
			auto chromeAdapter = std::dynamic_pointer_cast<AdapterChrome>(GProfilerAdapter);
			if (!chromeAdapter) {
				std::shared_ptr<ProfilerAdapter> newAdapter = std::make_shared<AdapterChrome>();
				GProfilerAdapter.swap(newAdapter);
				newAdapter->cleanup();
			}
		} else if (waitForAdapter == "Brofiler") {
			auto brofilerAdapter = std::dynamic_pointer_cast<AdapterBrofiler>(GProfilerAdapter);
			if (!brofilerAdapter) {
				std::shared_ptr<ProfilerAdapter> newAdapter = std::make_shared<AdapterBrofiler>();
				GProfilerAdapter.swap(newAdapter);
				newAdapter->cleanup();
			}
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
};

static ArmaScriptProfiler_ProfInterface profIface;


void scriptProfiler::registerInterfaces() {
    client::host::register_plugin_interface("ArmaScriptProfilerProfIFace"sv, 1, &profIface);
}
