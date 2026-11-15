cmake_minimum_required(VERSION 3.22)
project(EngineSoundDesigner VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ESD_BUILD_APP   "Build the JUCE desktop application" ON)
option(ESD_BUILD_TESTS "Build the Catch2 unit tests" ON)
option(ESD_BUILD_TOOLS "Build the command line tools" ON)
option(ESD_ENABLE_ASIO "Enable ASIO (requires the Steinberg ASIO SDK, see docs/BUILD.md)" OFF)
set(ESD_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (folder containing common/iasiodrv.h)")

if(MSVC)
    add_compile_options(/utf-8 /MP)
endif()

# ---------------------------------------------------------------- third party
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(external/json EXCLUDE_FROM_ALL)

add_library(esd_sqlite STATIC external/sqlite/sqlite3.c)
target_include_directories(esd_sqlite PUBLIC external/sqlite)
target_compile_definitions(esd_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)

# ---------------------------------------------------------------- core (no JUCE)
add_subdirectory(src/core)

if(ESD_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(ESD_BUILD_TESTS)
    add_subdirectory(external/Catch2 EXCLUDE_FROM_ALL)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ESD_BUILD_APP)
    add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)
    add_subdirectory(src/app)
endif()
