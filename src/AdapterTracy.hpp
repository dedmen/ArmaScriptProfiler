#pragma once
#include "ProfilerAdapter.hpp"
#include <unordered_set>

class ScopeInfoTracy;

class AdapterTracy final : public ProfilerAdapter
{
public:
	AdapterTracy();
	~AdapterTracy();
	void perFrame() override;
	std::shared_ptr<ScopeInfo> createScope(intercept::types::r_string name, intercept::types::r_string filename,
		uint32_t fileline) override;
	std::shared_ptr<ScopeTempStorage> enterScope(std::shared_ptr<ScopeInfo> scope) override;
	std::shared_ptr<ScopeTempStorage> enterScope(std::shared_ptr<ScopeInfo> scope, uint64_t threadID) override;
	void leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) override;
	void setName(std::shared_ptr<ScopeTempStorage> tempStorage, const intercept::types::r_string& name) override;
	void setDescription(std::shared_ptr<ScopeTempStorage> tempStorage, const intercept::types::r_string& descr) override;
	void addLog(intercept::types::r_string message) override;
    void setCounter(intercept::types::r_string name, float val) override;
    void setCounter(const char* name, float val) const;

	std::shared_ptr<ScopeInfo> createScopeStatic(const char* name, const char* filename, uint32_t fileline) const;
	static bool isConnected();

private:
	static void ensureReady();
	using scopeCacheKey = std::tuple<intercept::types::r_string, intercept::types::r_string,uint32_t>;

	struct ScopeCacheFastEqual {
		bool operator()(const scopeCacheKey& left, const scopeCacheKey& right) const {
	        return
	            std::get<0>(left).data() == std::get<0>(right).data() &&
	            std::get<1>(left).data() == std::get<1>(right).data() &&
	            std::get<2>(left) == std::get<2>(right);
		}
	};

	struct ScopeCacheFastHash {
	public:
		size_t operator()(const scopeCacheKey& key) const {
	        //intercept pairhash
	        size_t _hash = std::hash<uint64_t>()(reinterpret_cast<uint64_t>(std::get<0>(key).data()));
	        _hash ^= std::hash<uint64_t>()(reinterpret_cast<uint64_t>(std::get<1>(key).data())) + 0x9e3779b9 + (_hash << 6) + (_hash >> 2);
	        _hash ^= std::hash<uint32_t>()(std::get<2>(key)) + 0x9e3779b9 + (_hash << 6) + (_hash >> 2);
	        return _hash;
		}
	};

	std::unordered_map<
		scopeCacheKey,
		std::shared_ptr<ScopeInfoTracy>
		,ScopeCacheFastHash,ScopeCacheFastEqual> scopeCache;

	std::unordered_set<intercept::types::r_string> counterCache;
};

