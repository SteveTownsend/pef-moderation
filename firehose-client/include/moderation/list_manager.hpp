#ifndef __list_manager__
#define __list_manager__
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
#include "moderation/session_manager.hpp"
#include "queue/readerwritercircularbuffer.h"
#include "yaml-cpp/yaml.h"
#include <optional>
#include <thread>

namespace bsky {

namespace moderation {

// app.bsky.graph.getLists
struct get_lists_request {
  std::string actor;
  // currently we know for a fact there are not more than 100 lists
  int32_t limit = 50;
  std::string cursor;
};

struct list_definition {
  std::string uri;
  std::string name;
  int32_t listItemCount;
};

struct get_lists_response {
  std::string cursor;
  std::vector<list_definition> lists;
};

// app.bsky.graph.getList
struct get_list_request {
  std::string list;
  // currently we know for a fact there are not more than 100 lists
  int32_t limit = 50;
  std::string cursor;
};

struct item_definition {
  std::string uri;
};

struct get_list_response {
  std::string cursor;
  std::vector<item_definition> items;
};

// com.atproto.repo.createRecord (any)
struct create_record_response {
  std::string uri;
};

// com.atproto.repo.createRecord (app.bsky.graph.list)
struct list {
  std::string _type = std::string(AppBskyGraphList);
  std::string purpose = std::string(AppBskyGraphDefsModlist);
  std::string name;
  std::string description;
  std::string createdAt =
      std::format("{0:%F}T{0:%T}Z", std::chrono::utc_clock::now());
};

struct create_list_request {
  std::string repo;
  std::string collection = std::string(AppBskyGraphList);
  list record;
};

// com.atproto.repo.createRecord (app.bsky.graph.listitem)
struct listitem {
  std::string _type = std::string(AppBskyGraphListItem);
  std::string subject;
  std::string list;
  std::string createdAt =
      std::format("{0:%F}T{0:%T}Z", std::chrono::utc_clock::now());
};

struct create_listitem_request {
  std::string repo;
  std::string collection = std::string(AppBskyGraphListItem);
  listitem record;
};

} // namespace moderation

} // namespace bsky

struct block_list_addition {
  std::string _did;
  std::string _list_name;
};

typedef std::unordered_map<std::string, atproto::at_uri> list_uri_by_name;
typedef std::unordered_map<atproto::at_uri, std::unordered_set<std::string>,
                           atproto::at_uri_hash>
    list_membership;

class list_manager {
public:
  // allow a large backlog - queued items are small and we need to manage rate
  // of record creation to obey rate limits
  static constexpr size_t QueueLimit = 50000;
  static constexpr std::chrono::milliseconds DequeueTimeout =
      std::chrono::milliseconds(10000);

  static list_manager &instance();
  void set_config(YAML::Node const &settings);

  void start();
  void wait_enqueue(block_list_addition &&value);
  void register_block_reason(std::string const &list_name,
                             std::string const &reason) {
    _block_reasons[list_name].insert(reason);
  }

private:
  list_manager();
  ~list_manager() = default;

  void lazy_load_managed_lists();

  inline atproto::at_uri list_is_available(std::string const &list_name) const {
    auto const &list_xref(_list_lookup.find(list_name));
    if (list_xref != _list_lookup.cend() &&
        _list_members.find(list_xref->second) != _list_members.cend()) {
      return list_xref->second;
    }
    return atproto::at_uri::empty();
  }

  inline bool is_account_in_list(std::string const &did,
                                 std::string const &list_name) const {
    auto const &list_xref(_list_lookup.find(list_name));
    if (list_xref == _list_lookup.cend()) {
      return false;
    }
    auto this_list(_list_members.find(list_xref->second));
    return this_list != _list_members.cend() && this_list->second.contains(did);
  }

  inline atproto::at_uri record_account_in_list(std::string const &did,
                                                std::string const &list_name) {
    auto const &list_xref(_list_lookup.find(list_name));
    if (list_xref == _list_lookup.end()) {
      return atproto::at_uri::empty();
    }
    auto this_list(_list_members.find(list_xref->second));
    if (this_list == _list_members.end()) {
      _list_members.insert({list_xref->second, {{did}}});
    } else {
      this_list->second.insert(did);
    }
    return list_xref->second;
  }

  inline void make_known_list_available(std::string const &list_name,
                                        atproto::at_uri const &uri) {
    if (!_list_lookup.insert({list_name, uri}).second) {
      REL_ERROR("Registering list {} with uri {} failed, already registered",
                list_name, std::string(uri));
    }
    if (!_list_members.insert({uri, {}}).second) {
      REL_ERROR(
          "Registering list {} with uri {} failed, membership-list already "
          "registered",
          list_name, std::string(uri));
    }
  }

  inline std::string block_reasons(std::string const &list_name) const {
    std::ostringstream oss;
    oss << "Auto-blocked by string-match rule(s):";
    auto const &reasons(_block_reasons.find(list_name));
    if (reasons != _block_reasons.cend()) {
      for (auto const &reason : reasons->second) {
        oss << ' \'' << reason << '\'';
      }
      return oss.str();
    }
    // unexpected but OK
    return {};
  }

  atproto::at_uri load_or_create_list(std::string const &list_name);
  atproto::at_uri ensure_list_is_available(std::string const &list_name);

  atproto::at_uri add_account_to_list(std::string const &did,
                                      std::string const &list_name);

  std::thread _thread;
  std::unique_ptr<restc_cpp::RestClient> _rest_client;
  std::unique_ptr<bsky::pds_session> _session;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingReaderWriterCircularBuffer<block_list_addition> _queue;
  std::string _handle;
  std::string _password;
  std::string _host;
  std::string _port;
  std::string _client_did;
  bool _dry_run = true;
  list_uri_by_name _list_lookup;
  list_membership _list_members;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      _block_reasons;
};
#endif