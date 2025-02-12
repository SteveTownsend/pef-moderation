project(pef-tools VERSION 1.0.0)

add_library(common STATIC
  ./config.cpp
  ./helpers.cpp
  ./log_wrapper.cpp
  ./bluesky/client.cpp
  ./metrics_factory.cpp
  ./rest_utils.cpp
  ./activity/account_events.cpp
  ./activity/event_cache.cpp
  ./activity/event_recorder.cpp
  ./activity/neo4j_adapter.cpp
  ./moderation/ozone_adapter.cpp
  ./moderation/report_agent.cpp
  ./moderation/session_manager.cpp)
add_library(pef-tools::common ALIAS common)

target_include_directories(common PUBLIC ../../include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(common ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES}
  nlohmann_json::nlohmann_json spdlog yaml-cpp::yaml-cpp prometheus-cpp::pull pqxx jwt-cpp::jwt-cpp)

if(UNIX)
  target_link_libraries(common stdc++ ${RESTC_CPP_LIBRARIES} ${ZLIB_LIBRARY})
else()
  target_link_libraries(common ${ZLIB_LIBRARY} ${REST_CPP_LIBRARY})
endif()

# TODO decide if I care about this
# include(CTest)
# enable_testing()
# add_subdirectory(test)
