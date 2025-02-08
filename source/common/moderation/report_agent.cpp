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
                          (std::string, _type), (std::string, did))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_request,
                          (std::string, reasonType), (std::string, reason),
                          (bsky::moderation::report_subject, subject))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_response,
                          (std::string, createdAt), (int64_t, id),
                          (std::string, reportedBy))

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::filter_matches,
                          (std::vector<std::string>, _filters),
                          (std::vector<std::string>, _paths),
                          (std::vector<std::string>, _labels))

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::filter_match_info,
                          (std::string,
                           descriptor)(std::vector<std::string>,
                                       filters)(std::vector<std::string>,
                                                paths))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::link_redirection_info,
                          (std::string,
                           descriptor)(std::string,
                                       path)(std::vector<std::string>, uris))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::blocks_moderation_info,
                          (std::string, descriptor))

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
  _thread = std::thread([&, this, settings] {
    try {
      // create client
      _pds_client = std::make_unique<bsky::client>();
      _pds_client->set_config(settings);

      while (controller::instance().is_active()) {
        account_report report;
        if (_queue.wait_dequeue_timed(report, DequeueTimeout)) {
          // process the item
          metrics_factory::instance()
              .get_gauge("process_operation")
              .Get({{"report_agent", "backlog"}})
              .Decrement();

          // Don't reprocess previously-labeled accounts
          if (_moderation_data->already_processed(report._did) ||
              is_reported(report._did)) {
            REL_INFO("Report of {} skipped, already known", report._did);
            metrics_factory::instance()
                .get_counter("realtime_alerts")
                .Get({{"auto_reports", "skipped"}})
                .Increment();
            continue;
          }

          std::visit(report_content_visitor(*this, report._did),
                     report._content);
          reported(report._did);
        }
      }
    } catch (std::exception const &exc) {
      REL_WARNING("report_agent exception {}", exc.what());
      controller::instance().force_stop();
    }
    REL_INFO("report_agent stopping");
  });
}

void report_agent::wait_enqueue(account_report &&value) {
  _queue.enqueue(value);
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"report_agent", "backlog"}})
      .Increment();
}

// TODO add metrics
void report_agent::string_match_report(std::string const &did,
                                       std::vector<std::string> const &filters,
                                       std::vector<std::string> const &paths) {
  bsky::moderation::filter_match_info reason(_project_name);
  reason.filters = filters;
  reason.paths = paths;
  _pds_client->send_report<bsky::moderation::filter_match_info>(did, reason);
}

// TODO add metrics
void report_agent::link_redirection_report(
    std::string const &did, std::string const &path,
    std::vector<std::string> const &uri_chain) {
  bsky::moderation::link_redirection_info reason(_project_name);
  reason.path = path;
  reason.uris = uri_chain;
  _pds_client->send_report<bsky::moderation::link_redirection_info>(did,
                                                                    reason);
}

// TODO add metrics
void report_agent::blocks_moderation_report(std::string const &did) {
  bsky::moderation::blocks_moderation_info reason(_project_name);
  _pds_client->send_report<bsky::moderation::blocks_moderation_info>(did,
                                                                     reason);
}

// TODO add metrics
void report_agent::label_account(std::string const &subject_did,
                                 std::vector<std::string> const &labels) {
  _pds_client->label_account(subject_did, labels);
}

void report_content_visitor::operator()(filter_matches const &value) {
  _agent.string_match_report(_did, value._filters, value._paths);
  if (!value._labels.empty()) {
    // auto-label request augments the report
    _agent.label_account(_did, value._labels);
    // Acknowledge the report to close out workflow
    bsky::moderation::acknowledge_event_comment comment(_agent.project_name());
    std::ostringstream oss;
    restc_cpp::serialize_properties_t properties;
    properties.ignore_empty_fileds = true;
    restc_cpp::SerializeToJson(value, oss);
    comment.context = "filter_matches: " + oss.str();
    comment.did = _agent.service_did();
  }
}
void report_content_visitor::operator()(link_redirection const &value) {
  _agent.link_redirection_report(_did, value._path, value._uri_chain);
}
void report_content_visitor::operator()(blocks_moderation const &value) {
  _agent.blocks_moderation_report(_did);
  // auto-label request augments the report
  _agent.label_account(_did, {"blocks"});
  // Acknowledge the report to close out workflow
  bsky::moderation::acknowledge_event_comment comment(_agent.project_name());
  comment.context = "blocks_moderation_service";
  comment.did = _agent.service_did();
}
} // namespace moderation
} // namespace bsky
