cmake_minimum_required(VERSION 3.23.0)
project(firehose_client VERSION 2.3.0 LANGUAGES C CXX)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# #######################################################################################################################
# # Configuration for all targets
# #######################################################################################################################
if(WIN32)
        add_compile_definitions(
                UNICODE
                _UNICODE
                NOMINMAX
                _AMD64_
                WIN32_LEAN_AND_MEAN)
endif()
if(${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
  add_compile_definitions(
          _FULL_LOGGING
          NDEBUG
  )
elseif(${CMAKE_BUILD_TYPE} STREQUAL Release)
  add_compile_definitions(
          NDEBUG
  )
elseif(${CMAKE_BUILD_TYPE} STREQUAL DEBUG)
  add_compile_definitions(
          _DEBUG
  )
endif()

include(CMakePrintHelpers)

configure_file(./cmake/firehost_client_config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/firehost_client_config.hpp)
SET(MAIN_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

find_package(gtest)

include(FetchContent)
# activity caching (header-only)
set(BUILD_TEST OFF)
FetchContent_Declare(
  caching
  GIT_TAG 3fc9e07d49ac46272a583de0555a9cf376cdf316
  GIT_REPOSITORY https://github.com/vpetrigo/caches
)
FetchContent_MakeAvailable(caching)
include_directories(${caching_SOURCE_DIR}/include)

# Unicode support
find_package(ICU REQUIRED COMPONENTS uc)
include_directories(${ICU_INCLUDE_DIR})
if (WIN32)
  SET(ICU_UC_LIBRARY "$ENV{ICU_ROOT}/lib64/icuuc.lib")
  SET(ICU_LIBRARIES "${ICU_UC_LIBRARY}")
endif()

FetchContent_Declare(
  aho_corasick
  GIT_HASH 3d2fb8ca0f8a024d9fec69dbaf3860f08cdd09a3
  GIT_REPOSITORY https://github.com/SteveTownsend/aho_corasick
)
FetchContent_MakeAvailable(aho_corasick)
include_directories(${aho_corasick_SOURCE_DIR}/src)

# inline the package to avoid memory model mismatch
# https://github.com/jupp0r/prometheus-cpp/issues/621
FetchContent_Declare(
  prometheus-cpp
  GIT_HASH adb553e865c917fa1c00323209b2de2dfcb94270
  GIT_REPOSITORY https://github.com/SteveTownsend/prometheus-cpp
)
SET(ENABLE_PUSH OFF)
SET(ENABLE_COMPRESSION OFF)
SET(ENABLE_TESTING OFF)
FetchContent_MakeAvailable(prometheus-cpp)
include_directories(${prometheus-cpp_SOURCE_DIR})

# For Boost.Beast (Websocket)
SET(Boost_USE_STATIC_LIBS YES)
SET(Boost_USE_MULTITHREAD ON)
SET(Boost_NO_BOOST_CMAKE TRUE)
# SET(Boost_ROOT CACHE STRING $ENV{BOOST_ROOT})
if (WIN32)
  SET(Boost_DIR "$ENV{BOOST_ROOT}/stage/lib/cmake/Boost-1.86.0")
endif()
# set(BOOST_LIBRARYDIR CACHE STRING "$ENV{BOOST_LIBRARYDIR}")

find_package(Boost REQUIRED COMPONENTS system context)
IF(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
  ADD_DEFINITIONS( "-DHAS_BOOST" )
ENDIF()

# For OpenSSL
set(OPENSSL_INCLUDE_DIR $ENV{OPENSSL_INCLUDE_DIR})
set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
set(OPENSSL_VERSION $ENV{OPENSSL_VERSION})

find_package(OpenSSL REQUIRED)
IF(OpenSSL_FOUND)
include_directories(${OPENSSL_INCLUDE_DIR})
  LINK_DIRECTORIES(${OPENSSL_LIBRARIES})
ENDIF()

# JSON
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/stevetownsend/json
  GIT_TAG 13b6b8878d161ee1efe56237c62afa45abb54772)
FetchContent_MakeAvailable(json)

# logging
set(spdlog_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
FetchContent_Declare(
 spdlog
 GIT_REPOSITORY https://github.com/gabime/spdlog
 GIT_TAG        v1.15.0
 OVERRIDE_FIND_PACKAGE
)
FetchContent_GetProperties(spdlog)
if (NOT spdlog_POPULATED)
        FetchContent_Populate(spdlog)
        set(SPDLOG_INSTALL ON CACHE INTERNAL "Install SPDLOG for firehose client")
        set(SPDLOG_USE_STD_FORMAT ON CACHE INTERNAL "Use std::format in SPDLOG, not fmt")
        add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif()
find_package(spdlog CONFIG REQUIRED)

# YAML reader
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
if (UNIX)
  SET(YAML_BUILD_SHARED_LIBS ON)
endif()
FetchContent_MakeAvailable(yaml-cpp)
include_directories(${yaml-cpp_SOURCE_DIR}/include)

# postgres
SET(BUILD_TEST OFF)
FetchContent_Declare(
  libpqxx
  GIT_REPOSITORY https://github.com/jtv/libpqxx
  GIT_TAG 7.10.0
)
FetchContent_MakeAvailable(libpqxx)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

# rest-cpp/zlib
if (UNIX)
  find_package(rest-cpp)
  find_package(zlib)
else()
  include_directories("$ENV{rest-cpp_DIR}/../include" "$ENV{rest-cpp_DIR}/generated-include" "$ENV{rest-cpp_DIR}/external-projects/src/externalRapidJson/include")
  if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
    SET(REST_CPP_LIBRARY $ENV{rest-cpp_DIR}/lib/Debug/restc-cppD.lib)
  else()
    SET(REST_CPP_LIBRARY $ENV{rest-cpp_DIR}/lib/Release/restc-cpp.lib)
  endif()
  SET(ZLIB_LIBRARY $ENV{ZLIB_ROOT}/lib/zlibstatic)
endif()

# JSON Web Token handling
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "disable building examples" FORCE)
set(JWT_BUILD_TESTS OFF CACHE BOOL "disable building tests" FORCE)
set(JWT_DISABLE_PICOJSON ON CACHE BOOL "only use Boost" FORCE)
set(JWT_JSON_TRAITS_OPTIONS boost-json)
FetchContent_Declare(jwt-cpp 
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_HASH 71c3d36507183ceb74b9cf10d1232fe1223bdfb0
)
FetchContent_MakeAvailable(jwt-cpp)

add_executable(firehose_client
  ./source/main.cpp
  ./source/config.cpp
  ./source/helpers.cpp
  ./source/log_wrapper.cpp
  ./source/matcher.cpp
  ./source/metrics.cpp
  ./source/parser.cpp
  ./source/post_processor.cpp
  ./source/activity/account_events.cpp
  ./source/activity/event_cache.cpp
  ./source/activity/event_recorder.cpp
  ./source/moderation/action_router.cpp
  ./source/moderation/list_manager.cpp
  ./source/moderation/ozone_adapter.cpp
  ./source/moderation/session_manager.cpp)

target_include_directories(firehose_client PUBLIC ./include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(firehose_client ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES}
nlohmann_json::nlohmann_json spdlog yaml-cpp::yaml-cpp prometheus-cpp::pull pqxx jwt-cpp::jwt-cpp)
if (UNIX)
  target_link_libraries(firehose_client stdc++ rest-cpp zlib)
else()
  target_link_libraries(firehose_client ${ZLIB_LIBRARY}  ${REST_CPP_LIBRARY})
endif()

# TODO decide if I care about this
# include(CTest)
# enable_testing()
# add_subdirectory(test)

