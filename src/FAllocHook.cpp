#include "FAllocHook.h"
#include <containers.hpp>
#include "AdapterTracy.hpp"
extern std::shared_ptr<ProfilerAdapter> GProfilerAdapter;
#include <Tracy.hpp>
using namespace std::string_view_literals;

FAllocHook::FAllocHook()
{
}


FAllocHook::~FAllocHook()
{
}


extern "C" {
    uintptr_t engineAlloc;
    uintptr_t engineFree;
    intercept::types::rv_pool_allocator* freealloctmp;
    intercept::types::rv_pool_allocator* allocalloctmp;

    void afterAlloc() {
        auto tracyProf = std::reinterpret_pointer_cast<AdapterTracy>(GProfilerAdapter);
        tracyProf->setCounter(allocalloctmp->_allocName, allocalloctmp->allocated_count);
    }

    void afterFree() {
        auto tracyProf = std::reinterpret_pointer_cast<AdapterTracy>(GProfilerAdapter);
        tracyProf->setCounter(freealloctmp->_allocName, freealloctmp->allocated_count);
    }


    void engineAllocRedir();
    void engineFreeRedir();
}


HookManager::Pattern pat_allocReg{ // "Out of FastCAlloc slots"
    "xxxxxxxxxxxxxxx?????x????xxx????xx????xxxxx????xxxxxxxxxxx????xxxxxxxxx????x????xxxxxxxxx"sv,
    "\x40\x53\x48\x83\xEC\x30\x45\x33\xC9\x48\x8B\xD9\xC7\x44\x24\x00\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x63\x15\x00\x00\x00\x00\x81\xFA\x00\x00\x00\x00\x7D\x1C\x48\x8D\x0D\x00\x00\x00\x00\x48\x8B\xC3\x48\x89\x1C\xD1\xFF\xC2\x89\x15\x00\x00\x00\x00\x48\x83\xC4\x30\x5B\xC3\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x8B\xC3\x48\x83\xC4\x30\x5B\xC3"sv
};

HookManager::Pattern pat_allocC{
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxx"sv,
    "\x40\x53\x48\x83\xEC\x20\xFF\x41\x60\x48\x8B\x41\x08\x48\x8B\xD9\x48\x3B\xC1\x74\x0B\x48\x85\xC0\x74\x06\x48\x83\xC0\xE0\x75\x2B\x48\x8D\x41\x18\x48\x8B\x49\x20\x48\x3B\xC8\x74\x0E\x48\x85\xC9\x74\x09\x48\x8D\x41\xE0\x48\x85\xC0\x75\x10\x48\x8B\xCB\xE8\x00\x00\x00\x00\x84\xC0\x0F\x84\x00\x00\x00\x00\x4C\x8B\x43\x08\x32\xC9\x45\x33\xD2\x4C\x3B\xC3\x74\x0B\x4D\x85\xC0\x74\x06\x49\x83\xC0\xE0\x75\x2A\x4C\x8B\x43\x20\x48\x8D\x43\x18\x4C\x3B\xC0\x0F\x84\x00\x00\x00\x00\x4D\x85\xC0\x74\x06\x49\x83\xC0\xE0\xEB\x03"sv
};

HookManager::Pattern pat_freeC{
    "xxxxx????xxxxxxxxxxxxx?xxxxxxxxxxxxxxxxxxxxxxx????x????xxxxxx????xxxxxxx?xxxxxx????xxxxxxxxxx??xxxxxxxxxxx"sv,
    "\x48\x85\xD2\x0F\x84\x00\x00\x00\x00\x53\x48\x83\xEC\x20\x48\x63\x41\x58\x48\x89\x7C\x24\x00\x48\x8B\xFA\x48\xFF\xC8\x48\x8B\xD9\x48\x23\xC2\x48\x2B\xF8\x83\x3F\x00\x74\x28\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x44\x8B\x07\x48\x8D\x0D\x00\x00\x00\x00\x48\x8B\xD7\x48\x8B\x7C\x24\x00\x48\x83\xC4\x20\x5B\xE9\x00\x00\x00\x00\x48\x8B\x47\x18\x48\x89\x02\x48\x83\x7F\x00\x00\x48\x89\x57\x18\x0F\x94\xC0\x48\x89\x7A\x08"sv
};

void FAllocHook::init() {
    
    auto found = hooks.findPattern(pat_allocReg, 0x0);



    auto aicp = *reinterpret_cast<uint32_t*>(found + 0x1C);
    auto rip = found + 0x20;
    auto aic = *reinterpret_cast<int32_t*>(rip + aicp);

    //+1C 
    //70 C4 4C 01
    //014cc470
    //0002932C90


    auto aidp = *reinterpret_cast<uint32_t*>(found + 0x2B);
    rip = found + 0x2F;
    auto aid = reinterpret_cast<intercept::types::rv_pool_allocator**>(rip + aidp);
    //71 C4 4C 01


    auto allocF = hooks.findPattern(pat_allocC);
    engineAlloc = hooks.placeHookTotalOffs(allocF, reinterpret_cast<uintptr_t>(engineAllocRedir))+2;

    auto FreeF = hooks.findPattern(pat_freeC);
    engineFree = hooks.placeHookTotalOffs(FreeF + 0x9, reinterpret_cast<uintptr_t>(engineFreeRedir));
    //
    //__debugbreak();

}
