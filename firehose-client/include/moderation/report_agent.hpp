#ifndef __report_agent__
#define __report_agent__
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
#include "blockingconcurrentqueue.h"
#include "firehost_client_config.hpp"
#include "helpers.hpp"
#include "moderation/ozone_adapter.hpp"
#include "moderation/session_manager.hpp"
#include "yaml-cpp/yaml.h"
#include <thread>

namespace bsky {
namespace moderation {

struct filter_match_info {
  std::string descriptor = std::string(PROJECT_NAME);
  std::vector<std::string> filters;
  std::vector<std::string> paths;
};
struct link_redirection_info {
  std::string descriptor = std::string(PROJECT_NAME);
  std::string path;
  std::vector<std::string> uris;
};
struct report_subject {
  std::string _type = std::string(atproto::AdminDefsRepoRef);
  // TODO should be optional, add std::optional<strong_ref> for content, if
  // needed
  std::string did;
};
// always report the account. Report content indicates context.
struct report_request {
  std::string reasonType = std::string(bsky::moderation::ReasonOther);
  std::string reason;
  report_subject subject;
};
struct report_response {
  // ignore other fields
  std::string createdAt;
  int64_t id;
  std::string reportedBy;
};

struct label_event {
  std::string _type = std::string(bsky::moderation::EventLabel);
  std::vector<std::string> createLabelVals;
  std::vector<std::string> negateLabelVals;
};
// Label auto-reported account. Associated eport indicates context
struct emit_event_label_request {
  label_event event;
  report_subject subject;
  std::string createdBy;
};
struct emit_event_label_response {
  // ignore other fields
  std::string createdAt;
  int64_t id;
  std::string createdBy;
};

struct no_content {};

struct filter_matches {
  std::vector<std::string> _filters;
  std::vector<std::string> _paths;
  std::vector<std::string> _labels;
};
struct link_redirection {
  std::string _path;
  std::vector<std::string> _uri_chain;
};
typedef std::variant<no_content, filter_matches, link_redirection>
    report_content;
struct account_report {
  inline account_report() : _content(no_content()) {}
  inline account_report(std::string const &did, report_content content)
      : _did(did), _content(content) {}
  std::string _did;
  report_content _content;
};

class report_agent;
// visitor for report-specific logic
struct report_content_visitor {
public:
  inline report_content_visitor(report_agent &agent, std::string const &did)
      : _agent(agent), _did(did) {}
  template <typename T> void operator()(T const &) {}

  void operator()(filter_matches const &value);
  void operator()(link_redirection const &value);

private:
  report_agent &_agent;
  std::string _did;
};

class report_agent {
public:
  static constexpr size_t QueueLimit = 10000;
  static constexpr std::chrono::milliseconds DequeueTimeout =
      std::chrono::milliseconds(10000);

  static report_agent &instance();
  void set_config(YAML::Node const &settings);
  void
  set_moderation_data(std::shared_ptr<bsky::moderation::ozone_adapter> &ozone) {
    _moderation_data = ozone;
  }

  void start();
  void wait_enqueue(account_report &&value);

  void string_match_report(std::string const &did,
                           std::vector<std::string> const &filters,
                           std::vector<std::string> const &paths);
  void link_redirection_report(std::string const &did, std::string const &path,
                               std::vector<std::string> const &uri_chain);
  void label_account(std::string const &did,
                     std::vector<std::string> const &labels);

private:
  report_agent();
  ~report_agent() = default;

  inline bool is_reported(std::string const &did) const {
    return _reported_dids.contains(did);
  }
  inline void reported(std::string const &did) { _reported_dids.insert(did); }

  std::thread _thread;
  std::unique_ptr<restc_cpp::RestClient> _rest_client;
  std::unique_ptr<pds_session> _session;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<account_report> _queue;
  std::string _handle;
  std::string _password;
  std::string _did;
  std::string _host;
  std::string _port;
  std::string _service_did;
  bool _dry_run = true;
  std::shared_ptr<ozone_adapter> _moderation_data;
  std::unordered_set<std::string> _reported_dids;
};

} // namespace moderation
} // namespace bsky
#endif