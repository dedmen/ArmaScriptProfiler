#pragma once
#include "HookManager.hpp"

class FAllocHook
{
public:
    void init();
    FAllocHook();
    ~FAllocHook();
    HookManager hooks;
};

