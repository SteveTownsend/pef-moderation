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
#include "common/activity/event_recorder.hpp"
#include "common/bluesky/client.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include <boost/fusion/adapted.hpp>
#include <functional>
#include <unordered_set>

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
        // Load the list of labeled and pending-review accounts
        // we may track some false positines but that's no big deal
        check_refresh_tracked_accounts();
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
void ozone_adapter::check_refresh_tracked_accounts() {
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::seconds>(now - _last_refresh) >
      ProcessedAccountRefreshInterval) {
    decltype(_tracked_accounts) new_tracked;
    pqxx::work tx(*_cx);
    for (auto [did] : tx.query<std::string>(
             "select distinct(ms.\"subjectDid\") from moderation_event ms "
             "where ms.\"action\" = "
             "'tools.ozone.moderation.defs#modEventLabel' union "
             "select distinct(did) from moderation_subject_status mss "
             "where mss.\"reviewState\" in "
             "('tools.ozone.moderation.defs#reviewOpen', "
             "'tools.ozone.moderation.defs#reviewEscalated'))")) {
      new_tracked.insert(did);
    }
    // Closed reports at account level
    decltype(_closed_reports) new_closed;
    for (auto [did] : tx.query<std::string>(
             "SELECT mss.did FROM moderation_subject_status mss WHERE "
             "(mss.\"recordPath\" <> '') IS NOT true AND "
             "(mss.\"reviewState\" = "
             "'tools.ozone.moderation.defs#reviewClosed')")) {
      if (!new_tracked.contains(did)) {
        new_closed.insert(did);
      }
    }

    std::lock_guard guard(_lock);
    _tracked_accounts.swap(new_tracked);
    _closed_reports.swap(new_closed);
    // make tracked accounts sticky in the tracked account event cache by
    // touching them each time
    for (auto const &account : _tracked_accounts) {
      activity::event_recorder::instance().touch_account(account);
    }
    _last_refresh = std::chrono::steady_clock::now();
  }
}

void ozone_adapter::load_pending_report_tags() {
  try {
    if (!_cx) {
      _cx = std::make_unique<pqxx::connection>(_connection_string);
    }
    // load the list of open/escalated reports
    std::unordered_set<std::string> deleted_accounts;
    pqxx::work tx(*_cx);
    for (auto [did, record_path, json_tags] :
         tx.query<std::string, std::string, std::optional<std::string>>(
             "SELECT did, \"recordPath\", tags FROM "
             "moderation_subject_status WHERE \"reviewState\""
             " in ('tools.ozone.moderation.defs#reviewOpen', "
             "'tools.ozone.moderation.defs#reviewEscalated')")) {
      auto account_pending_reports(_pending_report_tags.find(did));
      if (account_pending_reports == _pending_report_tags.end()) {
        account_pending_reports = _pending_report_tags.insert({did, {}}).first;
        REL_INFO("{} registered as pending", did);
      }
      // explicitly record the absence of a reported account as well as each
      // reported post
      std::vector<std::string> tags;
      if (json_tags.has_value() && !json_tags.value().empty()) {
        try {
          nlohmann::json parsed_tags(nlohmann::json::parse(json_tags.value()));
          if (parsed_tags.is_array()) {
            for (auto tag : parsed_tags) {
              tags.push_back(tag);
            }
          }
        } catch (std::exception const &exc) {
          REL_ERROR("({}) ({}) error on tags {}", did, record_path,
                    json_tags.value());
        }
      }
      if (!record_path.empty()) {
        if (account_pending_reports->second.insert({record_path, tags})
                .second) {
          REL_INFO("{} {} pending with tags {}", did, record_path,
                   json_tags.value_or(""));
        } else {
          REL_INFO("{} {} pending duplicate", did, record_path);
        }
      } else {
        if (account_pending_reports->second.insert({did, tags}).second) {
          REL_INFO("{} pending with tags {}", did, json_tags.value_or(""));
        } else {
          REL_INFO("{} pending duplicate", did);
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

void ozone_adapter::load_content_reporters(std::string const &auto_reporter) {
  try {
    if (!_cx) {
      _cx = std::make_unique<pqxx::connection>(_connection_string);
    }
    // load the list of open/escalated reports
    pqxx::work tx(*_cx);
    // default target is DID
    for (auto [target, full_path, reporter, reason] :
         tx.query<std::string, std::optional<std::string>, std::string,
                  std::optional<std::string>>(
             "SELECT \"subjectDid\", \"subjectUri\", \"createdBy\","
             " \"comment\" FROM public.moderation_event"
             " WHERE action = 'tools.ozone.moderation.defs#modEventReport' "
             "  AND meta->>'reportType' <> "
             "'com.atproto.moderation.defs#reasonAppeal';")) {
      if (full_path.has_value() && !full_path.value().empty()) {
        target = full_path.value();
      }
      auto subject_reports(_content_reporters.find(target));
      if (subject_reports == _content_reporters.end()) {
        subject_reports = _content_reporters.insert({target, {}}).first;
        REL_INFO("{} registered as reported", target);
      }
      bool is_auto(false);
      if (reason.has_value()) {
        try {
          nlohmann::json parsed_reason(nlohmann::json::parse(reason.value()));
          is_auto = parsed_reason.contains("descriptor") &&
                    parsed_reason["descriptor"].template get<std::string>() ==
                        auto_reporter;
        } catch (std::exception &) {
          // must be a manual report
        }
      }
      if (is_auto) {
        ++subject_reports->second.automatic;
      } else {
        ++subject_reports->second.manual;
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

// This is unsafe, should run with dry_run first to make sure it does what is
// expected
void ozone_adapter::filter_subjects(std::string const &filter) {
  REL_INFO("Filter reports using {}", filter);
  try {
    if (!_cx) {
      _cx = std::make_unique<pqxx::connection>(_connection_string);
    }
    // load the list of matching open/escalated reports
    pqxx::work tx(*_cx);
    // default target is DID
    for (auto [target, full_path, reporter, reason] :
         tx.query<std::string, std::optional<std::string>, std::string,
                  std::optional<std::string>>(
             "SELECT \"subjectDid\", \"subjectUri\", \"createdBy\","
             " \"comment\" FROM public.moderation_event " +
             filter)) {
      if (full_path.has_value() && !full_path.value().empty()) {
        target = full_path.value();
      }
      auto subject_entry(_filtered_subjects.insert(
          std::make_pair(target, reason.value_or(""))));
      if (subject_entry.second) {
        REL_INFO("{} matched context {}", target, reason.value_or(""));
      } else {
        REL_INFO("{} duplicate match context {}", target, reason.value_or(""));
        subject_entry.first->second.append("\n");
        subject_entry.first->second.append(reason.value_or(""));
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
  return _tracked_accounts.contains(did) || _closed_reports.contains(did);
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