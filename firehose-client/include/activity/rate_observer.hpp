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

#include "chrono"

namespace activity {

// after
// https://www.rdiachenko.com/posts/arch/rate-limiting/sliding-window-algorithm/
template <typename TIME_UNIT, typename COUNT_UNIT> class rate_observer {
public:
  rate_observer() = delete;
  rate_observer(TIME_UNIT const window_size, COUNT_UNIT const limit)
      : _window_size(window_size), _limit(limit) {}
  ~rate_observer() = default;
  // Calculates excess over allowed observations
  COUNT_UNIT observed_excess() {
    std::chrono::time_point<TIME_UNIT> now =
        std::chrono::time_point_cast<TIME_UNIT>(
            std::chrono::system_clock::now());
    if (now > _current_fixed_end) {
      _last_fixed_end = _current_fixed_end;
      _last_count = _current_count;
      _current_fixed_end =
          std::chrono::time_point_cast<TIME_UNIT>(now + _window_size);
      _current_count = 0;
    }
    std::chrono::time_point<TIME_UNIT> sliding_window_start =
        now - _window_size;
    float previous_window_weight =
        std::max(0, _last_fixed_end - sliding_window_start) / _window_size;
    COUNT_UNIT requests(static_cast<COUNT_UNIT>(
        std::floor(previous_window_weight * static_cast<float>(_last_count)) +
        static_cast<float>(_++ current_count)));
    return std::max(0, requests - _limit);
  }

private:
  std::chrono::time_point<TIME_UNIT> _last_fixed_end = {};
  COUNT_UNIT _last_count = 0;
  std::chrono::time_point<TIME_UNIT> _current_fixed_end =
      std::chrono::time_point_cast<TIME_UNIT>(std::chrono::system_clock::now() +
                                              _window_size);
  COUNT_UNIT _current_count = 0;
  std::chrono::duration<TIME_UNIT> _window_size;
  COUNT_UNIT _limit;
};

} // namespace activity
#endif