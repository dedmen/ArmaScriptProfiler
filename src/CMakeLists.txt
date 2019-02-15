cmake_minimum_required (VERSION 3.13)

set(INTERCEPT_CLIENT_PATH "${CMAKE_SOURCE_DIR}/intercept/src/client")
set(BROFILER_BASE_PATH "${CMAKE_SOURCE_DIR}/brofiler")

set(INTERCEPT_INCLUDE_PATH "${INTERCEPT_CLIENT_PATH}/headers" "${INTERCEPT_CLIENT_PATH}/headers/shared" "${INTERCEPT_CLIENT_PATH}/headers/client/" "${INTERCEPT_CLIENT_PATH}/headers/client/sqf")
set(BROFILER_INCLUDE_PATH "${BROFILER_BASE_PATH}/BrofilerCore" "${BROFILER_BASE_PATH}/ThirdParty/TaskScheduler/Scheduler/Include")
set(TRACY_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/tracy")


if(USE_64BIT_BUILD)
    set(INTERCEPT_PLUGIN_NAME "ArmaScriptProfiler_x64")
else()
    set(INTERCEPT_PLUGIN_NAME "ArmaScriptProfiler")
endif()


add_definitions(/DINTERCEPT_NO_THREAD_SAFETY)
add_definitions(/DNOMINMAX)
add_definitions(/DINTERCEPT_SQF_STRTYPE_RSTRING)
add_definitions(/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS) #No I don't cheat!

#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/rv/${INTERCEPT_PLUGIN_NAME}/intercept")

file(GLOB_RECURSE INTERCEPT_ASP_SOURCES *.h *.hpp *.c *.cpp *.asm *.s)
SOURCE_GROUP("src" FILES ${INTERCEPT_ASP_SOURCES})

file(GLOB INTERCEPT_SOURCES "${INTERCEPT_CLIENT_PATH}/intercept/client/*.cpp"  "${INTERCEPT_CLIENT_PATH}/intercept/client/sqf/*.cpp" "${INTERCEPT_CLIENT_PATH}/intercept/shared/*.cpp")
SOURCE_GROUP("intercept" FILES ${INTERCEPT_SOURCES})

if (WITH_BROFILER)
	add_compile_definitions(WITH_BROFILER)
	file(GLOB BROFILER_SOURCES 
	"${BROFILER_BASE_PATH}/ThirdParty/TaskScheduler/Scheduler/Source/"
	#"${BROFILER_BASE_PATH}/BrofilerCore/CallstackCollector.cpp"
	#"${BROFILER_BASE_PATH}/BrofilerCore/Core.cpp"
	#"${BROFILER_BASE_PATH}/BrofilerCore/Event.cpp"
	#"${BROFILER_BASE_PATH}/BrofilerCore/EventDescriptionBoard.cpp"
	#"${BROFILER_BASE_PATH}/BrofilerCore/Message.cpp"
	#"${BROFILER_BASE_PATH}/BrofilerCore/ProfilerServer.cpp"
	#"${BROFILER_BASE_PATH}/BrofilerCore/Serialization.cpp"
	#"${BROFILER_BASE_PATH}/BrofilerCore/SysCallCollector.cpp"
	"${BROFILER_BASE_PATH}/BrofilerCore/*.cpp"
	"${BROFILER_BASE_PATH}/BrofilerCore/Platform/*.cpp"
	"${BROFILER_BASE_PATH}/BrofilerCore/Platform/Windows/*.cpp"
	)
	SOURCE_GROUP("src/brofiler" FILES ${BROFILER_SOURCES})
else()
	SET(BROFILER_SOURCES "")
	list(REMOVE_ITEM INTERCEPT_ASP_SOURCES "${CMAKE_SOURCE_DIR}/src/AdapterBrofiler.cpp")
endif()

if (WITH_CHROME)
	add_compile_definitions(WITH_CHROME)
else()
	list(REMOVE_ITEM INTERCEPT_ASP_SOURCES "${CMAKE_SOURCE_DIR}/src/AdapterChrome.cpp")
endif()


if(CMAKE_COMPILER_IS_GNUCXX)
	list(REMOVE_ITEM INTERCEPT_ASP_SOURCES "${CMAKE_SOURCE_DIR}/src/hooks.asm")
else()
	list(REMOVE_ITEM INTERCEPT_ASP_SOURCES "${CMAKE_SOURCE_DIR}/src/hooks.s")
endif()

set(library_sources ${INTERCEPT_ASP_SOURCES})

add_library( ${INTERCEPT_PLUGIN_NAME} SHARED ${library_sources} ${INTERCEPT_SOURCES} ${BROFILER_SOURCES})

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${INTERCEPT_INCLUDE_PATH} )
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${BROFILER_INCLUDE_PATH} )
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${TRACY_INCLUDE_PATH} )

target_link_libraries (${INTERCEPT_PLUGIN_NAME} ${CMAKE_THREAD_LIBS_INIT})

target_link_libraries(${INTERCEPT_PLUGIN_NAME} ${LINK_LIBS})

set_target_properties(${INTERCEPT_PLUGIN_NAME} PROPERTIES PREFIX "")
set_target_properties(${INTERCEPT_PLUGIN_NAME} PROPERTIES FOLDER ArmaScriptProfiler)
message("${CONFIGURATION}")
if(CMAKE_COMPILER_IS_GNUCXX)
	set_source_files_properties(hooks.s PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
	add_compile_definitions(__linux__)
	list(REMOVE_ITEM INTERCEPT_ASP_SOURCES "${CMAKE_SOURCE_DIR}/src/hooks.asm")

	target_compile_options(
		${INTERCEPT_PLUGIN_NAME} PRIVATE 
		"-std=c++1z"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3>"
		"-s"
		"-fPIC"
		"-fpermissive"
		"-static-libgcc"
		"-static-libstdc++"
		"-march=i686"
		"-m32"
		"-Wno-ignored-attributes"
		"-static"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-ffunction-sections>"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-fdata-sections>"

	)
	target_link_options(${INTERCEPT_PLUGIN_NAME} PRIVATE "-m32" "-fPIC" "-static" "-static-libgcc" "-static-libstdc++"
	"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-Wl,--gc-sections>"
	"$<$<OR:$<CONFIG:Release>>:-Wl,--strip-all>"
	) 
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

	target_link_libraries(${INTERCEPT_PLUGIN_NAME} "libstdc++.a")

else()
	target_compile_options(
		${INTERCEPT_PLUGIN_NAME} PRIVATE 
		"/MP" "/Zi"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/MT>"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Ox>"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Ob2>"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Oi>"
		"$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:/Ot>"
	)
	target_link_options(${INTERCEPT_PLUGIN_NAME} PRIVATE "/OPT:REF" "/OPT:ICF" "/DEBUG:FULL") 
endif()


if(CMAKE_COMPILER_IS_GNUCXX)
	set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES LINK_SEARCH_START_STATIC 1)
	set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()

#Binary signing
if(EXISTS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/sign.bat" AND MSVC)
	add_custom_command(TARGET ${INTERCEPT_PLUGIN_NAME}
		POST_BUILD
		COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/sign.bat
		WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
	)
endif()
