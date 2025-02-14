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

#include "common/activity/event_recorder.hpp"
#include "common/bluesky/async_loader.hpp"
#include "common/controller.hpp"
#include "common/metrics_factory.hpp"

namespace activity {
event_recorder::event_recorder() : _queue(MaxBacklog) {
  _thread = std::thread([&, this] {
    static size_t matches(0);
    while (controller::instance().is_active()) {
      timed_event my_payload;
      _queue.wait_dequeue(my_payload);
      metrics_factory::instance()
          .get_gauge("process_operation")
          .Get({{"events", "backlog"}})
          .Decrement();

      // record the activity
      _events.record(my_payload);
    }
    REL_INFO("event_recorder stopping");
  });
}

void event_recorder::wait_enqueue(timed_event &&value) {
  _queue.enqueue(value);
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"events", "backlog"}})
      .Increment();
}

std::string event_recorder::ensure_loaded(std::string const &did) {
  std::string handle(get_handle(did));
  if (handle.empty()) {
    // try to load the handle
    bsky::async_loader::instance().wait_enqueue({did});
  }
  return handle;
}

caches::WrappedValue<account>
event_recorder::add_if_needed(std::string const &did) {
  return _events.get_account(did);
}

void event_recorder::update_handle(std::string const &did,
                                   std::string const &handle) {
  add_if_needed(did)->get_statistics()._handle = handle;
}

std::string event_recorder::get_handle(std::string const &did) {
  return add_if_needed(did)->get_statistics()._handle;
}

} // namespace activity
