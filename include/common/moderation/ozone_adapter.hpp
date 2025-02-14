#ifndef __ozone_adapter__
#define __ozone_adapter__
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
#include "common/activity/event_cache.hpp"
#include "common/config.hpp"
#include <chrono>
#include <mutex>
#include <pqxx/pqxx>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace bsky {

namespace moderation {

class ozone_adapter {
public:
  static inline ozone_adapter &instance() {
    static ozone_adapter adapter;
    return adapter;
  }
  ozone_adapter() = default;
  void start(std::string const &connection_string,
             const bool use_thread = false);
  bool already_processed(std::string const &did) const;
  typedef std::unordered_map<
      std::string, std::unordered_map<std::string, std::vector<std::string>>>
      pending_report_tags;
  void load_pending_report_tags();
  inline const pending_report_tags &get_pending_reports() {
    return _pending_report_tags;
  }

  // Stores reported content with a count of automated and manual reports
  struct reports_by_category {
    size_t manual = 0;
    size_t automatic = 0;
  };
  typedef std::unordered_map<std::string, reports_by_category>
      content_reporters;
  void load_content_reporters(std::string const &auto_reporter);
  inline const content_reporters &get_content_reporters() {
    return _content_reporters;
  }

  typedef std::unordered_map<std::string, std::string> filtered_subjects;
  void filter_subjects(std::string const &filter);
  inline const filtered_subjects &get_filtered_subjects() {
    return _filtered_subjects;
  }

  typedef std::unordered_set<std::string> account_list;
  bool is_tracked(std::string const &did) const {
    std::lock_guard guard(_lock);
    return _tracked_accounts.contains(did);
  }
  void track_account(std::string const &did);

private:
  void check_refresh_tracked_accounts();
  std::string safe_connection_string() const;

  static constexpr std::chrono::milliseconds ThreadDelay =
      std::chrono::milliseconds(15000);
  static constexpr std::chrono::minutes ProcessedAccountRefreshInterval =
      std::chrono::minutes(15);

  std::unique_ptr<pqxx::connection> _cx;
  std::string _connection_string;
  std::thread _thread;
  account_list _tracked_accounts;
  std::chrono::steady_clock::time_point _last_refresh;
  std::unordered_set<std::string> _closed_reports;
  pending_report_tags _pending_report_tags;
  content_reporters _content_reporters;
  filtered_subjects _filtered_subjects;
  mutable std::mutex _lock;
};

} // namespace moderation
} // namespace bsky

#endif
