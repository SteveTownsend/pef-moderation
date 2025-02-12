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

#include "common/bluesky/async_loader.hpp"
#include "common/activity/event_recorder.hpp"
#include "common/controller.hpp"
#include "common/metrics_factory.hpp"

namespace bsky {
async_loader::async_loader() : _queue(MaxBacklog) {}

void async_loader::start(YAML::Node const &settings) {
  // create client
  _appview_client = std::make_unique<bsky::client>();
  _appview_client->set_config(settings);
  _thread = std::thread([&, this] {
    static size_t matches(0);
    while (controller::instance().is_active()) {
      std::string did;
      _queue.wait_dequeue(did);
      metrics_factory::instance()
          .get_gauge("process_operation")
          .Get({{"bsky_api", "backlog"}})
          .Decrement();
      std::string handle;
      bool valid(true);
      try {
        bsky::profile_view_detailed profile(_appview_client->get_profile(did));
        handle = profile.handle;
      } catch (std::exception const &exc) {
        handle = bsky::HandleInvalid;
        valid = std::string(exc.what()).find("AccountTakedown") ==
                std::string::npos;
      }
      if (valid) {
        activity::event_recorder::instance().update_handle(did, handle);
        REL_INFO("DID {} has handle {}", did, handle);
      } else {
        REL_INFO("DID {} suspended", did);
      }
    }
    REL_INFO("async_loader stopping");
  });
}

void async_loader::wait_enqueue(std::string &&value) {
  _queue.enqueue(value);
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"bsky_api", "backlog"}})
      .Increment();
}

} // namespace bsky
