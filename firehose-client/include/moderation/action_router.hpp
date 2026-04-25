#ifndef __action_router__
#define __action_router__
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
#include <thread>
#include <unordered_set>

#include "blockingconcurrentqueue.h"
#include "common/helpers.hpp"
#include "matcher.hpp"
#include "yaml-cpp/yaml.h"

constexpr std::string_view BlacklistName = "Soft_Deleted";

class action_router {
 public:
  static constexpr size_t QueueLimit = 1000;

  static action_router &instance();

  void start();
  void check_wait_enqueue(std::string const &did,
                          account_filter_matches &&value);
  void wait_enqueue(account_filter_matches &&value);
  void update_blacklist(std::unordered_set<std::string> new_blacklist);
  void update_whitelist(std::unordered_set<std::string> new_whitelist);
  void update_ignored(std::unordered_set<std::string> new_ignored);

 private:
  action_router();
  ~action_router() = default;

  std::thread _thread;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<account_filter_matches> _queue;
  std::shared_ptr<matcher> _matcher;
  std::unordered_set<std::string> _blacklist;
  std::unordered_set<std::string> _whitelist;
  std::unordered_set<std::string> _ignored;
  mutable std::mutex _lock;
};
#endif