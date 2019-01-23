#pragma once
#include <cstdint>
#include <array>
#include <utility>
#include <vector>
#include <functional>

//From ArmaDebugEngine

enum class hookTypes {
    shouldTime,   //FrameEnd/FrameStart
    doEnd,
    End
};

class HookManager {//Implementation in dllmain
public:

    struct Pattern {
        Pattern(const char* _mask, const char* _pattern) : mask(_mask), pattern(_pattern) {}
        Pattern(const char* _mask, const char* _pattern, int64_t _offset) : mask(_mask), pattern(_pattern), offset(static_cast<uintptr_t>(_offset)) {}
        Pattern(const char* _mask, const char* _pattern, std::function<uintptr_t(uintptr_t)> _offset) : mask(_mask), pattern(_pattern), offsetFunc(
                                                                                                            std::move(
                                                                                                                _offset)) {}
        const char* mask;
        const char* pattern;
        uintptr_t offset{ 0 };
        std::function<uintptr_t(uintptr_t)> offsetFunc;
    };


    HookManager();
    bool placeHook(hookTypes, const Pattern& pat, uintptr_t jmpTo, uintptr_t& jmpBackRef, uint8_t jmpBackOffset = 0);
    bool placeHook(hookTypes, const Pattern& pat, uintptr_t jmpTo);
    uintptr_t placeHook(uintptr_t offset, uintptr_t jmpTo, uint8_t jmpBackOffset = 0);
    uintptr_t placeHookTotalOffs(uintptr_t offset, uintptr_t jmpTo);
    bool MatchPattern(uintptr_t addr, const char* pattern, const char* mask);
    uintptr_t findPattern(const char* pattern, const char* mask, uintptr_t offset = 0);
    uintptr_t findPattern(const Pattern& pat, uintptr_t offset = 0);


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
