#pragma once
#include <chrono>
#include <types.hpp>
#include "ProfilerAdapter.hpp"

class scriptProfiler {
public:
    scriptProfiler();
    ~scriptProfiler();
    void preStart();
    void perFrame();
    void preInit();

    void registerInterfaces();



    std::shared_ptr<ScopeInfo> compileScope;
    std::shared_ptr<ScopeInfo> callExtScope;
    intercept::types::r_string waitForAdapter;
};

extern scriptProfiler profiler;
