cmake_minimum_required(VERSION 3.23.0)
project(firehose_client VERSION 1.3.1 LANGUAGES C CXX)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(UNIT_TESTS_ENABLED ON)

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
        if(${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
                add_compile_definitions(
                        _FULL_LOGGING
                        NDEBUG
                )
        endif()
elseif(UNIX)
	# add_compile_definitions(std=gnu++23)
endif()

include(CMakePrintHelpers)

configure_file(./cmake/firehost_client_config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/firehost_client_config.hpp)
SET(MAIN_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_TAG v1.15.2
  GIT_REPOSITORY https://github.com/google/googletest
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  aho_corasick
  GIT_HASH 93eb16ba2571f9e2bd05ae17681c62121cc436d1
  GIT_REPOSITORY https://github.com/SteveTownsend/aho_corasick
)
FetchContent_MakeAvailable(aho_corasick)
include_directories(${aho_corasick_SOURCE_DIR}/src)

if (UNIX)
  find_package(prometheus-cpp CONFIG REQUIRED)
  if(NOT DEFINED prometheus-cpp_VERSION)
    message(FATAL_ERROR "prometheus-cpp_VERSION is not defined")
  endif()
elseif(WIN32)
  # inline the package to avoid memory model mismatch
  # https://github.com/jupp0r/prometheus-cpp/issues/621
  FetchContent_Declare(
    prometheus-cpp
    GIT_HASH cfc8ebee3e6dae78c578c88dfb60f2719c82d873
    GIT_REPOSITORY https://github.com/SteveTownsend/prometheus-cpp
  )
  SET(ENABLE_PUSH OFF)
  SET(ENABLE_COMPRESSION OFF)
  SET(ENABLE_TESTING OFF)
  FetchContent_MakeAvailable(prometheus-cpp)
  include_directories(${prometheus-cpp_SOURCE_DIR})
endif()
cmake_print_variables(prometheus-cpp_FOUND prometheus-cpp_SOURCE_DIR)

# Unicode support
find_package(ICU REQUIRED COMPONENTS uc)
cmake_print_variables(ICU_FOUND ICU_LIBRARIES)
include_directories(${ICU_INCLUDE_DIR})

# For Boost.Beast (REST, Websocket)
SET(Boost_USE_STATIC_LIBS YES)
SET(Boost_USE_MULTITHREAD ON)
SET(Boost_NO_BOOST_CMAKE TRUE)
SET(BOOST_ROOT $ENV{BOOST_ROOT})
set(BOOST_LIBRARYDIR $ENV{BOOST_LIBRARYDIR})

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

FetchContent_Declare(
  nlohmann_json_schema_validator
  GIT_REPOSITORY https://github.com/pboettch/json-schema-validator
  GIT_TAG        2.3.0
  OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(nlohmann_json_schema_validator)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

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

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

add_executable(firehose_client
  ./source/main.cpp
  ./source/config.cpp
  ./source/content_handler.cpp
  ./source/datasource.cpp
  ./source/helpers.cpp
  ./source/log_wrapper.cpp
  ./source/matcher.cpp
  ./source/metrics.cpp
  ./source/parser.cpp)
target_include_directories(firehose_client PUBLIC ./include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(firehose_client ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES}
    nlohmann_json_schema_validator spdlog yaml-cpp::yaml-cpp prometheus-cpp::pull)
if (UNIX)
  target_link_libraries(firehose_client stdc++)
endif()

if (UNIT_TESTS_ENABLED)
  include(CTest)
  enable_testing()
  add_subdirectory(test)
ENDIF()

