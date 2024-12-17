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

#include "log_wrapper.hpp"
#include "config.hpp"
#include "firehost_client_config.hpp"
#include <filesystem>
#include <spdlog/sinks/daily_file_sink.h>

std::shared_ptr<spdlog::logger> logger;

void init_logging(std::string const &log_file,
                  spdlog::level::level_enum log_level) {
  std::filesystem::path logPath(log_file);
  try {
    std::string fileName(logPath.generic_string());
    logger = spdlog::daily_logger_mt(PROJECT_NAME, log_file, 3, 0);
    logger->set_pattern("%Y-%m-%d %T.%e %8l %6t %v");
  } catch (const spdlog::spdlog_ex &) {
  }
  logger->set_level(log_level); // Set mod's log level
  spdlog::flush_every(std::chrono::seconds(15));
}
