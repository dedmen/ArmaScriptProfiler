#include "HookManager.hpp"
#ifndef __linux__
#include <Windows.h>
#include <Psapi.h>
#pragma comment (lib, "Psapi.lib")//GetModuleInformation
#else
#include <fstream>
#endif

//This is here because I want to keep includes of windows.h low
HookManager::HookManager() {
    #ifdef __linux__
        std::ifstream maps("/proc/self/maps");
        uintptr_t start;
        uintptr_t end;
        char placeholder;
        maps >> std::hex >> start >> placeholder >> end;
        //link_map *lm = (link_map*) dlopen(0, RTLD_NOW);
        //uintptr_t baseAddress = reinterpret_cast<uintptr_t>(lm->l_addr);
        //uintptr_t moduleSize = 35000000; //35MB hardcoded till I find out how to detect it properly
        engineBase = start;
        engineSize = end - start;
    #else
        MODULEINFO modInfo = { 0 };
        HMODULE hModule = GetModuleHandle(NULL);
        GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO));
        engineBase = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
        engineSize = static_cast<uintptr_t>(modInfo.SizeOfImage);
    #endif
}

bool HookManager::placeHook(hookTypes type, const Pattern& pat, uintptr_t jmpTo, uintptr_t & jmpBackRef, uint8_t jmpBackOffset, bool taintRax) {

    auto found = findPattern(pat);
    if (found == 0) {
//#ifdef _DEBUG
//        __debugbreak(); //#TODO report somehow
//#endif
        return false;
    }
    jmpBackRef = placeHookTotalOffs(found, jmpTo, taintRax) + jmpBackOffset;
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

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

uintptr_t HookManager::placeHookTotalOffs(uintptr_t totalOffset, uintptr_t jmpTo, bool taintRax) {
    unsigned long dwVirtualProtectBackup;


    /*
    32bit
    jmp 0x123122
    0:  e9 1e 31 12 00          jmp    123123 <_main+0x123123>
    64bit
    FF 25 64bit relative
    */
#ifdef _WIN64
    //auto distance = std::max(totalOffset, jmpTo) - std::min(totalOffset, jmpTo);
    // if distance < 2GB (2147483648) we could use the 32bit relative jmp


    if (taintRax) {
        //This is shorter, but messes up RAX

        /*
        push rax; //to restore it inside out target
        moveabs rax, address
        push rax
        ret
         */
#ifndef __linux__
        VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 12u, 0x40u, &dwVirtualProtectBackup);
#endif
        auto memberRax = reinterpret_cast<unsigned char*>(totalOffset);
        auto jmpInstr1 = reinterpret_cast<unsigned char*>(totalOffset+1);
        auto jmpInstr2 = reinterpret_cast<unsigned char*>(totalOffset+2);
        auto addrOffs = reinterpret_cast<uint64_t*>(totalOffset + 3);
        *memberRax = 0x50; //push rax
        *jmpInstr1 = 0x48; //moveabs
        *jmpInstr2 = 0xb8; //into rax
        *addrOffs = static_cast<uint64_t>(jmpTo) /*- totalOffset - 6*/;//offset

        auto pushInstr = reinterpret_cast<unsigned char*>(totalOffset + 11);
        *pushInstr = 0x50; //push rax
        *reinterpret_cast<unsigned char*>(totalOffset + 12) = 0xc3;//ret
        #ifndef __linux__
        VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 12u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        #endif
        return totalOffset + 12;
    } else {
        #ifndef __linux__
        VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 14u, 0x40u, &dwVirtualProtectBackup);
        #endif
        auto jmpInstr = reinterpret_cast<unsigned char*>(totalOffset);
        auto addrOffs = reinterpret_cast<uint32_t*>(totalOffset + 1);
        *jmpInstr = 0x68; //push DWORD
        *addrOffs = static_cast<uint32_t>(jmpTo) /*- totalOffset - 6*/;//offset
        *reinterpret_cast<uint32_t*>(totalOffset + 5) = 0x042444C7; //MOV [RSP+4],
        *reinterpret_cast<uint32_t*>(totalOffset + 9) = static_cast<uint64_t>(jmpTo) >> 32;//DWORD
        *reinterpret_cast<unsigned char*>(totalOffset + 13) = 0xc3;//ret
        #ifndef __linux__
        VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 14u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
        #endif
        return totalOffset + 14;
    }


#else

#ifndef __linux__
    VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 5u, 0x40u, &dwVirtualProtectBackup);
#else
    void* page =reinterpret_cast<void *>(static_cast<unsigned long>(totalOffset & ~(getpagesize() - 1)));
    auto mret = mprotect(page, getpagesize(), PROT_READ|PROT_WRITE|PROT_EXEC);
#endif
    auto jmpInstr = reinterpret_cast<unsigned char *>(totalOffset);
    auto addrOffs = reinterpret_cast<unsigned int *>(totalOffset + 1);
    *jmpInstr = 0xE9;
    *addrOffs = jmpTo - totalOffset - 5;
#ifndef __linux__
    VirtualProtect(reinterpret_cast<LPVOID>(totalOffset), 5u, dwVirtualProtectBackup, &dwVirtualProtectBackup);
#endif
    return totalOffset + 5;
#endif


}


bool HookManager::MatchPattern(uintptr_t addr, std::string_view pattern, std::string_view mask) {
    size_t size = mask.length();
    #ifndef __linux__
    if (IsBadReadPtr(reinterpret_cast<void*>(addr), size))
        return false;
    #endif
    bool found = true;
    for (size_t j = 0; j < size; j++) {
        found &= mask[j] == '?' || pattern[j] == *reinterpret_cast<char*>(addr + j);
    }
    return found;
}

uintptr_t HookManager::findPattern(std::string_view pattern, std::string_view mask, uintptr_t offset /*= 0*/) const {
    uintptr_t base = engineBase;
    uint32_t size = engineSize;

    uintptr_t patternLength = mask.length();

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

uintptr_t HookManager::findPattern(const Pattern & pat, uintptr_t offset) const {
    if (pat.offsetFunc) {
        auto found = findPattern(pat.pattern, pat.mask, pat.offset + offset);
        if (found)
            return pat.offsetFunc(found);
        return found;
    }

    return findPattern(pat.pattern, pat.mask, pat.offset + offset);
}
