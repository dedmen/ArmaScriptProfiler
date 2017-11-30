#include "scriptProfiler.hpp"
#include <client/headers/intercept.hpp>
#include <sstream>
#include <numeric>

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
            uint64_t _scopeID) : name(std::move(_name)), start(_start), scopeID(_scopeID) {

        }
        ~scopeData() {
            if (scopeID == -1) return;
            auto timeDiff = std::chrono::high_resolution_clock::now() - start;
            auto runtime = std::chrono::duration_cast<chrono::microseconds>(timeDiff);
            profiler.endScope(scopeID, std::move(name), runtime);
        }

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

game_value createProfileScope(uintptr_t, game_value_parameter name) {
    if (sqf::can_suspend()) return {};
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
    if (profiler.shouldRecord) {
        profiler.addLog(message);
    }
    return {};
}

game_value compileRedirect(uintptr_t, game_value_parameter message) {
    r_string str = message;
    if (str.empty() || str.front() == '[' && *(str.begin() + str.length() - 1) == ']') return sqf::compile(str);
    if (str.find("createProfileScope", 0) == -1) {
        if (str.find("#line", 0) != -1) {
            auto offs = str.find("#line", 0);
            auto length = str.find("\"", offs + 9) - offs - 9;
            auto name = std::string_view(str.begin() + offs + 9, length);
            str = r_string("private _scoooope = createProfileScope \""sv) + name + "\"; "sv + str;
        } else {
            if (str.find("scriptname", 0) != -1) __debugbreak();
            auto linebreak = str.find("\n", 0);
            if (linebreak < 20) {
                auto linebreak2 = str.find("\n", linebreak);
                if (linebreak2 > linebreak) linebreak = linebreak2;
            }
            if (linebreak != -1) {
                auto name = std::string(str.begin(), linebreak);
                std::transform(name.begin(), name.end(), name.begin(), [](char ch) {
                    if (ch == '"') return '\'';
                    if (ch == '\n') return ' ';
                    return ch;
                });
                str = r_string("private _scoooope = createProfileScope \""sv) + name + "\"; "sv + str;
            } else if (str.length() < 100) {
                auto name = std::string(str.begin());
                std::transform(name.begin(), name.end(), name.begin(), [](char ch) {
                    if (ch == '"') return '\'';
                    return ch;
                });
                str = r_string("private _scoooope = createProfileScope \""sv) + name + "\"; "sv + str;
            } else
                str = r_string("private _scoooope = createProfileScope \"unknown\"; "sv) + str;
            //OutputDebugStringA(str.data());
            //OutputDebugStringA("\n\n#######\n");
        }

    }
    return sqf::compile(str);
}



scriptProfiler::scriptProfiler() { frames.resize(framesToGo + 1); }


scriptProfiler::~scriptProfiler() {}


void scriptProfiler::preStart() {
    auto codeType = client::host::registerType("ProfileScope"sv, "ProfileScope"sv, "Dis is a profile scope. It profiles things."sv, "ProfileScope"sv, createGameDataProfileScope);
    GameDataProfileScope_type = codeType.second;
    static auto _createProfileScope = client::host::registerFunction("createProfileScope", "Creates a ProfileScope", createProfileScope, codeType.first, GameDataType::STRING);
    static auto _profilerSleep = client::host::registerFunction("profilerBlockingSleep", "Pauses the engine for 17ms. Used for testing.", profilerSleep, GameDataType::NOTHING);
    static auto _profilerCaptureFrame = client::host::registerFunction("profilerCaptureFrame", "Captures the next frame", profilerCaptureFrame, GameDataType::NOTHING);
    static auto _profilerCaptureFrames = client::host::registerFunction("profilerCaptureFrames", "Captures the next frame", profilerCaptureFrames, GameDataType::NOTHING, GameDataType::SCALAR);
    static auto _profilerCaptureSlowFrame = client::host::registerFunction("profilerCaptureSlowFrame", "Captures the first frame that hits the threshold in ms", profilerCaptureSlowFrame, GameDataType::NOTHING, GameDataType::SCALAR);
    static auto _profilerCaptureTrigger = client::host::registerFunction("profilerCaptureTrigger", "Starts recording and captures the frame that contains a trigger", profilerCaptureTrigger, GameDataType::NOTHING);
    static auto _profilerTrigger = client::host::registerFunction("profilerTrigger", "Trigger", profilerTrigger, GameDataType::NOTHING);
    static auto _profilerLog = client::host::registerFunction("profilerLog", "Logs message to capture", profilerLog, GameDataType::NOTHING, GameDataType::STRING);
    static auto _profilerCompile = client::host::registerFunction("compile", "Profiler redirect", compileRedirect, GameDataType::CODE, GameDataType::STRING);
    static auto _profilerCompileF = client::host::registerFunction("compileFinal", "Profiler redirect", compileRedirect, GameDataType::CODE, GameDataType::STRING);
}

client::EHIdentifierHandle endFrameHandle;

void scriptProfiler::preInit() {
    endFrameHandle = client::addMissionEventHandler<client::eventhandlers_mission::Draw3D>([this]() {
        if (shouldRecord && shouldCapture() && !framesToGo) { //We always want to log if a capture is ready don't we?
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
        if (shouldRecord && !waitingForCapture && !isRecording) {//If we are waiting for capture don't clear everything
            frameStart = std::chrono::high_resolution_clock::now();
            isRecording = true;
        }
        if (framesToGo) {
            currentFrame++;
            framesToGo--;
        }
    });
    frames.resize(framesToGo + 1);
}

uint64_t scriptProfiler::startNewScope() {
    if (!shouldRecord) return -1;
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
    if (!shouldRecord) return;
    auto& scope = frames[currentFrame].scopes[scopeID];
    scope->name = name;
    scope->runtime += runtime;
    if (currentScope) {
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
    auto newLog = std::make_shared<profileLog>(msg);
    if (currentScope) {
        currentScope->subelements.emplace_back(std::move(newLog));
        newLog->parent = currentScope;
        //currentScope->runtime -= chrono::microseconds(1.5); //try to compensate the calltime for log command
    } else {
        frames[currentFrame].elements.push_back(std::move(newLog));
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
