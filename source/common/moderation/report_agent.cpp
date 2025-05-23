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

#include "common/moderation/report_agent.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "common/rest_utils.hpp"
#include "restc-cpp/RequestBuilder.h"
#include "restc-cpp/SerializeJson.h"
#include <algorithm>
#include <boost/fusion/adapted.hpp>
#include <functional>

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_subject,
                          (std::string, _type), (std::string, did),
                          (std::string, uri), (std::string, cid))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_request,
                          (std::string, reasonType), (std::string, reason),
                          (bsky::moderation::report_subject, subject))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_response,
                          (std::string, createdAt), (int64_t, id),
                          (std::string, reportedBy))

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::filter_match_info,
                          (std::string, descriptor)(std::vector<int>, rules)(
                              std::vector<std::string>, filters))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::link_redirection_info,
                          (std::string, descriptor)(std::vector<std::string>,
                                                    uris))

namespace bsky {
namespace moderation {

report_agent &report_agent::instance() {
  static report_agent my_instance;
  return my_instance;
}

report_agent::report_agent() : _queue(QueueLimit) {}

void report_agent::start(YAML::Node const &settings,
                         std::string const &project_name) {
  _project_name = project_name;
  _handle = settings["handle"].as<std::string>();
  _did = settings["did"].as<std::string>();
  _service_did = settings["service_did"].as<std::string>();
  _dry_run = settings["dry_run"].as<bool>();
  _number_of_threads = settings["number_of_threads"].as<size_t>();
  _pds_clients.reserve(_number_of_threads);
  _threads.reserve(_number_of_threads);

  for (size_t client = 0; client < _number_of_threads; ++client) {
    // create and configure client
    auto next_client(std::make_unique<bsky::client>());
    next_client->set_config(settings);
    _pds_clients.push_back(std::move(next_client));
    _threads.push_back(std::thread([this, client] {
      try {
        while (controller::instance().is_active()) {
          account_report report;
          if (_queue.wait_dequeue_timed(report, DequeueTimeout)) {
            // process the item
            metrics_factory::instance()
                .get_gauge("process_operation")
                .Get({{"report_agent", "backlog"}})
                .Decrement();

            // Track all reported accounts
            if (bsky::moderation::ozone_adapter::instance().track_account(
                    report._did)) {
              REL_INFO("Track account {}", report._did);
              metrics_factory::instance()
                  .get_counter("realtime_alerts")
                  .Get({{"auto_reports", "first_time"}})
                  .Increment();
            } else {
              metrics_factory::instance()
                  .get_counter("realtime_alerts")
                  .Get({{"auto_reports", "already_known"}})
                  .Increment();
            }

            std::visit(report_content_visitor(*this, client, report._did),
                       report._content);
          }
        }
      } catch (std::exception const &exc) {
        REL_WARNING("report_agent exception {}", exc.what());
        controller::instance().force_stop();
      }
      REL_INFO("report_agent stopping");
    }));
  }
}

void report_agent::wait_enqueue(account_report &&value) {
  _queue.enqueue(value);
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"report_agent", "backlog"}})
      .Increment();
}

// TODO add metrics
void report_agent::string_match_report(
    const size_t client, std::string const &did, std::string const &path,
    std::string const &cid, std::unordered_set<int> const &rules,
    std::unordered_set<std::string> const &filters) {
  bsky::moderation::filter_match_info reason(_project_name);
  reason.rules = std::vector<int>(rules.cbegin(), rules.cend());
  reason.filters = std::vector<std::string>(filters.cbegin(), filters.cend());
  bsky::moderation::report_subject target(did, path, cid);
  _pds_clients[client]
      ->send_report_for_subject<bsky::moderation::filter_match_info>(target,
                                                                     reason);
}

// TODO add metrics
// TODO why is this never seen?
void report_agent::link_redirection_report(
    const size_t client, std::string const &did, std::string const &path,
    std::string const &cid, std::vector<std::string> const &uri_chain) {
  bsky::moderation::link_redirection_info reason(_project_name);
  reason.uris = uri_chain;
  bsky::moderation::report_subject target(did, path, cid);
  _pds_clients[client]
      ->send_report_for_subject<bsky::moderation::link_redirection_info>(
          target, reason);
}

// TODO add metrics
void report_agent::label_subject(
    const size_t client, bsky::moderation::report_subject const &subject,
    std::unordered_set<std::string> const &add_labels,
    std::unordered_set<std::string> const &remove_labels,
    bsky::moderation::acknowledge_event_comment const &comment) {
  _pds_clients[client]->label_subject(subject, add_labels, remove_labels,
                                      comment);
}

void report_content_visitor::operator()(filter_matches const &value) {
  for (auto &next_scope : value._scoped_matches) {
    if (next_scope.second._labels.empty()) {
      // no label, report for review
      _agent.string_match_report(
          _client, value._did, next_scope.first, next_scope.second._cid,
          next_scope.second._rules, next_scope.second._filters);
    } else {
      // if we automatically label, report is not needed
      bsky::moderation::acknowledge_event_comment comment(
          _agent.project_name());
      bsky::moderation::filter_match_info filter_info(_agent.project_name());
      filter_info.rules = std::vector<int>(next_scope.second._rules.cbegin(),
                                           next_scope.second._rules.cend());
      filter_info.filters =
          std::vector<std::string>(next_scope.second._filters.cbegin(),
                                   next_scope.second._filters.cend());
      std::ostringstream oss;
      restc_cpp::serialize_properties_t properties;
      restc_cpp::SerializeToJson(filter_info, oss);
      comment.context = "filter_matches: " + oss.str();
      comment.did = _agent.service_did();
      bsky::moderation::report_subject subject(value._did, next_scope.first,
                                               next_scope.second._cid);
      _agent.label_subject(_client, subject, next_scope.second._labels, {},
                           comment);
    }
  }
}
void report_content_visitor::operator()(link_redirection const &value) {
  bsky::moderation::report_subject subject(_did);
  _agent.link_redirection_report(_client, _did, value._path, value._cid,
                                 value._uri_chain);
}
void report_content_visitor::operator()(blocks_moderation const &value) {
  // auto-labeling, no report needed
  bsky::moderation::acknowledge_event_comment comment(_agent.project_name());
  comment.context = "blocks_moderation_service";
  comment.did = _agent.service_did();
  bsky::moderation::report_subject subject(_did);
  _agent.label_subject(_client, subject, {"blocks"}, {}, comment);
}
void report_content_visitor::operator()(high_facet_count const &value) {
  // auto-labeling, no report needed
  bsky::moderation::acknowledge_event_comment comment(_agent.project_name());
  comment.context =
      "facet spam " + value.get_name() + ' ' + std::to_string(value._count);
  comment.did = _agent.service_did();
  bsky::moderation::report_subject subject(_did, value._path, value._cid);
  _agent.label_subject(_client, subject, {value.get_name()}, {}, comment);
}
} // namespace moderation
} // namespace bsky
