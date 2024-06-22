cmake_minimum_required(VERSION 3.25)
project(PPTPN)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_TOOLCHAIN_FILE "/home/kevin/vcpkg/scripts/buildsystems/vcpkg.cmake")
find_package(Boost REQUIRED COMPONENTS graph log program_options)
if (BOOST_FOUND)
    message("Found boost")
    include_directories(${BOOST_INCLUDE_DIR})
endif ()

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(FetchContent)
include_directories(include)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)
add_executable(PTPN main.cpp include/clap.h include/dag.h src/clap.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/state_class_graph.h
        src/state_class_graph.cpp
        include/owner_assert.h
        include/owner_error.h)
target_link_libraries(PTPN Boost::graph Boost::log Boost::program_options)

#enable_testing()
#add_executable(parse_time_vec ${PROJECT_SOURCE_DIR}/test/test_parse_time_vec.cpp include/clap.h include/dag.h src/clap.cpp
#        include/owner_error.h)
#target_link_libraries(parse_time_vec GTest::gtest_main Boost::graph Boost::log)
#
#include(GoogleTest)
#gtest_discover_tests(parse_time_vec)