#pragma once
#include <chrono>
#include <types.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "ProfilerAdapter.hpp"
#include "EngineProfiling.h"
#include "FAllocHook.h"

class scriptProfiler {
public:
    scriptProfiler();
    ~scriptProfiler() = default;
    void preStart();
    void perFrame();
    void preInit();

    void registerInterfaces();



    std::shared_ptr<ScopeInfo> compileScope;
    std::shared_ptr<ScopeInfo> compileScriptScope;
    std::shared_ptr<ScopeInfo> callExtScope;
    std::shared_ptr<ScopeInfo> preprocFileScope;
    intercept::types::r_string waitForAdapter;
    std::shared_ptr<EngineProfiling> engineProf;
    std::shared_ptr<FAllocHook> allocHook;
    std::optional<std::string> getCommandLineParam(std::string_view needle);
    bool engineFrameEnd = false;
    bool hasParameterFile = false;
    nlohmann::json json;
};

extern scriptProfiler profiler;
