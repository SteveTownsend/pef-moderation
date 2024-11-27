cmake_minimum_required(VERSION 3.23.0)
project(firehose-client VERSION 0.1.0 LANGUAGES C CXX)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(UNIT_TESTS_ENABLED ON)

include(CMakePrintHelpers)

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
  GIT_HASH b4fb360ef2764bf0cd22daaa62dd4bfd18b8ad0c
  GIT_REPOSITORY https://github.com/cjgdev/aho_corasick
)
FetchContent_MakeAvailable(aho_corasick)
include(CMakePrintHelpers)

# For Boost.Beast (REST, Websocket)
SET(Boost_USE_STATIC_LIBS YES)
SET(Boost_USE_MULTITHREAD ON)
SET(Boost_NO_BOOST_CMAKE TRUE)
SET(BOOST_ROOT $ENV{BOOST_ROOT})
set(BOOST_LIBRARYDIR $ENV{BOOST_LIBRARYDIR})
cmake_print_variables(BOOST_ROOT)
cmake_print_variables(BOOST_LIBRARYDIR)
set(OPENSSL_INCLUDE_DIR $ENV{OPENSSL_INCLUDE_DIR})
set(OPENSSL_ROOT_DIR $ENV{OPENSSL_ROOT_DIR})
set(OPENSSL_VERSION $ENV{OPENSSL_VERSION})
cmake_print_variables(OPENSSL_INCLUDE_DIR)
cmake_print_variables(OPENSSL_ROOT_DIR)
cmake_print_variables(OPENSSL_VERSION)

find_package(Boost REQUIRED COMPONENTS system context)
IF(Boost_FOUND)
  INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
  LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
  ADD_DEFINITIONS( "-DHAS_BOOST" )
ENDIF()

find_package(OpenSSL REQUIRED)
IF(OpenSSL_FOUND)
  INCLUDE_DIRECTORIES(${OPENSSL_INCLUDE_DIR})
  LINK_DIRECTORIES(${OPENSSL_LIBRARIES})
ENDIF()

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

cmake_print_variables(CMAKE_INCLUDE_PATH)
cmake_print_variables(CMAKE_CXX_COMPILER)
cmake_print_variables(Boost_FOUND)
cmake_print_variables(Boost_INCLUDE_DIRS)
cmake_print_variables(Boost_LIBRARY_DIRS)
cmake_print_variables(OpenSSL_FOUND)
cmake_print_variables(OPENSSL_INCLUDE_DIR)
cmake_print_variables(OPENSSL_LIBRARIES)

add_executable(firehose-client main.cpp)
IF(Boost_FOUND)
  target_link_libraries(firehose-client ${Boost_LIBRARIES})
ENDIF()
IF(OpenSSL_FOUND)
  target_link_libraries(firehose-client ${OPENSSL_LIBRARIES})
ENDIF()

if (UNIT_TESTS_ENABLED)
  add_subdirectory(test)
ENDIF()

