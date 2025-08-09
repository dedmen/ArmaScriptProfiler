#pragma once
#include <intercept.hpp>
#include <chrono>
using namespace intercept;
using namespace std::chrono_literals;

class NetworkProfiler {
public:
    void init();
    static inline auto_array<std::pair<r_string, uint32_t>> (*getCallstackRaw)(game_state* gs) = nullptr;
    static uint32_t getVariableSize(const game_value& var, std::string* data);
    static inline int64_t remoteExecSize = 0;
    static inline int64_t setVariableSize = 0;
};

static inline NetworkProfiler GNetworkProfiler = NetworkProfiler();