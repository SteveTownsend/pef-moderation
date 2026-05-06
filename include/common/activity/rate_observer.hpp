#ifndef __rate_observer_hpp__
#define __rate_observer_hpp__
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

#include <chrono>
#include <cmath>
#include <mutex>

#include "common/log_wrapper.hpp"

namespace activity {

using std::chrono::system_clock;
// after
// https://www.rdiachenko.com/posts/arch/rate-limiting/sliding-window-algorithm/
template <typename TIME_UNIT, typename COUNT_UNIT>
class rate_observer {
 public:
  rate_observer() = delete;
  rate_observer(TIME_UNIT const window_size, COUNT_UNIT const limit)
      : _window_size(
            std::chrono::duration_cast<system_clock::duration>(window_size)),
        _limit(limit) {
    _current_fixed_end = system_clock::now() + _window_size;
    _event_interval = std::chrono::duration_cast<system_clock::duration>(
        std::max(system_clock::duration(TIME_UNIT(window_size.count() / limit)),
                 system_clock::duration(1)));
    REL_INFO(
        "Initialized rate observer with window size {} and limit {}, "
        "calculated event interval {}",
        _window_size.count(), _limit, _event_interval.count());
  }
  ~rate_observer() = default;

  // Calculates excess over allowed observations, assuiming this observation is
  // added.
  COUNT_UNIT observe_and_get_excess() {
    std::lock_guard lock(_lock);
    COUNT_UNIT requests(requests_so_far() + 1);
    ++_current_count;  // record this observation
    return std::max(COUNT_UNIT(0), requests - _limit);
  }

  // Determines if current request is allowed and opens gate if so
  bool observe_if_permitted() {
    std::lock_guard lock(_lock);
    COUNT_UNIT requests(requests_so_far());
    if (requests >= _limit) {
      return false;
    } else {
      ++_current_count;  // record this observation
      return true;
    }
  }

  system_clock::duration event_interval() const { return _event_interval; }

 private:
  COUNT_UNIT requests_so_far() {
    system_clock::time_point now = system_clock::now();
    if (now > _current_fixed_end) {
      _last_fixed_end = _current_fixed_end;
      _last_count = _current_count;
      _current_fixed_end = now + _window_size;
      _current_count = 0;
    }
    system_clock::time_point sliding_window_start = now - _window_size;
    float previous_window_weight =
        std::max(0.0f, static_cast<float>(
                           (_last_fixed_end - sliding_window_start).count())) /
        static_cast<float>(_window_size.count());
    return static_cast<COUNT_UNIT>(
        std::floor((previous_window_weight * static_cast<float>(_last_count)) +
                   static_cast<float>(++_current_count)));
  }

  // storage optimized order of fields
  system_clock::time_point _last_fixed_end = {};
  system_clock::time_point _current_fixed_end;
  system_clock::duration _window_size;
  system_clock::duration _event_interval;
  COUNT_UNIT _last_count = 0;
  COUNT_UNIT _current_count = 0;
  COUNT_UNIT _limit;
  std::mutex _lock;
  static_assert(std::is_integral<COUNT_UNIT>::value,
                "COUNT_UNIT must be an integral type");
  static_assert(std::is_signed<COUNT_UNIT>::value,
                "COUNT_UNIT must be a signed type");
};

}  // namespace activity
#endif