include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(ptpn_test
  test_json_parser.cpp
  test_validation.cpp
  test_tdg_export.cpp
  test_example_artifacts.cpp
  test_ptpn_reachability.cpp
)

target_link_libraries(ptpn_test
  PRIVATE
  PTPN_core
  gtest_main
)

target_include_directories(ptpn_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ptpn_test PRIVATE PTPN_ENABLE_TEST_ACCESS)

include(GoogleTest)
gtest_discover_tests(ptpn_test)