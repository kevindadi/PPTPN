cmake_minimum_required(VERSION 3.25)
project(PPTPN)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_TOOLCHAIN_FILE "/home/kevin/vcpkg/scripts/buildsystems/vcpkg.cmake")

# 设置CMP0167策略为NEW
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# 设置 fmt 库的查找路径，优先使用 homebrew 的版本
set(CMAKE_PREFIX_PATH "/opt/homebrew" ${CMAKE_PREFIX_PATH})
set(fmt_DIR "/opt/homebrew/lib/cmake/fmt")

# 查找所需的库
find_package(fmt REQUIRED)
find_package(Boost REQUIRED COMPONENTS graph program_options filesystem system)
find_package(Eigen3 REQUIRED)
find_package(spdlog REQUIRED)

# 包含头文件目录
include_directories(${BOOST_INCLUDE_DIR})
include_directories(${EIGEN3_INCLUDE_DIR})

include_directories(include)

# 添加编译选项
add_compile_definitions(FMT_SHARED)

add_executable(PTPN main.cpp include/clap.h include/dag.h src/clap.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/priority_state_class.h
        src/priority_state_class.cpp
        include/differential_state_class.h
        src/differential_state_class.cpp
        include/owner_assert.h
        include/owner_error.h)
target_link_libraries(PTPN PRIVATE 
    fmt::fmt
    Boost::graph 
    Boost::program_options
    Boost::filesystem
    Boost::system
    Eigen3::Eigen 
    spdlog::spdlog)
