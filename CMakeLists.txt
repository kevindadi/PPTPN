cmake_minimum_required(VERSION 3.27)

# vcpkg toolchain must be set before project()
if(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT $ENV{VCPKG_ROOT})
elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
    set(VCPKG_ROOT $ENV{VCPKG_INSTALLATION_ROOT})
elseif(EXISTS "$ENV{HOME}/vcpkg")
    set(VCPKG_ROOT "$ENV{HOME}/vcpkg")
elseif(EXISTS "/Users/kevin/vcpkg")
    set(VCPKG_ROOT "/Users/kevin/vcpkg")
endif()

if(VCPKG_ROOT AND EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(PTPN)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Suppress warnings from dependencies with older cmake_minimum_required
cmake_policy(SET CMP0167 NEW)
cmake_policy(SET CMP0057 NEW)

# FetchContent for dependencies
include(FetchContent)
include(CTest)

# nlohmann/json
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(json)

# CLI11
FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.6.2
)
FetchContent_MakeAvailable(cli11)

# spdlog (FetchContent avoids vcpkg pkg-config dependency on Linux)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.3
)
FetchContent_MakeAvailable(spdlog)

# fmt (spdlog dependency, also used directly)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.2.0
)
FetchContent_MakeAvailable(fmt)

find_package(Boost REQUIRED COMPONENTS graph filesystem thread date_time)
include_directories(${Boost_INCLUDE_DIRS})
message(STATUS "Boost include dir: ${Boost_INCLUDE_DIRS}")

include_directories(src)

# Core library containing all logic
add_library(PTPN_core STATIC
    src/types/types.cpp
    src/json/json.cpp
    src/tdg/tdg.cpp
    src/petri/petri.cpp
    src/petri/export_ptpn.cpp
    src/petri/export_dot.cpp
    src/petri/export_romeo.cpp
    src/tdg2pn/tdg2pn.cpp
    src/analysis/dbm.cpp
    src/analysis/state.cpp
    src/analysis/graph.cpp
    src/analysis/canonicalization.cpp
    src/analysis/scheduling.cpp
)
target_include_directories(PTPN_core PUBLIC src)
target_link_libraries(PTPN_core PUBLIC
    Boost::graph
    Boost::filesystem
    Boost::thread
    Boost::date_time
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    fmt::fmt
)

# Main executable
add_executable(ptpn src/main.cpp)
target_include_directories(ptpn PUBLIC src)
target_link_libraries(ptpn PRIVATE PTPN_core CLI11::CLI11)

# Test subdirectory
add_subdirectory(test)

# Expose compile_commands.json at repo root for IDE/clangd
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    set(_compile_commands_link "${CMAKE_SOURCE_DIR}/compile_commands.json")
    if(EXISTS "${_compile_commands_link}" AND NOT IS_SYMLINK "${_compile_commands_link}")
        file(REMOVE "${_compile_commands_link}")
    endif()
    if(NOT EXISTS "${_compile_commands_link}")
        file(CREATE_LINK
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${_compile_commands_link}"
            SYMBOLIC
        )
    endif()
endif()
