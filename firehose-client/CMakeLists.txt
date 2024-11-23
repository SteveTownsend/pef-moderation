cmake_minimum_required(VERSION 3.23.0)
project(firehose-client VERSION 0.1.0 LANGUAGES C CXX)

add_executable(firehose-client main.cpp)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
