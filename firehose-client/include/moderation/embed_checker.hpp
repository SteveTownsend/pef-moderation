#ifndef __embed_checker__
#define __embed_checker__
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
#include "jwt-cpp/jwt.h"
#include "matcher.hpp"
#include "restc-cpp/restc-cpp.h"
#include <boost/url.hpp>
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
  inline void operator()(embed::image const &value) {}
  inline void operator()(embed::record const &value) {}
  inline void operator()(embed::video const &value) {}

  bool on_url_redirect(int code, std::string &url,
                       const restc_cpp::Reply &reply);

private:
  embed_checker &_checker;
  restc_cpp::RestClient &_rest_client;
  std::string _repo;
  std::string _path;
  std::string _root_url;
  nlohmann::json _results;
};

class embed_checker {
public:
  // allow a large backlog - queued items are small and we need to manage rate
  // of record creation to obey rate limits
  static constexpr size_t QueueLimit = 50000;
  static constexpr size_t NumberOfThreads = 5;
  static constexpr size_t UrlRedirectLimit = 5;
  static constexpr std::string_view _uri_host_prefix = "www.";

  static embed_checker &instance();

  void start();
  void wait_enqueue(embed::embed_info_list &&value);
  bool uri_seen(std::string const &uri) {
    // return true if insert fails, we already know this one
    std::lock_guard<std::mutex> guard(_lock);
    auto inserted(_checked_uris.insert({uri, 1}));
    if (!inserted.second) {
      ++(inserted.first->second);
      return true;
    }
    return false;
  }
  bool should_process_uri(std::string const &uri);
  inline void set_matcher(std::shared_ptr<matcher> my_matcher) {
    _matcher = my_matcher;
  }
  inline std::shared_ptr<matcher> get_matcher() const { return _matcher; }

private:
  embed_checker();
  ~embed_checker() = default;

  std::array<std::thread, NumberOfThreads> _threads;
  std::mutex _lock;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<embed::embed_info_list> _queue;
  std::unique_ptr<restc_cpp::RestClient> _rest_client;
  std::shared_ptr<matcher> _matcher;
  std::unordered_map<std::string, size_t> _checked_uris;
  std::unordered_set<std::string> _whitelist_uris = {
      "youtube.com",  "x.com",
      "bsky.app",     "media.tenor.com",
      "facebook.com", "instagram.com",
      "twitch.tv",    "amazon.com",
      "amzn.to",      "youtu.be",
      "etsy.com",     "google.com",
      "tiktok.com",   "netflix.com",
      "ebay.com",     "reddit.com",
      "cnn.com",      "open.spotify.com",
      "twitter.com",  "open.substack.com"};
};

} // namespace moderation
} // namespace bsky

#endif