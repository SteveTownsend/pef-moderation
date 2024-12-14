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
#include <thread>

constexpr size_t QueueLimit = 10000;

struct payload {
  std::string _json_msg;
  match_results _matches;
};

template <typename T> class post_processor {
public:
  post_processor()
      : _queue(QueueLimit),
        _matched_elements(metrics::instance().add_counter(
            "message_field_matches",
            "Number of matches within each field of message")) {
    _thread = std::thread([&] {
      static size_t matches(0);
      while (true) {
        T my_payload;
        _queue.wait_dequeue(my_payload);
        // Publish metrics for matches
        for (auto &result : my_payload._matches) {
          // this is the substring of the full JSON that matched one or more
          // desired strings
          REL_INFO("Candidate {}/{}/{}\nmatches {}\non message:{}",
                   result._candidate._type, result._candidate._field,
                   result._candidate._value, result._matches,
                   my_payload._json_msg);
          for (auto const &match : result._matches) {
            prometheus::Labels labels(
                {{"type", result._candidate._type},
                 {"field", result._candidate._field},
                 {"filter", wstring_to_utf8(match.get_keyword())}});
            _matched_elements.Get(labels).Increment();
          }
        }
        // TODO auto-report if rule indicates, ignoring dups
        // TODO avoid spam for automated posts

        // TODO terminate gracefully
      }
    });
  }
  ~post_processor() = default;
  void wait_enqueue(T &&value) { _queue.wait_enqueue(value); }

private:
  // Declare queue between websocket and match post-processing
  moodycamel::BlockingReaderWriterCircularBuffer<T> _queue;
  std::thread _thread;
  // Cardinality =
  //   (number of rules) times (number of elements - profile/post -
  //                            times number of fields per element)
  prometheus::Family<prometheus::Counter> &_matched_elements;
};

#endif