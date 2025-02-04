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
#include "common/moderation/ozone_adapter.hpp"
#include "common/bluesky/client.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include <boost/fusion/adapted.hpp>
#include <functional>
#include <unordered_set>

// app.bsky.actor.getProfiles
BOOST_FUSION_ADAPT_STRUCT(bsky::profile, (std::string, did))
BOOST_FUSION_ADAPT_STRUCT(bsky::get_profiles_response,
                          (std::vector<bsky::profile>, profiles))

namespace bsky {
namespace moderation {

ozone_adapter::ozone_adapter(std::string const &connection_string)
    : _connection_string(connection_string) {
  REL_INFO("Connected OK to moderation DB: {}", safe_connection_string());
}

void ozone_adapter::start() {
  _thread = std::thread([&, this] {
    while (controller::instance().is_active()) {
      try {
        if (!_cx) {
          _cx = std::make_unique<pqxx::connection>(_connection_string);
        }
        // load the list of labeled accounts
        check_refresh_processed();
      } catch (pqxx::broken_connection const &exc) {
        // will reconnect on next loop
        REL_ERROR("pqxx::broken_connection {}", exc.what());
        _cx.reset();
      } catch (std::exception const &exc) {
        // try to reconnect on next loop, unlikely to work though
        REL_ERROR("database exception {}", exc.what());
        _cx.reset();
      }
      std::this_thread::sleep_for(ThreadDelay);
    }
    REL_INFO("ozone_adapter stopping");
  });
}

// Don't refresh until interval has elapsed
void ozone_adapter::check_refresh_processed() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::seconds>(now - _last_refresh) >
      ProcessedAccountRefreshInterval) {
    decltype(_labeled_accounts) new_labeled;
    pqxx::work tx(*_cx);
    for (auto [did] : tx.query<std::string>(
             "SELECT DISTINCT(\"subjectDid\") FROM moderation_event WHERE "
             "(action = 'tools.ozone.moderation.defs#modEventLabel')")) {
      new_labeled.insert(did);
    }
    decltype(_processed_accounts) new_done;
    for (auto [did] : tx.query<std::string>(
             "SELECT DISTINCT(\"subjectDid\") FROM moderation_event WHERE "
             "(action = 'tools.ozone.moderation.defs#modEventAcknowledge')")) {
      if (!new_labeled.contains(did)) {
        new_done.insert(did);
      }
    }

    std::lock_guard guard(_lock);
    _labeled_accounts.swap(new_labeled);
    _processed_accounts.swap(new_done);
    _last_refresh = std::chrono::steady_clock::now();
  }
}

void ozone_adapter::load_pending_reports() {
  try {
    if (!_cx) {
      _cx = std::make_unique<pqxx::connection>(_connection_string);
    }
    // load the list of open/escalated reports
    std::unordered_set<std::string> deleted_accounts;
    pqxx::work tx(*_cx);
    for (auto [did, record_path] : tx.query<std::string, std::string>(
             "SELECT did, \"recordPath\" FROM moderation_subject_status WHERE "
             "\"reviewState\" in ('tools.ozone.moderation.defs#reviewOpen', "
             "'tools.ozone.moderation.defs#reviewEscalated')")) {
      auto account_list(_pending_reports.find(did));
      if (account_list == _pending_reports.end()) {
        account_list = _pending_reports.insert({did, {}}).first;
        REL_INFO("{} recorded", did);
      }
      if (!record_path.empty()) {
        if (account_list->second.insert(record_path).second) {
          REL_INFO("{} {} recorded");
        } else {
          REL_INFO("{} {} duplicate");
        }
      }
    }
  } catch (pqxx::broken_connection const &exc) {
    // will reconnect on net loop
    REL_ERROR("pqxx::broken_connection {}", exc.what());
    _cx.reset();
  } catch (std::exception const &exc) {
    // try to reconnect on next loop, unlikely to work though
    REL_ERROR("database exception {}", exc.what());
    _cx.reset();
  }
}

bool ozone_adapter::already_processed(std::string const &did) const {
  std::lock_guard guard(_lock);
  return _labeled_accounts.contains(did) || _processed_accounts.contains(did);
}

// mask the password
std::string ozone_adapter::safe_connection_string() const {
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