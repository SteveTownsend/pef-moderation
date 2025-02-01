project(firehose_client VERSION 2.5.7 LANGUAGES C CXX)

configure_file(./cmake/firehost_client_config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/firehost_client_config.hpp)

add_compile_definitions(
  _FIREHOSE_CLIENT
)

add_executable(firehose_client
  ./source/main.cpp
  ./source/content_handler.cpp
  ./source/helpers.cpp
  ./source/matcher.cpp
  ./source/metrics.cpp
  ./source/parser.cpp
  ./source/payload.cpp
  ./source/activity/account_events.cpp
  ./source/activity/event_cache.cpp
  ./source/activity/event_recorder.cpp
  ./source/moderation/action_router.cpp
  ./source/moderation/auxiliary_data.cpp
  ./source/moderation/embed_checker.cpp
  ./source/moderation/list_manager.cpp
  ../source/common/config.cpp
  ../source/common/log_wrapper.cpp
  ../source/common/metrics_factory.cpp
  ../source/common/rest_utils.cpp
  ../source/common/moderation/ozone_adapter.cpp
  ../source/common/moderation/report_agent.cpp
  ../source/common/moderation/session_manager.cpp)

target_include_directories(firehose_client PUBLIC ./include ../include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(firehose_client ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES}
nlohmann_json::nlohmann_json spdlog yaml-cpp::yaml-cpp prometheus-cpp::pull pqxx jwt-cpp::jwt-cpp)
if (UNIX)
	target_link_libraries(firehose_client stdc++ ${RESTC_CPP_LIBRARIES} ${ZLIB_LIBRARY})
else()
  target_link_libraries(firehose_client ${ZLIB_LIBRARY} ${REST_CPP_LIBRARY})
endif()

# TODO decide if I care about this
# include(CTest)
# enable_testing()
# add_subdirectory(test)
