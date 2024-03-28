cmake_minimum_required(VERSION 3.25)
project(PPTPN)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_TOOLCHAIN_FILE "/home/kevin/vcpkg/scripts/buildsystems/vcpkg.cmake")
find_package(Boost REQUIRED COMPONENTS graph log program_options)
if (BOOST_FOUND)
    include_directories(${BOOST_INCLUDE_DIR})
endif ()

include(FetchContent)
include_directories(include)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz)
FetchContent_MakeAvailable(json)
add_executable(PTPN main.cpp include/clap.h include/dag.h src/clap.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/state_class_graph.h
        src/state_class_graph.cpp
        include/owner_assert.h)
target_link_libraries(PTPN Boost::graph Boost::log Boost::program_options)

