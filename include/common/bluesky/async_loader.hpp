#pragma once
/*************************************************************************
Public Education Forum Moderation Firehose Client
Copyright (c) Steve Townsend 2025

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
#include "common/bluesky/client.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "common/rest_utils.hpp"
#include "readerwriterqueue.h"

#include <thread>

namespace bsky {

class async_loader {
public:
  // aloow load spike during startup
  static constexpr size_t MaxBacklog = 250000;
  async_loader();
  static inline async_loader &instance() {
    static async_loader loader;
    return loader;
  }

  void start(YAML::Node const &settings);
  void wait_enqueue(std::string &&value);

private:
  ~async_loader() = default;
  // Use queue to buffer incoming requests for bsky API data
  moodycamel::BlockingReaderWriterQueue<std::string> _queue;
  std::thread _thread;
  std::unique_ptr<client> _appview_client;
};

} // namespace bsky
