#ifndef __log_wrapper_hpp__
#define __log_wrapper_hpp__
/*************************************************************************
NAFO Forum Moderation Firehose Client
Copyright (c) Steve Townsend 2024

>>> SOURCE LICENSE >>>
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation (www.fsf.org); either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

A copy of the GNU General Public License is available at
http://www.fsf.org/licensing/licenses
>>> END OF LICENSE >>>
*************************************************************************/

#include <spdlog/spdlog.h>

extern std::shared_ptr<spdlog::logger> logger;

// wrappers for spdLog to make release/debug logging easier
#if DISABLE_LOGGING
#define DBG_TRACE(a_fmt, ...)
#define DBG_DEBUG(a_fmt, ...)
#define DBG_INFO(a_fmt, ...)
#define DBG_WARNING(a_fmt, ...)
#define DBG_ERROR(a_fmt, ...)
#define DBG_CRITICAL(a_fmt, ...)

#define REL_TRACE(a_fmt, ...)
#define REL_DEBUG(a_fmt, ...)
#define REL_INFO(a_fmt, ...)
#define REL_WARNING(a_fmt, ...)
#define REL_ERROR(a_fmt, ...)
#define REL_CRITICAL(a_fmt, ...)
#else
// Debug build only
#if _DEBUG || defined(_FULL_LOGGING)
#define DBG_TRACE(a_fmt, ...)                                                  \
  if (logger->level() <= spdlog::level::trace) {                               \
    logger->trace(a_fmt __VA_OPT__(, ) __VA_ARGS__);                           \
  }
#define DBG_DEBUG(a_fmt, ...)                                                  \
  if (logger->level() <= spdlog::level::debug) {                               \
    logger->debug(a_fmt __VA_OPT__(, ) __VA_ARGS__);                           \
  }
#define DBG_INFO(a_fmt, ...)                                                   \
  if (logger->level() <= spdlog::level::info) {                                \
    logger->info(a_fmt __VA_OPT__(, ) __VA_ARGS__);                            \
  }
#define DBG_WARNING(a_fmt, ...)                                                \
  if (logger->level() <= spdlog::level::warn) {                                \
    logger->warn(a_fmt __VA_OPT__(, ) __VA_ARGS__);                            \
  }
#define DBG_ERROR(a_fmt, ...)                                                  \
  if (logger->level() <= spdlog::level::err) {                                 \
    logger->error(a_fmt __VA_OPT__(, ) __VA_ARGS__);                           \
  }
#define DBG_CRITICAL(a_fmt, ...)                                               \
  if (logger->level() <= spdlog::level::critical) {                            \
    logger->critical(a_fmt __VA_OPT__(, ) __VA_ARGS__);                        \
  }
#else
#define DBG_TRACE(a_fmt, ...)
#define DBG_DEBUG(a_fmt, ...)
#define DBG_INFO(a_fmt, ...)
#define DBG_WARNING(a_fmt, ...)
#define DBG_ERROR(a_fmt, ...)
#define DBG_CRITICAL(a_fmt, ...)
#endif

// Always log
#define REL_TRACE(a_fmt, ...)                                                  \
  if (logger->level() <= spdlog::level::trace) {                               \
    logger->trace(a_fmt __VA_OPT__(, ) __VA_ARGS__);                           \
  }
#define REL_DEBUG(a_fmt, ...)                                                  \
  if (logger->level() <= spdlog::level::debug) {                               \
    logger->debug(a_fmt __VA_OPT__(, ) __VA_ARGS__);                           \
  }
#define REL_INFO(a_fmt, ...)                                                   \
  if (logger->level() <= spdlog::level::info) {                                \
    logger->info(a_fmt __VA_OPT__(, ) __VA_ARGS__);                            \
  }
#define REL_WARNING(a_fmt, ...)                                                \
  if (logger->level() <= spdlog::level::warn) {                                \
    logger->warn(a_fmt __VA_OPT__(, ) __VA_ARGS__);                            \
  }
#define REL_ERROR(a_fmt, ...)                                                  \
  if (logger->level() <= spdlog::level::err) {                                 \
    logger->error(a_fmt __VA_OPT__(, ) __VA_ARGS__);                           \
  }
#define REL_CRITICAL(a_fmt, ...)                                               \
  if (logger->level() <= spdlog::level::critical) {                            \
    logger->critical(a_fmt __VA_OPT__(, ) __VA_ARGS__);                        \
  }

void init_logging(std::string const &log_file,
                  spdlog::level::level_enum log_level);
#endif

#endif