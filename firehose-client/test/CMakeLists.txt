add_executable(
  filter_test
  filter_test.cpp
)
target_link_libraries(
  filter_test
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(filter_test)