/*************************************************************************
Public Education Forum Moderation DB Crawler
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
#include "common/config.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "common/moderation/ozone_adapter.hpp"
#include "project_defs.hpp"
#include "restc-cpp/logging.h"
#include <boost/fusion/adapted.hpp>
#include <chrono>
#include <functional>
#include <iostream>
#include <ranges>
#include <thread>

int main(int argc, char **argv) {
  bool log_ready(false);
#if _DEBUG
  // std::this_thread::sleep_for(std::chrono::milliseconds(20000));
#endif
  try {
    // Check command line arguments.
    if (argc != 2) {
      std::cerr << "Usage: db_crawler <config-file-name>\n";
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
    REL_INFO("db_crawler v{}.{}.{}", PROJECT_NAME_VERSION_MAJOR,
             PROJECT_NAME_VERSION_MINOR, PROJECT_NAME_VERSION_PATCH);
    // auto-reporting metrics
    metrics_factory::instance().add_counter(
        "automation",
        "Automated moderation activity: block-list, report, emit-event");

    // use AppView for account/content checking, auth client for moderation
    // actions
    bsky::client appview_client;
    appview_client.set_config(
        settings->get_config()[PROJECT_NAME]["appview_client"]);
    bsky::client pds_client;
    pds_client.set_config(settings->get_config()[PROJECT_NAME]["pds_client"]);

    // for Escalated or Open subjects, check that account and post are still
    // present and if not, auto-acknowledge the subject
    std::shared_ptr<bsky::moderation::ozone_adapter> moderation_data =
        std::make_shared<bsky::moderation::ozone_adapter>(
            build_db_connection_string(
                settings->get_config()[PROJECT_NAME]["moderation_data"]));
    moderation_data->load_pending_reports();

    // Confirm validity of did and content on pending reports
    // Confirmed in testing that auth and non-auth clients see the same response
    // counts
    auto pending(moderation_data->get_pending_reports());
    std::vector<std::string> candidate_profiles;
    candidate_profiles.reserve(pending.size());
#ifdef __GNUC__
    candidate_profiles.insert(candidate_profiles.end(),
                              std::views::keys(pending).cbegin(),
                              std::views::keys(pending).cend());
#else
    candidate_profiles.insert_range(candidate_profiles.end(),
                                    std::views::keys(pending));
#endif
    auto active_profiles(appview_client.get_profiles(candidate_profiles));

    // iterate a view of the pending reports that includes only *inactive*
    // accounts
    std::ranges::for_each(
        candidate_profiles |
            std::views::filter(
                [&active_profiles](std::string const &candidate) {
                  return !active_profiles.contains(
                      bsky::profile_view_detailed(candidate));
                }),
        [&appview_client, &pending, &pds_client](std::string const &match) {
          // double check account status before garbage collceting the reports
          try {
            bsky::profile_view_detailed profile(
                appview_client.get_profile(match));
            REL_ERROR("Skip deleted account {}, getProfile returned OK", match);
          } catch (std::exception &exc) {
            // expected to fail
            REL_INFO("Scrub reports for deleted account {}", match);
            auto to_scrub(pending.find(match));
            if (to_scrub != pending.cend()) {
              for (auto subject : to_scrub->second) {
                std::ostringstream oss;
                bsky::moderation::acknowledge_event_comment comment(
                    PROJECT_NAME);
                comment.context = exc.what();
                comment.did = match;
                if (subject == match) {
                  // TODO reports of content not yet supported - move this when
                  // they are
                  pds_client.acknowledge_subject(match, comment);
                } else {
                  comment.path = subject;
                }
              }
            }
          }
        });

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
