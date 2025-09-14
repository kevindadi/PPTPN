cmake_minimum_required(VERSION 3.10)
project(PTPN)

set(CMAKE_CXX_STANDARD 17)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# 设置架构为当前系统架构
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(CMAKE_SYSTEM_PROCESSOR "arm64")
endif()

set(BOOST_ROOT "/opt/homebrew/opt/boost")
set(BOOST_DIR "/opt/homebrew/opt/boost")
set(Boost_NO_SYSTEM_PATHS ON)
set(BOOST_INCLUDEDIR "/opt/homebrew/opt/boost/include")
set(BOOST_LIBRARYDIR "/opt/homebrew/opt/boost/lib")

find_package(Boost REQUIRED COMPONENTS graph program_options filesystem log log_setup thread date_time)
include_directories(${Boost_INCLUDE_DIRS})
message(STATUS "Boost include dir: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost library dir: ${Boost_LIBRARY_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")

include_directories(include)

# 添加编译选项
#add_compile_definitions(FMT_SHARED)

add_executable(PTPN main.cpp include/clap.h include/dag.h src/clap.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/priority_state_class.h
        include/priority_state_graph.h
        src/priority_state_graph.cpp
        include/task_analysis_manager.h
        src/task_analysis_manager.cpp
        include/task_analysis_types.h
        include/deadlock_detector.h
        src/deadlock_detector.cpp
        include/schedulability_analyzer.h
        src/schedulability_analyzer.cpp
        include/semantic_wcrt_calculator.h
        src/semantic_wcrt_calculator.cpp
        include/wcrt_calculator.h
        src/static_wcrt_calculator.cpp
        src/dynamic_wcrt_calculator.cpp)
target_link_libraries(PTPN PRIVATE
    Boost::graph 
    Boost::program_options
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time)

add_executable(test_petri_import test/test_petri_import.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/clap.h
        src/clap.cpp
)
target_link_libraries(test_petri_import PRIVATE
    Boost::graph 
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time)

add_executable(test_state_class test/test_state_class.cpp
        include/priority_time_petri_net.h
        src/priority_time_petri_net.cpp
        include/priority_state_class.h
        include/priority_state_graph.h
        src/priority_state_graph.cpp
        include/clap.h
        src/clap.cpp
)
target_link_libraries(test_state_class PRIVATE
    Boost::graph 
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time)

enable_testing()

add_test(NAME TestPetriImport COMMAND test_petri_import)
add_test(NAME TestStateClass COMMAND test_state_class)
