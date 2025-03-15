project(firehose_client VERSION 2.6.15 LANGUAGES C CXX)

configure_file(./cmake/firehost_client_config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/project_defs.hpp)

add_compile_definitions(
  _FIREHOSE_CLIENT
)

add_executable(firehose_client
  ./source/main.cpp
  ./source/content_handler.cpp
  ./source/matcher.cpp
  ./source/parser.cpp
  ./source/payload.cpp
  ./source/moderation/action_router.cpp
  ./source/moderation/auxiliary_data.cpp
  ./source/moderation/embed_checker.cpp
  ./source/moderation/list_manager.cpp)

target_include_directories(firehose_client PUBLIC ./include ../include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(firehose_client pef-tools::common ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES}
  nlohmann_json::nlohmann_json spdlog yaml-cpp::yaml-cpp prometheus-cpp::pull pqxx jwt-cpp::jwt-cpp multiformats)

if(UNIX)
  target_link_libraries(firehose_client stdc++ ${RESTC_CPP_LIBRARIES} ${ZLIB_LIBRARY} neo4j-client)
else()
  target_link_libraries(firehose_client ${ZLIB_LIBRARY} ${REST_CPP_LIBRARY})
endif()

# TODO decide if I care about this
# include(CTest)
# enable_testing()
# add_subdirectory(test)
