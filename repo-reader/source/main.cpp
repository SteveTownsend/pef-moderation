/*************************************************************************
Public Education Forum Moderation Repo Reader
Copyright (c) Steve Townsend 2026

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

#include <boost/fusion/adapted.hpp>
#include <chrono>
#include <functional>
#include <iostream>
#include <ranges>
#include <thread>

#include "common/bluesky/client.hpp"
#include "common/bluesky/platform.hpp"
#include "common/config.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "common/parser.hpp"
#include "project_defs.hpp"
#include "restc-cpp/logging.h"

int main(int argc, char **argv) {
  bool log_ready(false);
#if _DEBUG
  // std::this_thread::sleep_for(std::chrono::milliseconds(20000));
#endif
  try {
    // Check command line arguments.
    if (argc != 2) {
      std::cerr << "Usage: repo_reader <config-file-name>\n";
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
    REL_INFO("repo_reader v{}.{}.{}", PROJECT_NAME_VERSION_MAJOR,
             PROJECT_NAME_VERSION_MINOR, PROJECT_NAME_VERSION_PATCH);
    // auto-reporting metrics
    metrics_factory::instance().add_counter(
        "automation",
        "Automated moderation activity: block-list, report, emit-event");

    // use unauthenticated client per
    // https://atproto.com/specs/sync#repository-exports
    // TODO make sure this works for accounts that require login to view
    // actions
    bsky::client appview_client;
    appview_client.set_config(
        settings->get_config()[PROJECT_NAME]["appview_client"]);

    constexpr bool default_execute(false);
    auto get_repos_settings(
        settings->get_config()[PROJECT_NAME]["jobs"]["get_repos"]);
    if (get_repos_settings["execute"].as<bool>(default_execute)) {
      // Confirm validity of each did and dump the repo as JSON
      auto dids(get_repos_settings["dids"].as<std::vector<std::string>>());
      for (auto const &did :
           get_repos_settings["dids"].as<std::vector<std::string>>()) {
        std::string repo_for_did(appview_client.get_repo(did));
        REL_INFO("Repo for DID {} is {} bytes", did, repo_for_did.size());
        parser car_parser;
        if (!car_parser.json_from_car(repo_for_did.cbegin(),
                                      repo_for_did.cend())) {
          REL_ERROR("Failed to parse repo for DID {}", did);
        } else {
          std::string output_path(
              get_repos_settings["output_path"].as<std::string>());
          static std::string const did_prefix("did:plc:");
          std::string filename(output_path + "/" +
                               did.substr(did_prefix.length()) + "-" +
                               std::to_string(std::time(nullptr)) + ".json");
          std::ofstream output_file(filename);
          if (!output_file.is_open()) {
            REL_ERROR("Failed to open output file for DID {}", did);
          } else {
            REL_INFO("Matchable CBORs: {}",
                     car_parser.matchable_cbors().size());
            output_file << "Matchable CBORs" << '\n';
            for (const auto &item : car_parser.matchable_cbors()) {
              output_file << item << '\n';
            }
            REL_INFO("Content CBORs: {}", car_parser.content_cbors().size());
            output_file << "Content CBORs" << '\n';
            for (const auto &item : car_parser.content_cbors()) {
              output_file << item << '\n';
            }
            REL_INFO("Other CBORs: {}", car_parser.other_cbors().size());
            output_file << "Other CBORs" << '\n';
            for (const auto &item : car_parser.other_cbors()) {
              output_file << item << '\n';
            }
            output_file.close();
            REL_INFO("Successfully dumped repo for DID {} to {}", did,
                     filename);
          }
        }
      }
      return EXIT_SUCCESS;
    }
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
