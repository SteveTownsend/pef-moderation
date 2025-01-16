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

#include "activity/account_events.hpp"
#include "activity/event_cache.hpp"
#include "metrics.hpp"
#include <algorithm>

namespace activity {

account::account(did_type const &did)
    : _did(did),
      _content_hits(std::make_shared<
                    lfu_cache_at_uri_t<atproto::at_uri, content_hit_count>>(
          MaxContentItems, CustomLFUCachePolicy<atproto::at_uri>(),
          std::function<void(atproto::at_uri const &,
                             std::shared_ptr<content_hit_count> const &)>(
              std::bind(&account::on_erase, this, std::placeholders::_1,
                        std::placeholders::_2)))) {}

void account::tags(const size_t) {
  if (bsky::alert_needed(++_tags, FacetFactor)) {
    REL_INFO("Account flagged tag-facets {} {}", _did, _tags);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "tag_facets"}})
        .Increment();
    ++_alert_count;
  }
}
void account::links(const size_t) {
  if (bsky::alert_needed(++_links, FacetFactor)) {
    REL_INFO("Account flagged link-facets {} {}", _did, _links);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "link_facets"}})
        .Increment();
    ++_alert_count;
  }
}
void account::mentions(const size_t) {
  if (bsky::alert_needed(++_mentions, FacetFactor)) {
    REL_INFO("Account flagged mention-facets {} {}", _did, _mentions);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "mention_facets"}})
        .Increment();
    ++_alert_count;
  }
}
void account::facets(const size_t) {
  if (bsky::alert_needed(++_facets, FacetFactor)) {
    REL_INFO("Account flagged total-facets {} {}", _did, _facets);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "all_facets"}})
        .Increment();
    ++_alert_count;
  }
}

void account::record(event_cache &parent_cache, timed_event const &event) {
  ++_event_count;
  std::visit(augment_account_event(parent_cache, *this), event._event);
}

void account::post(atproto::at_uri const &) {
  if (bsky::alert_needed(++_posts, PostFactor)) {
    REL_INFO("Account flagged posts {} {}", _did, _posts);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "posts"}})
        .Increment();
    ++_alert_count;
  }
}

void account::replied_to() {
  if (bsky::alert_needed(++_replied_to, RepliedToFactor)) {
    REL_INFO("Account flagged replied-to {} {}", _did, _replied_to);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "replied_to"}})
        .Increment();
    ++_alert_count;
  }
}
void account::reply() {
  if (bsky::alert_needed(++_replies, ReplyFactor)) {
    REL_INFO("Account flagged replies {} {}", _did, _replies);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "replies"}})
        .Increment();
    ++_alert_count;
  }
}
void account::quoted() {
  if (bsky::alert_needed(++_quoted, QuotedFactor)) {
    REL_INFO("Account flagged quoted {} {}", _did, _quoted);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "quoted"}})
        .Increment();
    ++_alert_count;
  }
}
void account::quote() {
  if (bsky::alert_needed(++_quotes, QuoteFactor)) {
    REL_INFO("Account flagged quotes {} {}", _did, _quotes);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "quotes"}})
        .Increment();
    ++_alert_count;
  }
}
void account::reposted() {
  ++_reposted;
  if (bsky::alert_needed(++_reposted, RepostedFactor)) {
    REL_INFO("Account flagged reposted {} {}", _did, _reposted);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "reposted"}})
        .Increment();
    ++_alert_count;
  }
}
void account::repost() {
  if (bsky::alert_needed(++_reposts, RepostFactor)) {
    REL_INFO("Account flagged reposts {} {}", _did, _reposts);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "reposts"}})
        .Increment();
    ++_alert_count;
  }
}
void account::liked() {
  if (bsky::alert_needed(++_liked, LikedFactor)) {
    REL_INFO("Account flagged liked {} {}", _did, _liked);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "liked"}})
        .Increment();
    ++_alert_count;
  }
}
void account::like() {
  if (bsky::alert_needed(++_likes, LikeFactor)) {
    REL_INFO("Account flagged likes {} {}", _did, _likes);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "likes"}})
        .Increment();
    ++_alert_count;
  }
}

void account::cache_content_item(atproto::at_uri const &uri) {
  _content_hits->Put(uri, {});
  metrics::instance()
      .operational_stats()
      .Get({{"cached_items", "content"}})
      .Increment();
}

// Callback for tracked account removal
void account::on_erase(atproto::at_uri const &uri,
                       caches::WrappedValue<content_hit_count> const &entry) {
  metrics::instance()
      .operational_stats()
      .Get({{"cached_items", "content"}})
      .Decrement();
  size_t alerts(entry->alerts());
  if (alerts > 0) {
    REL_INFO("Content-item evicted {} with {} alerts {} events",
             std::string(uri), alerts, entry->hits());
    // TODO analyze evicted record and report via log file if it is of interest
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "content_evictions"}, {"state", "flagged"}})
        .Increment();
  } else {
    metrics::instance()
        .realtime_alerts()
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
void account::add_matches(const size_t matches) {
  size_t old_matches(_matches);
  _matches += matches;
  if ((old_matches == 0) ||
      (old_matches / MatchFactor != _matches / MatchFactor)) {
    REL_INFO("Account flagged matches {} {}", _did, _matches);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "match_alert"}})
        .Increment();
    ++_alert_count;
  }
}

// account-level updates - flag if frequent
void account::updated() {
  size_t old_updates(_updates);
  ++_updates;
  if (old_updates / UpdateFactor != _updates / UpdateFactor) {
    REL_INFO("Account flagged updates {} {}", _did, _updates);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "updates"}})
        .Increment();
    ++_alert_count;
  }
}

void account::blocks() {
  if (bsky::alert_needed(++_blocks, BlocksFactor)) {
    REL_INFO("Account flagged blocks {} {}", _did, _blocks);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "blocks"}})
        .Increment();
    ++_alert_count;
  }
}
void account::blocked_by() {
  if (bsky::alert_needed(++_blocked_by, BlockedByFactor)) {
    REL_INFO("Account flagged blocked-by {} {}", _did, _blocked_by);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "blocked_by"}})
        .Increment();
    ++_alert_count;
  }
}
void account::follows() {
  if (bsky::alert_needed(++_follows, FollowsFactor)) {
    REL_INFO("Account flagged follows {} {}", _did, _follows);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "follows"}})
        .Increment();
    ++_alert_count;
  }
}
void account::followed_by() {
  if (bsky::alert_needed(++_followed_by, FollowedByFactor)) {
    REL_INFO("Account flagged followed-by {} {}", _did, _followed_by);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "followed_by"}})
        .Increment();
    ++_alert_count;
  }
}

augment_account_event::augment_account_event(event_cache &cache,
                                             account &account)
    : _account(account), _cache(cache) {}

void augment_account_event::augment_account_event::operator()(
    activity::post const &value) {
  _account.post(atproto::make_at_uri(_account.did(), value._ref));
}

void augment_account_event::augment_account_event::operator()(
    activity::reply const &value) {
  // record interactions with parent/root
  reply_to(value._parent);
  reply_to(value._root);
  _account.reply();
}
void augment_account_event::augment_account_event::operator()(
    activity::repost const &value) {
  auto post_account(_cache.get_account(value._post._authority));
  post_account->reposted();
  auto content(post_account->get_content_item(value._post));
  if (bsky::alert_needed(++content->_reposts, account::ContentRepostFactor)) {
    content->alert();
    REL_INFO("Account flagged content-reposts {} {}", value._post._authority,
             content->_reposts);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "content-reposts"}})
        .Increment();
    _account.alert();
  }
  _account.repost();
}
void augment_account_event::augment_account_event::operator()(
    activity::quote const &value) {
  auto post_account(_cache.get_account(value._post._authority));
  post_account->quoted();
  auto content(post_account->get_content_item(value._post));
  if (bsky::alert_needed(++content->_quotes, account::ContentQuoteFactor)) {
    content->alert();
    REL_INFO("Account flagged content-quotes {} {}", value._post._authority,
             content->_quotes);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "content-quotes"}})
        .Increment();
    _account.alert();
  }
  _account.quote();
}

void augment_account_event::augment_account_event::operator()(
    activity::block const &value) {
  _account.blocks();
  auto target(_cache.get_account(value._blocked));
  target->blocked_by();
}
void augment_account_event::augment_account_event::operator()(
    activity::follow const &value) {
  _account.follows();
  auto target(_cache.get_account(value._followed));
  target->followed_by();
}

void augment_account_event::augment_account_event::operator()(
    activity::like const &value) {
  auto liked_account(_cache.get_account(value._content._authority));
  liked_account->liked();
  auto content(liked_account->get_content_item(value._content));
  if (bsky::alert_needed(++content->_likes, account::ContentLikeFactor)) {
    content->alert();
    REL_INFO("Account flagged content-likes {} {}", value._content._authority,
             content->_likes);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "content-likes"}})
        .Increment();
    _account.alert();
  }
  _account.like();
}

void augment_account_event::augment_account_event::operator()(
    activity::active const &) {
  _account.updated();
}
void augment_account_event::augment_account_event::operator()(
    activity::handle const &) {
  _account.updated();
}
void augment_account_event::augment_account_event::operator()(
    activity::inactive const &) {
  _account.updated();
}
void augment_account_event::augment_account_event::operator()(
    activity::profile const &) {
  _account.updated();
}

void augment_account_event::augment_account_event::operator()(
    activity::matches const &value) {
  _account.add_matches(value._count);
}

void augment_account_event::operator()(activity::tags const &value) {
  _account.tags(value._count);
}
void augment_account_event::operator()(activity::links const &value) {
  _account.links(value._count);
}
void augment_account_event::operator()(activity::mentions const &value) {
  _account.mentions(value._count);
}
void augment_account_event::operator()(activity::facets const &value) {
  _account.facets(value._count);
}

void augment_account_event::augment_account_event::reply_to(
    atproto::at_uri const &uri) {
  auto account(_cache.get_account(uri._authority));
  account->replied_to();
  auto content(account->get_content_item(uri));
  if (bsky::alert_needed(++content->_replies, account::ContentReplyFactor)) {
    content->alert();
    REL_INFO("Account flagged content-replies {} {}", uri._authority,
             content->_replies);
    metrics::instance()
        .realtime_alerts()
        .Get({{"account", "content-replies"}})
        .Increment();
    account->alert();
  }
}

} // namespace activity
