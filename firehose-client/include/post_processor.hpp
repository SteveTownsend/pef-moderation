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
#include "queue/readerwritercircularbuffer.h"
#include <thread>

constexpr size_t QueueLimit = 10000;

struct payload {
  std::string _json_msg;
  matcher::match_results _matches;
};

template <typename T> class post_processor {
private:
  // Declare queue between websocket and match post-processing
  moodycamel::BlockingReaderWriterCircularBuffer<T> _queue;
  std::thread _thread;

public:
  post_processor() : _queue(QueueLimit) {
    _thread = std::thread([&] {
      static size_t matches(0);
      while (true) {
        T my_payload;
        _queue.wait_dequeue(my_payload);
        matcher::match_results updated;
        for (auto &result : my_payload._matches) {
          // this is the substring of the full JSON that matched one or more
          // desired strings
          std::string candidate(result.first);
          if (!result.second.empty()) {
            REL_INFO("Candidate {}\nmatches {}\non message:{}", result.first,
                     result.second, my_payload._json_msg);
          } else {
            REL_WARNING("False positive {}\non message:{}", result.first,
                        my_payload._json_msg);
          }
        }
        // TODO Handle the matches
        // TODO auto-report if rule indicates, ignoring dups
        // TODO avoid spam for automated posts

        // TODO terminate gracefully
      }
    });
  }
  ~post_processor() = default;
  void wait_enqueue(T &&value) { _queue.wait_enqueue(value); }
};

#endif