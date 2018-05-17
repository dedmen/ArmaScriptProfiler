#include "scriptProfiler.hpp"
#include <intercept.hpp>
#include <sstream>
#include <numeric>
#include <Brofiler.h>
#include <random>

using namespace intercept;
using namespace std::chrono_literals;
static sqf_script_type GameDataProfileScope_type;
scriptProfiler profiler{};
class GameDataProfileScope : public game_data {

public:
    class scopeData {
    public:
        scopeData(r_string _name,
            std::chrono::high_resolution_clock::time_point _start,
            uint64_t _scopeID, game_value thisArgs, Brofiler::EventDescription* evtDscr = nullptr) : name(std::move(_name)), start(_start), scopeID(_scopeID) {

            if (evtDscr)
                evtDt = Brofiler::Event::Start(*evtDscr);
            if (evtDt)
                evtDt->thisArgs = thisArgs;
        }
        ~scopeData() {
            if (scopeID == -1) return;
            auto timeDiff = std::chrono::high_resolution_clock::now() - start;
            auto runtime = std::chrono::duration_cast<chrono::microseconds>(timeDiff);
            profiler.endScope(scopeID, std::move(name), runtime);
            if (evtDt) Brofiler::Event::Stop(*evtDt);
        }
        Brofiler::EventData* evtDt {nullptr};
        r_string name;
        std::chrono::high_resolution_clock::time_point start;
        uint64_t scopeID = -1;
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
    Brofiler::EventDescription* eventDescription{nullptr};

    void lastRefDeleted() const override {
        rv_allocator<GameInstructionProfileScopeStart>::destroy_deallocate(const_cast<GameInstructionProfileScopeStart *>(this), 1);
    }

    bool exec(game_state& state, vm_context& ctx) override {
	    if (/*ctx.scheduled || */sqf::can_suspend()) return false;
        


        auto data = std::make_shared<GameDataProfileScope::scopeData>(name, std::chrono::high_resolution_clock::now(), profiler.startNewScope(),
            state.eval->varspace->varspace.get("_this").val,
        eventDescription);

        state.eval->varspace->varspace.insert(
            game_variable("1scp"sv, game_value(new GameDataProfileScope(std::move(data))), false)
        );
        return false;
    }
    int stack_size(void* t) const override { return 0; }
    r_string get_name() const override { return "GameInstructionProfileScopeStart"sv; }
    ~GameInstructionProfileScopeStart() override {}

};

std::map<const ref<game_data>, Brofiler::EventDescription*> tempEventDescriptions;

game_value createProfileScope(uintptr_t st, game_value_parameter name) {
    if (sqf::can_suspend()) return {};

    game_state* state = (game_state*) st;
	auto found = tempEventDescriptions.find(name.data);
	if (found == tempEventDescriptions.end()) {
		auto newDesc = Brofiler::EventDescription::Create((r_string)name, "scriptProfiler.cpp", __LINE__);
		found = tempEventDescriptions.insert({ name.data, newDesc }).first;
	}


    auto data = std::make_shared<GameDataProfileScope::scopeData>(name, std::chrono::high_resolution_clock::now(),
    profiler.startNewScope(),
    sqf::str(state->eval->varspace->varspace.get("_this").val), //#TODO remove this. We don't want this
	found->second
    );
    return game_value(new GameDataProfileScope(std::move(data)));
}

game_value profilerSleep(uintptr_t) {
    std::this_thread::sleep_for(17ms);
    return {};
}

game_value profilerCaptureFrame(uintptr_t) {
    profiler.profileStartFrame = sqf::diag_frameno();
    profiler.shouldRecord = true;
    profiler.forceCapture = true;
    return {};
}

game_value profilerCaptureFrames(uintptr_t, game_value_parameter count) {
    profiler.profileStartFrame = sqf::diag_frameno();
    profiler.shouldRecord = true;
    profiler.forceCapture = true;
    profiler.framesToGo = static_cast<float>(count);
    profiler.frames.resize(static_cast<float>(count) + 1);
    return {};
}

game_value profilerCaptureSlowFrame(uintptr_t, game_value_parameter threshold) {
    profiler.shouldRecord = true;
    profiler.slowCheck = chrono::milliseconds(static_cast<float>(threshold));
    profiler.triggerMode = true;
    return {};
}

game_value profilerCaptureTrigger(uintptr_t) {
    profiler.shouldRecord = true;
    profiler.trigger = false;
    profiler.triggerMode = true;
    return {};
}

game_value profilerTrigger(uintptr_t) {
    profiler.trigger = true;
    return {};
}

game_value profilerLog(uintptr_t, game_value_parameter message) {
    if (profiler.shouldBeRecording()) {
        profiler.addLog(message);
    }
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
		state->eval->varspace->varspace.insert({ "_this"sv,  par[1] });
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

uint32_t getRandColor() {
    static std::array<uint32_t,140> colors{
        Brofiler::Color::AliceBlue,
        Brofiler::Color::AntiqueWhite,
        Brofiler::Color::Aqua,
        Brofiler::Color::Aquamarine,
        Brofiler::Color::Azure,
        Brofiler::Color::Beige,
        Brofiler::Color::Bisque,
        Brofiler::Color::Black,
        Brofiler::Color::BlanchedAlmond,
        Brofiler::Color::Blue,
        Brofiler::Color::BlueViolet,
        Brofiler::Color::Brown,
        Brofiler::Color::BurlyWood,
        Brofiler::Color::CadetBlue,
        Brofiler::Color::Chartreuse,
        Brofiler::Color::Chocolate,
        Brofiler::Color::Coral,
        Brofiler::Color::CornflowerBlue,
        Brofiler::Color::Cornsilk,
        Brofiler::Color::Crimson,
        Brofiler::Color::Cyan,
        Brofiler::Color::DarkBlue,
        Brofiler::Color::DarkCyan,
        Brofiler::Color::DarkGoldenRod,
        Brofiler::Color::DarkGray,
        Brofiler::Color::DarkGreen,
        Brofiler::Color::DarkKhaki,
        Brofiler::Color::DarkMagenta,
        Brofiler::Color::DarkOliveGreen,
        Brofiler::Color::DarkOrange,
        Brofiler::Color::DarkOrchid,
        Brofiler::Color::DarkRed,
        Brofiler::Color::DarkSalmon,
        Brofiler::Color::DarkSeaGreen,
        Brofiler::Color::DarkSlateBlue,
        Brofiler::Color::DarkSlateGray,
        Brofiler::Color::DarkTurquoise,
        Brofiler::Color::DarkViolet,
        Brofiler::Color::DeepPink,
        Brofiler::Color::DeepSkyBlue,
        Brofiler::Color::DimGray,
        Brofiler::Color::DodgerBlue,
        Brofiler::Color::FireBrick,
        Brofiler::Color::FloralWhite,
        Brofiler::Color::ForestGreen,
        Brofiler::Color::Fuchsia,
        Brofiler::Color::Gainsboro,
        Brofiler::Color::GhostWhite,
        Brofiler::Color::Gold,
        Brofiler::Color::GoldenRod,
        Brofiler::Color::Gray,
        Brofiler::Color::Green,
        Brofiler::Color::GreenYellow,
        Brofiler::Color::HoneyDew,
        Brofiler::Color::HotPink,
        Brofiler::Color::IndianRed,
        Brofiler::Color::Indigo,
        Brofiler::Color::Ivory,
        Brofiler::Color::Khaki,
        Brofiler::Color::Lavender,
        Brofiler::Color::LavenderBlush,
        Brofiler::Color::LawnGreen,
        Brofiler::Color::LemonChiffon,
        Brofiler::Color::LightBlue,
        Brofiler::Color::LightCoral,
        Brofiler::Color::LightCyan,
        Brofiler::Color::LightGoldenRodYellow,
        Brofiler::Color::LightGray,
        Brofiler::Color::LightGreen,
        Brofiler::Color::LightPink,
        Brofiler::Color::LightSalmon,
        Brofiler::Color::LightSeaGreen,
        Brofiler::Color::LightSkyBlue,
        Brofiler::Color::LightSlateGray,
        Brofiler::Color::LightSteelBlue,
        Brofiler::Color::LightYellow,
        Brofiler::Color::Lime,
        Brofiler::Color::LimeGreen,
        Brofiler::Color::Linen,
        Brofiler::Color::Magenta,
        Brofiler::Color::Maroon,
        Brofiler::Color::MediumAquaMarine,
        Brofiler::Color::MediumBlue,
        Brofiler::Color::MediumOrchid,
        Brofiler::Color::MediumPurple,
        Brofiler::Color::MediumSeaGreen,
        Brofiler::Color::MediumSlateBlue,
        Brofiler::Color::MediumSpringGreen,
        Brofiler::Color::MediumTurquoise,
        Brofiler::Color::MediumVioletRed,
        Brofiler::Color::MidnightBlue,
        Brofiler::Color::MintCream,
        Brofiler::Color::MistyRose,
        Brofiler::Color::Moccasin,
        Brofiler::Color::NavajoWhite,
        Brofiler::Color::Navy,
        Brofiler::Color::OldLace,
        Brofiler::Color::Olive,
        Brofiler::Color::OliveDrab,
        Brofiler::Color::Orange,
        Brofiler::Color::OrangeRed,
        Brofiler::Color::Orchid,
        Brofiler::Color::PaleGoldenRod,
        Brofiler::Color::PaleGreen,
        Brofiler::Color::PaleTurquoise,
        Brofiler::Color::PaleVioletRed,
        Brofiler::Color::PapayaWhip,
        Brofiler::Color::PeachPuff,
        Brofiler::Color::Peru,
        Brofiler::Color::Pink,
        Brofiler::Color::Plum,
        Brofiler::Color::PowderBlue,
        Brofiler::Color::Purple,
        Brofiler::Color::Red,
        Brofiler::Color::RosyBrown,
        Brofiler::Color::RoyalBlue,
        Brofiler::Color::SaddleBrown,
        Brofiler::Color::Salmon,
        Brofiler::Color::SandyBrown,
        Brofiler::Color::SeaGreen,
        Brofiler::Color::SeaShell,
        Brofiler::Color::Sienna,
        Brofiler::Color::Silver,
        Brofiler::Color::SkyBlue,
        Brofiler::Color::SlateBlue,
        Brofiler::Color::SlateGray,
        Brofiler::Color::Snow,
        Brofiler::Color::SpringGreen,
        Brofiler::Color::SteelBlue,
        Brofiler::Color::Tan,
        Brofiler::Color::Teal,
        Brofiler::Color::Thistle,
        Brofiler::Color::Tomato,
        Brofiler::Color::Turquoise,
        Brofiler::Color::Violet,
        Brofiler::Color::Wheat,
        Brofiler::Color::White,
        Brofiler::Color::WhiteSmoke,
        Brofiler::Color::Yellow,
        Brofiler::Color::YellowGreen
    };



    static std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<size_t> colorsDist(0, colors.size() - 1);
    return colors[colorsDist(rng)];
}

std::string getScriptFromFirstLine(game_instruction::sourcedocpos& pos, bool compact) {//https://github.com/dedmen/ArmaDebugEngine/blob/master/BIDebugEngine/BIDebugEngine/Script.cpp
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



game_value compileRedirect2(uintptr_t st, game_value_parameter message) {
    static r_string compileEventText("compile");
    static ::Brofiler::EventDescription* autogenerated_description_356 = Brofiler::EventDescription::Create( compileEventText, "scriptProfiler.cpp", __LINE__ ); 
    Brofiler::Event autogenerated_event_356( *(autogenerated_description_356) );

    game_state* state = reinterpret_cast<game_state*>(st);
    r_string str = message;

	auto comp = sqf::compile(str);
	auto bodyCode = static_cast<game_data_code*>(comp.data.get());
	if (!bodyCode->instructions) return comp;

	r_string src = getScriptFromFirstLine(bodyCode->instructions->front()->sdp, false);
	if (autogenerated_event_356.data)
		autogenerated_event_356.data->sourceCode = src;

    auto& funcPath = bodyCode->instructions->front()->sdp.sourcefile;

    std::string scriptName = getScriptName(str, funcPath, 32);
    if (scriptName.empty()) scriptName = "<unknown>";



    //Insert instruction to set _x
    ref<GameInstructionProfileScopeStart> curElInstruction = rv_allocator<GameInstructionProfileScopeStart>::create_single();
    curElInstruction->name = scriptName;
    curElInstruction->sdp = bodyCode->instructions->front()->sdp;
    curElInstruction->eventDescription = Brofiler::EventDescription::Create(curElInstruction->name, 
    funcPath.empty() ? curElInstruction->name.c_str() : funcPath.c_str(),
            curElInstruction->sdp.sourceline, getRandColor());
    //if (scriptName == "<unknown>")
        curElInstruction->eventDescription->source = src;


    auto oldInstructions = bodyCode->instructions;
    ref<compact_array<ref<game_instruction>>> newInstr = compact_array<ref<game_instruction>>::create(*oldInstructions, oldInstructions->size() + 1);

    std::copy(oldInstructions->begin(), oldInstructions->begin() + oldInstructions->size(), newInstr->begin() + 1);
    newInstr->data()[0] = curElInstruction;
    bodyCode->instructions = newInstr;
    return comp;
}


game_value callExtensionRedirect(uintptr_t st, game_value_parameter ext, game_value_parameter msg) {
    static r_string compileEventText("callExtension");
    static ::Brofiler::EventDescription* autogenerated_description_356 = Brofiler::EventDescription::Create(compileEventText, "scriptProfiler.cpp", __LINE__);
    Brofiler::Event autogenerated_event_356(*(autogenerated_description_356));

    game_state* state = reinterpret_cast<game_state*>(st);
    if (autogenerated_event_356.data) {
        autogenerated_event_356.data->thisArgs = ext;
        autogenerated_event_356.data->sourceCode = msg;
    }

    return sqf::call_extension(ext,msg);
}



scriptProfiler::scriptProfiler() { frames.resize(framesToGo + 1); }


scriptProfiler::~scriptProfiler() {}


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
    static auto _profilerCompile = client::host::register_sqf_command("compile", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
    //static auto _profilerCompile2 = client::host::register_sqf_command("compile2", "Profiler redirect", compileRedirect, game_data_type::CODE, game_data_type::STRING);
    static auto _profilerCompile3 = client::host::register_sqf_command("compile3", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
    static auto _profilerCompileF = client::host::register_sqf_command("compileFinal", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
	static auto _profilerCallExt = client::host::register_sqf_command("callExtension", "Profiler redirect", callExtensionRedirect, game_data_type::STRING, game_data_type::STRING, game_data_type::STRING);
	static auto _profilerProfScript = client::host::register_sqf_command("profileScript", "Profiler redirect", profileScript, game_data_type::ARRAY, game_data_type::ARRAY);
}

client::EHIdentifierHandle endFrameHandle;

Brofiler::EventData* frameEvent = nullptr;

void scriptProfiler::preInit() {
    endFrameHandle = client::addMissionEventHandler<client::eventhandlers_mission::Draw3D>([this]() {
        if (shouldBeRecording() && shouldCapture() && !framesToGo) { //We always want to log if a capture is ready don't we?
            if (currentScope == nullptr)
                capture();
            else
                waitingForCapture = true; //Wait till we left all scopes
        }
        if (triggerMode) {
            frameStart = std::chrono::high_resolution_clock::now();
            frames.clear(); //#TODO recursive...
            frames.resize(framesToGo + 1);
        }
        if (shouldBeRecording() && !waitingForCapture && !isRecording) {//If we are waiting for capture don't clear everything
            frameStart = std::chrono::high_resolution_clock::now();
            isRecording = true;
        }
        if (framesToGo) {
            currentFrame++;
            framesToGo--;
        }

        Brofiler::NextFrame(); 
        static r_string frameName("Frame");
        static Brofiler::EventDescription* autogenerated_description_276 = ::Brofiler::EventDescription::Create(frameName, "scriptProfiler.cpp", __LINE__ );
        
        if (frameEvent)
            Brofiler::Event::Stop(*frameEvent);

        frameEvent = Brofiler::Event::Start(*autogenerated_description_276);
    });
    frames.resize(framesToGo + 1);
}

uint64_t scriptProfiler::startNewScope() {
    if (!shouldBeRecording()) return -1;
    auto newScopeID = lastScopeID++;
    auto newScope = std::make_shared<profileScope>(newScopeID);
    frames[currentFrame].scopes[newScopeID] = newScope;
    if (currentScope) {
        currentScope->subelements.push_back(newScope);
        newScope->parent = currentScope;
    } else {
        frames[currentFrame].elements.push_back(newScope);
    }
    currentScope = newScope.get();
    return newScopeID;
}

void scriptProfiler::endScope(uint64_t scopeID, intercept::types::r_string&& name, chrono::microseconds runtime) {
    if (!shouldBeRecording()) return;
    auto& scope = frames[currentFrame].scopes[scopeID];
    scope->name = name;
    scope->runtime += runtime;
    if (currentScope) {                       //#TODO if scope is lower scope close lower first then current and ignore when current requests to end again
    #ifndef __linux__
        //if (currentScope != scope.get()) __debugbreak(); //wut?
                                                         /*
                                                         * This can happen :/
                                                         * Create 2 scopes at the start of a function. At the end scope 1 might end before scope 2.
                                                         */
    #endif
        if (scope->parent)
            currentScope = dynamic_cast<profileScope*>(scope->parent);
        else
            currentScope = nullptr;
    #ifndef __linux__
        if (scope->parent && !currentScope) __debugbreak(); //wutwatwut?!! 
    #endif
    } else if (waitingForCapture) {
        capture();
    }
}

void scriptProfiler::addLog(intercept::types::r_string msg) {
    auto newLog = std::make_shared<profileLog>(std::move(msg));
    if (currentScope) {
        newLog->parent = currentScope;
        currentScope->subelements.emplace_back(std::move(newLog));
        //currentScope->runtime -= chrono::microseconds(1.5); //try to compensate the calltime for log command
    } else {
        frames[currentFrame].elements.emplace_back(std::move(newLog));
    }
}

void scriptProfiler::iterateElementTree(const frameData& frame, std::function<void(profileElement*, size_t)> func) {
    //https://stackoverflow.com/a/5988138
    for (auto& element : frame.elements) {
        profileElement* node = element.get();
        size_t depth = 0;
        func(node, depth);
        while (node) {
            if (!node->subelements.empty() && node->curElement <= node->subelements.size() - 1) {
                profileElement* prev = node;
                if (node->subelements[node->curElement]) {
                    node = node->subelements[node->curElement].get();
                }
                depth++;
                prev->curElement++;
                func(node, depth);
            } else {

                node->curElement = 0; // Reset counter for next traversal.
                node = node->parent;
                depth--;
            }
        }
    }



}

intercept::types::r_string scriptProfiler::generateLog() {
    //https://pastebin.com/raw/4gfJSwdB
    //#TODO don't really want scopes empty. Just doing it to use it as time reference
    if (frames.size() == 1 && frames[currentFrame].elements.empty() || frames[currentFrame].scopes.empty()) return r_string();
    std::stringstream output;
    auto baseTimeReference = frameStart;
    chrono::milliseconds totalRuntime = std::chrono::duration_cast<chrono::milliseconds>(std::chrono::high_resolution_clock::now() - baseTimeReference);
    output.precision(4);
    output << "* THREAD! YEAH!\n";
    output << std::fixed << "total; " << 0.0 << "; " << totalRuntime.count() << ";\"Frame " << sqf::diag_frameno() << "\"\n";


    auto iterateFunc = [&output, &baseTimeReference](profileElement* element, size_t depth) {
        for (size_t i = 0; i < depth; ++i) {
            output << " ";
        }
        chrono::milliseconds startTime = std::chrono::duration_cast<chrono::milliseconds>(element->getStartTime() - baseTimeReference);
        switch (element->type) {

            case profileElementType::scope:
            {
                output << std::fixed << element->getAsString() << "; " << startTime.count() << "; " << std::chrono::duration_cast<chrono::milliseconds>(element->getRunTime()).count() << ";\"" << element->getAsString() << "\"\n"; //#TODO remove or escape quotes inside string
            }
            break;
            case profileElementType::log:
            {
                output << std::fixed << "log; " << startTime.count() << "; " << 0.0 << ";\"" << element->getAsString() << "\"\n";
            }
            break;
        }

    };


    for (int i = 0; i < frames.size(); ++i) {
        output << "* Frame " << i << "\n";
        iterateElementTree(frames[i], iterateFunc);
    }


    return r_string(output.str());
}

chrono::milliseconds scriptProfiler::totalScriptRuntime() {
    return std::accumulate(frames[currentFrame].elements.begin(), frames[currentFrame].elements.end(), chrono::milliseconds(0), [](chrono::milliseconds accu, const std::shared_ptr<profileElement>& element) -> chrono::milliseconds {
        if (element->type != profileElementType::scope) return accu;
        return accu + element->getRunTime();
    });
}

bool scriptProfiler::shouldCapture() {
    if (frames[currentFrame].elements.empty()) return false;
    if (forceCapture && sqf::diag_frameno() > profileStartFrame) return true;
    if (trigger) return true;
    if (slowCheck.count() != 0.0) return (totalScriptRuntime() > slowCheck);
    return false;
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

void scriptProfiler::capture() {
    auto log = generateLog();
    if (!log.empty()) {
        copyToClipboard(log);
        forceCapture = false;
        shouldRecord = false;
        trigger = false;
        sqf::diag_capture_frame(1);//Show user that we captured stuff
        isRecording = false;
        currentFrame = 0;
        frames.clear(); //#TODO recursive...
        frames.resize(framesToGo + 1);
        triggerMode = false;
    }
    waitingForCapture = false;
}




class ArmaScriptProfiler_ProfInterface {
public:
    virtual game_value createScope(r_string name) {
        auto data = std::make_shared<GameDataProfileScope::scopeData>(name, std::chrono::high_resolution_clock::now(), profiler.startNewScope(), r_string());

        return game_value(new GameDataProfileScope(std::move(data)));
    }
};

static ArmaScriptProfiler_ProfInterface profIface;


void scriptProfiler::registerInterfaces() {
    client::host::register_plugin_interface("ArmaScriptProfilerProfIFace"sv, 1, &profIface);
}

bool scriptProfiler::shouldBeRecording() const {
    return shouldRecord || Brofiler::IsActive();
}
