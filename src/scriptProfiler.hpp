#pragma once
#include <chrono>
#include <client/headers/shared/types.hpp>

namespace chrono {
    using nanoseconds = std::chrono::duration<double, std::nano>;
    using microseconds = std::chrono::duration<double, std::micro>;
    using milliseconds = std::chrono::duration<double, std::milli>;
    using seconds = std::chrono::duration<double>;
}



enum class profileElementType {
    scope,
    log
};


class profileElement {
public:
    std::vector<std::shared_ptr<profileElement>> subelements;
    size_t curElement = 0; //Used for iterating over subelements
    profileElementType type;
    profileElement* parent = nullptr;
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    profileElement(profileElementType _type) : type(_type) {}

    virtual ~profileElement() {}
    virtual intercept::types::r_string getAsString() = 0;

    virtual std::chrono::high_resolution_clock::time_point getStartTime() { return start; }
    virtual chrono::microseconds getRunTime() = 0;

};

class profileScope : public profileElement {
public:
    ~profileScope() override {};
    explicit profileScope(uint64_t _scopeID) : profileElement(profileElementType::scope), scopeID(_scopeID) {}
    intercept::types::r_string getAsString() override { return name; };
    chrono::microseconds getRunTime() override { return runtime; }
    chrono::microseconds runtime{ 0 };
    uint64_t scopeID;
    intercept::types::r_string name;
};

class profileLog : public profileElement {
public:
    explicit profileLog(intercept::types::r_string&& _message) : profileElement(profileElementType::log), message(_message) {}
    ~profileLog() override {};
    intercept::types::r_string getAsString() override { return message; }
    chrono::microseconds getRunTime() override { return chrono::microseconds(0); }
    intercept::types::r_string message;
};

class frameData {
public:
    std::vector<std::shared_ptr<profileElement>> elements; //tree structure
    std::map<uint64_t, std::shared_ptr<profileScope>> scopes; //map of scopes
};

class scriptProfiler {
public:
    scriptProfiler();
    ~scriptProfiler();
    void preStart();
    void preInit();
    //Starts new scope and returns it's assigned scopeID
    uint64_t startNewScope();
    void endScope(uint64_t scopeID, intercept::types::r_string&& name, chrono::microseconds runtime);
    void addLog(intercept::types::r_string msg);
    void iterateElementTree(const frameData& frame, std::function<void(profileElement*, size_t)>);
    intercept::types::r_string generateLog();
    //Only returns the time scripts ran. Will have gaps inbetween where none were executed. Basically sums up all top level scope runtimes
    chrono::milliseconds totalScriptRuntime();
    bool shouldCapture();
    void capture();
    void registerInterfaces();


    std::vector<frameData> frames;

    uint32_t framesToGo = 0;
    uint32_t currentFrame = 0;
    uint64_t lastScopeID = 1;
    profileScope* currentScope = nullptr;
    bool forceCapture = false;
    bool shouldRecord = false;
    chrono::milliseconds slowCheck{ 0 };
    std::chrono::high_resolution_clock::time_point frameStart;
    bool waitingForCapture = false;
    float profileStartFrame = false;
    bool isRecording = false;
    bool trigger = false;
    bool triggerMode = false;
};

extern scriptProfiler profiler;
