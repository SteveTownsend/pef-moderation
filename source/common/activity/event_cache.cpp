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
#include "common/metrics_factory.hpp"
#include <functional>

namespace activity {

event_cache::event_cache()
    : _account_events(
          MaxAccounts, caches::LFUCachePolicy<std::string>(),
          std::function<void(std::string const &,
                             std::shared_ptr<account> const &)>(
              std::bind(&event_cache::on_erase, this, std::placeholders::_1,
                        std::placeholders::_2))) {}

void event_cache::record(timed_event const &value) {
  metrics_factory::instance()
      .get_counter("realtime_alerts")
      .Get({{"events", "total"}})
      .Increment();
  // look up the account, add if not known yet
  caches::WrappedValue<account> source(get_account(value._did));
  source->record(*this, value);

  std::visit(augment_event{}, value._event);
}

caches::WrappedValue<account> event_cache::get_account(std::string const &did) {
  if (!_account_events.Cached(did)) {
    add_account(did);
  }
  return _account_events.Get(did);
}

void event_cache::add_account(std::string const &did) {
  _account_events.Put(did, account(did));
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"cached_items", "account"}})
      .Increment();
}

// Callback for tracked account removal
void event_cache::on_erase(std::string const &did,
                           caches::WrappedValue<account> const &account) {
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"cached_items", "account"}})
      .Decrement();
  size_t alerts(account->alert_count());
  if (alerts > 0) {
    REL_INFO("Account evicted {} with {} alerts {} events", did, alerts,
             account->event_count());
    // TODO analyze evicted record and report via log file if it is of interest
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "evictions"}, {"state", "flagged"}})
        .Increment();
  } else {
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "evictions"}, {"state", "clean"}})
        .Increment();
  }
}

} // namespace activity
