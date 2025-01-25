#ifndef __ozone_adapter__
#define __ozone_adapter__
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
#include "config.hpp"
#include <chrono>
#include <mutex>
#include <pqxx/pqxx>
#include <thread>
#include <unordered_set>

namespace bsky {
namespace moderation {

class ozone_adapter {
public:
  ozone_adapter(std::string const &connection_string);
  void start();
  bool already_processed(std::string const &did) const;

private:
  void check_refresh_processed();
  std::string safe_connection_string() const;

  static constexpr std::chrono::milliseconds ThreadDelay =
      std::chrono::milliseconds(15000);
  static constexpr std::chrono::minutes ProcessedAccountRefreshInterval =
      std::chrono::minutes(15);

  std::unique_ptr<pqxx::connection> _cx;
  std::string _connection_string;
  std::thread _thread;
  std::unordered_set<std::string> _labeled_accounts;
  std::chrono::steady_clock::time_point _last_refresh;
  std::unordered_set<std::string> _processed_accounts;
  mutable std::mutex _lock;
};

} // namespace moderation
} // namespace bsky

#endif
