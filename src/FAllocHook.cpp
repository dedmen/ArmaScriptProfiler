#include "FAllocHook.h"
#include <containers.hpp>
#include "AdapterTracy.hpp"
extern std::shared_ptr<ProfilerAdapter> GProfilerAdapter;
#define TRACY_ENABLE
#include <tracy/Tracy.hpp>
using namespace std::string_view_literals;

#include <intercept.hpp>

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
        tracyProf->setCounter(allocalloctmp->_allocName, (int64_t)allocalloctmp->allocated_count);
    }

    void afterFree() {
        auto tracyProf = std::reinterpret_pointer_cast<AdapterTracy>(GProfilerAdapter);
        tracyProf->setCounter(freealloctmp->_allocName, (int64_t)freealloctmp->allocated_count);
    }


    void engineAllocRedir();
    void engineFreeRedir();
}

void FAllocHook::init() {
#ifndef __linux__
    auto poolAlloc = intercept::client::host::functions.get_engine_allocator()->poolFuncAlloc;
    auto poolDealloc = intercept::client::host::functions.get_engine_allocator()->poolFuncDealloc;

    auto allocF = poolAlloc; // hooks.findPattern(pat_allocC);
    engineAlloc = hooks.placeHookTotalOffs(allocF, reinterpret_cast<uintptr_t>(engineAllocRedir))+4;

    auto FreeF = poolDealloc; // We inject after the null check
    engineFree = hooks.placeHookTotalOffs(FreeF + 0x9, reinterpret_cast<uintptr_t>(engineFreeRedir))+1;
    //
    //__debugbreak();
#endif
}
