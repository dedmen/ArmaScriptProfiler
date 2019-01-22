#pragma once
#include <chrono>
#include <types.hpp>
#include <Brofiler.h>


class scriptProfiler {
public:
    scriptProfiler();
    ~scriptProfiler();
    void preStart();
    void perFrame();
    void preInit();

    void registerInterfaces();




};

extern scriptProfiler profiler;
