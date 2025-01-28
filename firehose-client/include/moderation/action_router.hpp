#ifndef __action_router__
#define __action_router__
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
#include "blockingconcurrentqueue.h"
#include "helpers.hpp"
#include "matcher.hpp"
#include "yaml-cpp/yaml.h"
#include <thread>

class action_router {
public:
  static constexpr size_t QueueLimit = 1000;

  static action_router &instance();

  void start();
  void wait_enqueue(account_filter_matches &&value);

private:
  action_router();
  ~action_router() = default;

  std::thread _thread;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<account_filter_matches> _queue;
  std::shared_ptr<matcher> _matcher;
};
#endif