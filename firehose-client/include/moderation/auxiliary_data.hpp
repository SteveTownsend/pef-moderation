#ifndef __auxiliary_data__
#define __auxiliary_data__
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
#include "common/config.hpp"
#include <chrono>
#include <mutex>
#include <pqxx/pqxx>
#include <thread>
#include <unordered_set>

namespace bsky {
namespace moderation {

class auxiliary_data {
public:
  auxiliary_data(std::string const &connection_string);
  void start();
  bool already_processed(std::string const &did) const;
  // Periodic refresh
  void update_match_filters();
  void update_popular_hosts();

private:
  std::string safe_connection_string() const;

  static constexpr std::chrono::milliseconds ThreadDelay =
      std::chrono::milliseconds(15000);
  static constexpr std::chrono::minutes MatchFiltersRefreshInterval =
      std::chrono::minutes(5);
  static constexpr std::chrono::minutes PopularHostsRefreshInterval =
      std::chrono::minutes(15);

  std::unique_ptr<pqxx::connection> _cx;
  std::string _connection_string;
  std::thread _thread;
  std::chrono::steady_clock::time_point _last_match_filter_refresh;
  std::chrono::steady_clock::time_point _last_popular_host_refresh;
  mutable std::mutex _lock;
};

} // namespace moderation
} // namespace bsky

#endif
