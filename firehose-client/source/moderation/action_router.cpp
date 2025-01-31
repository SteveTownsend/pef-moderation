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

#include "moderation/action_router.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "matcher.hpp"
#include "metrics.hpp"
#include "moderation/list_manager.hpp"
#include "moderation/report_agent.hpp"

action_router &action_router::instance() {
  static action_router my_instance;
  return my_instance;
}

action_router::action_router() : _queue(QueueLimit) {}

void action_router::start() {
  _thread = std::thread([&] {
    while (controller::instance().is_active()) {
      account_filter_matches matches;
      _queue.wait_dequeue(matches);
      // process the item
      metrics::instance()
          .operational_stats()
          .Get({{"action_router", "backlog"}})
          .Decrement();
      matcher::shared().report_if_needed(matches);
    }
    REL_INFO("action_router stopping");
  });
}

void action_router::wait_enqueue(account_filter_matches &&value) {
  _queue.enqueue(value);
  metrics::instance()
      .operational_stats()
      .Get({{"action_router", "backlog"}})
      .Increment();
}
