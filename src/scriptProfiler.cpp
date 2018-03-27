#include "scriptProfiler.hpp"
#include <client/headers/intercept.hpp>
#include <sstream>
#include <numeric>
#include <Brofiler.h>

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
            uint64_t _scopeID, Brofiler::EventDescription* evtDscr = nullptr) : name(std::move(_name)), start(_start), scopeID(_scopeID) {

            if (evtDscr)
                evtDt = Brofiler::Event::Start(*evtDscr);
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
        if (ctx.scheduled || sqf::can_suspend()) return false;
        auto data = std::make_shared<GameDataProfileScope::scopeData>(name, std::chrono::high_resolution_clock::now(), profiler.startNewScope(), eventDescription);

        state.eval->varspace->varspace.insert(
            game_variable("1scp"sv, game_value(new GameDataProfileScope(std::move(data))), false)
        );
        return false;
    }
    int stack_size(void* t) const override { return 0; }
    r_string get_name() const override { return "GameInstructionProfileScopeStart"sv; }
    ~GameInstructionProfileScopeStart() override {}

};

game_value createProfileScope(uintptr_t st, game_value_parameter name) {
    if (sqf::can_suspend()) return {};

    game_state* state = (game_state*) st;


    auto data = std::make_shared<GameDataProfileScope::scopeData>(name, std::chrono::high_resolution_clock::now(), profiler.startNewScope());
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
  
std::string getScriptName(const r_string& str) {
    if (str.empty() || str.front() == '[' && *(str.begin() + str.length() - 1) == ']') return "";
    if (str.find("createProfileScope", 0) == -1) {
        if (str.find("#line", 0) != -1) {
            auto offs = str.find("#line", 0);
            auto length = str.find("\"", offs + 9) - offs - 9;
            auto name = std::string_view(str.data() + offs + 9, length);
            return std::string(name);
        } else {
            if (str.find("scriptname", 0) != -1) __debugbreak();
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
            } else if (str.length() < 100) {
                auto name = std::string(str.data());
                std::transform(name.begin(), name.end(), name.begin(), [](char ch) {
                    if (ch == '"') return '\'';
                    return ch;
                });
                return name;
            } else
                return "unknown";
            //OutputDebugStringA(str.data());
            //OutputDebugStringA("\n\n#######\n");
        }
    }
    return "";
}

game_value compileRedirect(uintptr_t st, game_value_parameter message) {
    game_state* state = reinterpret_cast<game_state*>(st);
    r_string str = message;

    std::string scriptName = getScriptName(str);

    if (!scriptName.empty())
        str = r_string("private _scoooope = createProfileScope \""sv) + scriptName + "\"; "sv + str;
    return sqf::compile(str);
}

game_value compileRedirect2(uintptr_t st, game_value_parameter message) {
    game_state* state = reinterpret_cast<game_state*>(st);
    r_string str = message;

    std::string scriptName = getScriptName(str);
    auto comp = sqf::compile(str);

    auto bodyCode = static_cast<game_data_code*>(comp.data.get());
    if (!bodyCode->instructions) return comp;

    //Insert instruction to set _x
    ref<GameInstructionProfileScopeStart> curElInstruction = rv_allocator<GameInstructionProfileScopeStart>::create_single();
    curElInstruction->name = scriptName;
	curElInstruction->sdp = bodyCode->instructions->front()->sdp;
	curElInstruction->eventDescription = Brofiler::EventDescription::Create("scope",curElInstruction->sdp.sourcefile.c_str(),
			curElInstruction->sdp.sourceline);


    auto oldInstructions = bodyCode->instructions;
    ref<compact_array<ref<game_instruction>>> newInstr = compact_array<ref<game_instruction>>::create(*oldInstructions, oldInstructions->size() + 1);

    std::_Copy_no_deprecate(oldInstructions->data(), oldInstructions->data() + oldInstructions->size(), newInstr->data() + 1);
    newInstr->data()[0] = curElInstruction;
    bodyCode->instructions = newInstr;
    return comp;
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
    static auto _profilerCompile2 = client::host::register_sqf_command("compile2", "Profiler redirect", compileRedirect, game_data_type::CODE, game_data_type::STRING);
    static auto _profilerCompile3 = client::host::register_sqf_command("compile3", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
    static auto _profilerCompileF = client::host::register_sqf_command("compileFinal", "Profiler redirect", compileRedirect2, game_data_type::CODE, game_data_type::STRING);
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
        static Brofiler::EventDescription* autogenerated_description_276 = ::Brofiler::EventDescription::Create( "Frame", "scriptProfiler.cpp", 276 ); 
        
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
        if (currentScope != scope.get()) __debugbreak(); //wut?
                                                         /*
                                                         * This can happen :/
                                                         * Create 2 scopes at the start of a function. At the end scope 1 might end before scope 2.
                                                         */
        if (scope->parent)
            currentScope = dynamic_cast<profileScope*>(scope->parent);
        else
            currentScope = nullptr;
        if (scope->parent && !currentScope) __debugbreak(); //wutwatwut?!! 
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
        auto data = std::make_shared<GameDataProfileScope::scopeData>(name, std::chrono::high_resolution_clock::now(), profiler.startNewScope());

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
