cmake_minimum_required(VERSION 3.25)
project(PPTPN)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS graph log)
if(BOOST_FOUND)
    include_directories(${BOOST_INCLUDE_DIR})
endif()

find_package(nlohmann_json CONFIG REQUIRED)

add_executable(PPTPN main.cpp petrinet.cpp petrinet.h config.cpp config.h timepetrinet.cpp timepetrinet.h probpetrinet.cpp probpetrinet.h StateClass.h StateClass.h StateClass.cpp)
target_link_libraries(PPTPN Boost::graph Boost::log)
