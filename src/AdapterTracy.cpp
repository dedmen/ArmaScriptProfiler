#include "AdapterTracy.hpp"

#define TRACY_ENABLE
#define TRACY_ON_DEMAND
#include <TracyClient.cpp>
#include <Tracy.hpp>
#include <unordered_set>
#include <string_view>
//#TODO libpthread and libdl on linux
using namespace std::chrono_literals;

class ScopeInfoTracy final: public ScopeInfo {
public:
    tracy::SourceLocationData info;
};

class ScopeTempStorageTracy final : public ScopeTempStorage {
public:

    ScopeTempStorageTracy(const tracy::SourceLocationData* srcloc) : zone(srcloc) {}
    ScopeTempStorageTracy(const tracy::SourceLocationData* srcloc, uint64_t threadID) : zone(srcloc, threadID) {}
    tracy::ScopedZone zone;
};

AdapterTracy::AdapterTracy() {
    type = AdapterType::Tracy;
}

AdapterTracy::~AdapterTracy() {
    tracy::s_profiler.RequestShutdown();
    while (!tracy::s_profiler.HasShutdownFinished())
        std::this_thread::sleep_for(5ms);
}

void AdapterTracy::perFrame() {
    FrameMark;
}

std::shared_ptr<ScopeInfo> AdapterTracy::createScope(intercept::types::r_string name,
    intercept::types::r_string filename, uint32_t fileline) {
    
    if (getOmitFilePaths()) filename.clear();

    auto tuple = std::make_tuple(name,filename,fileline);
    auto found = scopeCache.find(tuple);
    if (found == scopeCache.end()) {
        auto info = std::make_shared<ScopeInfoTracy>();
        info->info = tracy::SourceLocationData{nullptr, std::get<0>(tuple).c_str(), std::get<1>(tuple).c_str(), std::get<2>(tuple), 0};

        scopeCache.insert({tuple, info});
        return info;
    }
    return found->second; 
}

std::shared_ptr<ScopeTempStorage> AdapterTracy::enterScope(std::shared_ptr<ScopeInfo> scope) {
    auto info = std::dynamic_pointer_cast<ScopeInfoTracy>(scope);
    if (!info || !isConnected()) return nullptr; //#TODO debugbreak? log error?
    ensureReady();

    auto ret = std::make_shared<ScopeTempStorageTracy>(&info->info);
    return ret;
}

std::shared_ptr<ScopeTempStorage> AdapterTracy::enterScope(std::shared_ptr<ScopeInfo> scope, uint64_t threadID) {
    auto info = std::dynamic_pointer_cast<ScopeInfoTracy>(scope);
    if (!info || !isConnected()) return nullptr; //#TODO debugbreak? log error?
    ensureReady();

    auto ret = std::make_shared<ScopeTempStorageTracy>(&info->info, threadID);
    return ret;
}

void AdapterTracy::leaveScope(std::shared_ptr<ScopeTempStorage> tempStorage) {
    auto tmpStorage = std::dynamic_pointer_cast<ScopeTempStorageTracy>(tempStorage);
    if (!tmpStorage) return; //#TODO debugbreak? log error?

    tmpStorage->zone.end(); //zone destructor ends zone
}

void AdapterTracy::setName(std::shared_ptr<ScopeTempStorage> tempStorage, const intercept::types::r_string& name) {
    auto tmpStorage = std::dynamic_pointer_cast<ScopeTempStorageTracy>(tempStorage);
    if (!tmpStorage) return; //#TODO debugbreak? log error?
    tmpStorage->zone.Name(name.c_str(), name.length());
}

void AdapterTracy::setDescription(std::shared_ptr<ScopeTempStorage> tempStorage, const intercept::types::r_string& descr) {
    auto tmpStorage = std::dynamic_pointer_cast<ScopeTempStorageTracy>(tempStorage);
    if (!tmpStorage) return; //#TODO debugbreak? log error?
    tmpStorage->zone.Text(descr.c_str(), descr.length());
}

void AdapterTracy::addLog(intercept::types::r_string message) {
    if (message.empty()) return;
    tracy::Profiler::Message(message.c_str(), message.length());
}

void AdapterTracy::setCounter(intercept::types::r_string name, float val) {
    counterCache.insert(name);
    tracy::Profiler::PlotData(name.c_str(), val);
}

void AdapterTracy::setCounter(const char* name, float val) const {
    tracy::Profiler::PlotData(name, val);
}

std::shared_ptr<ScopeInfo> AdapterTracy::createScopeStatic(const char* name, const char* filename, uint32_t fileline) const {
    auto info = std::make_shared<ScopeInfoTracy>();
    info->info = tracy::SourceLocationData{nullptr, name, filename,fileline, 0};
    return info;
}

bool AdapterTracy::isConnected() {
    return tracy::s_profiler.IsConnected();
}

void AdapterTracy::ensureReady() {
    if (tracy::s_token.ptr) return;

    tracy::rpmalloc_thread_initialize();
    tracy::s_token_detail = tracy::moodycamel::ProducerToken(tracy::s_queue);
    tracy::s_token = tracy::ProducerWrapper{tracy::s_queue.get_explicit_producer(tracy::s_token_detail) };


}
