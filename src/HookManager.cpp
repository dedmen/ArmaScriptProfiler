#include "HookManager.hpp"
#include <windows.h>
#include <psapi.h>
#pragma comment (lib, "Psapi.lib")//GetModuleInformation

//This is here because I want to keep includes of windows.h low
HookManager::HookManager() {
    MODULEINFO modInfo = { 0 };
    HMODULE hModule = GetModuleHandle(NULL);
    GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO));
    engineBase = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    engineSize = static_cast<uintptr_t>(modInfo.SizeOfImage);
}

bool HookManager::placeHook(hookTypes type, const Pattern& pat, uintptr_t jmpTo, uintptr_t & jmpBackRef, uint8_t jmpBackOffset) {

    auto found = findPattern(pat);
    if (found == 0) {
//#ifdef _DEBUG
//        __debugbreak(); //#TODO report somehow
//#endif
        return false;
    }
    jmpBackRef = placeHookTotalOffs(found, jmpTo) + jmpBackOffset;
    return true;
}

bool HookManager::placeHook(hookTypes, const Pattern & pat, uintptr_t jmpTo) {
    auto found = findPattern(pat);
    if (found == 0) {
#ifdef _DEBUG
        __debugbreak(); //#TODO report somehow
#endif
        return false;
    }
    placeHookTotalOffs(found, jmpTo);
    return true;
}

uintptr_t HookManager::placeHook(uintptr_t offset, uintptr_t jmpTo, uint8_t jmpBackOffset) {
    auto totalOffset = offset + engineBase;
    return placeHookTotalOffs(totalOffset, jmpTo) + jmpBackOffset;
}

uintptr_t HookManager::placeHookTotalOffs(uintptr_t totalOffset, uintptr_t jmpTo) {
    DWORD dwVirtualProtectBackup;


    /*
    32bit
    jmp 0x123122
    0:  e9 1e 31 12 00          jmp    123123 <_main+0x123123>
    64bit
    FF 25 64bit relative
    */
#ifdef X64
    //auto distance = std::max(totalOffset, jmpTo) - std::min(totalOffset, jmpTo);
    // if distance < 2GB (2147483648) we could use the 32bit relative jmp
    VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 14u, 0x40u, &dwVirtualProtectBackup);
    auto jmpInstr = reinterpret_cast<unsigned char*>(totalOffset);
    auto addrOffs = reinterpret_cast<uint32_t*>(totalOffset + 1);
    *jmpInstr = 0x68; //push DWORD
    *addrOffs = static_cast<uint32_t>(jmpTo) /*- totalOffset - 6*/;//offset
    *reinterpret_cast<uint32_t*>(totalOffset + 5) = 0x042444C7; //MOV [RSP+4],
    *reinterpret_cast<uint32_t*>(totalOffset + 9) = static_cast<uint64_t>(jmpTo) >> 32;//DWORD
    *reinterpret_cast<unsigned char*>(totalOffset + 13) = 0xc3;//ret
    VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
    return totalOffset + 14;
#else
    VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 5u, 0x40u, &dwVirtualProtectBackup);
    auto jmpInstr = reinterpret_cast<unsigned char *>(totalOffset);
    auto addrOffs = reinterpret_cast<unsigned int *>(totalOffset + 1);
    *jmpInstr = 0xE9;
    *addrOffs = jmpTo - totalOffset - 5;
    VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 5u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
    return totalOffset + 5;
#endif


}


bool HookManager::MatchPattern(uintptr_t addr, const char* pattern, const char* mask) {
    size_t size = strlen(mask);
    if (IsBadReadPtr((void*) addr, size))
        return false;
    bool found = true;
    for (size_t j = 0; j < size; j++) {
        found &= mask[j] == '?' || pattern[j] == *(char*) (addr + j);
    }
    if (found)
        return true;
    return false;
}

uintptr_t HookManager::findPattern(const char* pattern, const char* mask, uintptr_t offset /*= 0*/) {
    uintptr_t base = engineBase;
    uint32_t size = engineSize;

    uintptr_t patternLength = (DWORD) strlen(mask);

    for (uintptr_t i = 0; i < size - patternLength; i++) {
        bool found = true;
        for (uintptr_t j = 0; j < patternLength; j++) {
            found &= mask[j] == '?' || pattern[j] == *reinterpret_cast<char*>(base + i + j);
            if (!found)
                break;
        }
        if (found)
            return base + i + offset;
    }
    return 0x0;
}

uintptr_t HookManager::findPattern(const Pattern & pat, uintptr_t offset) {
    if (pat.offsetFunc) {
        auto found = findPattern(pat.pattern, pat.mask, pat.offset + offset);
        if (found)
            return pat.offsetFunc(found);
        return found;
    }

    return findPattern(pat.pattern, pat.mask, pat.offset + offset);
}