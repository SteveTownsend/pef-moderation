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
#include <unordered_map>
#include <unordered_set>

namespace bsky {
namespace moderation {

struct filter_match_info {
  filter_match_info() = delete;
  inline filter_match_info(std::string const &project_name)
      : descriptor(project_name) {}
  std::string descriptor;
  std::vector<std::string> filters;
  constexpr std::string get_name() const { return "filter_match"; }
};
struct link_redirection_info {
  link_redirection_info() = delete;
  inline link_redirection_info(std::string const &project_name)
      : descriptor(project_name) {}
  std::string descriptor;
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

struct high_facet_count_info {
  high_facet_count_info() = delete;
  inline high_facet_count_info(std::string const &project_name,
                               std::string const &context, size_t const count)
      : descriptor(project_name), _context(context), _count(count) {}
  std::string descriptor;
  std::string _context;
  size_t _count;
  constexpr std::string get_name() const { return _context; }
};

struct no_content {};

struct path_matches {
  std::string _cid;
  std::unordered_set<std::string> _filters;
  std::unordered_set<std::string> _labels;
};
struct filter_matches {
  std::string _did;
  std::unordered_map<std::string, path_matches> _scoped_matches;
};
struct link_redirection {
  std::string _path;
  std::string _cid;
  std::vector<std::string> _uri_chain;
};
struct blocks_moderation {};
enum class facet_type { total = 1, link, mention, tag };
inline std::string facet_type_label(const facet_type facet) {
  switch (facet) {
  case facet_type::total:
    return "high-total-facet-count";
  case facet_type::link:
    return "high-offsite-link-count";
  case facet_type::mention:
    return "high-user-mention-count";
  case facet_type::tag:
    return "high-hashtag-count";
  default:
    return "facet-type-unrecognized";
  }
}
struct high_facet_count {
  inline high_facet_count(const facet_type facet, const std::string &path,
                          const std::string &cid, const size_t count)
      : _facet(facet), _path(path), _cid(cid), _count(count) {}
  inline high_facet_count(const high_facet_count &rhs)
      : _facet(rhs._facet), _path(rhs._path), _cid(rhs._cid),
        _count(rhs._count) {}
  inline high_facet_count &operator=(const high_facet_count &rhs) {
    _facet = rhs._facet;
    _path = rhs._path;
    _cid = rhs._cid;
    _count = rhs._count;
    return *this;
  }
  inline std::string get_name() const { return facet_type_label(_facet); }
  facet_type _facet;
  std::string _path;
  std::string _cid;
  size_t _count;
};
typedef std::variant<no_content, filter_matches, link_redirection,
                     blocks_moderation, high_facet_count>
    report_content;
struct account_report {
  inline account_report() : _content(no_content()) {}
  inline account_report(std::string const &did, report_content &&content)
      : _did(did), _content(std::move(content)) {}
  std::string _did;
  report_content _content;
};

class report_agent;
// visitor for report-specific logic
struct report_content_visitor {
public:
  inline report_content_visitor(report_agent &agent, const size_t index,
                                std::string const &did)
      : _agent(agent), _client(index), _did(did) {}
  template <typename T> void operator()(T const &) {}

  void operator()(filter_matches const &value);
  void operator()(link_redirection const &value);
  void operator()(blocks_moderation const &value);
  void operator()(high_facet_count const &value);

private:
  report_agent &_agent;
  size_t _client;
  std::string _did;
};

class report_agent {
public:
  static constexpr size_t QueueLimit = 10000;
  static constexpr std::chrono::milliseconds DequeueTimeout =
      std::chrono::milliseconds(10000);
  static constexpr size_t DefaultNumberOfReportingThreads = 3;

  static report_agent &instance();

  void start(YAML::Node const &settings, std::string const &project_name);
  void wait_enqueue(account_report &&value);

  void string_match_report(const size_t client, std::string const &did,
                           std::string const &path, std::string const &cid,
                           std::unordered_set<std::string> const &filters);
  void link_redirection_report(const size_t client, std::string const &did,
                               std::string const &path, std::string const &cid,
                               std::vector<std::string> const &uri_chain);
  void blocks_moderation_report(const size_t client, std::string const &did);
  void facet_spam_report(const size_t client, std::string const &did,
                         std::string const &path, std::string const &cid,
                         std::string const &context, size_t const count);
  void
  label_subject(const size_t client,
                bsky::moderation::report_subject const &subject,
                std::unordered_set<std::string> const &add_labels,
                std::unordered_set<std::string> const &remove_labels,
                bsky::moderation::acknowledge_event_comment const &comment);
  std::string service_did() const { return _service_did; }
  std::string project_name() const { return _project_name; }

private:
  report_agent();
  ~report_agent() = default;

  std::vector<std::unique_ptr<bsky::client>> _pds_clients;
  std::vector<std::thread> _threads;
  size_t _number_of_threads = DefaultNumberOfReportingThreads;
  std::string _project_name;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<account_report> _queue;
  std::string _handle;
  std::string _did;
  std::string _service_did;
  bool _dry_run = true;
};

} // namespace moderation
} // namespace bsky
