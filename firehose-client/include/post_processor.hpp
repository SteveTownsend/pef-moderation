#ifndef __verifier_hpp__
#define __verifier_hpp__
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
#include "log_wrapper.hpp"
#include "matcher.hpp"
#include "metrics.hpp"
#include "queue/readerwritercircularbuffer.h"
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <thread>

constexpr size_t QueueLimit = 10000;

namespace firehose {

enum class op { error = -1, message = 1 };

enum class op_type {
  account = 1,
  commit,
  handle,
  identity,
  info,
  migrate,
  tombstone,
  invalid = -1
};

constexpr std::string_view OpTypeAccount = "#account";
constexpr std::string_view OpTypeCommit = "#commit";
constexpr std::string_view OpTypeHandle = "#handle";
constexpr std::string_view OpTypeIdentity = "#identity";
constexpr std::string_view OpTypeInfo = "#info";
constexpr std::string_view OpTypeMigrate = "#migrate";
constexpr std::string_view OpTypeTombstone = "#tombstone";

inline op_type op_type_from_string(std::string const &op_type_str) {
  if (op_type_str == OpTypeAccount) {
    return op_type::account;
  } else if (op_type_str == OpTypeCommit) {
    return op_type::commit;
  } else if (op_type_str == OpTypeHandle) {
    return op_type::handle;
  } else if (op_type_str == OpTypeIdentity) {
    return op_type::identity;
  } else if (op_type_str == OpTypeInfo) {
    return op_type::info;
  } else if (op_type_str == OpTypeMigrate) {
    return op_type::migrate;
  } else if (op_type_str == OpTypeTombstone) {
    return op_type::tombstone;
  }
  return op_type::invalid;
}

enum class op_kind { create = 1, delete_, update, invalid = -1 };

constexpr std::string_view OpKindCreate = "create";
constexpr std::string_view OpKindDelete = "delete";
constexpr std::string_view OpKindUpdate = "update";

inline op_kind op_kind_from_string(std::string const &op_kind_str) {
  if (op_kind_str == OpKindCreate) {
    return op_kind::create;
  } else if (op_kind_str == OpKindDelete) {
    return op_kind::delete_;
  } else if (op_kind_str == OpKindUpdate) {
    return op_kind::update;
  }
  return op_kind::invalid;
}

} // namespace firehose

template <typename T> class post_processor {
public:
  post_processor()
      : _queue(QueueLimit),
        _matched_elements(metrics::instance().add_counter(
            "message_field_matches",
            "Number of matches within each field of message")),
        _firehose_stats(metrics::instance().add_counter(
            "firehose", "Statistics about received firehose data")),
        _firehose_facets(metrics::instance().add_histogram(
            "firehose_facets", "Statistics about received firehose facets")),
        _operational_stats(metrics::instance().add_gauge(
            "operational_stats", "Statistics about client internals")) {
    // Histogram metrics have to be added by hand, on-deman instantiation is not
    // possible
    prometheus::Histogram::BucketBoundaries boundaries = {
        0.0,  1.0,  2.0,  3.0,  4.0,  5.0,  6.0,  7.0,  8.0,
        9.0,  10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0,
        18.0, 19.0, 20.0, 21.0, 22.0, 23.0, 24.0, 25.0};
    _firehose_facets.Add(
        {{"facet", std::string(bsky::AppBskyRichtextFacetLink)}}, boundaries);
    _firehose_facets.Add(
        {{"facet", std::string(bsky::AppBskyRichtextFacetMention)}},
        boundaries);
    _firehose_facets.Add(
        {{"facet", std::string(bsky::AppBskyRichtextFacetTag)}}, boundaries);
    _firehose_facets.Add({{"facet", "total"}}, boundaries);
    _thread = std::thread([&] {
      static size_t matches(0);
      while (true) {
        T my_payload;
        _queue.wait_dequeue(my_payload);
        _operational_stats.Get({{"queue", "backlog"}}).Decrement();

        my_payload.handle(*this);
        // TODO auto-report if rule indicates, ignoring dups
        // TODO avoid spam for automated posts

        // TODO terminate gracefully
      }
    });
  }
  ~post_processor() = default;
  void wait_enqueue(T &&value) {
    _queue.wait_enqueue(value);
    _operational_stats.Get({{"queue", "backlog"}}).Increment();
  }
  inline prometheus::Family<prometheus::Counter> &metrics() {
    return _matched_elements;
  }
  inline prometheus::Family<prometheus::Counter> &firehose_stats() {
    return _firehose_stats;
  }
  inline prometheus::Family<prometheus::Histogram> &firehose_facets() {
    return _firehose_facets;
  }
  inline prometheus::Family<prometheus::Gauge> &operational_stats() {
    return _operational_stats;
  }
  inline void set_matcher(std::shared_ptr<matcher> my_matcher) {
    _matcher = my_matcher;
  }
  inline matcher &get_matcher() { return *_matcher; }

private:
  // Declare queue between websocket and match post-processing
  moodycamel::BlockingReaderWriterCircularBuffer<T> _queue;
  std::thread _thread;
  // Cardinality =
  //   (number of rules) times (number of elements - profile/post -
  //                            times number of fields per element)
  prometheus::Family<prometheus::Counter> &_matched_elements;
  prometheus::Family<prometheus::Counter> &_firehose_stats;
  prometheus::Family<prometheus::Gauge> &_operational_stats;
  prometheus::Family<prometheus::Histogram> &_firehose_facets;
  std::shared_ptr<matcher> _matcher;
};

class jetstream_payload {
public:
  jetstream_payload();
  jetstream_payload(std::string json_msg, match_results matches);
  void handle(post_processor<jetstream_payload> &processor);

private:
  std::string _json_msg;
  match_results _matches;
};

class firehose_payload {
public:
  firehose_payload();
  firehose_payload(parser &my_parser);
  void handle(post_processor<firehose_payload> &processor);

private:
  void handle_content(post_processor<firehose_payload> &processor,
                      nlohmann::json const &content);

  parser _parser;
  candidate_list _candidates;
};

#endif