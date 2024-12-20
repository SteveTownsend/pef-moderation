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
// Originally sourced from:

// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: WebSocket SSL client, coroutine
//
//------------------------------------------------------------------------------

#include "config.hpp"
#include "datasource.hpp"
#include "firehost_client_config.hpp"
#include "log_wrapper.hpp"
#include "post_processor.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char **argv) {
  bool log_ready(false);
#if _DEBUG
  std::this_thread::sleep_for(std::chrono::milliseconds(20000));
#endif
  try {
    // Check command line arguments.
    if (argc != 2) {
      std::cerr << "Usage: firehose-client <config-file-name>\n";
      // for profile and post commits:
      // subscribe?wantedCollections=app.bsky.actor.profile&wantedCollections=app.bsky.feed.post
      return EXIT_FAILURE;
    }
    std::shared_ptr<config> settings(std::make_shared<config>(argv[1]));
    metrics::instance().set_config(settings);
    parser::set_config(settings);

    std::string const log_file(
        settings->get_config()[PROJECT_NAME]["logging"]["filename"]
            .as<std::string>());
    spdlog::level::level_enum log_level(spdlog::level::from_str(
        settings->get_config()[PROJECT_NAME]["logging"]["level"]
            .as<std::string>()));
    init_logging(log_file, log_level);
    log_ready = true;

    REL_INFO("firehose_client v{}.{}.{}", PROJECT_NAME_VERSION_MAJOR,
             PROJECT_NAME_VERSION_MINOR, PROJECT_NAME_VERSION_PATCH);
    if (settings->is_full()) {
      datasource<firehose_payload>(settings).start();
    } else {
      datasource<jetstream_payload>(settings).start();
    }

    return EXIT_SUCCESS;
  } catch (std::exception const &exc) {
    if (log_ready) {
      REL_CRITICAL("Unhandled exception : {}", exc.what());
    } else {
      std::cerr << "Unhandled exception : " << exc.what() << '\n';
    }
    return EXIT_FAILURE;
  }
}
