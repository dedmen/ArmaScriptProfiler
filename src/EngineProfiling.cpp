#include "EngineProfiling.h"
#include "ProfilerAdapter.hpp"
#include "AdapterTracy.hpp"

extern std::shared_ptr<ProfilerAdapter> GProfilerAdapter;
std::unordered_map<PCounter*, std::shared_ptr<ScopeTempStorage>> openScopes;
std::unordered_map<PCounter*, std::shared_ptr<ScopeInfo>> scopeCache;

extern "C" {
    uintptr_t profEndJmpback;
    uintptr_t shouldTimeJmpback;


    void shouldTime();
    void doEnd();
}

bool PCounter::shouldTime() {

    if (GProfilerAdapter->getType() == AdapterType::Tracy) {
        auto tracyProf = std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter);

        auto found = scopeCache.find(this);
        if (found == scopeCache.end()) {
            auto res = scopeCache.insert({this, tracyProf->createScopeStatic(name, cat, 0)});
            found = res.first;
        }

        //make sure the add is not inside the scope
        openScopes[this] = nullptr;
        auto ins = openScopes.find(this);

        ins->second = tracyProf->enterScope(found->second);   
    }




    if (slot < 0) return false;
    if (!boss) return false;
    if (!boss->da) return false;
    if (!boss->stuffzi) return false;
    if (!boss->stuffzi[slot].enabled) return false;
    return true;
}

void ScopeProf::doEnd() {
    auto found = openScopes.find(counter);
    if (found == openScopes.end()) return;
    GProfilerAdapter->leaveScope(found->second);
    openScopes.erase(found);
}


HookManager::Pattern pat_doEnd{
    "xxxxxxxxxxxxxxxxx?????xxxx?????xxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxx????xxxx?x????xxxxxxxxxxxxxxxxx????xxxxxxxxx?????xxxxxx",
    "\x40\x53\x48\x83\xEC\x30\x80\x79\x11\x00\x48\x8B\xD9\x75\x09\x80\x3D\x00\x00\x00\x00\x00\x74\x38\x80\x3D\x00\x00\x00\x00\x00\x74\x0B\x0F\x31\x48\xC1\xE2\x20\x48\x0B\xC2\xEB\x05\xE8\x00\x00\x00\x00\x48\x8B\x13\x4C\x8B\xC0\x48\x8B\x43\x08\x4C\x8D\x4B\x18\x48\x8D\x0D\x00\x00\x00\x00\x48\x89\x44\x24\x00\xE8\x00\x00\x00\x00\x48\x8B\x53\x18\x48\x85\xD2\x74\x1A\xF0\xFF\x0A\x75\x0D\x48\x8B\x0D\x00\x00\x00\x00\x48\x8B\x01\xFF\x50\x18\x48\xC7\x43\x00\x00\x00\x00\x00\x48\x83\xC4\x30\x5B\xC3"
};


HookManager::Pattern pat_shouldTime{
    "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxx????xxxxxxx",
    "\x48\x63\x41\x18\x85\xC0\x78\x32\x4C\x8B\x01\x33\xD2\x4D\x85\xC0\x74\x12\x41\x38\x10\x74\x0D\x48\x69\xC8\x00\x00\x00\x00\x49\x03\x48\x20\xEB\x03\x48\x8B\xCA\x48\x85\xC9\x74\x0A\x38\x51\x4A\x74\x05\xBA\x00\x00\x00\x00\x0F\xB6\xC2\xC3\x32\xC0\xC3"
};


EngineProfiling::EngineProfiling()
{

     hooks.placeHook(hookTypes::shouldTime, pat_shouldTime, reinterpret_cast<uintptr_t>(shouldTime), shouldTimeJmpback, 0);
     hooks.placeHook(hookTypes::doEnd, pat_doEnd, reinterpret_cast<uintptr_t>(doEnd), profEndJmpback, 0);


}


EngineProfiling::~EngineProfiling()
{
}


extern "C" {

    
    
}