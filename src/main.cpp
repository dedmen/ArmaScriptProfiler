#include "scriptProfiler.hpp"
#include <intercept.hpp>


int intercept::api_version() {
    return 1;
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