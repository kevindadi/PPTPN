cmake_minimum_required(VERSION 3.10)
project(PTPN)

set(CMAKE_CXX_STANDARD 17)
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
    set(CMAKE_SYSTEM_PROCESSOR "arm64")
endif()

# FetchContent for nlohmann/json (header-only library)
include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

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

add_executable(matrix_ptpn_example test/matrix_ptpn_example.cpp
        include/matrix_ptpn.h
)
target_link_libraries(matrix_ptpn_example PRIVATE
    Boost::graph 
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time)

add_executable(test_matrix_conversion test/test_matrix_conversion.cpp
        include/matrix_ptpn.h
        src/matrix_ptpn.cpp
        include/graph_ptpn.h
        src/graph_ptpn.cpp
        include/clap.h
        src/clap.cpp
        include/dag.h
)
target_link_libraries(test_matrix_conversion PRIVATE
    Boost::graph 
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time)

add_executable(test_state_class_example test/test_state_class_example.cpp
        include/state_class.h
        src/state_class.cpp
        include/state_class_graph.h
        src/state_class_graph.cpp
        include/matrix_ptpn.h
        src/matrix_ptpn.cpp
        include/clap.h
        src/clap.cpp
        include/dag.h
)
target_link_libraries(test_state_class_example PRIVATE
    Boost::graph 
    Boost::filesystem
    Boost::log
    Boost::log_setup
    Boost::thread
    Boost::date_time)


add_test(NAME TestMatrixPTPNExample COMMAND matrix_ptpn_example)
add_test(NAME TestMatrixConversion COMMAND test_matrix_conversion)
add_test(NAME TestStateClassExample COMMAND test_state_class_example)

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