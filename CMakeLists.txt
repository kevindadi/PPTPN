cmake_minimum_required(VERSION 3.25)
project(PPTPN)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS graph log program_options)
if (BOOST_FOUND)
    include_directories(${BOOST_INCLUDE_DIR})
endif ()

find_package(nlohmann_json CONFIG REQUIRED)
link_directories("C:/Users/Kevin/CLionProjects/PPTPN/libs")
add_executable(PTPN main.cpp petrinet.cpp petrinet.h config.cpp config.h timepetrinet.cpp timepetrinet.h
        StateClass.h StateClass.cpp)
add_executable(PPTPN main.cpp petrinet.cpp petrinet.h probpetrinet.cpp probpetrinet.h GConfig.cpp
        GConfig.h StateClass.h StateClass.cpp)
target_link_libraries(PTPN Boost::graph Boost::log Boost::program_options)
target_link_libraries(PPTPN Boost::graph Boost::log gvc cgraph Boost::program_options)
#add_test(test/test_sub_task.cpp Boost::graph)
