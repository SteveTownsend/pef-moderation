#pragma once
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
#if 0
#include "common/config.hpp"
#include <chrono>
#include <mutex>
#include <pqxx/pqxx>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#endif
#include "yaml-cpp/yaml.h"
#include <neo4j-client.h>

namespace activity {

class neo4j_adapter {
public:
  neo4j_adapter(YAML::Node const &settings);

private:
#if 0
  void check_refresh_tracked_accounts();
#endif
  std::string safe_connection_string() const;

#if 0
  static constexpr std::chrono::milliseconds ThreadDelay =
      std::chrono::milliseconds(15000);
  static constexpr std::chrono::minutes ProcessedAccountRefreshInterval =
      std::chrono::minutes(15);

  std::unique_ptr<pqxx::connection> _cx;
#endif
  std::string _connection_string;
#if 0
  std::thread _thread;
  account_list _tracked_accounts;
  std::chrono::steady_clock::time_point _last_refresh;
  std::unordered_set<std::string> _closed_reports;
  pending_report_tags _pending_report_tags;
  content_reporters _content_reporters;
  filtered_subjects _filtered_subjects;
  mutable std::mutex _lock;
#endif
};

} // namespace activity
#endif