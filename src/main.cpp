#include "scriptProfiler.hpp"
#include <intercept.hpp>


int intercept::api_version() {
    return INTERCEPT_SDK_API_VERSION;
}

//bool __cdecl intercept::is_signed() {
//    return true;
//}

void intercept::register_interfaces() {
    profiler.registerInterfaces();
}

void intercept::pre_start() {
    profiler.preStart();
}

void intercept::pre_init() {
    profiler.preInit();
}

void intercept::on_frame() {
    profiler.perFrame();
}