#include "EngineProfiling.h"
#include "ProfilerAdapter.hpp"
#include "AdapterTracy.hpp"
#include <client.hpp>
#include <shared_mutex>
#include "scriptProfiler.hpp"
#include "SignalSlot.hpp"
#if _WIN32
#include <windows.h>
#endif

struct CounterHasher {
public:
    size_t operator()(const std::pair<PCounter*, int>& key) const {
        //intercept pairhash
        size_t _hash = std::hash<PCounter*>()(key.first);
        _hash ^= std::hash<uint64_t>()(key.second) + 0x9e3779b9 + (_hash << 6) + (_hash >> 2);
        return _hash;
    }
};

// We need to check to not close a scope that was never opened. But if scopes always open and close in order AND we don't need ScopeTempStorage, a simple stack works too
#define OPEN_SCOPE_MAP 1

#if OPEN_SCOPE_MAP
thread_local std::unique_ptr<std::unordered_map<std::pair<PCounter*, int>, std::shared_ptr<ScopeTempStorage>, CounterHasher>> openScopes;
#else
thread_local std::vector<std::pair<PCounter*, int64_t>> openScopes;
std::atomic<uint64_t> globalZoneFlushC = 0;
thread_local uint64_t threadZoneFlushC = 0;
#endif

thread_local std::unordered_map<PCounter*, std::shared_ptr<ScopeInfo>> scopeCache;
//std::shared_mutex scopeCacheMtx;
bool noFile = false;
bool noMem = false;
bool tracyConnected = false;
bool checkMainThread = false;
thread_local bool isMainThread = false;
bool EngineProfilingEnabled = true;
thread_local bool ignoreScopes = false;

std::string getScriptName(const r_string& str, const r_string& filePath, uint32_t returnFirstLineIfNoName = 0);
void addScopeInstruction(auto_array<ref<game_instruction>>& bodyCode, const r_string& scriptName);


extern "C" {
    uintptr_t profEndJmpback;
    uintptr_t shouldTimeJmpback;
    uintptr_t frameEndJmpback;
    uintptr_t compileCacheInsJmpback;


    void shouldTime();
    void doEnd();
    void scopeCompleted();
    void frameEnd();
    void compileCacheIns();

    void insertCompileCache(uintptr_t code, sourcedocpos& sdp) {
        //broken
        //auto x = reinterpret_cast<ref<compact_array<ref<game_instruction>>>*>(code);
        //
        //if (sdp.content.length() < 64 || !x || !*x || x->get()->size() < 16) return;
        //
        //r_string name(getScriptName(sdp.content, sdp.sourcefile, 32));
        //if (!name.empty() && name != "<unknown>"sv)
        //    addScopeInstruction(x, name);
    }
}

bool PCounter::shouldTime() {
    if (slot < 0) return false;

    if (checkMainThread && !isMainThread) return false;
    if (!tracyConnected || !EngineProfilingEnabled) return false;
    if (ignoreScopes) return false;

    //exclude security cat, evwfGet evGet and so on as they spam too much and aren't useful
    if (cat && cat[0] == 's' && cat[1] == 'e' && cat[2] == 'c' && cat[3] == 'u') return false;
    if (noFile && (cat && cat[0] == 'f' && cat[1] == 'i' && cat[2] == 'l' && cat[3] == 'e') || name[0] == 'f' && name[1] == 's') return false;
    if (noMem && cat&& cat[0] == 'm' && cat[1] == 'e' && cat[2] == 'm') return false;
    if (cat && cat[0] == 'd' && cat[1] == 'r' && cat[2] == 'w') return false; //drw
    if (cat && cat[0] == 'd' && cat[1] == 'd' && cat[2] == '1') return false; //dd11
    if (cat && cat[0] == 't' && cat[1] == 'e' && cat[2] == 'x' && cat[3] == 0) return false; //tex
    if (cat && cat[0] == 'o' && cat[1] == 'g' && cat[2] == 'g' && cat[3] == 0) return false; //ogg jumps between new/different threads alot
    if (name && name[0] == 'I' && name[1] == 'G' && name[2] == 'S' && name[3] == 'M') return false; //IGSMM no idea what that is, but generates a lot of calls
    if (name && name[0] == 'm' && name[1] == 'a' && name[2] == 'n' && name[3] == 'C') return false; //Man update error. calltime is about constant and uninteresting

    auto tracyProf = std::reinterpret_pointer_cast<AdapterTracy>(GProfilerAdapter);

    std::unordered_map<PCounter*, std::shared_ptr<ScopeInfo>>::iterator found = scopeCache.find(this);
    if (found == scopeCache.end()) {
        auto res = scopeCache.insert({ this, tracyProf->createScopeStatic(name, cat, 0) });
        found = res.first;
    }

#if OPEN_SCOPE_MAP
    if (!openScopes)
        openScopes = std::make_unique<std::unordered_map<std::pair<PCounter*, int>, std::shared_ptr<ScopeTempStorage>, CounterHasher>>();

    //make sure the add is not inside the scope
    auto p = std::make_pair(this, slot);
    auto ins = openScopes->insert_or_assign(p,nullptr);
    auto tmp = tracyProf->enterScope(found->second);
    //if (tmp) // Entering scope might fail? Only really if the source location data is invalid
        ins.first->second = tmp;
    //else
    //    openScopes.erase(p);
#else
    tracyProf->enterScopeNoStorage(found->second);

    while (globalZoneFlushC > threadZoneFlushC)
    {
        // We were supposed to flush all our scopes (because the tracy client connection ID changed)
        openScopes.clear();
        ++threadZoneFlushC;
    }

    openScopes.emplace_back(this, (int64_t)__rdtsc());
#endif

    return true;
}

void ScopeProf::doEnd() {
    // deprecated old stuff
}

void ArmaProf::frameEnd(float fps, float time, int smth) {
    if (capture) {
        auto armaAdapter = std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter);
        armaAdapter->perFrame();
    }
}

void ArmaProf::scopeCompleted(int64_t start, int64_t end, intercept::types::r_string* extraInfo, PCounter* counter) {
#if OPEN_SCOPE_MAP
    if (!openScopes || openScopes->empty() || !counter) return;
    auto found = openScopes->find({ counter, counter->slot });
    if (found == openScopes->end()) return;
    if (extraInfo && !extraInfo->empty())
        GProfilerAdapter->setDescription(found->second, *extraInfo);
    GProfilerAdapter->leaveScope(found->second);
    openScopes->erase(found);
#else
    if (openScopes.empty())
        return;
    bool scopeMatchesLastCounter = openScopes.back().first == counter;
    bool scopeStartedBeforeLastCounter = start < openScopes.back().second;

    if (!scopeMatchesLastCounter && !scopeStartedBeforeLastCounter)
        return;
    
    // We never opened this counter?
    
    while (globalZoneFlushC > threadZoneFlushC)
    {
        // We were supposed to flush all our scopes (because the tracy client connection ID changed)
        openScopes.clear();
        ++threadZoneFlushC;
        return;
    }
    
    if (ignoreScopes) return;

    auto tracyProf = std::reinterpret_pointer_cast<AdapterTracy>(GProfilerAdapter);

    if (scopeStartedBeforeLastCounter && !scopeMatchesLastCounter)
    {
        // We got desynchronized.
        // We are currently exiting a scope, that started before the last active scope. We missed the exit of the last active scope
        // This is probably because the last active scope hasn't ended, because it's in a yielded coroutine somewhere else. We'll just have to drop it

        // End all scopes, which started after the one we are now exiting
        while (!openScopes.empty() && openScopes.back().second > start)
        {
            openScopes.pop_back();
            tracyProf->leaveScopeNoStorage();
        }

        if (openScopes.empty())
            return; // This is very unlikely, I don't think this ever happens
    }

    openScopes.pop_back();

    if (extraInfo && !extraInfo->empty())
        tracyProf->setDescriptionNoStorage(*extraInfo);
    tracyProf->leaveScopeNoStorage(end);
#endif


}


#ifdef __linux__

HookManager::Pattern pat_frameEnd{
    "xxxxxxxxxxxxxxxxxxxxxxxx????xx?????xxxxxxxxxxxxxxxxx????xxxxxxx????xx????xxxx????xxxxxxx????xx????xxxxxxxxxxx????x????xxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxx?????xx????xx"sv,
    "\x55\x57\x56\x53\x83\xEC\x5C\x8B\x6C\x24\x70\xD9\x44\x24\x74\x8B\x45\x48\x8B\x55\x44\x0F\xB6\x9D\x00\x00\x00\x00\xC6\x85\x00\x00\x00\x00\x00\xD9\x5C\x24\x04\x89\x2C\x24\x39\xC2\x0F\x4E\xC2\x89\x44\x24\x30\xE8\x00\x00\x00\x00\x8B\x45\x40\x85\xC0\x0F\x84\x00\x00\x00\x00\x8B\x85\x00\x00\x00\x00\x85\xC0\x0F\x8E\x00\x00\x00\x00\x83\xE8\x01\x85\xC0\x89\x85\x00\x00\x00\x00\x0F\x84\x00\x00\x00\x00\x8D\x44\x24\x48\x89\x44\x24\x04\xC7\x04\x24\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x8B\x44\x24\x4C\x89\xC2\xC1\xFA\x1F\x89\x44\x24\x28\xB8\x00\x00\x00\x00\x89\x54\x24\x2C\xF7\x6C\x24\x48\x01\x44\x24\x28\x11\x54\x24\x2C\x83\xBD\x00\x00\x00\x00\x00\x0F\x84\x00\x00\x00\x00\x84\xDB"sv
};


//Not real doend
HookManager::Pattern pat_doEnd{
    "xxxxxx????xxx?????xxx????xxxxxxxxxxxxxxxxx????x????xxxxxxxxxxx????xxxxx"sv,
//                                             ^^^^ that's it
    "\x55\x57\x56\x53\x81\xEC\x00\x00\x00\x00\x80\xBC\x24\x00\x00\x00\x00\x00\x8B\x84\x24\x00\x00\x00\x00\x75\x2C\x89\x44\x24\x08\x8D\x44\x24\x50\x89\x44\x24\x04\xC7\x04\x24\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x89\xC5\x8B\x45\x04\x85\xC0\x75\x52\x81\xC4\x00\x00\x00\x00\x5B\x5E\x5F\x5D\xC3"
};

HookManager::Pattern pat_scopeCompleted{
    "xxxxxxxxxxxxxxxxxxxxxxxxx????xx????xxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx"sv,
    "\x55\x57\x56\x53\x83\xEC\x3C\x8B\x74\x24\x50\x8B\x5C\x24\x58\x8B\x7C\x24\x5C\x8B\x6C\x24\x60\x8B\x96\x00\x00\x00\x00\x8B\x86\x00\x00\x00\x00\x8B\x4C\x24\x54\x39\xD3\x7F\x0A\x0F\x8D\x00\x00\x00\x00\x89\xC1\x89\xD3\x0F\xAC\xD9\x04\x0F\xAC\xD0\x04\x89\xCB\x29\xC3\x89\xF8\x0F\xAC\xE8\x04\x89\xC7\x8B\x44\x24\x68\x29\xCF\x8B\x68\x0C\x85\xED\x78\x68\x8B\x4E\x44\x8B\x46\x48\x89\x7C\x24\x1C\xDB\x44\x24\x1C\x89\x4C\x24\x1C\xD1\xF9\xDB\x44\x24\x1C\x29\xC8\xDE\xC9\x89\x44\x24\x1C\x8D\x04\x3B\xDB\x44\x24\x1C\x89\x44\x24\x1C\xDB\x44\x24\x1C\xDE\xC9\xDF\xE9\xDD\xD8\x76\x53\x80\x3E\x00\x74\x2C\x69\xD5\x00\x00\x00\x00\x03\x56\x20\x74\x21\x8D\x76\x00"sv
};

HookManager::Pattern pat_shouldTime{
    "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxx"sv,
    "\x8B\x44\x24\x04\x8B\x48\x0C\x85\xC9\x78\x25\x8B\x10\x31\xC0\x85\xD2\x74\x15\x80\x3A\x00\x74\x10\x69\xC9\x00\x00\x00\x00\x03\x4A\x20\x74\x05\x0F\xB6\x41\x4A\xC3"sv
};

#else
//#FIXME
//HookManager::Pattern pat_compileCacheIns{ //1.88.145.302 profv1 013D40B3
//    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxx????xxxx????xxxxx????xxxxxxxxxxxxxxxxxxxxxxxxx????xxx?????xxxx?x????xxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxx"sv,
//    "\x48\x89\x45\xB0\x8B\x43\x10\x89\x45\xB8\x48\x8B\x43\x18\x48\x85\xC0\x74\x03\xF0\xFF\x00\x48\x89\x45\xC0\x8B\x43\x20\x48\x8D\x54\x24\x00\x48\x8D\x0D\x00\x00\x00\x00\x89\x45\xC8\xE8\x00\x00\x00\x00\x48\x8D\x4D\xA8\xE8\x00\x00\x00\x00\x48\x8B\x4D\xA0\x48\x85\xC9\x74\x1C\x41\x8B\xC7\xF0\x0F\xC1\x01\xFF\xC8\x75\x09\x48\x8B\x4D\xA0\xE8\x00\x00\x00\x00\x48\xC7\x45\x00\x00\x00\x00\x00\x48\x8D\x4C\x24\x00\xE8\x00\x00\x00\x00\x4D\x85\xE4\x74\x1D\x41\x8B\xC7\xF0\x41\x0F\xC1\x04\x24\xFF\xC8\x75\x10\x48\x8B\x0D\x00\x00\x00\x00\x49\x8B\xD4\x48\x8B\x01\xFF\x50\x18\x4D\x85\xF6\x74\x1C\x41\x8B\xC7"sv
//};


HookManager::Pattern pat_frameEnd{
    "xxxxxxxx????xxx????xxxxxx????xxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxx????xxx????xxx????xxx????xxxxxxxxxxxxxxxxxxxx?xxxxxx?xxxxxxxxxxxxx??"sv,
    "\x40\x56\x57\x41\x57\x48\x81\xEC\x00\x00\x00\x00\x48\x8B\x81\x00\x00\x00\x00\x48\x8B\xF9\x48\x89\x81\x00\x00\x00\x00\x8B\x41\x1C\x39\x41\x28\x0F\x29\xB4\x24\x00\x00\x00\x00\x0F\x28\xF1\x0F\x4E\x41\x28\x33\xF6\x4C\x63\xF8\x85\xC0\x7E\x64\x8B\xD6\x4D\x8B\xC7\x48\x8B\x4F\x20\x48\x8D\x92\x00\x00\x00\x00\x8B\x84\x0A\x00\x00\x00\x00\x89\x84\x0A\x00\x00\x00\x00\x8B\x84\x0A\x00\x00\x00\x00\x89\x84\x0A\x00\x00\x00\x00\x49\x83\xE8\x01\x75\xD3\x48\x8B\xCE\x4D\x8B\xC7\x48\x8B\x57\x20\x40\x38\x74\x11\x00\x75\x14\x40\x38\x74\x11\x00\x75\x0D\x8B\x44\x11\x30\x85\xC0\x74\x05\xC6\x44\x11\x00\x00"sv
};

// This is destructor of a scope. Whereas scopeCompleted is what the destructor calls
HookManager::Pattern pat_doEnd{
    "xxxx?xxxxxxxxxxxxxxxx?????xxxxxxxxxx?????xxxxxxxxxxxxxx????xxxxxxxxxxxxx????xxxxxxxxxxx?x????"sv,
    "\x48\x89\x5C\x24\x00\x57\x48\x83\xEC\x30\x80\x79\x11\x00\x48\x8B\xD9\x75\x0F\x80\x3D\x00\x00\x00\x00\x00\x75\x06\x48\x8D\x79\x18\xEB\x3B\x80\x3D\x00\x00\x00\x00\x00\x74\x0B\x0F\x31\x48\xC1\xE2\x20\x48\x0B\xC2\xEB\x05\xE8\x00\x00\x00\x00\x48\x8B\x13\x48\x8D\x7B\x18\x4C\x8B\xC0\x48\x8D\x0D\x00\x00\x00\x00\x48\x8B\x43\x08\x4C\x8B\xCF\x48\x89\x44\x24\x00\xE8\x00\x00\x00\x00"sv
};

// Just some scope start, so we can find the boss man
//HookManager::Pattern pat_aScopeStart{
//    "xxxxxxxxx?????xxxxxxxxxxxxxxxxxxxxxx????xxx????xxx????xxx????xxx????xxx????xxxxxx????xxxxx????x????xx????xx?????xxx????xxxx????xxxxxx?xx?????xxxxxxxx?xxxxx????xxxxxxxx????xx????xx"sv,
//    "\x48\x8B\xC4\x48\x83\xEC\x68\x80\x3D\x00\x00\x00\x00\x00\x48\x89\x58\x10\x48\x89\x68\x18\x48\x8B\xE9\x48\x89\x78\xF0\x4C\x89\x70\xE8\x4C\x8D\x35\x00\x00\x00\x00\x75\x46\xE8\x00\x00\x00\x00\x48\x8D\x15\x00\x00\x00\x00\x4C\x89\x35\x00\x00\x00\x00\x4C\x8D\x05\x00\x00\x00\x00\x48\x89\x15\x00\x00\x00\x00\x44\x8B\xC8\x4C\x89\x05\x00\x00\x00\x00\x49\x8B\xCE\x89\x05\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x89\x05\x00\x00\x00\x00\xC6\x05\x00\x00\x00\x00\x00\x48\x8D\x1D\x00\x00\x00\x00\x48\x8B\xCB\xE8\x00\x00\x00\x00\x33\xFF\x48\x89\x5C\x24\x00\x83\x3D\x00\x00\x00\x00\x00\x88\x44\x24\x40\x48\x89\x7C\x24\x00\x74\x45\x40\x38\x3D\x00\x00\x00\x00\x75\x12\x84\xC0\x74\x38\xFF\x15\x00\x00\x00\x00\x39\x05\x00\x00\x00\x00\x75\x2A, "sv
//};

HookManager::Pattern pat_scopeCompleted{
    "xxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxx????xxxxxx????"sv,
    "\x40\x53\x56\x41\x54\x41\x55\x41\x57\x48\x83\xEC\x20\x48\x8B\x81\x00\x00\x00\x00\x49\x8B\xF0\x48\x3B\xD0\x4D\x8B\xE9\x48\x8B\xD9\x48\x0F\x4C\xD0\x48\xC1\xF8\x04\x48\xC1\xFA\x04\x48\xC1\xFE\x04\x44\x8B\xE2\x44\x2B\xE0\x2B\xF2\x48\x8B\x44\x24\x00\x4C\x63\x78\x18\x45\x85\xFF\x0F\x88\x00\x00\x00\x00\x8B\x41\x68\x8B\xC8\xD1\xF9\x66\x0F\x6E\xC6\x0F\x5B\xC0\x66\x0F\x6E\xD0\x8B\x43\x6C\x2B\xC1\x0F\x5B\xD2\x66\x0F\x6E\xC8\x42\x8D\x04\x26\xF3\x0F\x59\xD0\x66\x0F\x6E\xC0\x0F\x5B\xC0\x0F\x5B\xC9\xF3\x0F\x59\xC8\x0F\x2F\xD1\x73\x3C\x80\x3B\x00\x0F\x84\x00\x00\x00\x00\x49\x69\xD7\x00\x00\x00\x00\x48\x03\x53\x20\x0F\x84\x00\x00\x00\x00"sv
};

HookManager::Pattern pat_shouldTime{
    "xxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxx"sv,
    "\x48\x63\x41\x18\x85\xC0\x78\x2A\x48\x8B\x11\x48\x85\xD2\x74\x22\x80\x3A\x00\x74\x0D\x48\x69\xC8\x00\x00\x00\x00\x48\x03\x4A\x20\xEB\x02\x33\xC9\x48\x85\xC9\x74\x09\x80\x79\x4A\x00\x74\x03\xB0\x01\xC3"sv
};

#endif

EngineProfiling::EngineProfiling() {

}

extern Signal<void(bool)> tracyConnectionChanged;


#pragma region PETools
// Small extract of PETools from APE
#if _WIN32

PIMAGE_IMPORT_DESCRIPTOR GetImportDirectory(HANDLE module) {
    auto dosHeader = (PIMAGE_DOS_HEADER)module;
    auto pNTHeader = (PIMAGE_NT_HEADERS)((BYTE*)dosHeader + dosHeader->e_lfanew);
    auto base = (DWORD64)dosHeader;

    auto importDirStartRVA = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    auto size = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

    auto importDir = (PIMAGE_IMPORT_DESCRIPTOR)((PBYTE)base + importDirStartRVA);
    return importDir;
}

PIMAGE_THUNK_DATA GetModuleImportDescriptor(HMODULE module, std::string_view libName, PIMAGE_THUNK_DATA* lookupThunk = nullptr) {

    PIMAGE_IMPORT_DESCRIPTOR importDescriptor = GetImportDirectory(module);

    bool found = false;
    while (/*importDescriptor->Characteristics &&*/ importDescriptor->Name) {
        PSTR importName = (PSTR)((PBYTE)module + importDescriptor->Name);
        if (_stricmp(importName, libName.data()) == 0) {
            found = true;
            break;
        }
        importDescriptor++;
    }
#if REL_DEBUG
    if (!found) __debugbreak();
#endif
    PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((PBYTE)module + importDescriptor->FirstThunk);
    if (lookupThunk)
        *lookupThunk = (PIMAGE_THUNK_DATA)((PBYTE)module + importDescriptor->Characteristics);

    return thunk;
}

// Get a method address by using a known modules import address table (if that dll imports the method we want)
uintptr_t PatchIAT(HMODULE module, std::string_view libname, std::string_view procname, uintptr_t newTarget) {
    PIMAGE_THUNK_DATA lookupThunk;
    auto thunk = GetModuleImportDescriptor(module, libname, &lookupThunk);
    while (thunk->u1.Function) {
        PROC* funcStorage = (PROC*)&thunk->u1.Function;
        auto funcString = (char*)((uintptr_t)module + lookupThunk->u1.ForwarderString + 2);
        // Found it, now let's patch it
        if (std::string_view(funcString) == procname) {

            auto old = (uintptr_t)*funcStorage;

            // Get the memory page where the info is stored
            MEMORY_BASIC_INFORMATION mbi;
            VirtualQuery(funcStorage, &mbi, sizeof(MEMORY_BASIC_INFORMATION));

            // Try to change the page to be writable if it's not already
            if (!VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_READWRITE, &mbi.Protect))
                break;

            *(uintptr_t*)funcStorage = newTarget;

            // Restore the old flag on the page
            DWORD dwOldProtect;
            VirtualProtect(mbi.BaseAddress, mbi.RegionSize, mbi.Protect, &dwOldProtect);

            return old;
        }

        thunk++;
        lookupThunk++;
    }
    return 0;
}


#endif // _WIN32
#pragma endregion PETools


decltype(SwitchToFiber)* origSwitchToFiber;

VOID
WINAPI
SwitchToFiberReplacement(
    _In_ LPVOID lpFiber
) {
    AdapterTracy::SwitchToFiber("Fib");
    origSwitchToFiber(lpFiber);
    AdapterTracy::LeaveFiber();
}

void EngineProfiling::init() {

    isMainThread = true;

    tracyConnected = AdapterTracy::isConnected();
    tracyConnectionChanged.connect([](bool state)
        {
            tracyConnected = state;
#if !OPEN_SCOPE_MAP
            ++globalZoneFlushC;
#endif
        });

    if (auto tracyAdapter = std::dynamic_pointer_cast<AdapterTracy>(GProfilerAdapter)) {

        // Handler for these is in TracyParameterUpdated
        tracyAdapter->addParameter(TP_EngineProfilingEnabled, "EngineProfilingEnabled", true, 1);
        tracyAdapter->addParameter(TP_EngineProfilingMainThreadOnly, "EngineProfilingMainThreadOnly", true, 0);
    }

    //order is important
    hooks.placeHook(hookTypes::scopeCompleted, pat_scopeCompleted, reinterpret_cast<uintptr_t>(scopeCompleted), profEndJmpback, 0);
    hooks.placeHook(hookTypes::shouldTime, pat_shouldTime, reinterpret_cast<uintptr_t>(shouldTime), shouldTimeJmpback, 0);
    hooks.placeHook(hookTypes::frameEnd, pat_frameEnd, reinterpret_cast<uintptr_t>(frameEnd), frameEndJmpback, 0);
#ifndef __linux__
    //hooks.placeHook(hookTypes::compileCacheIns, pat_compileCacheIns, reinterpret_cast<uintptr_t>(compileCacheIns), compileCacheInsJmpback, 0);
#endif
#ifdef __linux__
    auto found = hooks.findPattern(pat_doEnd, 0);

    if (found)
    {
        auto stuffByte = found + 0x2A;
        uint32_t base = *reinterpret_cast<uint32_t*>(stuffByte);
#else
    auto found = hooks.findPattern(pat_doEnd, 0x45);

    if (found)
    {
        // lea r14, bossman

        uint64_t afterInstruction = found + 7;
        uint32_t offs = *reinterpret_cast<uint32_t*>(found + 0x3);
        uint64_t addr = afterInstruction + offs;
        uint64_t base = addr;
#endif
        armaP = reinterpret_cast<ArmaProf*>(base);
        armaP->slowFrameScopeFilter.clear();
        armaP->forceCapture = true;
        armaP->capture = true;
    }


    //disable captureSlowFrame because it can set forceCapture to false
#ifndef _DEBUG
    static auto stuff = intercept::client::host::register_sqf_command("diag_captureSlowFrame"sv, ""sv, [](game_state&, game_value_parameter) -> game_value
        {
            return {};
        }, game_data_type::NOTHING, game_data_type::ARRAY);
#endif


#if !OPEN_SCOPE_MAP
    // Game uses Fiber's, which is fine if we have the exact per-scope mapping. But its not fine if we have to rely on ordering of scopes.
    // We are using a IAT hook for this, to catch all Fiber switches

    HMODULE armaHandle;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(found), &armaHandle);
    origSwitchToFiber = reinterpret_cast<decltype(SwitchToFiber)*>(PatchIAT(armaHandle, "kernel32.dll", "SwitchToFiber", (uintptr_t)&SwitchToFiberReplacement));
#endif


}

void EngineProfiling::setMainThreadOnly() {
    checkMainThread = true;
}

void EngineProfiling::setNoFile() {
    noFile = true;
}

void EngineProfiling::setNoMem() {
    noMem = true;
}
