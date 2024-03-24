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
# find_package(nlohmann_json CONFIG REQUIRED)
# link_directories("/usr/local/opt/graphviz/lib")
# add_executable(PTPN main.cpp petrinet.cpp petrinet.h config.cpp config.h timepetrinet.cpp timepetrinet.h
#         StateClass.h StateClass.cpp)
add_executable(PPTPN main.cpp petrinet.cpp petrinet.h probpetrinet.cpp probpetrinet.h GConfig.cpp
        GConfig.h config.cpp config.h timepetrinet.cpp timepetrinet.h StateClass.h StateClass.cpp
        )
add_executable(PTPN main.cpp include/clap.h include/dag.h src/clap.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/state_class_graph.h
        src/state_class_graph.cpp)
# target_link_libraries(PTPN Boost::graph Boost::log Boost::program_options)
target_link_libraries(PPTPN Boost::graph Boost::log gvc cgraph Boost::program_options nlohmann_json::nlohmann_json)
target_link_libraries(PTPN Boost::graph Boost::log Boost::program_options)
#add_test(test/test_sub_task.cpp Boost::graph)
