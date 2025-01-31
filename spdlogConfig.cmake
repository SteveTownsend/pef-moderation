if (NOT SPDLOG_FOUND) # Necessary because the file may be invoked multiple times
    message(NOTICE "Using injected spdlogConfig.cmake")
    set(SPDLOG_INCLUDE_DIRS ${spdlog_SOURCE_DIR}/include)
    set(SPDLOG_LIBRARIES ${spdlog_BINARY_DIR})
    set(SPDLOG_BUILD_SHARED OFF)
endif()