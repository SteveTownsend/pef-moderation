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

#include "activity/account_events.hpp"
#include "activity/event_cache.hpp"
#include "common/metrics_factory.hpp"
#include "common/moderation/report_agent.hpp"
#include <algorithm>
#include <boost/fusion/adapted.hpp>

BOOST_FUSION_ADAPT_STRUCT(
    activity::account::statistics, (std::string, _did), (size_t, _event_count),
    (size_t, _alert_count), (size_t, _tags), (size_t, _links),
    (size_t, _mentions), (size_t, _facets), (int32_t, _posts),
    (int32_t, _replied_to), (int32_t, _replies), (int32_t, _quoted),
    (int32_t, _quotes), (int32_t, _reposted), (int32_t, _reposts),
    (int32_t, _liked), (int32_t, _likes), (int32_t, _follows),
    (int32_t, _followed_by), (int32_t, _blocks), (int32_t, _blocked_by),
    (unsigned short, _updates), (unsigned short, _activations),
    (unsigned short, _profiles), (unsigned short, _handles), (size_t, _unposts),
    (size_t, _unlikes), (size_t, _unreposts), (size_t, _unfollows),
    (size_t, _unblocks), (unsigned short, _matches))

namespace activity {

account::account(did_type const &did)
    : _content_hits(std::make_shared<
                    lfu_cache_at_uri_t<atproto::at_uri, content_hit_count>>(
          MaxContentItems, CustomLFUCachePolicy<atproto::at_uri>(),
          std::function<void(atproto::at_uri const &,
                             std::shared_ptr<content_hit_count> const &)>(
              std::bind(&account::on_erase, this, std::placeholders::_1,
                        std::placeholders::_2)))) {
  _statistics._did = did;
}

void account::statistics::tags(const size_t count) {
  if (count > activity::account::TagFacetThreshold) {
    if (alert_needed(++_tags, FacetFactor)) {
      REL_INFO("Account flagged tag-facets {} {}", _did, _tags);
      metrics_factory::instance()
          .get_counter("realtime_alerts")
          .Get({{"account", "tag_facets"}})
          .Increment();
      alert();
    }
  }
}
void account::statistics::links(const size_t count) {
  if (count > activity::account::LinkFacetThreshold) {
    if (alert_needed(++_links, FacetFactor)) {
      REL_INFO("Account flagged link-facets {} {}", _did, _links);
      metrics_factory::instance()
          .get_counter("realtime_alerts")
          .Get({{"account", "link_facets"}})
          .Increment();
      alert();
    }
  }
}
void account::statistics::mentions(const size_t count) {
  if (count > activity::account::MentionFacetThreshold) {
    if (alert_needed(++_mentions, FacetFactor)) {
      REL_INFO("Account flagged mention-facets {} {}", _did, _mentions);
      metrics_factory::instance()
          .get_counter("realtime_alerts")
          .Get({{"account", "mention_facets"}})
          .Increment();
      alert();
    }
  }
}
void account::statistics::facets(const size_t count) {
  if (count > activity::account::TotalFacetThreshold) {
    if (alert_needed(++_facets, FacetFactor)) {
      REL_INFO("Account flagged total-facets {} {}", _did, _facets);
      metrics_factory::instance()
          .get_counter("realtime_alerts")
          .Get({{"account", "all_facets"}})
          .Increment();
      alert();
    }
  }
}

void account::statistics::record(event_cache &parent_cache,
                                 timed_event const &event) {
  std::visit(augment_account_event(parent_cache, *this), event._event);
  if (alert_needed(++_event_count, EventFactor)) {
    std::ostringstream oss;
    restc_cpp::SerializeToJson(*this, oss);
    REL_INFO("Account flagged events: {}", oss.str());
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "event_volume"}})
        .Increment();
    alert();
  }
}

void account::record(event_cache &parent_cache, timed_event const &event) {
  _statistics.record(parent_cache, event);
}

void account::statistics::alert() {
  if (alert_needed(++_alert_count, AlertFactor)) {
    std::ostringstream oss;
    restc_cpp::SerializeToJson(*this, oss);
    REL_INFO("Account flagged alerts: {}", oss.str());
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "alerts"}})
        .Increment();
  }
}

void account::statistics::post(atproto::at_uri const &) {
  if (alert_needed(++_posts, PostFactor)) {
    REL_INFO("Account flagged posts {} {}", _did, _posts);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "posts"}})
        .Increment();
    alert();
  }
}

void account::statistics::replied_to() {
  if (alert_needed(++_replied_to, RepliedToFactor)) {
    REL_INFO("Account flagged replied-to {} {}", _did, _replied_to);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "replied_to"}})
        .Increment();
    alert();
  }
}
void account::statistics::reply() {
  if (alert_needed(++_replies, ReplyFactor)) {
    REL_INFO("Account flagged replies {} {}", _did, _replies);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "replies"}})
        .Increment();
    alert();
  }
}
void account::statistics::quoted() {
  if (alert_needed(++_quoted, QuotedFactor)) {
    REL_INFO("Account flagged quoted {} {}", _did, _quoted);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "quoted"}})
        .Increment();
    alert();
  }
}
void account::statistics::quote() {
  if (alert_needed(++_quotes, QuoteFactor)) {
    REL_INFO("Account flagged quotes {} {}", _did, _quotes);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "quotes"}})
        .Increment();
    alert();
  }
}
void account::statistics::reposted() {
  ++_reposted;
  if (alert_needed(++_reposted, RepostedFactor)) {
    REL_INFO("Account flagged reposted {} {}", _did, _reposted);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "reposted"}})
        .Increment();
    alert();
  }
}
void account::statistics::repost() {
  if (alert_needed(++_reposts, RepostFactor)) {
    REL_INFO("Account flagged reposts {} {}", _did, _reposts);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "reposts"}})
        .Increment();
    alert();
  }
}
void account::statistics::liked() {
  if (alert_needed(++_liked, LikedFactor)) {
    REL_INFO("Account flagged liked {} {}", _did, _liked);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "liked"}})
        .Increment();
    alert();
  }
}
void account::statistics::like() {
  if (alert_needed(++_likes, LikeFactor)) {
    REL_INFO("Account flagged likes {} {}", _did, _likes);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "likes"}})
        .Increment();
    alert();
  }
}

void account::cache_content_item(atproto::at_uri const &uri) {
  _content_hits->Put(uri, {});
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"cached_items", "content"}})
      .Increment();
}

// Callback for tracked account removal
void account::on_erase(atproto::at_uri const &uri,
                       caches::WrappedValue<content_hit_count> const &entry) {
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"cached_items", "content"}})
      .Decrement();
  size_t alerts(entry->alerts());
  if (alerts > 0) {
    REL_INFO("Content-item evicted {} with {} alerts {} events",
             std::string(uri), alerts, entry->hits());
    // TODO analyze evicted record and report via log file if it is of
    // interest
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "content_evictions"}, {"state", "flagged"}})
        .Increment();
  } else {
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "content_evictions"}, {"state", "clean"}})
        .Increment();
  }
}

caches::WrappedValue<content_hit_count>
account::get_content_hits(atproto::at_uri const &uri) {
  if (!_content_hits->Cached(uri)) {
    cache_content_item(uri);
  }
  return _content_hits->Get(uri);
}

caches::WrappedValue<content_hit_count>
account::get_content_item(const atproto::at_uri &uri) {
  caches::WrappedValue<content_hit_count> content_hits(get_content_hits(uri));
  content_hits->hit();
  return content_hits;
}

// toxic string filter matches, flag verbose accounts
void account::statistics::add_matches(const unsigned short matches) {
  size_t old_matches(_matches);
  _matches += matches;
  if ((old_matches == 0) ||
      (old_matches / MatchFactor != _matches / MatchFactor)) {
    REL_INFO("Account flagged matches {} {}", _did, _matches);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "match_alert"}})
        .Increment();
    alert();
  }
}

// account-level updates - flag if frequent
void account::statistics::updated() {
  size_t old_updates(_updates);
  ++_updates;
  if (old_updates / UpdateFactor != _updates / UpdateFactor) {
    REL_INFO("Account flagged updates {} {} profile={}, handle={}, "
             "(in)activation={}, active-state={}",
             _did, _updates, _profiles, _handles, _activations,
             to_string(_state));
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "updates"}})
        .Increment();
    alert();
  }
}
void account::statistics::activation(const bool active) {
  _state = active ? state::active : state::inactive;
  size_t old_activations(_activations);
  ++_activations;
  if (old_activations / UpdateFactor != _activations / UpdateFactor) {
    REL_INFO("Account flagged activations {} {}", _did, _activations);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "activations"}})
        .Increment();
    alert();
  }
  updated();
}
void account::statistics::handle() {
  size_t old_handles(_handles);
  ++_handles;
  if (old_handles / UpdateFactor != _handles / UpdateFactor) {
    REL_INFO("Account flagged handles {} {}", _did, _handles);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "handles"}})
        .Increment();
    alert();
  }
  updated();
}
void account::statistics::profile() {
  size_t old_profiles(_profiles);
  ++_profiles;
  if (old_profiles / UpdateFactor != _profiles / UpdateFactor) {
    REL_INFO("Account flagged profiles {} {}", _did, _profiles);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "profiles"}})
        .Increment();
    alert();
  }
  updated();
}

// TODO unwind content in the account's cache that gets deleted
void account::statistics::deleted(std::string const &path) {
  if (starts_with(path, bsky::AppBskyFeedLike)) {
    ++_unlikes;
  } else if (starts_with(path, bsky::AppBskyFeedPost)) {
    ++_unposts;
  } else if (starts_with(path, bsky::AppBskyFeedRepost)) {
    ++_unreposts;
  } else if (starts_with(path, bsky::AppBskyGraphBlock)) {
    ++_unblocks;
  } else if (starts_with(path, bsky::AppBskyGraphFollow)) {
    ++_unfollows;
  } else {
    // other collections not handled
    return;
  }
  size_t deletes(_unlikes + _unposts + _unreposts + _unblocks + _unfollows);
  if ((deletes - 1) / DeleteFactor != deletes / DeleteFactor) {
    REL_INFO("Account flagged deletes {} {} likes {} posts {} reposts {} "
             "blocks {} follows",
             _did, _unlikes, _unposts, _unreposts, _unblocks, _unfollows);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "deletes"}})
        .Increment();
    alert();
  }
}

void account::statistics::blocks() {
  if (alert_needed(++_blocks, BlocksFactor)) {
    REL_INFO("Account flagged blocks {} {}", _did, _blocks);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "blocks"}})
        .Increment();
    alert();
  }
}
void account::statistics::blocked_by() {
  if (alert_needed(++_blocked_by, BlockedByFactor)) {
    REL_INFO("Account flagged blocked-by {} {}", _did, _blocked_by);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "blocked_by"}})
        .Increment();
    alert();
  }
}
void account::statistics::follows() {
  if (alert_needed(++_follows, FollowsFactor)) {
    REL_INFO("Account flagged follows {} {}", _did, _follows);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "follows"}})
        .Increment();
    alert();
  }
}
void account::statistics::followed_by() {
  if (alert_needed(++_followed_by, FollowedByFactor)) {
    REL_INFO("Account flagged followed-by {} {}", _did, _followed_by);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "followed_by"}})
        .Increment();
    alert();
  }
}

augment_account_event::augment_account_event(event_cache &cache,
                                             account::statistics &stats)
    : _stats(stats), _cache(cache) {}

void augment_account_event::augment_account_event::operator()(
    activity::post const &value) {
  _stats.post(atproto::make_at_uri(_stats._did, value._ref));
}

void augment_account_event::augment_account_event::operator()(
    activity::reply const &value) {
  // record interactions with parent/root
  reply_to(value._parent);
  reply_to(value._root);
  _stats.reply();
}
void augment_account_event::augment_account_event::operator()(
    activity::repost const &value) {
  auto post_account(_cache.get_account(value._post._authority));
  post_account->get_statistics().reposted();
  auto content(post_account->get_content_item(value._post));
  if (alert_needed(++content->_reposts, account::ContentRepostFactor)) {
    content->alert();
    REL_INFO("Account flagged content-reposts {} {}", value._post._authority,
             content->_reposts);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "content-reposts"}})
        .Increment();
    _stats.alert();
  }
  _stats.repost();
}
void augment_account_event::augment_account_event::operator()(
    activity::quote const &value) {
  auto post_account(_cache.get_account(value._post._authority));
  post_account->get_statistics().quoted();
  auto content(post_account->get_content_item(value._post));
  if (alert_needed(++content->_quotes, account::ContentQuoteFactor)) {
    content->alert();
    REL_INFO("Account flagged content-quotes {} {}", value._post._authority,
             content->_quotes);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "content-quotes"}})
        .Increment();
    _stats.alert();
  }
  _stats.quote();
}

void augment_account_event::augment_account_event::operator()(
    activity::block const &value) {
  _stats.blocks();
  auto target(_cache.get_account(value._blocked));
  target->get_statistics().blocked_by();
  // report and label if account blocked moderation service
  if (value._blocked ==
      bsky::moderation::report_agent::instance().service_did()) {
    bsky::moderation::report_agent::instance().wait_enqueue(
        bsky::moderation::account_report(
            _stats._did, bsky::moderation::blocks_moderation()));
  }
}
void augment_account_event::augment_account_event::operator()(
    activity::follow const &value) {
  _stats.follows();
  auto target(_cache.get_account(value._followed));
  target->get_statistics().followed_by();
}

void augment_account_event::augment_account_event::operator()(
    activity::like const &value) {
  auto liked_account(_cache.get_account(value._content._authority));
  liked_account->get_statistics().liked();
  auto content(liked_account->get_content_item(value._content));
  if (alert_needed(++content->_likes, account::ContentLikeFactor)) {
    content->alert();
    REL_INFO("Account flagged content-likes {} {}", value._content._authority,
             content->_likes);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "content-likes"}})
        .Increment();
    _stats.alert();
  }
  _stats.like();
}

void augment_account_event::augment_account_event::operator()(
    activity::active const &) {
  _stats.activation(true);
}
void augment_account_event::augment_account_event::operator()(
    activity::handle const &) {
  _stats.handle();
}
void augment_account_event::augment_account_event::operator()(
    activity::inactive const &) {
  _stats.activation(false);
}
void augment_account_event::augment_account_event::operator()(
    activity::profile const &) {
  _stats.profile();
}

void augment_account_event::augment_account_event::operator()(
    activity::deleted const &value) {
  _stats.deleted(value._path);
}

void augment_account_event::augment_account_event::operator()(
    activity::matches const &value) {
  _stats.add_matches(value._count);
}

void augment_account_event::operator()(activity::facets const &value) {
  if (value._tags > 0) {
    _stats.tags(value._tags);
  }
  if (value._links > 0) {
    _stats.links(value._links);
  }
  if (value._mentions > 0) {
    _stats.links(value._mentions);
  }
  _stats.facets(value._tags + value._mentions + value._links);
}

void augment_account_event::augment_account_event::reply_to(
    atproto::at_uri const &uri) {
  auto account(_cache.get_account(uri._authority));
  account->get_statistics().replied_to();
  auto content(account->get_content_item(uri));
  if (alert_needed(++content->_replies, account::ContentReplyFactor)) {
    content->alert();
    REL_INFO("Account flagged content-replies {} {}", uri._authority,
             content->_replies);
    metrics_factory::instance()
        .get_counter("realtime_alerts")
        .Get({{"account", "content-replies"}})
        .Increment();
    account->get_statistics().alert();
  }
}

} // namespace activity
