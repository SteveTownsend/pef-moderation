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

#include "moderation/action_router.hpp"

#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "common/moderation/report_agent.hpp"
#include "matcher.hpp"
#include "moderation/list_manager.hpp"

action_router &action_router::instance() {
  static action_router my_instance;
  return my_instance;
}

action_router::action_router() : _queue(QueueLimit) {}

void action_router::start() {
  _thread = std::thread([&, this] {
    while (controller::instance().is_active()) {
      account_filter_matches matches;
      _queue.wait_dequeue(matches);
      // process the item
      metrics_factory::instance()
          .get_gauge("process_operation")
          .Get({{"action_router", "backlog"}})
          .Decrement();
      matcher::shared().report_if_needed(matches);
    }
    REL_INFO("action_router stopping");
  });
}

void action_router::update_blacklist(
    std::unordered_set<std::string> new_blacklist) {
  std::lock_guard<std::mutex> lock{_lock};
  // add new blacklist entries to Moderation List
  std::ranges::for_each(
      new_blacklist | std::views::filter([&](const std::string &did) {
        return !_blacklist.contains(did);
      }),
      [&](const std::string &did) {
        list_manager::instance().wait_enqueue(
            {did, std::string(BlacklistName)});
      });
  std::swap(_blacklist, new_blacklist);
}

void action_router::update_whitelist(
    std::unordered_set<std::string> new_whitelist) {
  std::lock_guard<std::mutex> lock{_lock};
  std::swap(_whitelist, new_whitelist);
}

void action_router::update_ignored(
    std::unordered_set<std::string> new_ignored) {
  std::lock_guard<std::mutex> lock{_lock};
  std::swap(_ignored, new_ignored);
}

void action_router::check_wait_enqueue(std::string const &did,
                                       account_filter_matches &&value) {
  // skip backlisted accounts
  {
    std::lock_guard<std::mutex> lock{_lock};
    if (_blacklist.contains(did)) {
      REL_INFO("Skipping blacklisted account {}", did);
      return;
    }
    if (_whitelist.contains(did)) {
      REL_INFO("Processing whitelisted account {}", did);
      return;
    }
    if (_ignored.contains(did)) {
      REL_INFO("Skipping ignored account {}", did);
      return;
    }
  }
  wait_enqueue(std::move(value));
}

void action_router::wait_enqueue(account_filter_matches &&value) {
  _queue.enqueue(value);
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"action_router", "backlog"}})
      .Increment();
}
