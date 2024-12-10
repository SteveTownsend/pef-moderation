cmake_minimum_required(VERSION 3.23.0)
project(firehose_client VERSION 1.0.0 LANGUAGES C CXX)

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
                WIN32_LEAN_AND_MEAN
                SPDLOG_WCHAR_TO_UTF8_SUPPORT)
        if(${CMAKE_BUILD_TYPE} STREQUAL RelWithDebInfo)
                add_compile_definitions(
                        _FULL_LOGGING
                        NDEBUG
                )
        endif()
endif()

include(CMakePrintHelpers)

configure_file(./cmake/firehost_client_config.hpp.in firehost_client_config.hpp)

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
  GIT_HASH 4a38c77ec94bcf1a178260781bc5d8350f49ae4d
  GIT_REPOSITORY https://github.com/SteveTownsend/aho_corasick
)
FetchContent_MakeAvailable(aho_corasick)
include_directories(${aho_corasick_SOURCE_DIR}/src)

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

# For OpenSSL
set(OPENSSL_INCLUDE_DIR $ENV{OPENSSL_INCLUDE_DIR})
set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
set(OPENSSL_VERSION $ENV{OPENSSL_VERSION})

find_package(Boost REQUIRED COMPONENTS system context)
IF(Boost_FOUND)
include_directories(${Boost_INCLUDE_DIRS})
  LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
  ADD_DEFINITIONS( "-DHAS_BOOST" )
ENDIF()

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
FetchContent_MakeAvailable(yaml-cpp)
include_directories(${yaml-cpp_SOURCE_DIR}/include)

include(CTest)
enable_testing()

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
  ./source/parser.cpp)
target_include_directories(firehose_client PUBLIC ./include ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(firehose_client  ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${ICU_LIBRARIES} nlohmann_json_schema_validator spdlog yaml-cpp::yaml-cpp)

if (UNIT_TESTS_ENABLED)
  add_subdirectory(test)
ENDIF()

