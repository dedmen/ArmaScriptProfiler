#include "scriptProfiler.hpp"
#include <intercept.hpp>



int __cdecl intercept::api_version() {
    return 1;
}

//bool __cdecl intercept::is_signed() {
//    return true;
//}

void intercept::register_interfaces() {
    profiler.registerInterfaces();
}

void __cdecl intercept::pre_start() {
    profiler.preStart();
}

void __cdecl intercept::pre_init() {
    profiler.preInit();
}