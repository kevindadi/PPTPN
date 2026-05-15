cmake_minimum_required(VERSION 3.16)
project(PTPN)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
cmake_policy(SET CMP0167 OLD)

# Use vcpkg for dependencies
if(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT $ENV{VCPKG_ROOT})
elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
    set(VCPKG_ROOT $ENV{VCPKG_INSTALLATION_ROOT})
elseif(EXISTS "/Users/kevin/vcpkg")
    set(VCPKG_ROOT "/Users/kevin/vcpkg")
endif()

if(VCPKG_ROOT AND EXISTS "${VCPKG_ROOT}/vcpkg")
    message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain")
endif()

# FetchContent for dependencies
include(FetchContent)

# nlohmann/json
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(json)

# CLI11
FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.3.2
)
FetchContent_MakeAvailable(cli11)

find_package(Boost REQUIRED COMPONENTS graph filesystem thread date_time)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
message(STATUS "Boost include dir: ${Boost_INCLUDE_DIRS}")

include_directories(include)

# Core library containing all logic
add_library(PTPN_core STATIC
    src/clap.cpp
    src/matrix_ptpn.cpp
    src/graph_ptpn.cpp
    src/state_class.cpp
    src/state_class_graph.cpp
    src/json_tdg.cpp
)
target_include_directories(PTPN_core PUBLIC include)
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
add_executable(PTPN main.cpp)
target_link_libraries(PTPN PRIVATE PTPN_core CLI11::CLI11)

# Test subdirectory
add_subdirectory(test)