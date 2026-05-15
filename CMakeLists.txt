cmake_minimum_required(VERSION 3.10)
project(PTPN)

set(CMAKE_CXX_STANDARD 17)

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

# FetchContent for nlohmann/json (header-only library)
include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

find_package(Boost REQUIRED COMPONENTS graph program_options filesystem log log_setup thread date_time)
include_directories(${Boost_INCLUDE_DIRS})
message(STATUS "Boost include dir: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost library dir: ${Boost_LIBRARY_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")

include_directories(include)

add_executable(PTPN main.cpp include/clap.h include/dag.h src/clap.cpp
        include/matrix_ptpn.h
        src/matrix_ptpn.cpp
        include/graph_ptpn.h
        src/graph_ptpn.cpp
        include/state_class.h
        src/state_class.cpp
        include/state_class_graph.h
        src/state_class_graph.cpp
        include/json_tdg.h
        src/json_tdg.cpp)

target_link_libraries(PTPN PRIVATE
    Boost::graph
    Boost::program_options
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time
    nlohmann_json::nlohmann_json)

enable_testing()

# JSON Input Test
add_executable(test_json_input test/test_json_input.cpp
        include/json_tdg.h
        src/json_tdg.cpp
        include/dag.h
        include/clap.h
        src/clap.cpp
        include/matrix_ptpn.h
        src/matrix_ptpn.cpp
        include/graph_ptpn.h
        src/graph_ptpn.cpp
        include/state_class.h
        src/state_class.cpp
        include/state_class_graph.h
        src/state_class_graph.cpp)
target_link_libraries(test_json_input PRIVATE
    Boost::graph
    Boost::program_options
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time
    nlohmann_json::nlohmann_json)
add_test(NAME TestJsonInput COMMAND test_json_input)

