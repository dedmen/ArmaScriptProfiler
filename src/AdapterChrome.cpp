#include "AdapterChrome.hpp"
#include <fstream>

void printClean(std::ofstream& str, intercept::types::r_string string) {
    for (auto& it : string) {
        if (it == '"') 
            str << "\\\"";
        else if (it == '\\') 
            str << "\\\\";
        else str << it;
    }
}


void ChromeEvent::writeTo(std::ofstream& str) const {
    str << "{"
        << R"("name":")";
    printClean(str, name);
    str << R"(","ts":)" << start  << R"(,"pid":0,"tid":)" << threadID << ",";
    switch (type) { 
        case ChromeEventType::durationBegin: str << R"("ph":"B",)"; break;
        case ChromeEventType::durationEnd: str << R"("ph":"E",)"; break;
        case ChromeEventType::complete: 
        str << R"("ph":"X","dur":)" << duration.count() <<","; //#TODO args        
        break;
        case ChromeEventType::instant: str << R"("ph":"i",)"; break;
        case ChromeEventType::counter: 
        str << R"("ph":"C",)"; 
        str << R"("args":{")";
        printClean(str, name);
        str << "\":" << counterValue << "},";
        
        break;
        case ChromeEventType::metadata: str << R"("ph":"M",)"; break; //#TODO implement properly
        default: ; 
    }
    str << R"("cat":"profiler")"; //#TODO implement properly
    str << "},"; //#TODO \n for human readable
}

AdapterChrome::AdapterChrome() {
    profStart = std::chrono::high_resolution_clock::now();
    type = AdapterType::Chrome;
}

void AdapterChrome::perFrame() {}

std::shared_ptr<ScopeInfo> AdapterChrome::createScope(intercept::types::r_string name,
    intercept::types::r_string filename, uint32_t fileline) {
    auto ret = std::make_shared<ScopeInfoChrome>();
    ret->name = name;
    ret->file = filename;
    ret->line = fileline;
    return ret;
}

std::shared_ptr<ScopeTempStorage> AdapterChrome::enterScope(std::shared_ptr<ScopeInfo> scope) {
    auto scopeInfo = std::dynamic_pointer_cast<ScopeInfoChrome>(scope);
    if (!scopeInfo) return nullptr; //#TODO debugbreak? log error?

    auto ret = std::make_shared<ScopeTempStorageChrome>();
    ret->scopeInfo = scopeInfo;
    ret->start = std::chrono::high_resolution_clock::now();
    return ret;
}

void AdapterChrome::leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) {
    auto endTime = std::chrono::high_resolution_clock::now();

    auto tmpStorage = std::dynamic_pointer_cast<ScopeTempStorageChrome>(tempStorage);
    if (!tmpStorage) return; //#TODO debugbreak? log error?

    ChromeEvent newEvent;
    newEvent.name = tmpStorage->scopeInfo->name;
    newEvent.start = std::chrono::duration_cast<chrono::microseconds>(tmpStorage->start - profStart).count();
    newEvent.duration = endTime-tmpStorage->start;
    newEvent.threadID = tmpStorage->threadID;
    newEvent.type = ChromeEventType::complete;
    pushEvent(std::move(newEvent));
}

void AdapterChrome::addLog(intercept::types::r_string message) {
    ChromeEvent newEvent;
    newEvent.start = std::chrono::duration_cast<chrono::milliseconds>(std::chrono::high_resolution_clock::now() - profStart).count();
    newEvent.threadID = 0; //#TODO threadID for scheduled
    newEvent.type = ChromeEventType::instant;
    pushEvent(std::move(newEvent));
}

void AdapterChrome::setCounter(intercept::types::r_string name, float val) {
    ChromeEvent newEvent;
    newEvent.start = std::chrono::duration_cast<chrono::milliseconds>(std::chrono::high_resolution_clock::now() - profStart).count();
    newEvent.threadID = 0; //#TODO threadID for scheduled
    newEvent.counterValue = val;
    newEvent.name = name;
    newEvent.type = ChromeEventType::counter;
    pushEvent(std::move(newEvent));
}

void AdapterChrome::cleanup() {
    if (outputStream) {
        *outputStream << "]";
        outputStream->flush();
        outputStream.reset();
    }
    storedEvents.clear();
}

void AdapterChrome::setTargetFile(std::filesystem::path target) {
    outputStream = std::make_shared<std::ofstream>(target);

    *outputStream << "[";
    for (auto& it : storedEvents) {
        it.writeTo(*outputStream);
    }
    storedEvents.clear();
}

void AdapterChrome::pushEvent(ChromeEvent&& event) {
    if (outputStream) {
        event.writeTo(*outputStream);
    } else {
        storedEvents.emplace_back(event);
    }
}
