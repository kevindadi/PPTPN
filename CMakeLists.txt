cmake_minimum_required(VERSION 3.25)
project(PPTPN)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_TOOLCHAIN_FILE "/home/kevin/vcpkg/scripts/buildsystems/vcpkg.cmake")
find_package(Boost REQUIRED COMPONENTS graph log program_options)
find_package(Eigen3 REQUIRED)
if (BOOST_FOUND)
    message("Found boost")
    include_directories(${BOOST_INCLUDE_DIR})
endif ()

include_directories(include)
add_executable(PTPN main.cpp include/clap.h include/dag.h src/clap.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/priority_state_class.h
        src/priority_state_class.cpp
        include/differential_state_class.h
        src/differential_state_class.cpp
        include/owner_assert.h
        include/owner_error.h)
target_link_libraries(PTPN Boost::graph Boost::log Boost::program_options Eigen3::Eigen)
