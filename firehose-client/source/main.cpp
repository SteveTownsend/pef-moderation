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
#include "controller.hpp"
#include "datasource.hpp"
#include "firehost_client_config.hpp"
#include "log_wrapper.hpp"
#include "moderation/action_router.hpp"
#include "moderation/embed_checker.hpp"
#include "moderation/list_manager.hpp"
#include "moderation/ozone_adapter.hpp"
#include "moderation/report_agent.hpp"
#include "parser.hpp"
#include "payload.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char **argv) {
  bool log_ready(false);
#if _DEBUG
  // std::this_thread::sleep_for(std::chrono::milliseconds(20000));
#endif
  try {
    // Check command line arguments.
    if (argc != 2) {
      std::cerr << "Usage: firehose-client <config-file-name>\n";
      // for Jetstream profile and post commits:
      // subscribe?wantedCollections=app.bsky.actor.profile&wantedCollections=app.bsky.feed.post
      return EXIT_FAILURE;
    }

    std::shared_ptr<config> settings(std::make_shared<config>(argv[1]));
    std::string const log_file(
        settings->get_config()[PROJECT_NAME]["logging"]["filename"]
            .as<std::string>());
    spdlog::level::level_enum log_level(spdlog::level::from_str(
        settings->get_config()[PROJECT_NAME]["logging"]["level"]
            .as<std::string>()));
    if (!init_logging(log_file, log_level)) {
      return EXIT_FAILURE;
    }
    log_ready = true;

    controller::instance().set_config(settings);
    controller::instance().start();

    metrics::instance().set_config(settings);
    parser::set_config(settings);

#if _DEBUG
    restc_cpp::Logger::Instance().SetLogLevel(restc_cpp::LogLevel::WARNING);
    restc_cpp::Logger::Instance().SetHandler(
        [](restc_cpp::LogLevel level, const std::string &msg) {
          static const std::array<std::string, 6> levels = {
              "NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
          // REST log filtering is decoupled from app level setting
          logger->info("REST trace {}", msg);
        });
#else
    restc_cpp::Logger::Instance().SetLogLevel(restc_cpp::LogLevel::WARNING);
    restc_cpp::Logger::Instance().SetHandler(
        [](restc_cpp::LogLevel level, const std::string &msg) {
          static const std::array<std::string, 6> levels = {
              "NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
          // REST log filtering is decoupled from app level setting
          logger->info("REST trace {}", msg);
        });
#endif
    REL_INFO("firehose_client v{}.{}.{}", PROJECT_NAME_VERSION_MAJOR,
             PROJECT_NAME_VERSION_MINOR, PROJECT_NAME_VERSION_PATCH);
    std::shared_ptr<bsky::moderation::ozone_adapter> moderation_data =
        std::make_shared<bsky::moderation::ozone_adapter>(
            settings->build_moderation_db_connection_string());
    if (settings->is_full()) {
      datasource<firehose_payload>::instance().set_config(settings);
      datasource<firehose_payload>::instance().start();

      // seed moderation monitor before we start post-processing firehose
      // messages
      moderation_data->start();

      // prepare action handlers after we start processing firehose messages
      // this is time consuming - allow a backlog for handlers while
      // existing members load
      bsky::moderation::report_agent::instance().set_config(
          settings->get_config()[PROJECT_NAME]["auto_reporter"]);
      bsky::moderation::report_agent::instance().set_moderation_data(
          moderation_data);
      bsky::moderation::report_agent::instance().start();

      action_router::instance().start();
#if _DEBUG
      // std::this_thread::sleep_for(std::chrono::milliseconds(10000000));
#endif

      bsky::moderation::embed_checker::instance().set_config(
          settings->get_config()[PROJECT_NAME]["embed_checker"]);
      bsky::moderation::embed_checker::instance().start();

      list_manager::instance().set_config(
          settings->get_config()[PROJECT_NAME]["list_manager"]);
      list_manager::instance().set_moderation_data(moderation_data);
      list_manager::instance().start();

      // continue as long as firehose runs OK
      datasource<firehose_payload>::instance().wait_for_end_thread();
    } else {
      datasource<jetstream_payload>::instance().set_config(settings);
      datasource<jetstream_payload>::instance().start();

      // continue as long as data feed runs OK
      datasource<jetstream_payload>::instance().wait_for_end_thread();
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
