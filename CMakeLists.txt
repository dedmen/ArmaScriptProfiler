cmake_minimum_required (VERSION 3.13)
if (${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Windows")
	set (CMAKE_SYSTEM_VERSION 8.1 CACHE TYPE INTERNAL FORCE) #Force 8.1 SDK, to keep it compatible with win7
	project (ArmaScriptProfiler CXX ASM_MASM)
else()
	project (ArmaScriptProfiler CXX ASM)
endif()
find_package (Threads)

set(USE_64BIT_BUILD ${CMAKE_CL_64})
option(USE_STATIC_LINKING "USE_STATIC_LINKING" ON)
option(WITH_BROFILER "WITH_BROFILER" OFF)
option(WITH_CHROME "WITH_CHROME" OFF)

if(USE_STATIC_LINKING) 
	message("WARNING: Linking statically")
	set(INTERCEPT_LINK_TYPE "static")
else()
	message("WARNING: Linking dynamically")
	set(INTERCEPT_LINK_TYPE "dynamic")
endif()

if(MSVC)
	set( CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} /std:c++latest" )
#elseif(MSVC)
#	message(FATAL_ERROR "ERROR: You need a C++17 compatible compiler")
endif()

message("GENERATOR USED: '${CMAKE_GENERATOR}'")
message("COMPILER USED: '${CMAKE_CXX_COMPILER_ID}'")

if(USE_64BIT_BUILD)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/release/@ArmaScriptProfiler/intercept")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/release/@ArmaScriptProfiler/intercept")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

SET(CMAKE_INCLUDE_CURRENT_DIR ON) 
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_SUPPRESS_REGENERATION true)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)


set(TRACY_ENABLE ON)
set(TRACY_ON_DEMAND ON)
set(TRACY_NO_CALLSTACK ON) # This is for native profiling, which we aren't doing
set(TRACY_NO_CODE_TRANSFER ON)
set(TRACY_NO_SAMPLING ON) # No need, too much extra data
set(TRACY_NO_SYSTEM_TRACING ON) # No need, too much extra data
set(TRACY_FIBERS ON) # Arma coroutines (and might use it for scheduled scripts?)
#set(TRACY_NO_CRASH_HANDLER ON) # Might interfere with Arma's handler?
set(TRACY_NO_VSYNC_CAPTURE ON) # Doesn't compile, probably too old win SDK version?

add_subdirectory (tracy)

add_subdirectory(src)

