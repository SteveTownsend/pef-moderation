#pragma once
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
#include "blockingconcurrentqueue.h"
#include "common/bluesky/client.hpp"
#include "common/moderation/ozone_adapter.hpp"

#include "common/bluesky/platform.hpp"
#include "yaml-cpp/yaml.h"
#include <thread>

namespace bsky {
namespace moderation {

struct filter_match_info {
  filter_match_info() = delete;
  inline filter_match_info(std::string const &project_name)
      : descriptor(project_name) {}
  std::string descriptor;
  std::vector<std::string> filters;
  std::vector<std::string> paths;
  constexpr std::string get_name() const { return "filter_match"; }
};
struct link_redirection_info {
  link_redirection_info() = delete;
  inline link_redirection_info(std::string const &project_name)
      : descriptor(project_name) {}
  std::string descriptor;
  std::string path;
  std::vector<std::string> uris;
  constexpr std::string get_name() const { return "link_redirection"; }
};

struct blocks_moderation_info {
  blocks_moderation_info() = delete;
  inline blocks_moderation_info(std::string const &project_name)
      : descriptor(project_name) {}
  std::string descriptor;
  constexpr std::string get_name() const { return "blocks_moderation"; }
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
struct blocks_moderation {};

typedef std::variant<no_content, filter_matches, link_redirection,
                     blocks_moderation>
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
  void operator()(blocks_moderation const &value);

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
  void
  set_moderation_data(std::shared_ptr<bsky::moderation::ozone_adapter> &ozone) {
    _moderation_data = ozone;
  }

  void start(YAML::Node const &settings, std::string const &project_name);
  void wait_enqueue(account_report &&value);

  void string_match_report(std::string const &did,
                           std::vector<std::string> const &filters,
                           std::vector<std::string> const &paths);
  void link_redirection_report(std::string const &did, std::string const &path,
                               std::vector<std::string> const &uri_chain);
  void blocks_moderation_report(std::string const &did);
  void label_account(std::string const &subject_did,
                     std::vector<std::string> const &labels);
  std::string service_did() const { return _service_did; }

private:
  report_agent();
  ~report_agent() = default;

  inline bool is_reported(std::string const &did) const {
    return _reported_dids.contains(did);
  }
  inline void reported(std::string const &did) { _reported_dids.insert(did); }

  std::thread _thread;
  std::unique_ptr<bsky::client> _pds_client;
  std::string _project_name;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<account_report> _queue;
  std::string _handle;
  std::string _did;
  std::string _service_did;
  bool _dry_run = true;
  std::shared_ptr<ozone_adapter> _moderation_data;
  std::unordered_set<std::string> _reported_dids;
};

} // namespace moderation
} // namespace bsky
