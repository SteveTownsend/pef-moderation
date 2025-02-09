INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})
add_executable(
  firehose_client_tests
  ./source/cid_test.cpp
  ./source/rate_observer_test.cpp
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
  pqxx
  prometheus-cpp::pull
  jwt-cpp::jwt-cpp
  ${ICU_LIBRARIES}
  multiformats
  pef-tools::common
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

include(GoogleTest)
gtest_discover_tests(firehose_client_tests)