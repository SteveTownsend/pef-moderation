cmake_minimum_required(VERSION 3.23.0)
project(moderation-tools)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# executable build options
option(DB_CRAWLER_BUILD "build db crawler" ON)
option(FIREHOSE_CLIENT_BUILD "build firehose client" ON)
option(LABELER_UPDATE_BUILD "build labeler update agent" ON)

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
  if (MSVC)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("/std:c++20" COMPILER_SUPPORTS_CXX20)
    check_cxx_compiler_flag("/std:c++17" COMPILER_SUPPORTS_CXX17)
    add_compile_options(/Zc:__cplusplus)
  endif()
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

SET(MAIN_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

find_package(GTest CONFIG REQUIRED)

include(FetchContent)
# activity caching (header-only)
set(BUILD_TEST OFF)
FetchContent_Declare(
  caching
  GIT_TAG master
  GIT_REPOSITORY https://github.com/SteveTownsend/caches
)
FetchContent_MakeAvailable(caching)
include_directories(${caching_SOURCE_DIR}/include)

# lock-free queues
FetchContent_Declare(
  reader_writer_queue
  GIT_TAG 16b48ae1148284e7b40abf72167206a4390a4592
  GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue
)
FetchContent_MakeAvailable(reader_writer_queue)
include_directories(${reader_writer_queue_SOURCE_DIR})

FetchContent_Declare(
  concurrent_queue
  GIT_TAG 6dd38b8a1dbaa7863aa907045f32308a56a6ff5d
  GIT_REPOSITORY https://github.com/cameron314/concurrentqueue
)
FetchContent_MakeAvailable(concurrent_queue)
include_directories(${concurrent_queue_SOURCE_DIR})

# Unicode support
find_package(ICU REQUIRED COMPONENTS uc)
include_directories(${ICU_INCLUDE_DIR})
if (WIN32)
  SET(ICU_UC_LIBRARY "$ENV{ICU_ROOT}/lib64/icuuc.lib")
  SET(ICU_LIBRARIES "${ICU_UC_LIBRARY}")
endif()

FetchContent_Declare(
  aho_corasick
  GIT_TAG master
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

# includes those used by restc-cpp
find_package(Boost REQUIRED COMPONENTS
  system
  program_options
  filesystem
  date_time
  context
  coroutine
  chrono
  log
  json
  url)
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

# set(CPACK_PROJECT_NAME ${PROJECT_NAME})
# set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
# include(CPack)

# restc-cpp/zlib
if (UNIX)
  find_package(restc-cpp)
  find_package(zlib)
else()
  include_directories("$ENV{rest-cpp_DIR}/../include" "$ENV{rest-cpp_DIR}/generated-include" "$ENV{rest-cpp_DIR}/external-projects/src/externalRapidJson/include")
  if (${CMAKE_BUILD_TYPE} STREQUAL Debug)
    SET(REST_CPP_LIBRARY $ENV{rest-cpp_DIR}/lib/Debug/restc-cppD.lib)
  elseif(${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
    SET(REST_CPP_LIBRARY $ENV{rest-cpp_DIR}/lib/RelWithDebInfo/restc-cpp.lib)
  elseif(${CMAKE_BUILD_TYPE} STREQUAL Release)
    SET(REST_CPP_LIBRARY $ENV{rest-cpp_DIR}/lib/Release/restc-cpp.lib)
  endif()
  SET(ZLIB_LIBRARY $ENV{ZLIB_ROOT}/lib/zlibstatic.lib)
endif()

# JSON Web Token handling
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "disable building examples" FORCE)
set(JWT_BUILD_TESTS OFF CACHE BOOL "disable building tests" FORCE)
if (WIN32)
  set(JWT_DISABLE_PICOJSON ON CACHE BOOL "only use Boost" FORCE)
endif()
set(JWT_JSON_TRAITS_OPTIONS boost-json)
FetchContent_Declare(jwt-cpp 
  GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
  GIT_HASH 71c3d36507183ceb74b9cf10d1232fe1223bdfb0
)
FetchContent_MakeAvailable(jwt-cpp)

if (DB_CRAWLER_BUILD)
  add_subdirectory(db-crawler)
endif()
if (FIREHOSE_CLIENT_BUILD)
add_subdirectory(firehose-client)
endif()
if (LABELER_UPDATE_BUILD)
add_subdirectory(labeler-update)
endif()
