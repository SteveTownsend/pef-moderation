/*************************************************************************
Public Education Forum Moderation Firehose Client
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

#include "common/log_wrapper.hpp"
#include "common/config.hpp"
#include <filesystem>
#include <iostream>
#include <spdlog/sinks/daily_file_sink.h>

std::shared_ptr<spdlog::logger> logger;
bool logger_started = false;

bool init_logging(std::string const &log_file, std::string const &project_name,
                  spdlog::level::level_enum log_level) {
  std::filesystem::path logPath(log_file);
  try {
    std::string fileName(logPath.generic_string());
    logger = spdlog::daily_logger_mt(project_name, log_file, 3, 0);
    logger->set_pattern("%Y-%m-%d %T.%F %8l %6t %v");
    logger->set_level(log_level); // Set mod's log level
#if _DEBUG || defined(_FULL_LOGGING)
    logger->flush_on(spdlog::level::level_enum::trace);
#else
    spdlog::flush_every(std::chrono::seconds(3));
#endif
    logger_started = true;
  } catch (const spdlog::spdlog_ex &exc) {
    std::cerr << "logging error " << exc.what() << '\n';
  }
  return logger_started;
}

void stop_logging() {
  if (logger_started) {
    // make sure all logs are output
    logger->flush();
  }
}
