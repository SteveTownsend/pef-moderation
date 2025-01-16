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

#include "moderation/action_router.hpp"
#include "jwt-cpp/traits/boost-json/traits.h"
#include "log_wrapper.hpp"
#include "matcher.hpp"
#include "metrics.hpp"
#include "moderation/list_manager.hpp"
#include "restc-cpp/RequestBuilder.h"
#include <boost/fusion/adapted.hpp>
#include <functional>

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_reason,
                          (std::string,
                           descriptor)(std::vector<std::string>,
                                       filters)(std::vector<std::string>,
                                                paths))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_subject,
                          (std::string, _type), (std::string, did))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_request,
                          (std::string, reasonType), (std::string, reason),
                          (bsky::moderation::report_subject, subject))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_response,
                          (std::string, createdAt), (int64_t, id),
                          (std::string, reportedBy))

action_router &action_router::instance() {
  static action_router my_instance;
  return my_instance;
}

action_router::action_router() : _queue(QueueLimit) {}

void action_router::set_config(YAML::Node const &settings) {
  _handle = settings["handle"].as<std::string>();
  _password = settings["password"].as<std::string>();
  _host = settings["host"].as<std::string>();
  _port = settings["port"].as<std::string>();
  _service_did = settings["service_did"].as<std::string>();
  _dry_run = settings["dry_run"].as<bool>();
}

void action_router::start() {
  _thread = std::thread([&] {
    // create client
    _rest_client = restc_cpp::RestClient::Create();

    // create session
    // bootstrap self-managed session from the returned tokens
    _session = std::make_unique<bsky::pds_session>(*_rest_client, _host);
    _session->connect(bsky::login_info({_handle, _password}));

    while (true) {
      account_filter_matches matches;
      if (_queue.wait_dequeue_timed(matches, DequeueTimeout)) {
        // process the item
        metrics::instance()
            .operational_stats()
            .Get({{"action_router", "backlog"}})
            .Decrement();

        // Don't reprocess previously-labeled accounts
        if (_moderation_data->is_labeled(matches._did) ||
            is_reported(matches._did)) {
          REL_INFO("Report of {} skipped, already known", matches._did);
          metrics::instance()
              .realtime_alerts()
              .Get({{"auto_reports", "skipped"}})
              .Increment();
          continue;
        }

        // iterate the match results for any rules that are marked
        // auto-reportable
        std::vector<std::string> paths;
        std::vector<std::string> all_filters;
        for (auto const &result : matches._matches) {
          // this is the substring of the full JSON that matched one or more
          // desired strings
          std::string path(result.first);
          std::vector<std::string> filters;
          for (auto const &next_match : result.second) {
            for (auto const &match : next_match._matches) {
              matcher::rule matched_rule(
                  _matcher->find_rule(match.get_keyword()));
              if (!matched_rule._report) {
                // reqport not requested for this rule
                continue;
              }
              if (!matched_rule._block_list_name.empty()) {
                list_manager::instance().wait_enqueue(
                    {matches._did, matched_rule._block_list_name});
              }
              if (matched_rule._content_scope ==
                  matcher::rule::content_scope::any) {
                filters.push_back(matched_rule._target);
              } else if (matched_rule._content_scope ==
                         matcher::rule::content_scope::profile) {
                // report only if seen in profile
                if (next_match._candidate._type == bsky::AppBskyActorProfile) {
                  filters.push_back(matched_rule._block_list_name);
                }
              }
            }
            if (!filters.empty()) {
              paths.push_back(path);
              all_filters.insert(all_filters.cend(), filters.cbegin(),
                                 filters.cend());
            }
          }
        }

        // record the account as a delta to cache for dup detection
        if (!all_filters.empty()) {
          send_report(matches._did, all_filters, paths);
          metrics::instance()
              .realtime_alerts()
              .Get({{"auto_reports", "submitted"}})
              .Increment();
        }
      }
      // check session status
      _session->check_refresh();

      // TODO terminate gracefully
    }
  });
}

void action_router::wait_enqueue(account_filter_matches &&value) {
  _queue.wait_enqueue(value);
  metrics::instance()
      .operational_stats()
      .Get({{"action_router", "backlog"}})
      .Increment();
}

// TODO add metrics
void action_router::send_report(std::string const &did,
                                std::vector<std::string> const &filters,
                                std::vector<std::string> const &paths) {
  restc_cpp::serialize_properties_t properties;
  properties.name_mapping = &json::TypeFieldMapping;
  reported(did);
  if (_dry_run) {
    REL_INFO("Dry-run Report of {} for rules {} on paths {}", did, filters,
             paths);
    return;
  }
  size_t retries(0);
  bsky::moderation::report_response response;
  while (retries < 5) {
    try {
      response =
          _rest_client
              ->ProcessWithPromiseT<bsky::moderation::report_response>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a report_response structure.
                    bsky::moderation::report_request request;
                    request.subject.did = did;

                    bsky::moderation::report_reason reason;
                    reason.filters = filters;
                    reason.paths = paths;
                    std::ostringstream oss;
                    restc_cpp::SerializeToJson(reason, oss);
                    request.reason = oss.str();

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(
                        response,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "com.atproto.moderation.createReport")
                            .Header("Content-Type", "application/json")
                            .Header("Atproto-Accept-Labelers", _service_did)
                            .Header(
                                "Atproto-Proxy",
                                _service_did +
                                    std::string(atproto::ProxyLabelerSuffix))
                            .Header("Authorization",
                                    std::string("Bearer " +
                                                _session->access_token()))
                            .Data(body.str())
                            // Send the request
                            .Execute());

                    // Return the session instance through C++ future<>
                    return response;
                  })

              // Get the Post instance from the future<>, or any C++ exception
              // thrown within the lambda.
              .get();
      REL_INFO("Report of {} for rules {} on paths {} recorded at {}, reporter "
               "{} id={}",
               did, filters, paths, response.createdAt, response.reportedBy,
               response.id);
      break;
    } catch (boost::system::system_error const &exc) {
      if (exc.code().value() == boost::asio::error::eof &&
          exc.code().category() == boost::asio::error::get_misc_category()) {
        REL_WARNING("IoReaderImpl::ReadSome(createReport): asio eof, retry");
        ++retries;
      } else {
        // unrecoverable error
        REL_ERROR(
            "Create report of {} for rules {} on paths {} Boost exception {}",
            did, filters, paths, exc.what())
        break;
      }
    } catch (std::exception const &exc) {
      REL_ERROR("Create report of {} for rules {} on paths {} exception {}",
                did, filters, paths, exc.what())
      break;
    }
  }
}
