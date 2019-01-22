#pragma once
#include "ProfilerAdapter.hpp"
#include <filesystem>

namespace chrono {
    using milliseconds = std::chrono::duration<double, std::milli>;
}

class ScopeInfoChrome final: public ScopeInfo {
public:
	intercept::types::r_string name;
	intercept::types::r_string file;
	uint32_t line;
};

enum class ChromeEventCategory {
	none = 0,
	script = 1,
	extension = 2
};
enum class ChromeEventType {
	durationBegin,
	durationEnd,
	complete,
	counter,
	instant,
	metadata
};


struct ChromeEvent {
public:
	intercept::types::r_string name;
	ChromeEventType type;
	double start;
	chrono::milliseconds duration;
	uint64_t threadID;
	float counterValue;
	void writeTo(std::ofstream& str) const;
};

class ScopeTempStorageChrome final : public ScopeTempStorage {
public:
	ChromeEventCategory category = ChromeEventCategory::script;
	std::chrono::high_resolution_clock::time_point start;
	std::shared_ptr<ScopeInfoChrome> scopeInfo;
	uint64_t threadID = 0;
};



class AdapterChrome final : public ProfilerAdapter
{
public:
	AdapterChrome();
	~AdapterChrome();
	void perFrame() override;
	std::shared_ptr<ScopeInfo> createScope(intercept::types::r_string name, intercept::types::r_string filename,
		uint32_t fileline) override;
	std::shared_ptr<ScopeTempStorage> enterScope(std::shared_ptr<ScopeInfo> scope) override;
	void leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) override;
	void addLog(intercept::types::r_string message) override;

	void setTargetFile(std::filesystem::path target);

private:
	std::shared_ptr<std::ofstream> outputStream;
	std::chrono::high_resolution_clock::time_point profStart;
	void pushEvent(ChromeEvent&& event);
	std::vector<ChromeEvent> storedEvents;
};

