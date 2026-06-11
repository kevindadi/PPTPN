include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(scheduling_test
    test_dbm.cpp
    test_scheduler.cpp
    test_state.cpp
    test_reachability.cpp
)

target_link_libraries(scheduling_test
    PRIVATE
    scheduling_analyzer
    gtest_main
)

target_include_directories(scheduling_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/analysis/scheduling
)
