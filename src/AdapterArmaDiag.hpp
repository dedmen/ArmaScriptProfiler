#pragma once
#include "ProfilerAdapter.hpp"
#include <functional>


    
namespace chrono {
    using nanoseconds = std::chrono::duration<double, std::nano>;
    using microseconds = std::chrono::duration<double, std::micro>;
    using milliseconds = std::chrono::duration<double, std::milli>;
    using seconds = std::chrono::duration<double>;
}

class ScopeInfoArmaDiag final : public ScopeInfo {
public:
	intercept::types::r_string name;
};

class ScopeTempStorageArmaDiag final : public ScopeTempStorage {
public:
	uint64_t scopeID = -1;
    std::chrono::high_resolution_clock::time_point startTime;
};

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

    virtual ~profileElement() = default;
    virtual intercept::types::r_string getAsString() = 0;

    virtual std::chrono::high_resolution_clock::time_point getStartTime() { return start; }
    virtual std::chrono::microseconds getRunTime() = 0;

};

class profileScope : public profileElement {
public:
    ~profileScope() override = default;
    explicit profileScope(uint64_t _scopeID) : profileElement(profileElementType::scope), scopeID(_scopeID) {}
    intercept::types::r_string getAsString() override { return info->name; };
    std::chrono::microseconds getRunTime() override { return runtime; }
    std::chrono::microseconds runtime{ 0 };
    uint64_t scopeID;
    std::shared_ptr<ScopeInfoArmaDiag> info;
};

class profileLog : public profileElement {
public:
    explicit profileLog(intercept::types::r_string&& _message) : profileElement(profileElementType::log), message(_message) {}
    ~profileLog() override = default;
    intercept::types::r_string getAsString() override { return message; }
    std::chrono::microseconds getRunTime() override { return std::chrono::microseconds(0); }
    intercept::types::r_string message;
};

class frameData {
public:
    std::vector<std::shared_ptr<profileElement>> elements; //tree structure
    std::map<uint64_t, std::shared_ptr<profileScope>> scopes; //map of scopes
};


class AdapterArmaDiag final : public ProfilerAdapter
{
public:
	AdapterArmaDiag();
	virtual ~AdapterArmaDiag() = default;

	std::shared_ptr<ScopeInfo> createScope(intercept::types::r_string name, intercept::types::r_string filename, uint32_t fileline) override;

	std::shared_ptr<ScopeTempStorage> enterScope(std::shared_ptr<ScopeInfo> scope) override;
	std::shared_ptr<ScopeTempStorage> enterScope(std::shared_ptr<ScopeInfo> scope, uint64_t threadID) override;
	void leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) override;
	void setThisArgs(std::shared_ptr<ScopeTempStorage> tempStorage, intercept::types::game_value thisArgs) override;
	void cleanup() override;
    void perFrame() override;
    void addLog(intercept::types::r_string message) override;

    intercept::types::r_string dumpLog();

    void captureFrames(uint32_t framesToCapture);
    void captureFrame();

    void captureSlowFrame(chrono::milliseconds threshold);
    void captureTrigger();
    void profilerTrigger();




    //Only returns the time scripts ran. Will have gaps inbetween where none were executed. Basically sums up all top level scope runtimes
    chrono::milliseconds totalScriptRuntime();
    bool shouldCapture();
    void capture();

    bool forceCapture = false;
    bool shouldBeRecording() const;
    bool shouldRecord = false;
    chrono::milliseconds slowCheck{ 0 };
    bool waitingForCapture = false;
    float profileStartFrame = false;
    bool isRecording = false;
    bool trigger = false;
    bool triggerMode = false;

private:
	uint64_t lastScopeID = 1;
    profileScope* currentScope = nullptr;

    std::vector<frameData> frames;

    uint32_t currentFrame = 0;
    std::chrono::high_resolution_clock::time_point frameStart;
    uint32_t framesToGo = 0;

    static void iterateElementTree(const frameData& frame, const std::function<void(profileElement*, size_t)>& func);

};

