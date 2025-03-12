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
#include "moderation/auxiliary_data.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "matcher.hpp"
#include "moderation/embed_checker.hpp"

namespace bsky {
namespace moderation {

void auxiliary_data::start(YAML::Node const &settings) {
  // synchronously prepare for data source rewind after a planned or unplanned
  // stoppage

  _connection_string = build_db_connection_string(settings["db"]);
  _enable_rewind = settings["enable_rewind"].as<bool>(false);
  try {
    _cx = std::make_unique<pqxx::connection>(_connection_string);
    REL_INFO("Connected OK to auxiliary DB: {}", safe_connection_string());
    set_rewind_point();
  } catch (std::exception const &exc) {
    REL_ERROR("Get rewind point error: {}", exc.what());
  }
  _cx.reset();
  _thread = std::thread([&, this] {
    while (controller::instance().is_active()) {
      try {
        if (!_cx) {
          _cx = std::make_unique<pqxx::connection>(_connection_string);
          prepare_statements();
        }
        // update firehose checkpoint regularly
        check_rewind_point();
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
      std::this_thread::sleep_for(RewindFlushInterval);
    }
    REL_INFO("auxiliary_data stopping");
  });
}

void auxiliary_data::prepare_statements() {
  _cx->prepare("add_checkpoint", "INSERT INTO firehose_checkpoint (emitted_at, "
                                 "seq) VALUES ($1, $2)");
  _cx->prepare("update_cursor", "UPDATE firehose_state SET last_processed = "
                                "$1, emitted_at = $2 WHERE true");
}

void auxiliary_data::update_rewind_point(const int64_t seq,
                                         const std::string &emitted_at) {
  if (!_enable_rewind)
    return;
  // TODO should be safe but not guaranteed always accurate for lock-free read
  // seq/emitted_at may mismatch
  // emitted_at may contain part of old and new values
  const int64_t prior = _cursor.exchange(seq);
  // During backfill, observed the firehose apparently sometimes incorrectly
  // winds back. Treat this as a fatal error.
  if (seq < prior) {
    REL_ERROR("seq in hand {} precedes current cursor {}", seq, prior);
    controller::instance().force_stop();
  }
  _emitted_at[emitted_at.length()] = 0;
  std::copy(emitted_at.cbegin(), emitted_at.cend(), _emitted_at.data());
}

// prepare for data backfill - for malformed data, continue but do not backfill
void auxiliary_data::set_rewind_point() {
  if (!_enable_rewind)
    return;
  pqxx::work tx(*_cx);
  bool first(true);
  auto result = tx.exec("SELECT last_processed from firehose_state").one_row();
  auto [last_processed] = result.as<int64_t>();
  REL_INFO("Backfill to {}", last_processed);
  _cursor = last_processed;
}

void auxiliary_data::check_rewind_point() {
  if (!_enable_rewind)
    return;
  // Don't save a checkpoint until interval has elapsed, provided checkpoint
  // candidate has been recorded. This relies on emitted_at values, not
  // current/real time.
  int64_t cursor(get_rewind_point());
  std::string last_event_time(_emitted_at.data());
  if (cursor == 0 || _emitted_at[0] == '\0') {
    REL_INFO("No firehose data processed, skip check");
    return;
  } else {
    auto current_cursor(bsky::time_stamp_from_iso_8601(last_event_time));
    if (std::chrono::duration_cast<std::chrono::minutes>(
            current_cursor - _last_rewind_checkpoint) >
        RewindCheckpointInterval) {
      pqxx::work tx(*_cx);
      static pqxx::prepped inserter(
          "INSERT INTO firehose_checkpoint (emitted_at, "
          "seq) VALUES ($1, $2)");
      pqxx::params fields(last_event_time, cursor);
      tx.exec(pqxx::prepped("add_checkpoint"), fields);
      tx.commit();
      REL_INFO("firehose_checkpoint {} {}", last_event_time, cursor);
      _last_rewind_checkpoint = current_cursor;
    }
  }
  {
    // Update rewind position on every pass
    pqxx::work tx(*_cx);
    pqxx::params fields(cursor, last_event_time);
    tx.exec(pqxx::prepped("update_cursor"), fields);
    tx.commit();
    REL_TRACE("cursor advanced to {} {}", cursor, last_event_time);
  }
}

// Don't refresh until interval has elapsed
void auxiliary_data::update_match_filters() {
  if (!matcher::shared().use_db_for_rules())
    return;
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::minutes>(
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