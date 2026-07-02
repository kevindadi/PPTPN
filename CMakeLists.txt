cmake_minimum_required(VERSION 3.27)

# vcpkg toolchain must be set before project()
if(CMAKE_TOOLCHAIN_FILE MATCHES "[/\\\\]vcpkg\\.cmake$")
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
else()
    foreach(_ptpn_vcpkg_root
        "$ENV{VCPKG_ROOT}"
        "$ENV{VCPKG_INSTALLATION_ROOT}"
        "$ENV{HOME}/vcpkg"
    )
        if(_ptpn_vcpkg_root AND EXISTS "${_ptpn_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "${_ptpn_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "Vcpkg toolchain file")
            message(STATUS "Using vcpkg from: ${_ptpn_vcpkg_root}")
            break()
        endif()
    endforeach()
    unset(_ptpn_vcpkg_root)
endif()

# Pick a host-matching triplet when vcpkg is active and the caller did not set one.
if(CMAKE_TOOLCHAIN_FILE MATCHES "[/\\\\]vcpkg\\.cmake$" AND NOT VCPKG_TARGET_TRIPLET)
    if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT "$ENV{VCPKG_DEFAULT_TRIPLET}" STREQUAL "")
        set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "vcpkg target triplet")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(VCPKG_TARGET_TRIPLET "arm64-osx" CACHE STRING "vcpkg target triplet")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-osx" CACHE STRING "vcpkg target triplet")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(VCPKG_TARGET_TRIPLET "arm64-linux" CACHE STRING "vcpkg target triplet")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "vcpkg target triplet")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(VCPKG_TARGET_TRIPLET "arm64-windows" CACHE STRING "vcpkg target triplet")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg target triplet")
        endif()
    endif()
    if(VCPKG_TARGET_TRIPLET)
        message(STATUS "vcpkg target triplet: ${VCPKG_TARGET_TRIPLET}")
    endif()
endif()

project(PTPN LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Suppress warnings from dependencies with older cmake_minimum_required
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

set(PTPN_BOOST_COMPONENTS graph filesystem thread date_time)

# CONFIG mode (vcpkg / BoostConfig.cmake); fall back to FindBoost on system installs.
find_package(Boost CONFIG QUIET COMPONENTS ${PTPN_BOOST_COMPONENTS})
if(Boost_FOUND)
    message(STATUS "Boost ${Boost_VERSION} (CONFIG)")
else()
    find_package(Boost REQUIRED COMPONENTS ${PTPN_BOOST_COMPONENTS})
    message(STATUS "Boost ${Boost_VERSION} (system paths)")
endif()

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
    src/tdg2ptopner/validate.cpp
    src/tdg2ptopner/export_ppn.cpp
    src/tdg2ptopner/ptpn_to_ppn.cpp
    src/tdg2ptopner/tdg2ptopner.cpp
    src/analysis/dbm.cpp
    src/analysis/state_class.cpp
    src/analysis/ptpn_analysis.cpp
    src/analysis/canonicalization.cpp
    src/analysis/scheduling.cpp
    src/analysis/metrics.cpp
    src/parser/ptpn_parser.cpp
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
