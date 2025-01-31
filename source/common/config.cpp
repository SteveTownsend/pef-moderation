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

#include "common/config.hpp"
#include "common/log_wrapper.hpp"

std::string build_db_connection_string(YAML::Node const &config_section) {
  bool first(true);
  std::ostringstream oss;
  for (auto field : config_section) {
    if (!first) {
      oss << ' ';
    } else {
      first = false;
    }
    oss << field.first.as<std::string>() << '='
        << field.second.as<std::string>();
  }
  return oss.str();
}

config::config(std::string const &filename) {
  try {
    _config = YAML::LoadFile(filename);
  } catch (std::exception const &exc) {
    REL_CRITICAL("Error processing config file {}:{}", filename, exc.what());
  }
}

YAML::Node const &config::get_config() const { return _config; }
