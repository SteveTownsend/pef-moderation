#pragma once
/*************************************************************************
Public Education Forum Moderation Firehose Client
Copyright (c) Steve Townsend 2025

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
#include "common/bluesky/platform.hpp"
#include "common/config.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <pqxx/pqxx>
#include <thread>
#include <unordered_set>

namespace bsky {
namespace moderation {

class auxiliary_data {
public:
  static inline auxiliary_data &instance() {
    static auxiliary_data data;
    return data;
  }
  auxiliary_data() = default;
  void start(YAML::Node const &settings);
  void set_rewind_point();
  // this returns 0 by design, if handling is disabled
  inline int64_t get_rewind_point() const { return _cursor.load(); };
  void update_rewind_point(const int64_t seq, const std::string &emitted_at);

  // Periodic refresh
  void check_rewind_point();
  void update_match_filters();
  void update_popular_hosts();

private:
  static constexpr size_t UtcDateTimeMaxLength = 48;
  ~auxiliary_data() = default;
  std::string safe_connection_string() const;
  void prepare_statements();

  static constexpr std::chrono::milliseconds RewindFlushInterval =
      std::chrono::milliseconds(15000);
  static constexpr std::chrono::minutes RewindCheckpointInterval =
      std::chrono::minutes(60);
  static constexpr std::chrono::minutes MatchFiltersRefreshInterval =
      std::chrono::minutes(5);
  static constexpr std::chrono::minutes PopularHostsRefreshInterval =
      std::chrono::minutes(15);

  std::unique_ptr<pqxx::connection> _cx;
  std::string _connection_string;
  std::thread _thread;

  bool _enable_rewind = false;
  std::atomic<int64_t> _cursor = 0;
  std::array<char, UtcDateTimeMaxLength> _emitted_at;
  bsky::time_stamp _last_rewind_checkpoint;
  std::chrono::steady_clock::time_point _last_rewind_flush;
  std::chrono::steady_clock::time_point _last_match_filter_refresh;
  std::chrono::steady_clock::time_point _last_popular_host_refresh;
  mutable std::mutex _lock;
  // Bluesky only for now
};

} // namespace moderation
} // namespace bsky
