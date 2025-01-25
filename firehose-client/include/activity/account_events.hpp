#ifndef __account_events_hpp__
#define __account_events_hpp__
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

#include "helpers.hpp"
#include <cache.hpp>
#include <chrono>
#include <deque>
#include <lfu_cache_policy.hpp>
#include <string>
#include <unordered_map>
#include <variant>

namespace activity {
class event_cache;

typedef std::string did_type;

struct post {
  std::string _ref;
};
struct reply {
  std::string _reply;
  atproto::at_uri _root;
  atproto::at_uri _parent;
};
struct repost {
  std::string _repost;
  atproto::at_uri _post;
};
struct quote {
  std::string _quote;
  atproto::at_uri _post;
};
struct follow {
  std::string _follow;
  std::string _followed;
};
struct block {
  std::string _block;
  std::string _blocked;
};
struct like {
  std::string _like;
  atproto::at_uri _content;
};
struct active {};
struct inactive {
  bsky::down_reason _reason;
};
struct handle {
  std::string _handle;
};
struct profile {
  std::string _profile;
};
struct matches {
  unsigned short _count;
};
struct facets {
  unsigned short _tags;
  unsigned short _mentions;
  unsigned short _links;
};
typedef std::variant<post, reply, repost, quote, follow, block, like, active,
                     inactive, handle, profile, matches, facets>
    event;
struct timed_event {
  inline timed_event() : _event(active()) {}
  inline timed_event(const did_type &did, bsky::time_stamp created_at,
                     event &&this_event)
      : _did(did), _created_at(created_at), _event(std::move(this_event)) {}
  inline timed_event(const timed_event &event)
      : _did(event._did), _created_at(event._created_at), _event(event._event) {
  }
  inline timed_event &operator=(const timed_event &event) {
    _did = event._did;
    _created_at = event._created_at;
    _event = event._event;
    return *this;
  }
  inline timed_event(timed_event &&event)
      : _did(std::move(event._did)), _created_at(std::move(event._created_at)),
        _event(std::move(event._event)) {}

  did_type _did;
  bsky::time_stamp _created_at;
  event _event;
};
typedef std::deque<timed_event> events;

// evict LFU content-items to mitigate unbounded memory growth
// See https://github.com/SteveTownsend/nafo-forum-moderation/issues/82
constexpr size_t MaxContentItems = 25;
struct content_hit_count {
  int32_t _likes = 0;
  int32_t _reposts = 0;
  int32_t _quotes = 0;
  int32_t _replies = 0;
  size_t _alerts = 0;
  size_t _hits = 0;
  inline void alert() { ++_alerts; }
  inline size_t alerts() const { return _alerts; }
  inline void hit() { ++_hits; }
  inline size_t hits() const { return _hits; }
};
typedef std::unordered_map<atproto::at_uri,
                           caches::WrappedValue<content_hit_count>,
                           atproto::at_uri_hash>
    content_hits;

// cache policy for Key with custom hash
template <typename Key>
class CustomLFUCachePolicy : public caches::ICachePolicy<Key> {
public:
  using lfu_iterator = typename std::multimap<std::size_t, Key>::iterator;

  CustomLFUCachePolicy() = default;
  ~CustomLFUCachePolicy() override = default;

  void Insert(const Key &key) override {
    constexpr std::size_t INIT_VAL = 1;
    // all new value initialized with the frequency 1
    lfu_storage[key] = frequency_storage.emplace_hint(
        frequency_storage.cbegin(), INIT_VAL, key);
  }

  void Touch(const Key &key) override {
    // get the previous frequency value of a key
    auto elem_for_update = lfu_storage[key];
    auto updated_elem =
        std::make_pair(elem_for_update->first + 1, elem_for_update->second);
    // update the previous value
    frequency_storage.erase(elem_for_update);
    lfu_storage[key] = frequency_storage.emplace_hint(frequency_storage.cend(),
                                                      std::move(updated_elem));
  }

  void Erase(const Key &key) noexcept override {
    frequency_storage.erase(lfu_storage[key]);
    lfu_storage.erase(key);
  }

  const Key &ReplCandidate() const noexcept override {
    // at the beginning of the frequency_storage we have the
    // least frequency used value
    return frequency_storage.cbegin()->second;
  }

private:
  std::multimap<std::size_t, Key> frequency_storage;
  std::unordered_map<Key, lfu_iterator, atproto::at_uri_hash> lfu_storage;
};

template <typename Key, typename Value>
using lfu_cache_at_uri_t =
    typename caches::fixed_sized_cache<Key, Value, CustomLFUCachePolicy,
                                       content_hits>;
class account {
public:
  enum class state { unknown, active, inactive };
  static inline std::string to_string(state my_state) {
    switch (my_state) {
    case state::active:
      return "active";
    case state::inactive:
      return "inactive";
    case state::unknown:
    default:
      return "unknown";
    }
  }
  // per-post facet abuse thresholds - hashtag, links, mentions, total
  // See https://github.com/SteveTownsend/nafo-forum-moderation/issues/75
  // 99.9% threshold based on observed metrics
  static constexpr size_t TagFacetThreshold = 32;
  static constexpr size_t LinkFacetThreshold = 10;
  static constexpr size_t MentionFacetThreshold = 11;
  static constexpr size_t TotalFacetThreshold = 29;
  // allow occasional verbosity in facets
  static constexpr size_t FacetFactor = 10;

  // output a log every few events to highlight frequent activity
  static constexpr size_t AlertFactor = 10; // all alerts for the account
  static constexpr size_t PostFactor = 25;

  // track content interactions at account and content-item level
  static constexpr size_t RepliedToFactor = 50;
  static constexpr size_t QuotedFactor = 50;
  static constexpr size_t RepostedFactor = 100;
  static constexpr size_t LikedFactor = 500;

  static constexpr size_t ReplyFactor = 15;
  static constexpr size_t QuoteFactor = 15;
  static constexpr size_t RepostFactor = 25;
  static constexpr size_t LikeFactor = 100;

  static constexpr size_t ContentReplyFactor = 10;
  static constexpr size_t ContentQuoteFactor = 10;
  static constexpr size_t ContentRepostFactor = 20;
  static constexpr size_t ContentLikeFactor = 80;

  // track follows both ways
  static constexpr size_t FollowsFactor = 500;
  static constexpr size_t FollowedByFactor = 125;
  // track blocks both ways
  static constexpr size_t BlocksFactor = 50;
  static constexpr size_t BlockedByFactor = 25;
  // track account-level updates, total and individual buckets
  static constexpr size_t UpdateFactor = 10;
  // output a log every few matches to highlight suspect activity
  static constexpr size_t MatchFactor = 5;

  account(did_type const &did);

  inline std::string did() const { return _did; }

  void tags(const size_t count);
  void links(const size_t count);
  void mentions(const size_t count);
  void facets(const size_t count);

  void record(event_cache &parent_cache, timed_event const &event);
  inline size_t event_count() const { return _event_count; }
  void alert();
  inline size_t alert_count() const { return _alert_count; }

  void post(atproto::at_uri const &uri);
  inline int32_t posts() const { return _posts; }

  void replied_to();
  void reply_to(atproto::at_uri const &uri);
  void reply();
  void quoted();
  void quote();
  void reposted();
  void repost();
  void liked();
  void like();

  void follows();
  void followed_by();
  void blocks();
  void blocked_by();

  void updated();
  void activation(const bool active);
  void profile();
  void handle();

  void add_matches(const unsigned short matches);
  size_t matches() const { return _matches; }

  caches::WrappedValue<content_hit_count>
  get_content_item(atproto::at_uri const &uri);
  // Callback on LFU cache eviction
  void on_erase(atproto::at_uri const &uri,
                caches::WrappedValue<content_hit_count> const &entry);

private:
  caches::WrappedValue<content_hit_count>
  get_content_hits(atproto::at_uri const &uri);
  void cache_content_item(atproto::at_uri const &uri);

  std::string _did;
  size_t _event_count = 0;
  size_t _alert_count = 0;

  // facet abuse
  size_t _tags = 0;
  size_t _links = 0;
  size_t _mentions = 0;
  size_t _facets = 0;

  // content interactions may have a negative count
  int32_t _posts = 0;
  // TODO might be better to indirect to event_cache
  std::shared_ptr<lfu_cache_at_uri_t<atproto::at_uri, content_hit_count>>
      _content_hits;

  // these may go negative, depending on the state of the account when recorded,
  // and subsequent events
  int32_t _replied_to = 0;
  int32_t _replies = 0;
  int32_t _quoted = 0;
  int32_t _quotes = 0;
  int32_t _reposted = 0;
  int32_t _reposts = 0;
  int32_t _liked = 0;
  int32_t _likes = 0;

  int32_t _follows = 0;
  int32_t _followed_by = 0;
  int32_t _blocks = 0;
  int32_t _blocked_by = 0;

  state _state = state::unknown;
  unsigned short _updates = 0;
  unsigned short _activations = 0;
  unsigned short _profiles = 0;
  unsigned short _handles = 0;
  unsigned short _matches = 0;
};

// visitor for account-specific logic
struct augment_account_event {
  augment_account_event(event_cache &cache, account &account);
  template <typename T> void operator()(T const &value) {}

  void operator()(activity::post const &value);
  void operator()(activity::reply const &value);
  void operator()(activity::repost const &value);
  void operator()(activity::quote const &value);

  void operator()(activity::block const &value);
  void operator()(activity::follow const &value);

  void operator()(activity::like const &value);

  void operator()(activity::active const &value);
  void operator()(activity::handle const &value);
  void operator()(activity::inactive const &value);
  void operator()(activity::profile const &value);

  void operator()(activity::matches const &value);

  void operator()(activity::facets const &value);

private:
  void reply_to(atproto::at_uri const &uri);

  account &_account;
  event_cache &_cache;
};

} // namespace activity

#endif