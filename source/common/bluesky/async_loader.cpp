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
      std::unordered_set<std::string> dids;
      _queue.wait_dequeue(dids);
      metrics_factory::instance()
          .get_gauge("process_operation")
          .Get({{"bsky_api", "backlog"}})
          .Decrement();
      try {
        if (dids.size() != 1) {
          // Avoid a backlog of batch invocations, all but the first should be
          // small
          _batch_in_progress = true;
          REL_INFO("Batch load {} accounts", dids.size());
          // batch load happens only at startup - use batch API, and do not spam
          // log
          auto profiles(_appview_client->get_profiles(dids));
          for (auto const &profile : profiles) {
            activity::event_recorder::instance().update_handle(profile.did,
                                                               profile.handle);
            REL_TRACE("Batch-load DID {} has handle {}", profile.did,
                      profile.handle);
          }
        } else {
          auto profile(_appview_client->get_profile(*dids.cbegin()));
          REL_INFO("DID {} has handle {}", profile.did, profile.handle);
        }
      } catch (std::exception const &exc) {
        REL_ERROR("load failed for {} DIDs", dids.size());
      }
      _batch_in_progress = false;
    }
    REL_INFO("async_loader stopping");
  });
}

void async_loader::wait_enqueue(std::unordered_set<std::string> &&value) {
  _queue.enqueue(value);
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"bsky_api", "backlog"}})
      .Increment();
}

} // namespace bsky
