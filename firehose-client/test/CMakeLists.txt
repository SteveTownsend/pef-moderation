INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})
add_executable(
  firehose_client_tests
  ${PROJECT_SOURCE_DIR}/source/helpers.cpp
  ${PROJECT_SOURCE_DIR}/source/log_wrapper.cpp
  ${PROJECT_SOURCE_DIR}/source/matcher.cpp
  ${PROJECT_SOURCE_DIR}/source/matcher.cpp
  ${PROJECT_SOURCE_DIR}/source/parser.cpp
  ./source/matcher_test.cpp
  ./source/parser_test.cpp
)
# No logging in tests
target_compile_definitions(firehose_client_tests PUBLIC DISABLE_LOGGING)
target_include_directories(firehose_client_tests PUBLIC ${MAIN_BINARY_DIR} ${PROJECT_SOURCE_DIR}/include ./include)
target_link_libraries(
  firehose_client_tests
  nlohmann_json::nlohmann_json
  GTest::gtest_main
  GTest::gmock_main
  spdlog
  ${ICU_LIBRARIES}
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

include(GoogleTest)
gtest_discover_tests(firehose_client_tests)