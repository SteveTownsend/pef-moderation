#ifndef __embed_checker__
#define __embed_checker__
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
#include "common/helpers.hpp"
#include "jwt-cpp/jwt.h"
#include "matcher.hpp"
#include "project_defs.hpp"
#include "restc-cpp/restc-cpp.h"
#include "yaml-cpp/yaml.h"
#include <boost/url.hpp>
#include <cache.hpp>
#include <lfu_cache_policy.hpp>
#include <optional>
#include <thread>
#include <unordered_set>

namespace embed {

struct external {
  std::string _uri;
};
struct image {
  nlohmann::json _cid;
};
struct record {
  std::string _uri;
};
struct video {
  nlohmann::json _cid;
};
typedef std::variant<external, image, record, video> embed_info;

struct embed_info_list {
  std::string _did;
  std::string _path;
  std::vector<embed_info> _embeds;
};

} // namespace embed

namespace bsky {
namespace moderation {

// visitor for report-specific logic
class embed_checker;
struct embed_handler {
public:
  inline embed_handler(embed_checker &checker,
                       restc_cpp::RestClient &rest_client,
                       std::string const &repo, std::string const &path)
      : _checker(checker), _rest_client(rest_client), _repo(repo), _path(path) {
  }
  template <typename T> void operator()(T const &) {}

  void operator()(embed::external const &value);
  void operator()(embed::image const &value);
  void operator()(embed::record const &value);
  void operator()(embed::video const &value);

  bool on_url_redirect(int code, std::string &url,
                       const restc_cpp::Reply &reply);

private:
  embed_checker &_checker;
  restc_cpp::RestClient &_rest_client;
  std::string _repo;
  std::string _path;
  std::string _root_url;
  std::vector<std::string> _uri_chain;
  nlohmann::json _results;
};

class embed_checker {
public:
  // allow a large backlog - queued items are small and we need to manage rate
  // of record creation to obey rate limits
  static constexpr size_t QueueLimit = 50000;
  static constexpr size_t DefaultNumberOfThreads = 5;
  static constexpr size_t UrlRedirectLimit = 10;
  static constexpr size_t MaxHosts = 10000;
  static constexpr size_t HostsOfInterest = 250;
  static constexpr std::chrono::minutes HostDumpInterval =
      std::chrono::minutes(60);

  // embed repetition is logged
  static constexpr size_t ImageFactor = 5;
  static constexpr size_t LinkFactor = 5;
  static constexpr size_t RecordFactor = 5;
  static constexpr size_t VideoFactor = 5;

  static embed_checker &instance();
  bool is_ready() const { return _is_ready; }

  void set_config(YAML::Node const &settings);
  void start();
  void wait_enqueue(embed::embed_info_list &&value);
  void refresh_hosts(std::unordered_set<std::string> &&new_hosts);
  void image_seen(std::string const &repo, std::string const &path,
                  std::string const &cid);
  void record_seen(std::string const &repo, std::string const &path,
                   std::string const &uri);
  bool should_process_uri(std::string const &uri);
  bool is_popular_host(std::string const &host);
  bool uri_seen(std::string const &repo, std::string const &path,
                std::string const &uri);
  void video_seen(std::string const &repo, std::string const &path,
                  std::string const &cid);
  inline bool follow_links() const { return _follow_links; }

private:
  embed_checker();
  ~embed_checker() = default;

  bool _is_ready = false;
  std::vector<std::unique_ptr<restc_cpp::RestClient>> _rest_clients;
  std::vector<std::thread> _threads;
  std::mutex _lock;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<embed::embed_info_list> _queue;
  bool _follow_links = false;
  size_t _number_of_threads = DefaultNumberOfThreads;
  std::unordered_map<std::string, size_t> _checked_images;
  std::unordered_map<std::string, size_t> _checked_records;
  std::unordered_map<std::string, size_t> _checked_uris;
  std::unordered_map<std::string, size_t> _checked_videos;
  std::unordered_set<std::string> _popular_hosts;

  // LFU cache of recently-active accounts
  caches::fixed_sized_cache<std::string, size_t, caches::LFUCachePolicy>
      _observed_hosts;
  std::chrono::system_clock::time_point _last_host_dump =
      std::chrono::system_clock::now();
};

} // namespace moderation
} // namespace bsky

#endif