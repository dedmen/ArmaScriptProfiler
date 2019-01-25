#include "AdapterArmaDiag.hpp"
#include <debug.hpp>
#include <sstream>
#include "scriptProfiler.hpp"
#include <numeric>

AdapterArmaDiag::AdapterArmaDiag() {
    frames.resize(framesToGo + 1);
    type = AdapterType::ArmaDiag;
}

std::shared_ptr<ScopeInfo> AdapterArmaDiag::createScope(intercept::types::r_string name,
    intercept::types::r_string filename, uint32_t fileline) {

    auto ret = std::make_shared<ScopeInfoArmaDiag>();

    ret->name = name;

    return ret;
}

std::shared_ptr<ScopeTempStorage> AdapterArmaDiag::enterScope(std::shared_ptr<ScopeInfo> scope) {
    auto armaDiagScopeInfo = std::dynamic_pointer_cast<ScopeInfoArmaDiag>(scope);
    if (!armaDiagScopeInfo) return nullptr;

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

    newScope->info = armaDiagScopeInfo;

    auto tempStorage = std::make_shared<ScopeTempStorageArmaDiag>();
    tempStorage->scopeID = newScopeID;
    tempStorage->startTime = std::chrono::high_resolution_clock::now();

    return tempStorage;
}

void AdapterArmaDiag::leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) {
     auto tmpStorage = std::dynamic_pointer_cast<ScopeTempStorageArmaDiag>(tempStorage);
    if (!tmpStorage) return; //#TODO debugbreak? log error?

    if (tmpStorage->scopeID == -1) return;

    


    auto timeDiff = std::chrono::high_resolution_clock::now() - tmpStorage->startTime;
    auto runtime = std::chrono::duration_cast<std::chrono::microseconds>(timeDiff);



    auto& scope = frames[currentFrame].scopes[tmpStorage->scopeID];
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

void AdapterArmaDiag::setThisArgs(std::shared_ptr<ScopeTempStorage> tempStorage,
    intercept::types::game_value thisArgs) {
    
}

void AdapterArmaDiag::cleanup() {}
void AdapterArmaDiag::perFrame() {
    

    

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



}

void AdapterArmaDiag::addLog(intercept::types::r_string message) {
    if (!shouldBeRecording()) return;
    auto newLog = std::make_shared<profileLog>(std::move(message));
    if (currentScope) {
        newLog->parent = currentScope;
        currentScope->subelements.emplace_back(std::move(newLog));
        //currentScope->runtime -= chrono::microseconds(1.5); //try to compensate the calltime for log command
    } else {
        frames[currentFrame].elements.emplace_back(std::move(newLog));
    }

}

void AdapterArmaDiag::iterateElementTree(const frameData& frame, const std::function<void(profileElement*, size_t)>& func) {
    //https://stackoverflow.com/a/5988138
    for (auto& element : frame.elements) {
        profileElement* node = element.get();
        size_t depth         = 0;
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
                node             = node->parent;
                depth--;
            }
        }
    }
}

intercept::types::r_string AdapterArmaDiag::dumpLog() {
    //https://pastebin.com/raw/4gfJSwdB
    //#TODO don't really want scopes empty. Just doing it to use it as time reference
    if (frames.size() == 1 && frames[currentFrame].elements.empty() || frames[currentFrame].scopes.empty()) return intercept::types::r_string();
    std::stringstream output;
    auto baseTimeReference = frameStart;
    auto totalRuntime = std::chrono::duration_cast<chrono::milliseconds>(std::chrono::high_resolution_clock::now() - baseTimeReference);
    output.precision(4);
    output << "* THREAD! YEAH!\n";
    output << std::fixed << "total; " << 0.0 << "; " << totalRuntime.count() << ";\"Frame " << intercept::sqf::diag_frameno() << "\"\n";


    auto iterateFunc = [&output, &baseTimeReference](profileElement* element, size_t depth) {
        for (size_t i = 0; i < depth; ++i) {
            output << " ";
        }
        auto startTime = std::chrono::duration_cast<chrono::milliseconds>(element->getStartTime() - baseTimeReference);
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


    for (auto i = 0u; i < frames.size(); ++i) {
        output << "* Frame " << i << "\n";
        iterateElementTree(frames[i], iterateFunc);
    }


    return r_string(output.str());
}

void AdapterArmaDiag::captureFrames(uint32_t framesToCapture) {
    profileStartFrame = intercept::sqf::diag_frameno();
    shouldRecord = true;
    forceCapture = true;
    framesToGo = framesToCapture;
    frames.resize(framesToCapture + 1);
}

void AdapterArmaDiag::captureFrame() {
    profileStartFrame = intercept::sqf::diag_frameno();
    shouldRecord = true;
    forceCapture = true;

}

void AdapterArmaDiag::captureSlowFrame(chrono::milliseconds threshold) {
    shouldRecord = true;
    slowCheck = threshold;
    triggerMode = true;
}

void AdapterArmaDiag::captureTrigger() {
    shouldRecord = true;
    trigger = false;
    triggerMode = true;
}

void AdapterArmaDiag::profilerTrigger() {
    trigger = true;
}

chrono::milliseconds AdapterArmaDiag::totalScriptRuntime() {
    return std::accumulate(frames[currentFrame].elements.begin(), frames[currentFrame].elements.end(), chrono::milliseconds(0), [](chrono::milliseconds accu, const std::shared_ptr<profileElement>& element) -> chrono::milliseconds {
        if (element->type != profileElementType::scope) return accu;
        return accu + element->getRunTime();
    });
}

bool AdapterArmaDiag::shouldCapture() {    
    if (frames[currentFrame].elements.empty()) return false;
    if (forceCapture && intercept::sqf::diag_frameno() > profileStartFrame) return true;
    if (trigger) return true;
    if (slowCheck.count() != 0.0) return (totalScriptRuntime() > slowCheck);
    return false;
}

extern void copyToClipboard(r_string txt);

void AdapterArmaDiag::capture() {
    auto log = dumpLog();
    if (!log.empty()) {
        copyToClipboard(log);
        forceCapture = false;
        shouldRecord = false;
        trigger = false;
        intercept::sqf::diag_capture_frame(1);//Show user that we captured stuff
        isRecording = false;
        currentFrame = 0;
        frames.clear(); //#TODO recursive...
        frames.resize(framesToGo + 1);
        triggerMode = false;
    }
    waitingForCapture = false;
}

bool AdapterArmaDiag::shouldBeRecording() const {
    return shouldRecord;// || Brofiler::IsActive();
}
