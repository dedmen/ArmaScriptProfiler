#pragma once
#include <cstdint>
#include <array>
#include <utility>
#include <vector>
#include <functional>
#include <string_view>

//From ArmaDebugEngine

enum class hookTypes {
    shouldTime,   //FrameEnd/FrameStart
    doEnd,
    scopeCompleted,
    frameEnd,
    compileCacheIns,
    sVMSimulate,
    End
};

class HookManager {//Implementation in dllmain
public:

    struct Pattern {
        Pattern(std::string_view _mask, std::string_view _pattern) : mask(_mask), pattern(_pattern) {}
        Pattern(std::string_view _mask, std::string_view _pattern, int64_t _offset) : mask(_mask), pattern(_pattern), offset(static_cast<uintptr_t>(_offset)) {}
        Pattern(std::string_view _mask, std::string_view _pattern, std::function<uintptr_t(uintptr_t)> _offset) : mask(_mask), pattern(_pattern), offsetFunc(
                                                                                                            std::move(
                                                                                                                _offset)) {}
        std::string_view mask;
        std::string_view pattern;
        uintptr_t offset{ 0 };
        std::function<uintptr_t(uintptr_t)> offsetFunc;
    };


    HookManager();
    bool placeHook(hookTypes, const Pattern& pat, uintptr_t jmpTo, uintptr_t& jmpBackRef, uint8_t jmpBackOffset = 0, bool taintRax = false);
    bool placeHook(hookTypes, const Pattern& pat, uintptr_t jmpTo);
    uintptr_t placeHook(uintptr_t offset, uintptr_t jmpTo, uint8_t jmpBackOffset = 0);
    uintptr_t placeHookTotalOffs(uintptr_t offset, uintptr_t jmpTo, bool taintRax = false);
    static bool MatchPattern(uintptr_t addr, std::string_view pattern, std::string_view mask);
    uintptr_t findPattern(std::string_view pattern, std::string_view mask, uintptr_t offset = 0) const;
    uintptr_t findPattern(const Pattern& pat, uintptr_t offset = 0) const;


    struct PlacedHook {
        std::vector<unsigned char> originalInstructions;
        uint8_t originalInstructionsCount;
        uintptr_t startAddr;
        uintptr_t jumpBack;
    };

    std::array<PlacedHook, static_cast<size_t>(hookTypes::End)> placedHooks;
    uintptr_t engineBase;
    uintptr_t engineSize;
};
