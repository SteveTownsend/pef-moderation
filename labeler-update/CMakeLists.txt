project(labeler_update VERSION 1.1.0 LANGUAGES C CXX)

configure_file(./cmake/labeler_update_config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/project_defs.hpp)

add_compile_definitions(
  _LABELER_UPDATE
)

add_executable(labeler_update ./source/main.cpp)

target_include_directories(labeler_update PUBLIC ./include ../include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(labeler_update ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES}
nlohmann_json::nlohmann_json spdlog yaml-cpp::yaml-cpp prometheus-cpp::pull pqxx jwt-cpp::jwt-cpp pef-tools::common)
if (UNIX)
	target_link_libraries(labeler_update stdc++ ${RESTC_CPP_LIBRARIES} ${ZLIB_LIBRARY})
else()
  target_link_libraries(labeler_update ${ZLIB_LIBRARY} ${REST_CPP_LIBRARY})
endif()

# TODO decide if I care about this
# include(CTest)
# enable_testing()
# add_subdirectory(test)
