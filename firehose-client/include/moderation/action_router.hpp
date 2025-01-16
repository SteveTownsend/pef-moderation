#ifndef __action_router__
#define __action_router__
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
#include "firehost_client_config.hpp"
#include "helpers.hpp"
#include "jwt-cpp/jwt.h"
#include "matcher.hpp"
#include "moderation/ozone_adapter.hpp"
#include "moderation/session_manager.hpp"
#include "queue/readerwritercircularbuffer.h"
#include "yaml-cpp/yaml.h"
#include <thread>

namespace bsky {

namespace moderation {

struct report_reason {
  std::string descriptor = std::string(PROJECT_NAME);
  std::vector<std::string> filters;
  std::vector<std::string> paths;
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

} // namespace moderation

} // namespace bsky

struct account_filter_matches {
  std::string _did;
  path_match_results _matches;
};

class action_router {
public:
  static constexpr size_t QueueLimit = 1000;
  static constexpr std::chrono::milliseconds DequeueTimeout =
      std::chrono::milliseconds(10000);

  static action_router &instance();
  void set_config(YAML::Node const &settings);
  void
  set_moderation_data(std::shared_ptr<bsky::moderation::ozone_adapter> &ozone) {
    _moderation_data = ozone;
  }
  inline void set_matcher(std::shared_ptr<matcher> my_matcher) {
    _matcher = my_matcher;
  }

  void start();
  void wait_enqueue(account_filter_matches &&value);

private:
  action_router();
  ~action_router() = default;

  inline bool is_reported(std::string const &did) const {
    return _reported_dids.contains(did);
  }

  inline void reported(std::string const &did) { _reported_dids.insert(did); }

  void send_report(std::string const &did,
                   std::vector<std::string> const &filters,
                   std::vector<std::string> const &paths);

  std::thread _thread;
  std::unique_ptr<restc_cpp::RestClient> _rest_client;
  std::unique_ptr<bsky::pds_session> _session;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingReaderWriterCircularBuffer<account_filter_matches> _queue;
  std::string _handle;
  std::string _password;
  std::string _host;
  std::string _port;
  std::string _service_did;
  bool _dry_run = true;
  std::shared_ptr<matcher> _matcher;
  std::shared_ptr<bsky::moderation::ozone_adapter> _moderation_data;
  std::unordered_set<std::string> _reported_dids;
};
#endif