#pragma once
#include <chrono>
#include <types.hpp>
#include "ProfilerAdapter.hpp"
#include "EngineProfiling.h"

class scriptProfiler {
public:
    scriptProfiler();
    ~scriptProfiler() = default;
    void preStart();
    void perFrame();
    void preInit();

    void registerInterfaces();



    std::shared_ptr<ScopeInfo> compileScope;
    std::shared_ptr<ScopeInfo> callExtScope;
    intercept::types::r_string waitForAdapter;
    std::shared_ptr<EngineProfiling> engineProf;
    bool engineFrameEnd = false;
};

extern scriptProfiler profiler;
