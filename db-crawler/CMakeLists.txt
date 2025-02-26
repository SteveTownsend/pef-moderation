project(db_crawler VERSION 1.3.0 LANGUAGES C CXX)

configure_file(./cmake/db_crawler_config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/project_defs.hpp)

add_compile_definitions(
  _DB_CRAWLER
)

add_executable(db_crawler ./source/main.cpp)

target_include_directories(db_crawler PUBLIC ./include ../include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(db_crawler ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES}
  nlohmann_json::nlohmann_json spdlog yaml-cpp::yaml-cpp prometheus-cpp::pull pqxx jwt-cpp::jwt-cpp pef-tools::common)

if(UNIX)
  target_link_libraries(db_crawler stdc++ ${RESTC_CPP_LIBRARIES} ${ZLIB_LIBRARY})
else()
  target_link_libraries(db_crawler ${ZLIB_LIBRARY} ${REST_CPP_LIBRARY})
endif()

# TODO decide if I care about this
# include(CTest)
# enable_testing()
# add_subdirectory(test)
