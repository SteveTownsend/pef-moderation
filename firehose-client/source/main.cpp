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

#include "common/bluesky/async_loader.hpp"
#include "common/config.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#if defined(__GNUC__)
#include "common/activity/neo4j_adapter.hpp"
#endif
#include "common/moderation/ozone_adapter.hpp"
#include "common/moderation/report_agent.hpp"
#include "datasource.hpp"
#include "matcher.hpp"
#include "moderation/action_router.hpp"
#include "moderation/auxiliary_data.hpp"
#include "moderation/embed_checker.hpp"
#include "moderation/list_manager.hpp"
#include "parser.hpp"
#include "payload.hpp"
#include "project_defs.hpp"
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
      std::cerr << "Usage: firehose_client <config-file-name>\n";
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
    if (!init_logging(log_file, PROJECT_NAME, log_level)) {
      return EXIT_FAILURE;
    }
    log_ready = true;

    controller::instance().set_config(settings);
    controller::instance().start();

    metrics_factory::instance().set_config(settings, PROJECT_NAME);
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

    // optional graph DB for Linux only
    try {
      auto graph_data_config(
          settings->get_config()[PROJECT_NAME]["graph_data"]);
#if defined(__GNUC__)
      activity::neo4j_adapter graph_db(graph_data_config);
#else
      throw std::invalid_argument(
          "graph_data config is not supported on this platform");
#endif
    } catch (std::exception const &exc) {
      // graph DB config is optional on amy platform
      REL_INFO("No graph DB configured, returned error {}", exc.what());
    }

    if (is_full(*settings)) {
      metrics_factory::instance().add_counter(
          "automation",
          "Automated moderation activity: block-list, report, emit-event");
      metrics_factory::instance().add_counter(
          "realtime_alerts", "Alerts generated for possibly suspect activity");
      metrics_factory::instance().add_gauge(
          "process_operation", "Statistics about process internals");

      // seed database monitors before we start post-processing firehose
      // messages
      // requires poller thread
      bsky::moderation::ozone_adapter::instance().start(
          build_db_connection_string(
              settings->get_config()[PROJECT_NAME]["moderation_data"]["db"]),
          true);

      // prepare for Bluesky API calls
      bsky::async_loader::instance().start(
          settings->get_config()[PROJECT_NAME]["appview_client"]);

      // Matcher is shared by many classes. Loads from file or DB.
      matcher::shared().set_config(
          settings->get_config()[PROJECT_NAME]["filters"]);

      // seeds matcher with rules
      bsky::moderation::auxiliary_data::instance().start(
          settings->get_config()[PROJECT_NAME]["auxiliary_data"]);
      int64_t cursor(
          bsky::moderation::auxiliary_data::instance().get_rewind_point());

      // wait for matcher and embed checker to be ready
      do {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      } while (!matcher::shared().is_ready() ||
               !bsky::moderation::embed_checker::instance().is_ready());

      datasource<firehose_payload>::instance().set_config(settings, cursor);
      datasource<firehose_payload>::instance().start();

      // prepare action handlers after we start processing firehose messages
      // this is time consuming - allow a backlog for handlers while
      // existing members load
      bsky::moderation::report_agent::instance().start(
          settings->get_config()[PROJECT_NAME]["auto_reporter"], PROJECT_NAME);

      action_router::instance().start();
#if _DEBUG
      // std::this_thread::sleep_for(std::chrono::milliseconds(10000000));
#endif

      bsky::moderation::embed_checker::instance().set_config(
          settings->get_config()[PROJECT_NAME]["embed_checker"]);
      bsky::moderation::embed_checker::instance().start();

      list_manager::instance().start(
          settings->get_config()[PROJECT_NAME]["list_manager"]);

      // continue as long as firehose runs OK
      datasource<firehose_payload>::instance().wait_for_end_thread();
    } else {
      datasource<jetstream_payload>::instance().set_config(settings, 0);
      datasource<jetstream_payload>::instance().start();

      // continue as long as data feed runs OK
      datasource<jetstream_payload>::instance().wait_for_end_thread();
    }

    return EXIT_SUCCESS;
  } catch (std::exception const &exc) {
    if (log_ready) {
      REL_CRITICAL("Unhandled exception : {}", exc.what());
      stop_logging();
    } else {
      std::cerr << "Unhandled exception : " << exc.what() << '\n';
    }
    return EXIT_FAILURE;
  }
}
