include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(ptpn_test
  test_json_parser.cpp
  test_validation.cpp
  test_tdg_export.cpp
)

target_link_libraries(ptpn_test
  PRIVATE
  PTPN_core
  gtest_main
)

include(GoogleTest)
gtest_discover_tests(ptpn_test)