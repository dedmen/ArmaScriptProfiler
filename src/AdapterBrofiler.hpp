#pragma once
#include "ProfilerAdapter.hpp"


namespace Brofiler {
	struct EventData;
	struct EventDescription;
}

class ScopeInfoBrofiler final: public ScopeInfo {
public:
	Brofiler::EventDescription* eventDescription{nullptr};
};


class ScopeTempStorageBrofiler final : public ScopeTempStorage {
public:
	 Brofiler::EventData* evtDt {nullptr};
};

class AdapterBrofiler final : public ProfilerAdapter
{
public:
	AdapterBrofiler();
	~AdapterBrofiler();

	void perFrame() override;

	std::shared_ptr<ScopeInfo> createScope(intercept::types::r_string name, intercept::types::r_string filename, uint32_t fileline) override;

	std::shared_ptr<ScopeTempStorage> enterScope(std::shared_ptr<ScopeInfo> scope) override;
	void leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) override;
	void setThisArgs(std::shared_ptr<ScopeTempStorage> tempStorage, intercept::types::game_value thisArgs) override;
	void cleanup() override;
	
private:
	std::map<std::pair<const intercept::types::r_string, uint32_t>, std::shared_ptr<ScopeInfoBrofiler>> tempEventDescriptions;	
	Brofiler::EventData* frameEvent = nullptr;
};

