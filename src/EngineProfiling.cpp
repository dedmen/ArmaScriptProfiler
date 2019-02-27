#include "EngineProfiling.h"
#include "ProfilerAdapter.hpp"
#include "AdapterTracy.hpp"
#include <client.hpp>
#include <shared_mutex>
#include "scriptProfiler.hpp"
#include "SignalSlot.hpp"

extern std::shared_ptr<ProfilerAdapter> GProfilerAdapter;


struct CounterHasher {
public:
    size_t operator()(const std::pair<PCounter*, int>& key) const {
        //intercept pairhash
        size_t _hash = std::hash<PCounter*>()(key.first);
        _hash ^= std::hash<uint64_t>()(key.second) + 0x9e3779b9 + (_hash << 6) + (_hash >> 2);
        return _hash;
    }
};

thread_local std::unique_ptr<std::unordered_map<std::pair<PCounter*, int>, std::shared_ptr<ScopeTempStorage>, CounterHasher>> openScopes;
thread_local bool openScopesInit;
std::unordered_map<PCounter*, std::shared_ptr<ScopeInfo>> scopeCache;
std::shared_mutex scopeCacheMtx;
bool noFile = false;
bool noMem = false;
bool tracyConnected = false;
bool checkMainThread = false;
thread_local bool isMainThread = false;

std::string getScriptName(const r_string& str, const r_string& filePath, uint32_t returnFirstLineIfNoName = 0);
void addScopeInstruction(ref<compact_array<ref<game_instruction>>>& bodyCode, const r_string& scriptName);


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
        
        auto x = reinterpret_cast<ref<compact_array<ref<game_instruction>>>*>(code);

        if (sdp.content.length() < 64 || !x || !*x || x->get()->size() < 16) return;

        r_string name(getScriptName(sdp.content, sdp.sourcefile, 32));
        if (!name.empty() && name != "<unknown>"sv)
            addScopeInstruction(*x, name);
    }
}

bool PCounter::shouldTime() {
    if (slot < 0) return false;

    if (checkMainThread && !isMainThread) return false;
    if (!tracyConnected) return false;

    //exclude security cat, evwfGet evGet and so on as they spam too much and aren't useful
    if (cat && cat[0] == 's' && cat[1] == 'e' && cat[2] == 'c' && cat[3] == 'u') return false;
    if (noFile && cat && cat[0] == 'f' && cat[1] == 'i' && cat[2] == 'l' && cat[3] == 'e') return false;
    if (noMem&& cat&& cat[0] == 'm' && cat[1] == 'e' && cat[2] == 'm') return false;
    if (cat && cat[0] == 'd' && cat[1] == 'r' && cat[2] == 'w') return false; //drw
    if (cat && cat[0] == 'd' && cat[1] == 'd' && cat[2] == '1') return false; //dd11
    if (cat && cat[0] == 't' && cat[1] == 'e' && cat[2] == 'x' && cat[3] == 0) return false; //tex
    if (name && name[0] == 'I' && name[1] == 'G' && name[2] == 'S' && name[3] == 'M') return false; //IGSMM no idea what that is, but generates a lot of calls
    if (name && name[0] == 'm' && name[1] == 'a' && name[2] == 'n' && name[3] == 'C') return false; //Man update error. calltime is about constant and uninteresting

    auto tracyProf = std::reinterpret_pointer_cast<AdapterTracy>(GProfilerAdapter);

    std::unordered_map<PCounter*, std::shared_ptr<ScopeInfo>>::iterator found;

    if (checkMainThread) {//No locks needed
        found = scopeCache.find(this);
        if (found == scopeCache.end()) {
            auto res = scopeCache.insert({ this, tracyProf->createScopeStatic(name, cat, 0) });
            found = res.first;
        }
    } else {
        std::shared_lock lock(scopeCacheMtx);
        found = scopeCache.find(this);
        if (found == scopeCache.end()) {
            lock.unlock();
            std::unique_lock lockInternal(scopeCacheMtx);
            auto res = scopeCache.insert({ this, tracyProf->createScopeStatic(name, cat, 0) });
            lockInternal.unlock();//#TODO this is unsafe
            lock.lock();
            found = res.first;
        }
    }

    if (!openScopes)
        openScopes = std::make_unique<std::unordered_map<std::pair<PCounter*, int>, std::shared_ptr<ScopeTempStorage>, CounterHasher>>();

    //make sure the add is not inside the scope
    auto p = std::make_pair(this, slot);
    auto ins = openScopes->insert_or_assign(p,nullptr);
    auto tmp = tracyProf->enterScope(found->second);
    //if (tmp)
        ins.first->second = tmp;
    //else
    //    openScopes.erase(p);
    

    return true;
}

void ScopeProf::doEnd() {
    if (!openScopes || openScopes->empty() || !counter) return;
    auto found = openScopes->find({ counter, counter->slot });
    if (found == openScopes->end()) return;
    GProfilerAdapter->leaveScope(found->second);
    openScopes->erase(found);
}

void ArmaProf::frameEnd(float fps, float time, int smth) {
    if (capture)
        GProfilerAdapter->perFrame();
}

void ArmaProf::scopeCompleted(int64_t start, int64_t end, intercept::types::r_string* stuff, PCounter* counter) {
    if (!openScopes || openScopes->empty() || !counter) return;
    auto found = openScopes->find({ counter, counter->slot });
    if (found == openScopes->end()) return;
    GProfilerAdapter->leaveScope(found->second);
    openScopes->erase(found);
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

HookManager::Pattern pat_compileCacheIns{ //1.88.145.302 profv1 013D40B3
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxx????xxxx????xxxxx????xxxxxxxxxxxxxxxxxxxxxxxxx????xxx?????xxxx?x????xxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxx"sv,
    "\x48\x89\x45\xB0\x8B\x43\x10\x89\x45\xB8\x48\x8B\x43\x18\x48\x85\xC0\x74\x03\xF0\xFF\x00\x48\x89\x45\xC0\x8B\x43\x20\x48\x8D\x54\x24\x00\x48\x8D\x0D\x00\x00\x00\x00\x89\x45\xC8\xE8\x00\x00\x00\x00\x48\x8D\x4D\xA8\xE8\x00\x00\x00\x00\x48\x8B\x4D\xA0\x48\x85\xC9\x74\x1C\x41\x8B\xC7\xF0\x0F\xC1\x01\xFF\xC8\x75\x09\x48\x8B\x4D\xA0\xE8\x00\x00\x00\x00\x48\xC7\x45\x00\x00\x00\x00\x00\x48\x8D\x4C\x24\x00\xE8\x00\x00\x00\x00\x4D\x85\xE4\x74\x1D\x41\x8B\xC7\xF0\x41\x0F\xC1\x04\x24\xFF\xC8\x75\x10\x48\x8B\x0D\x00\x00\x00\x00\x49\x8B\xD4\x48\x8B\x01\xFF\x50\x18\x4D\x85\xF6\x74\x1C\x41\x8B\xC7"sv
};


HookManager::Pattern pat_frameEnd{
    "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxxxx?????x????xxxxxxxxx????xx????xxxx?xxxxxxxx????xx????xx?????x????xxx????xx????xx????xxxxxxxx????xxx????xxxxx????xxxxxxxxxxxxxxxxxxxxxxx"sv,
    "\x48\x8B\xC4\x57\x41\x56\x48\x83\xEC\x78\x48\x89\x58\x10\x0F\xB6\x99\x00\x00\x00\x00\x48\x89\x68\xE8\x48\x89\x70\xE0\x4C\x89\x60\xD8\x44\x8B\x61\x6C\x44\x3B\x61\x68\x4C\x89\x68\xD0\x48\x8B\xF9\x0F\x29\x78\xA8\x0F\x28\xFA\x44\x0F\x4F\x61\x00\x45\x8B\xE9\xC6\x81\x00\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x45\x33\xF6\x4C\x39\x77\x60\x0F\x84\x00\x00\x00\x00\x8B\x87\x00\x00\x00\x00\x4C\x89\x7C\x24\x00\x85\xC0\x7E\x1A\xFF\xC8\x89\x87\x00\x00\x00\x00\x0F\x85\x00\x00\x00\x00\xC6\x87\x00\x00\x00\x00\x00\xE9\x00\x00\x00\x00\x44\x38\xB7\x00\x00\x00\x00\x0F\x84\x00\x00\x00\x00\x8B\x87\x00\x00\x00\x00\x85\xC0\x7E\x08\xFF\xC8\x89\x87\x00\x00\x00\x00\x44\x39\xB7\x00\x00\x00\x00\x7F\x75\x48\x8B\x87\x00\x00\x00\x00\x48\x85\xC0\x74\x69\x44\x38\x70\x10\x74\x63\x48\x8D\x50\x10\x48\x85\xC0\x75\x07\x48\x8D\x15"sv
};

HookManager::Pattern pat_doEnd{
    "xxxxxxxxxxxxxxxxx?????xxxx?????xxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxx????xxxx?x????xxxxxxxxxxxxxxxxx????xxxxxxxxx?????xxxxxx"sv,
    "\x40\x53\x48\x83\xEC\x30\x80\x79\x11\x00\x48\x8B\xD9\x75\x09\x80\x3D\x00\x00\x00\x00\x00\x74\x38\x80\x3D\x00\x00\x00\x00\x00\x74\x0B\x0F\x31\x48\xC1\xE2\x20\x48\x0B\xC2\xEB\x05\xE8\x00\x00\x00\x00\x48\x8B\x13\x4C\x8B\xC0\x48\x8B\x43\x08\x4C\x8D\x4B\x18\x48\x8D\x0D\x00\x00\x00\x00\x48\x89\x44\x24\x00\xE8\x00\x00\x00\x00\x48\x8B\x53\x18\x48\x85\xD2\x74\x1A\xF0\xFF\x0A\x75\x0D\x48\x8B\x0D\x00\x00\x00\x00\x48\x8B\x01\xFF\x50\x18\x48\xC7\x43\x00\x00\x00\x00\x00\x48\x83\xC4\x30\x5B\xC3"sv
};

HookManager::Pattern pat_scopeCompleted{
    "xxxx?xxxx?xxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx?xxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxx????xxxxxx????xxxx?xx????xxx"sv,
    "\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x57\x41\x54\x41\x57\x48\x83\xEC\x20\x48\x8B\x81\x00\x00\x00\x00\x49\x8B\xF8\x4D\x8B\xE1\x48\x3B\xD0\x48\x8B\xD9\x48\x0F\x4C\xD0\x48\xC1\xF8\x04\x48\xC1\xFF\x04\x48\xC1\xFA\x04\x44\x8B\xFA\x2B\xFA\x44\x2B\xF8\x48\x8B\x44\x24\x00\x48\x63\x68\x18\x85\xED\x0F\x88\x00\x00\x00\x00\x8B\x41\x68\x66\x0F\x6E\xC7\x8B\xC8\xD1\xF9\x66\x0F\x6E\xD0\x8B\x43\x6C\x0F\x5B\xC0\x2B\xC1\x66\x0F\x6E\xC8\x0F\x5B\xD2\x42\x8D\x04\x3F\xF3\x0F\x59\xD0\x66\x0F\x6E\xC0\x0F\x5B\xC9\x0F\x5B\xC0\xF3\x0F\x59\xC8\x0F\x2F\xD1\x73\x48\x80\x3B\x00\x0F\x84\x00\x00\x00\x00\x4C\x69\xC5\x00\x00\x00\x00\x4C\x03\x43\x20\x0F\x84\x00\x00\x00\x00\x41\x0F\x0D\x48\x00\x41\xB9\x00\x00\x00\x00\x0F\x1F\x00"sv
};

HookManager::Pattern pat_shouldTime{
    "xxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxx????xxxxxxx"sv,
    "\x48\x63\x41\x18\x85\xC0\x78\x32\x4C\x8B\x01\x33\xD2\x4D\x85\xC0\x74\x12\x41\x38\x10\x74\x0D\x48\x69\xC8\x00\x00\x00\x00\x49\x03\x48\x20\xEB\x03\x48\x8B\xCA\x48\x85\xC9\x74\x0A\x38\x51\x4A\x74\x05\xBA\x00\x00\x00\x00\x0F\xB6\xC2\xC3\x32\xC0\xC3"sv
};

#endif

EngineProfiling::EngineProfiling() {

}

extern Signal<void(bool)> tracyConnectionChanged;


void EngineProfiling::init() {

    tracyConnectionChanged.connect([](bool state)
        {
            tracyConnected = state;
        });

    //order is important
    //hooks.placeHook(hookTypes::doEnd, pat_doEnd, reinterpret_cast<uintptr_t>(doEnd), profEndJmpback, 1, true);
    hooks.placeHook(hookTypes::scopeCompleted, pat_scopeCompleted, reinterpret_cast<uintptr_t>(scopeCompleted), profEndJmpback, 0);
    hooks.placeHook(hookTypes::shouldTime, pat_shouldTime, reinterpret_cast<uintptr_t>(shouldTime), shouldTimeJmpback, 0);
    hooks.placeHook(hookTypes::frameEnd, pat_frameEnd, reinterpret_cast<uintptr_t>(frameEnd), frameEndJmpback, 0);
#ifndef __linux__
    hooks.placeHook(hookTypes::compileCacheIns, pat_compileCacheIns, reinterpret_cast<uintptr_t>(compileCacheIns), compileCacheInsJmpback, 0);
#endif
#ifdef __linux__
    auto found = hooks.findPattern(pat_doEnd, 0);

    auto stuffByte = found + 0x2A;
    uint32_t base = *reinterpret_cast<uint32_t*>(stuffByte);
#else
    auto found = hooks.findPattern(pat_doEnd, 0xD);

    auto stuffByte = found + 0x2 + 2;
    uint32_t offs = *reinterpret_cast<uint32_t*>(stuffByte);
    uint64_t addr = stuffByte + 4 + 1 + offs;
    uint64_t base = addr - 0x121;
#endif
    armaP = reinterpret_cast<ArmaProf*>(base);
    armaP->blip.clear();
    armaP->forceCapture = true;
    armaP->capture = true;
    //disable captureSlowFrame because it can set forceCapture to false
    static auto stuff = intercept::client::host::register_sqf_command("diag_captureSlowFrame"sv, ""sv, [](game_state&, game_value_parameter) -> game_value
        {
            return {};
        }, game_data_type::NOTHING, game_data_type::ARRAY);
}

void EngineProfiling::setMainThreadOnly() {
    checkMainThread = true;
    isMainThread = true;
}

void EngineProfiling::setNoFile() {
    noFile = true;
}

void EngineProfiling::setNoMem() {
    noMem = true;
}
