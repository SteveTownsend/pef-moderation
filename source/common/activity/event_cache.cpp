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

#include <functional>

#include "common/activity/event_recorder.hpp"
#include "common/metrics_factory.hpp"

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

  std::visit(augment_event{value._did, value._created_at}, value._event);
}

caches::WrappedValue<account> event_cache::get_account(std::string const &did) {
  std::lock_guard guard(_cache_lock);
  if (!_account_events.Cached(did)) {
    _account_events.Put(did, account(did));
    metrics_factory::instance()
        .get_gauge("process_operation")
        .Get({{"cached_items", "account"}})
        .Increment();
  }
  return _account_events.Get(did);
}

void event_cache::augment_event::operator()(activity::post const &value) {
  if (event_recorder::instance().is_network_root(_did)) {
    REL_INFO("Network root: {} post", _did);
  }
}
void event_cache::augment_event::operator()(activity::reply const &value) {
  if (event_recorder::instance().is_network_root(_did) ||
      event_recorder::instance().is_network_root(value._parent._authority) ||
      event_recorder::instance().is_network_root(value._root._authority)) {
    REL_INFO("Network root: {} reply to {} (root {})", _did,
             value._parent._authority, value._root._authority);
  }
}
void event_cache::augment_event::operator()(activity::repost const &value) {
  if (event_recorder::instance().is_network_root(_did) ||
      event_recorder::instance().is_network_root(value._post._authority)) {
    REL_INFO("Network root: {} reposted {}", _did, value._post._authority);
  }
}
void event_cache::augment_event::operator()(activity::quote const &value) {
  if (event_recorder::instance().is_network_root(_did) ||
      event_recorder::instance().is_network_root(value._post._authority)) {
    REL_INFO("Network root: {} quoted {}", _did, value._post._authority);
  }
}
void event_cache::augment_event::operator()(activity::block const &value) {
  if (event_recorder::instance().is_network_root(_did) ||
      event_recorder::instance().is_network_root(value._blocked)) {
    REL_INFO("Network root: {} blocked {}", _did, value._blocked);
  }
}
void event_cache::augment_event::operator()(activity::follow const &value) {
  if (event_recorder::instance().is_network_root(_did) ||
      event_recorder::instance().is_network_root(value._followed)) {
    REL_INFO("Network root: {} followed {}", _did, value._followed);
  }
}
void event_cache::augment_event::operator()(activity::like const &value) {
  if (event_recorder::instance().is_network_root(_did) ||
      event_recorder::instance().is_network_root(value._content._authority)) {
    REL_INFO("Network root: {} liked {}", _did, value._content._authority);
  }
}
void event_cache::augment_event::operator()(activity::active const &value) {
  if (event_recorder::instance().is_network_root(_did)) {
    REL_INFO("Network root: {} active", _did);
  }
}
void event_cache::augment_event::operator()(activity::handle const &value) {
  if (event_recorder::instance().is_network_root(_did)) {
    REL_INFO("Network root: {} handle", _did);
  }
}
void event_cache::augment_event::operator()(activity::inactive const &value) {
  if (event_recorder::instance().is_network_root(_did)) {
    REL_INFO("Network root: {} inactive", _did);
  }
}
void event_cache::augment_event::operator()(activity::profile const &value) {
  if (event_recorder::instance().is_network_root(_did)) {
    REL_INFO("Network root: {} profile revised", _did);
  }
}
void event_cache::augment_event::operator()(activity::deleted const &value) {
  if (event_recorder::instance().is_network_root(_did)) {
    REL_INFO("Network root: {} deleted {}", _did, value._path);
  }
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
    REL_INFO("Account evicted {}/{} with {} alerts {} events", did,
             account->get_statistics()._handle, alerts, account->event_count());
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

}  // namespace activity
