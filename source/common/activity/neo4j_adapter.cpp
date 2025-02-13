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
#if defined(__GNUC__)
#include "common/activity/neo4j_adapter.hpp"
#include "common/activity/event_recorder.hpp"
#include "common/bluesky/client.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include <boost/fusion/adapted.hpp>
#include <functional>
#include <unordered_set>

namespace activity {

neo4j_adapter::neo4j_adapter(YAML::Node const &settings) {
  _connection_string = settings["connection_string"].as<std::string>();
  neo4j_connection_t *connection =
      neo4j_connect(_connection_string.c_str(), NULL, NEO4J_INSECURE);
  if (connection == NULL) {
    neo4j_perror(stderr, errno, "Connection failed");
    std::string error("Failed to connect to neo4j graph DB: " +
                      safe_connection_string());
    REL_CRITICAL(error);
    throw std::runtime_error(error);
  } else {
    REL_INFO("Connected OK to neo4j graph DB: {}", safe_connection_string());
  }
}

// mask the password
std::string neo4j_adapter::safe_connection_string() const {
  constexpr const char password_sentinel[] = "/";
  constexpr const char *password_mask = "********";
  size_t start =
      _connection_string.find(password_sentinel, sizeof(password_sentinel) - 1);
  if (start != std::string::npos) {
    // find first subsequent space (or end of string) and replace the password
    start += sizeof(password_sentinel) - 1;
    size_t end = _connection_string.find(' ', start);
    if (end == std::string::npos) {
      end = _connection_string.length();
    }
    return std::string(_connection_string)
        .replace(start, end - start, password_mask);
  }
  // TODO fix this
  return std::string();
  // return _connection_string;
}

} // namespace activity
#endif