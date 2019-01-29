#include "EngineProfiling.h"
#include "ProfilerAdapter.hpp"
#include "AdapterTracy.hpp"
#include <client.hpp>
#include <shared_mutex>

extern std::shared_ptr<ProfilerAdapter> GProfilerAdapter;


struct CounterHasher {
public:
    size_t operator()(const std::pair<PCounter*, int>& key) const {
        //intercept pairhash
        size_t _hash = std::hash<PCounter*>()(key.first);
        _hash ^= std::hash<uint64_t>()(key.second) + 0x9e3779b9 + (_hash << 6) + (_hash >> 2);
        return _hash;
    }
};

thread_local std::unique_ptr<std::unordered_map<std::pair<PCounter*, int>, std::shared_ptr<ScopeTempStorage>, CounterHasher>> openScopes;
thread_local bool openScopesInit;
std::unordered_map<PCounter*, std::shared_ptr<ScopeInfo>> scopeCache;
std::shared_mutex scopeCacheMtx;

extern "C" {
    uintptr_t profEndJmpback;
    uintptr_t shouldTimeJmpback;


    void shouldTime();
    void doEnd();
    void scopeCompleted();
}

bool PCounter::shouldTime() {
    if (slot < 0) return false;
    //if (!boss) return false;
    //if (!boss->da) return false;
    //if (!boss->stuffzi) return false;
    //if (!boss->stuffzi[slot].enabled) return false;

    if (GProfilerAdapter->getType() != AdapterType::Tracy) return false;
    
    auto tracyProf = std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter);
    if (!tracyProf->isConnected()) return false;


    std::shared_lock lock(scopeCacheMtx);
    auto found = scopeCache.find(this);
    if (found == scopeCache.end()) {
        lock.unlock();
        std::unique_lock lockInternal(scopeCacheMtx);
        auto res = scopeCache.insert({ this, tracyProf->createScopeStatic(name, cat, 0) });
        lockInternal.unlock();//#TODO this is unsafe
        lock.lock();
        found = res.first;
    }

    if (!openScopes)
        openScopes = std::make_unique<std::unordered_map<std::pair<PCounter*, int>, std::shared_ptr<ScopeTempStorage>, CounterHasher>>();

    //make sure the add is not inside the scope
    auto p = std::make_pair(this, slot);
    auto ins = openScopes->insert_or_assign(p,nullptr);
    auto tmp = tracyProf->enterScope(found->second);
    //if (tmp)
        ins.first->second = tmp;
    //else
    //    openScopes.erase(p);
    

    return true;
}

void ScopeProf::doEnd() {
    if (!openScopes || openScopes->empty() || !counter) return;
    auto found = openScopes->find({ counter, counter->slot });
    if (found == openScopes->end()) return;
    GProfilerAdapter->leaveScope(found->second);
    openScopes->erase(found);
}

void ArmaProf::scopeCompleted(int64_t start, int64_t end, intercept::types::r_string* stuff, PCounter* counter) {
    
    if (!openScopes || openScopes->empty() || !counter) return;
    auto found = openScopes->find({ counter, counter->slot });
    if (found == openScopes->end()) return;
    GProfilerAdapter->leaveScope(found->second);
    openScopes->erase(found);

}


HookManager::Pattern pat_doEnd{
    "xxxxxxxxxxxxxxxxx?????xxxx?????xxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxx????xxxx?x????xxxxxxxxxxxxxxxxx????xxxxxxxxx?????xxxxxx",
    "\x40\x53\x48\x83\xEC\x30\x80\x79\x11\x00\x48\x8B\xD9\x75\x09\x80\x3D\x00\x00\x00\x00\x00\x74\x38\x80\x3D\x00\x00\x00\x00\x00\x74\x0B\x0F\x31\x48\xC1\xE2\x20\x48\x0B\xC2\xEB\x05\xE8\x00\x00\x00\x00\x48\x8B\x13\x4C\x8B\xC0\x48\x8B\x43\x08\x4C\x8D\x4B\x18\x48\x8D\x0D\x00\x00\x00\x00\x48\x89\x44\x24\x00\xE8\x00\x00\x00\x00\x48\x8B\x53\x18\x48\x85\xD2\x74\x1A\xF0\xFF\x0A\x75\x0D\x48\x8B\x0D\x00\x00\x00\x00\x48\x8B\x01\xFF\x50\x18\x48\xC7\x43\x00\x00\x00\x00\x00\x48\x83\xC4\x30\x5B\xC3"
};

HookManager::Pattern pat_scopeCompleted{
    "xxxx?xxxx?xxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxx????xxxxxx????xxxx?xx????xxx",
    "\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x57\x41\x54\x41\x57\x48\x83\xEC\x20\x48\x8B\x81\x00\x00\x00\x00\x49\x8B\xF8\x4D\x8B\xE1\x48\x3B\xD0\x48\x8B\xD9\x48\x0F\x4C\xD0\x48\xC1\xF8\x04\x48\xC1\xFF\x04\x48\xC1\xFA\x04\x44\x8B\xFA\x2B\xFA\x44\x2B\xF8\x48\x8B\x44\x24\x00\x48\x63\x68\x18\x85\xED\x0F\x88\x00\x00\x00\x00\x8B\x41\x68\x66\x0F\x6E\xC7\x8B\xC8\xD1\xF9\x66\x0F\x6E\xD0\x8B\x43\x6C\x0F\x5B\xC0\x2B\xC1\x66\x0F\x6E\xC8\x0F\x5B\xD2\x42\x8D\x04\x3F\xF3\x0F\x59\xD0\x66\x0F\x6E\xC0\x0F\x5B\xC9\x0F\x5B\xC0\xF3\x0F\x59\xC8\x0F\x2F\xD1\x73\x48\x80\x3B\x00\x0F\x84\x00\x00\x00\x00\x4C\x69\xC5\x00\x00\x00\x00\x4C\x03\x43\x20\x0F\x84\x00\x00\x00\x00\x41\x0F\x0D\x48\x00\x41\xB9\x00\x00\x00\x00\x0F\x1F\x00"
};

HookManager::Pattern pat_shouldTime{
    "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxx????xxxxxxx",
    "\x48\x63\x41\x18\x85\xC0\x78\x32\x4C\x8B\x01\x33\xD2\x4D\x85\xC0\x74\x12\x41\x38\x10\x74\x0D\x48\x69\xC8\x00\x00\x00\x00\x49\x03\x48\x20\xEB\x03\x48\x8B\xCA\x48\x85\xC9\x74\x0A\x38\x51\x4A\x74\x05\xBA\x00\x00\x00\x00\x0F\xB6\xC2\xC3\x32\xC0\xC3"
};


EngineProfiling::EngineProfiling() {
    //order is important
    //hooks.placeHook(hookTypes::doEnd, pat_doEnd, reinterpret_cast<uintptr_t>(doEnd), profEndJmpback, 1, true);
    hooks.placeHook(hookTypes::scopeCompleted, pat_scopeCompleted, reinterpret_cast<uintptr_t>(scopeCompleted), profEndJmpback, 0);
    hooks.placeHook(hookTypes::shouldTime, pat_shouldTime, reinterpret_cast<uintptr_t>(shouldTime), shouldTimeJmpback, 0);

    auto found = hooks.findPattern(pat_doEnd, 0xD);



    auto stuffByte = found + 0x2 + 2;
    uint32_t offs = *reinterpret_cast<uint32_t*>(stuffByte);
    uint64_t addr = stuffByte + 4+1 + offs;
    uint64_t base = addr - 0x121;

    armaP = reinterpret_cast<ArmaProf*>(base);
    armaP->blip.clear();
    armaP->forceCapture = true;
    armaP->capture = true;

    //disable captureSlowFrame because it can set forceCapture to false
    static auto stuff = intercept::client::host::register_sqf_command("diag_captureSlowFrame"sv, ""sv, [](game_state&, game_value_parameter) -> game_value
        {
            return {};
        }, game_data_type::NOTHING, game_data_type::ARRAY);
}
