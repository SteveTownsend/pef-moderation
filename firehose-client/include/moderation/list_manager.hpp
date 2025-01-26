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
#include "blockingconcurrentqueue.h"
#include "firehost_client_config.hpp"
#include "helpers.hpp"
#include "jwt-cpp/jwt.h"
#include "matcher.hpp"
#include "moderation/ozone_adapter.hpp"
#include "moderation/session_manager.hpp"
#include "yaml-cpp/yaml.h"
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace bsky {
// app.bsky.richtext.facet
struct byte_slice {
  std::string _type;
  int32_t byteStart;
  int32_t byteEnd;
};

struct facet_data {
  std::string _type;
  std::string did; // mention
  std::string tag; // hashtag
  std::string uri; // link
};

// app.bsky.richtext.facet
struct richtext_facet {
  std::string _type = std::string(AppBskyRichtextFacet);
  byte_slice index;
  std::vector<facet_data> features;
};

// app.bsky.graph.list - restricted to the fields used by lists we manage
struct list {
  std::string _type = std::string(AppBskyGraphList);
  std::string purpose = std::string(AppBskyGraphDefsModlist);
  std::string name;
  std::string description;
  std::vector<richtext_facet> descriptionFacets;
  std::string createdAt = print_current_time();
};

// app.bsky.graph.listitem
struct listitem {
  std::string _type = std::string(AppBskyGraphListItem);
  std::string subject;
  std::string list;
  std::string createdAt = print_current_time();
};

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

struct item_subject {
  std::string did;
};
struct item_definition {
  std::string uri;
  item_subject subject;
};

struct get_list_response {
  std::string cursor;
  std::vector<item_definition> items;
};

} // namespace bsky

namespace atproto {

// com.atproto.repo.createRecord (any)
struct create_record_response {
  std::string uri;
  std::string cid;
};

// com.atproto.repo.putRecord (any)
struct put_record_response {
  std::string uri;
  std::string cid;
};

struct create_record_list_request {
  std::string repo;
  std::string collection = std::string(bsky::AppBskyGraphList);
  bsky::list record;
};

struct create_record_listitem_request {
  std::string repo;
  std::string collection = std::string(bsky::AppBskyGraphListItem);
  bsky::listitem record;
};

struct get_record_list_response {
  std::string uri;
  std::string cid;
  bsky::list value;
};

struct put_record_list_request {
  std::string repo;
  std::string collection;
  std::string rkey;
  bsky::list record;
};

} // namespace atproto

struct block_list_addition {
  std::string _did;
  // block={name} in string filter rule identifies a *group of lists*.
  // The most recent and current-active has a name matching the group-name. The
  // others have names suffixed by the date/time they were rolled off as full.
  // The dated ones are archived - we just load their members to avoid
  // reprocessing.
  std::string _list_group_name;
};

typedef std::unordered_map<std::string, atproto::at_uri> list_uris_by_name;
typedef std::unordered_map<std::string, std::unordered_set<std::string>>
    active_list_membership_for_group;
typedef std::unordered_map<std::string, std::unordered_set<std::string>>
    list_group_membership;

class list_manager {
public:
  // allow a large backlog - queued items are small and we need to manage rate
  // of record creation to obey rate limits
  static constexpr size_t QueueLimit = 50000;
  static constexpr std::chrono::milliseconds DequeueTimeout =
      std::chrono::milliseconds(10000);
  static constexpr size_t MaxItemsInList = 5000;

  static list_manager &instance();
  void set_config(YAML::Node const &settings);
  void
  set_moderation_data(std::shared_ptr<bsky::moderation::ozone_adapter> &ozone) {
    _moderation_data = ozone;
  }

  void start();
  void wait_enqueue(block_list_addition &&value);
  void register_block_reason(std::string const &list_name,
                             std::string const &reason) {
    _block_reasons[list_name].insert(reason);
  }

  inline static bool is_active_list_for_group(std::string const &list_name) {
    return !list_name.contains('-');
  }

private:
  list_manager();
  ~list_manager() = default;

  void lazy_load_managed_lists();

  inline atproto::at_uri list_is_available(std::string const &list_name) const {
    auto const &list_xref(_list_lookup.find(list_name));
    if (list_xref != _list_lookup.cend()) {
      return list_xref->second;
    }
    return atproto::at_uri::empty();
  }

  inline bool
  is_account_in_list_group(std::string const &did,
                           std::string const &list_group_name) const {
    auto const &list_group_members(_list_group_members.find(list_group_name));
    return list_group_members != _list_group_members.cend() &&
           list_group_members->second.contains(did);
  }

  inline void record_account_in_list_and_group(std::string const &did,
                                               std::string const &list_name) {
    if (is_active_list_for_group(list_name)) {
      auto this_list(_active_list_members_for_group.find(list_name));
      if (this_list == _active_list_members_for_group.end()) {
        _active_list_members_for_group.insert({list_name, {{did}}});
      } else {
        this_list->second.insert(did);
      }
    }
    auto this_list_group(
        _list_group_members.find(as_list_group_name(list_name)));
    if (this_list_group == _list_group_members.end()) {
      _list_group_members.insert({as_list_group_name(list_name), {{did}}});
    } else {
      this_list_group->second.insert(did);
    }
  }

  inline static std::string as_list_group_name(std::string const &list_name) {
    size_t offset(list_name.find('='));
    if (offset == std::string::npos) {
      return list_name;
    }
    return list_name.substr(0, offset);
  }

  inline void make_known_list_available(std::string const &list_name,
                                        atproto::at_uri const &uri) {
    if (!_list_lookup.insert({list_name, uri}).second) {
      REL_ERROR("Registering list {} with uri {} failed, already registered",
                list_name, std::string(uri));
    }
    // this is the normal case for list archival on size-limit being reached
    if (!_list_group_members.insert({as_list_group_name(list_name), {}})
             .second) {
      REL_INFO("Registering group for list {} with uri {} failed, already "
               "registered",
               list_name, std::string(uri));
    }
    if (is_active_list_for_group(list_name) &&
        !_active_list_members_for_group.insert({list_name, {}}).second) {
      REL_ERROR("Registering active list {} with uri {} failed, "
                "membership-list already "
                "registered",
                list_name, std::string(uri));
    }
  }

  inline std::string block_reasons(std::string const &list_name) const {
    std::ostringstream oss;
    constexpr size_t MaxRules = 20;
    auto const &reasons(_block_reasons.find(list_name));
    if (reasons != _block_reasons.cend()) {
      oss << "Auto-blocked by " << reasons->second.size()
          << " string-match rule(s):";
      size_t rule(0);
      for (auto const &reason : reasons->second) {
        if (++rule >= MaxRules) {
          oss << ", ...";
          break;
        }
        oss << " '" << reason << "'";
      }
      return oss.str();
    }
    // unexpected but OK
    return {};
  }

  atproto::at_uri load_or_create_list(std::string const &list_name);
  atproto::at_uri
  ensure_list_group_is_available(std::string const &list_group_name);
  atproto::at_uri archive_if_needed(std::string const &list_group_name,
                                    atproto::at_uri const &list_uri);

  atproto::at_uri
  add_account_to_list_and_group(std::string const &did,
                                std::string const &list_group_name);

  std::shared_ptr<bsky::moderation::ozone_adapter> _moderation_data;
  std::thread _thread;
  std::unique_ptr<restc_cpp::RestClient> _rest_client;
  std::unique_ptr<bsky::pds_session> _session;
  // Declare queue between match post-processing and HTTP Client
  moodycamel::BlockingConcurrentQueue<block_list_addition> _queue;
  std::string _handle;
  std::string _password;
  std::string _host;
  std::string _port;
  std::string _client_did;
  bool _dry_run = true;
  list_uris_by_name _list_lookup;
  list_group_membership _list_group_members;
  active_list_membership_for_group _active_list_members_for_group;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      _block_reasons;
};
#endif