/*************************************************************************
NAFO Forum Moderation Firehose Client
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
#include "moderation/auxiliary_data.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "matcher.hpp"
#include "moderation/embed_checker.hpp"

namespace bsky {
namespace moderation {

auxiliary_data::auxiliary_data(std::string const &connection_string)
    : _connection_string(connection_string) {
  REL_INFO("Connected OK to auxiliary DB: {}", safe_connection_string());
}

void auxiliary_data::start() {
  _thread = std::thread([&] {
    while (controller::instance().is_active()) {
      try {
        if (!_cx) {
          _cx = std::make_unique<pqxx::connection>(_connection_string);
        }
        // load/refresh string match filters
        update_match_filters();
        // load/refresh string popular hosts used in embed:external and other
        // places
        update_popular_hosts();
      } catch (pqxx::broken_connection const &exc) {
        // will reconnect on net loop
        REL_ERROR("pqxx::broken_connection {}", exc.what());
        _cx.reset();
      } catch (std::exception const &exc) {
        // try to reconnect on next loop, unlikely to work though
        REL_ERROR("database exception {}", exc.what());
        _cx.reset();
      }
      std::this_thread::sleep_for(ThreadDelay);
    }
    REL_INFO("auxiliary_data stopping");
  });
}

// Don't refresh until interval has elapsed
void auxiliary_data::update_match_filters() {
  if (!matcher::shared().use_db_for_rules())
    return;
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::seconds>(
          now - _last_match_filter_refresh) > MatchFiltersRefreshInterval) {
    pqxx::work tx(*_cx);
    bool load_failed(false);
    matcher replacement;
    for (auto [filter, labels, actions, contingent] :
         tx.query<std::string, std::string, std::string,
                  std::optional<std::string>>("SELECT * FROM match_filters;")) {
      try {
        replacement.add_rule(filter, labels, actions, contingent.value_or(""));
      } catch (std::exception const &exc) {
        REL_ERROR("check_refresh_match_filters '{}|{}|{}|{}' error {}", filter,
                  labels, actions, contingent.value_or(""), exc.what());
        load_failed = true;
      }
    }

    if (!load_failed) {
      // switch replacement rules into the main matcher
      std::lock_guard guard(_lock);
      matcher::shared().refresh_rules(std::move(replacement));
      _last_match_filter_refresh = std::chrono::steady_clock::now();
    }
  }
}
void auxiliary_data::update_popular_hosts() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::seconds>(
          now - _last_popular_host_refresh) > PopularHostsRefreshInterval) {
    pqxx::work tx(*_cx);
    bool load_failed(false);
    std::unordered_set<std::string> new_hosts;
    for (auto [hostname] :
         tx.query<std::string>("SELECT * FROM popular_hosts;")) {
      new_hosts.insert(hostname);
    }

    if (!load_failed) {
      // switch replacement rules into the main matcher
      std::lock_guard guard(_lock);
      embed_checker::instance().refresh_hosts(std::move(new_hosts));
      _last_popular_host_refresh = std::chrono::steady_clock::now();
    }
  }
}

// mask the password
std::string auxiliary_data::safe_connection_string() const {
  constexpr const char password_sentinel[] = "password=";
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
  return _connection_string;
}

} // namespace moderation
} // namespace bsky