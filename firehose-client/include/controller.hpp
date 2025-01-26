#ifndef __controller_hpp__
#define __controller_hpp__
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

#include "config.hpp"
#include "log_wrapper.hpp"
#include <atomic>

class controller {
public:
  inline static controller &instance() {
    static controller my_controller;
    return my_controller;
  }
  inline void set_config(std::shared_ptr<config> &settings) {
    _settings = settings;
  }
  inline void start() {
    _active = true;
    std::set_terminate(&on_terminate);
  }
  inline bool is_active() const { return _active; }
  inline void force_stop() {
    _active = false;
    REL_CRITICAL("controller shutdown requested");
  }

  inline static void on_terminate() {
    REL_CRITICAL("Controller terminating");
    std::abort();
  }

private:
  std::atomic<bool> _active = false;
  std::shared_ptr<config> _settings;
};

#endif