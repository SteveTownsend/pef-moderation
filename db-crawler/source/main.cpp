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
    bsky::moderation::ozone_adapter::instance().start(
        build_db_connection_string(
            settings->get_config()[PROJECT_NAME]["moderation_data"]["db"]));
    bsky::moderation::ozone_adapter::instance().load_pending_report_tags();

    // Load pending reports grouped by account
    auto pending(
        bsky::moderation::ozone_adapter::instance().get_pending_reports());
    std::unordered_set<std::string> candidate_profiles;
    candidate_profiles.reserve(pending.size());
#ifdef __GNUC__
    candidate_profiles.insert(std::views::keys(pending).cbegin(),
                              std::views::keys(pending).cend());
#else
    candidate_profiles.insert_range(std::views::keys(pending));
#endif
    // get a list of the active profiles only
    auto active_profiles(appview_client.get_profiles(candidate_profiles));
    constexpr bool default_execute(false);
    auto scrub_orphaned_settings(
        settings->get_config()[PROJECT_NAME]["jobs"]["scrub_orphaned"]);
    if (scrub_orphaned_settings["execute"].as<bool>(default_execute)) {
      // Confirm validity of did and content on pending reports
      // Confirmed in testing that auth and non-auth clients see the same
      // response counts
      // Iterate a view of the pending reports that includes
      // only *inactive* accounts
      std::ranges::for_each(
          candidate_profiles |
              std::views::filter(
                  [&active_profiles](std::string const &candidate) {
                    return !active_profiles.contains(
                        bsky::profile_view_detailed(candidate));
                  }),
          [&appview_client, &pending, &pds_client](std::string const &match) {
            // double check account status before garbage collecting the reports
            try {
              bsky::profile_view_detailed profile(
                  appview_client.get_profile(match));
              REL_ERROR("Skip deleted account {}, getProfile returned OK",
                        match);
            } catch (std::exception &exc) {
              // expected to fail
              REL_INFO("Scrub reports for deleted account {}", match);
              auto to_scrub(pending.find(match));
              if (to_scrub != pending.cend()) {
                for (auto subject : to_scrub->second) {
                  bsky::moderation::acknowledge_event_comment comment(
                      PROJECT_NAME);
                  comment.context = exc.what();
                  comment.did = match;
                  if (subject.first == match) {
                    // TODO reports of content not yet supported - move this
                    // when they are
                    pds_client.acknowledge_subject(match, comment);
                  } else {
                    comment.path = subject.first;
                  }
                }
              }
            }
          });
    }

    auto tag_source_settings(
        settings->get_config()[PROJECT_NAME]["jobs"]["tag_manual_and_auto"]);
    if (tag_source_settings["execute"].as<bool>(default_execute)) {
      // we need moderation events of type report to correlate with the subject
      std::string automatic_reporter(
          tag_source_settings["auto-reporter"].as<std::string>(""));
      bsky::moderation::ozone_adapter::instance().load_content_reporters(
          automatic_reporter);

      // For active profiles pending review, tag them as manual/auto-reported if
      // not already done
      auto const &content_reporters(
          bsky::moderation::ozone_adapter::instance().get_content_reporters());
      size_t both(0);
      size_t automatic(0);
      size_t manual(0);
      size_t removed_all(0);
      size_t inactive(0);
      size_t no_report(0);
      size_t untouched(0);
      for (auto &reported : content_reporters) {
        // check report sources for this content item
        bool has_auto(reported.second.automatic > 0);
        bool has_manual(reported.second.manual > 0);
        // TODO handle non-account reports - we might have an at-uri in hand
        // here but the pending-subject list contains either DID or relative
        // record_path
        std::string subject_did(reported.first);
        auto active_profile(
            active_profiles.find(bsky::profile_view_detailed(subject_did)));
        if (active_profile != active_profiles.cend()) {
          auto subject(pending.find(subject_did));
          if (subject != pending.cend()) {
            // 'subject' points to a list of reported content for the account
            // and the tags for each item
            // we only handle accounts so check for reports on DID
            auto account_reports(subject->second.find(subject_did));
            if (account_reports != subject->second.cend()) {
              std::vector<std::string> current_tags(account_reports->second);
              std::vector<std::string> add_tags;
              std::vector<std::string> remove_tags;
              if (has_auto && has_manual &&
                  !subject->second.contains("src:both")) {
                add_tags.push_back("src:both");
              } else if (has_auto && !subject->second.contains("src:auto")) {
                add_tags.push_back("src:auto");
              } else if (has_manual &&
                         !subject->second.contains("src:manual")) {
                add_tags.push_back("src:manual");
              }
              if (!(has_auto && has_manual) &&
                  subject->second.contains("src:both")) {
                remove_tags.push_back("src:both");
              } else if (!has_auto && subject->second.contains("src:auto")) {
                remove_tags.push_back("src:auto");
              } else if (!has_manual &&
                         subject->second.contains("src:manual")) {
                remove_tags.push_back("src:manual");
              }
              if (!add_tags.empty() || !remove_tags.empty()) {
                // Record the tags on the Ozone server
                bsky::moderation::tag_event_comment comment(PROJECT_NAME);
                pds_client.tag_report_subject(subject_did, {}, comment,
                                              add_tags, remove_tags);
                if (has_manual && has_auto) {
                  ++both;
                } else if (has_manual) {
                  ++manual;
                } else if (has_auto) {
                  ++automatic;
                } else {
                  ++removed_all;
                }
              } else {
                REL_WARNING("Account {}/{} report needs no Tags", subject_did,
                            active_profile->handle);
                ++untouched;
              }
            } else {
              REL_WARNING("Account {}/{} has only content reports", subject_did,
                          active_profile->handle);
              ++no_report;
            }
          } else {
            REL_WARNING("Account {}/{} has no active reports", subject_did,
                        active_profile->handle);
            ++no_report;
          }
        } else {
          REL_WARNING("Account {} is inactive", subject_did);
          ++inactive;
        }
      }
      REL_INFO("Manual/auto tag updated : {} manual, {} auto, {} both, {} none",
               manual, automatic, both, removed_all);
      REL_INFO("Manual/auto tag no update: {} inactive, {} no report, {} "
               "untouched",
               inactive, no_report, untouched);
    }
    // Acknowledge all reports that match a given SQL WHERE filter
    auto ack_settings(settings->get_config()[PROJECT_NAME]["jobs"]
                                            ["acknowledge_all_in_query"]);
    if (ack_settings["execute"].as<bool>(default_execute)) {
      std::string context("acknowledge_all_in_query " +
                          ack_settings["label"].as<std::string>());
      bsky::moderation::ozone_adapter::instance().filter_subjects(
          ack_settings["filter"].as<std::string>());
      auto targets(
          bsky::moderation::ozone_adapter::instance().get_filtered_subjects());
      for (auto const &subject_entry : targets) {
        std::string subject(subject_entry.first);
        std::string reason(subject_entry.second);
        auto active_profile(
            active_profiles.find(bsky::profile_view_detailed(subject)));
        if (active_profile != active_profiles.cend()) {
          auto subject_pending(pending.find(subject));
          if (subject_pending != pending.cend()) {
            REL_INFO("Acknowledge: {}/{} has active reports", subject,
                     active_profile->handle);
            bsky::moderation::acknowledge_event_comment comment(PROJECT_NAME);
            comment.context = context;
            comment.did = pds_client.service_did();
            pds_client.acknowledge_subject(subject, comment);

          } else {
            REL_INFO("Acknowledge: {}/{} has no active reports", subject,
                     active_profile->handle);
          }
        } else {
          REL_INFO("Acknowledge: {} is inactive", subject);
        }
      }
    }

    // Label all reports that match a given SQL WHERE filter
    auto label_settings(
        settings->get_config()[PROJECT_NAME]["jobs"]["label_all_in_query"]);
    if (label_settings["execute"].as<bool>(default_execute)) {
      std::string filter(label_settings["filter"].as<std::string>());
      bsky::moderation::ozone_adapter::instance().filter_subjects(filter);
      auto targets(
          bsky::moderation::ozone_adapter::instance().get_filtered_subjects());
      for (auto const &subject_entry : targets) {
        std::string subject(subject_entry.first);
        std::string reason(subject_entry.second);
        auto active_profile(
            active_profiles.find(bsky::profile_view_detailed(subject)));
        if (active_profile != active_profiles.cend()) {
          auto subject_pending(pending.find(subject));
          if (subject_pending != pending.cend()) {
            REL_INFO("Label: {}/{} has active reports", subject,
                     active_profile->handle);
            pds_client.label_account(
                subject,
                label_settings["labels"].as<std::vector<std::string>>());

            // Acknowledge the report to close out workflow
            bsky::moderation::acknowledge_event_comment comment(PROJECT_NAME);
            comment.context = filter + "\n" + reason;
            comment.did = pds_client.service_did();
            pds_client.acknowledge_subject(subject, comment);
          } else {
            // Acknowledge report to get it out of the queue
            REL_INFO("Label: {}/{} has no active reports", subject,
                     active_profile->handle);
          }
        } else {
          REL_INFO("Label: {} is inactive", subject);
        }
      }
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
