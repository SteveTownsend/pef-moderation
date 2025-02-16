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

#include "moderation/list_manager.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "common/rest_utils.hpp"
#include "jwt-cpp/traits/boost-json/traits.h"
#include "matcher.hpp"
#include "restc-cpp/RequestBuilder.h"
#include "restc-cpp/SerializeJson.h"
#include <boost/fusion/adapted.hpp>
#include <functional>

BOOST_FUSION_ADAPT_STRUCT(bsky::byte_slice, (std::string, _type),
                          (int32_t, byteStart), (int32_t, byteEnd))

BOOST_FUSION_ADAPT_STRUCT(bsky::facet_data, (std::string, _type),
                          (std::string, did), // mention
                          (std::string, tag), // hashtag
                          (std::string, uri)) // link

// app.bsky.richtext.facet
BOOST_FUSION_ADAPT_STRUCT(bsky::richtext_facet, (std::string, _type),
                          (bsky::byte_slice, index),
                          (std::vector<bsky::facet_data>, features))

// app.bsky.graph.list
BOOST_FUSION_ADAPT_STRUCT(bsky::list, (std::string, _type),
                          (std::string, purpose), (std::string, name),
                          (std::string, description),
                          (std::vector<bsky::richtext_facet>,
                           descriptionFacets),
                          (std::string, createdAt))

// app.bsky.graph.getLists
BOOST_FUSION_ADAPT_STRUCT(bsky::list_definition, (std::string, uri),
                          (std::string, name), (int32_t, listItemCount))
BOOST_FUSION_ADAPT_STRUCT(bsky::get_lists_response, (std::string, cursor),
                          (std::vector<bsky::list_definition>, lists))

// app.bsky.graph.getList
BOOST_FUSION_ADAPT_STRUCT(bsky::item_subject, (std::string, did))
BOOST_FUSION_ADAPT_STRUCT(bsky::item_definition,
                          (std::string, uri)(bsky::item_subject, subject))
BOOST_FUSION_ADAPT_STRUCT(bsky::get_list_response, (std::string, cursor),
                          (std::vector<bsky::item_definition>, items))

// com.atproto.repo.createRecord (any)
BOOST_FUSION_ADAPT_STRUCT(atproto::create_record_response, (std::string, uri),
                          (std::string, cid))

// com.atproto.repo.getRecord (any)
BOOST_FUSION_ADAPT_STRUCT(atproto::get_record_list_response, (std::string, uri),
                          (std::string, cid), (bsky::list, value))

// com.atproto.repo.putRecord (any)
BOOST_FUSION_ADAPT_STRUCT(atproto::put_record_response, (std::string, uri),
                          (std::string, cid))

// com.atproto.repo.createRecord (app.bsky.graph.list)
BOOST_FUSION_ADAPT_STRUCT(atproto::create_record_list_request,
                          (std::string, repo), (std::string, collection),
                          (bsky::list, record))

// com.atproto.repo.createRecord (app.bsky.graph.list)
BOOST_FUSION_ADAPT_STRUCT(atproto::put_record_list_request, (std::string, repo),
                          (std::string, collection),
                          (std::string, rkey)(bsky::list, record))

// com.atproto.repo.createRecord (app.bsky.graph.listitem)
BOOST_FUSION_ADAPT_STRUCT(bsky::listitem, (std::string, _type),
                          (std::string, subject), (std::string, list),
                          (std::string, createdAt))
BOOST_FUSION_ADAPT_STRUCT(atproto::create_record_listitem_request,
                          (std::string, repo), (std::string, collection),
                          (bsky::listitem, record))

list_manager &list_manager::instance() {
  static list_manager my_instance;
  return my_instance;
}

list_manager::list_manager() : _queue(QueueLimit) {}

void list_manager::start(YAML::Node const &settings) {
  _handle = settings["handle"].as<std::string>();
  _dry_run = settings["dry_run"].as<bool>();
  _client_did = settings["client_did"].as<std::string>();
  _thread = std::thread([&, this] {
    try {
      // create client
      _client = std::make_unique<bsky::client>();
      _client->set_config(settings);

      // this requires HTTP lookups and could take a while. Allow backlog while
      // we are doing this.
      lazy_load_managed_lists();

      while (controller::instance().is_active()) {
        block_list_addition to_block;
        if (_queue.wait_dequeue_timed(to_block, DequeueTimeout)) {
          // process the item
          metrics_factory::instance()
              .get_gauge("process_operation")
              .Get({{"list_manager", "backlog"}})
              .Decrement();

          // do not process if whitelisted
          if (bsky::moderation::ozone_adapter::instance().already_processed(
                  to_block._did)) {
            REL_INFO("skipping {} for list-group {}, already processed",
                     to_block._did, to_block._list_group_name);
            continue;
          }
          // do not process same account/list pair twice
          if (is_account_in_list_group(to_block._did,
                                       to_block._list_group_name)) {
            REL_INFO("skipping {}, aleady in list-group {}", to_block._did,
                     to_block._list_group_name);
            continue;
          }

          add_account_to_list_and_group(to_block._did,
                                        to_block._list_group_name);

          // crude rate limit obedience, wait 7 seconds between high-frequency
          // create ops 86400 (seconds per day) / 16667 (creates per day)
          // -> 7.406 seconds
          std::this_thread::sleep_for(std::chrono::milliseconds(7000));
        }
      }
    } catch (std::exception const &exc) {
      REL_ERROR("list_manager exception {}", exc.what());
      controller::instance().force_stop();
    }
    REL_INFO("list_manager stopping");
  });
}

void list_manager::wait_enqueue(block_list_addition &&value) {
  _queue.enqueue(value);
  metrics_factory::instance()
      .get_gauge("process_operation")
      .Get({{"list_manager", "backlog"}})
      .Increment();
}

void list_manager::lazy_load_managed_lists() {
  // Get my lists
  REL_INFO("List load starting");
  std::string current_cursor;
  bool done(false);
  while (!done) {
    bsky::client::get_callback_t callback =
        [&](restc_cpp::RequestBuilder &builder) {
          builder.Argument("actor", _client_did).Argument("limit", 50);
          if (!current_cursor.empty()) {
            builder.Argument("cursor", current_cursor);
          }
        };
    bsky::get_lists_response response =
        _client->do_get<bsky::get_lists_response>("app.bsky.graph.getLists",
                                                  callback);
    for (auto const &next_list : response.lists) {
      REL_INFO("List load processing {}", next_list.name);
      make_known_list_available(next_list.name, next_list.uri);
      // load membership for active and archived lists in every group
      load_or_create_list(next_list.name);
    }
    if (!response.cursor.empty()) {
      current_cursor = response.cursor;
      REL_INFO("List load found next {} lists, cursor {}",
               response.lists.size(), current_cursor);
    } else {
      REL_INFO("List load found final {} lists", response.lists.size());
      done = true;
    }
  }
  REL_INFO("List load complete");
}

// creates empty list if if does not exist
atproto::at_uri
list_manager::load_or_create_list(std::string const &list_name) {
  // assumes list was not created by hand after we loaded the startup list
  // skip this check for archived lists which are only loaded
  atproto::at_uri list_uri(list_is_available(list_name));
  if (!list_uri) {
    // Need to create the list before we add to it
    if (_dry_run) {
      REL_INFO("Dry-run creation of list {}", list_name);
      return list_uri;
    }
    REL_INFO("Create new list {}", list_name);
    atproto::create_record_list_request request;
    request.repo = _client_did;
    request.record.name = list_name;
    // for rule enforcement - truncate description if too long
    request.record.description =
        block_reasons(list_name).substr(0, bsky::GraphListDescriptionLimit);
    constexpr bool use_refresh_token(false);
    constexpr bool no_post_log(false);
    atproto::create_record_response response =
        _client->do_post<atproto::create_record_list_request,
                         atproto::create_record_response>(
            "com.atproto.repo.createRecord", request, use_refresh_token,
            no_post_log);
    // List was created, is empty, and is ready for use
    return list_uri;
  }

  // Existing list - load by iterating using cursor
  std::string current_cursor;
  bool done(false);
  while (!done) {
    restc_cpp::SerializeProperties properties;
    properties.ignore_empty_fileds = true;
    bsky::client::get_callback_t callback =
        [&](restc_cpp::RequestBuilder &builder) {
          builder.Argument("list", std::string(list_uri)).Argument("limit", 50);
          if (!current_cursor.empty()) {
            builder.Argument("cursor", current_cursor);
          }
        };
    bsky::get_list_response response = _client->do_get<bsky::get_list_response>(
        "app.bsky.graph.getList", callback);
    for (auto const &item : response.items) {
      // store list member using its DID
      record_account_in_list_and_group(item.subject.did, list_name);
    }
    if (!response.cursor.empty()) {
      current_cursor = response.cursor;
      REL_INFO("List load get_list returned next {} items, cursor={}",
               response.items.size(), current_cursor);
    } else {
      REL_INFO("List load get_list returned final {} items",
               response.items.size());
      done = true;
      metrics_factory::instance()
          .get_counter("automation")
          .Get({{"block_list", "list_group"},
                {"list_count", as_list_group_name(list_name)}})
          .Increment();
      break;
    }
  }
  return list_uri;
}

atproto::at_uri list_manager::ensure_list_group_is_available(
    std::string const &list_group_name) {
  atproto::at_uri list_uri(list_is_available(list_group_name));
  if (list_uri) {
    return list_uri;
  }
  // check if platform knows about the list - it might have been created
  // manually since we loaded all known lists during on startup
  return load_or_create_list(list_group_name);
}

// we have in hand the URI and name for a group's active list. If it is too
// large, archive with a timestamped rename and create a new group as the
// active
atproto::at_uri
list_manager::archive_if_needed(std::string const &list_group_name,
                                atproto::at_uri const &list_uri) {
  auto const &list_members(
      _active_list_members_for_group.find(list_group_name));
  if (_dry_run) {
    // no archival should happen
    return list_uri;
  }

  if (list_members != _active_list_members_for_group.cend()) {
    if (list_members->second.size() >= MaxItemsInList) {
      // rename the full active list and create a new list as the active.
      // Existing list members are already recorded by group.
      // Try rename first, if it fails just leave as is
      atproto::get_record_list_response response;
      try {
        response = _client->get_record<atproto::get_record_list_response>(
            list_uri._authority, list_uri._collection, list_uri._rkey);
      } catch (std::exception const &exc) {
        REL_ERROR("archive_if_needed: get_record error {}", exc.what());
        return list_uri;
      }

      // update to mark it archived and write back
      atproto::put_record_list_request request;
      request.repo = list_uri._authority;
      request.collection = list_uri._collection;
      request.rkey = list_uri._rkey;
      request.record = response.value;
      std::string archived_name(request.record.name + "-" +
                                print_current_time());
      request.record.name = archived_name;
      request.record.description =
          request.record.description + "\nArchived with " +
          std::to_string(list_members->second.size()) + " members";

      try {
        atproto::put_record_response put_response =
            _client->put_record<atproto::put_record_list_request>(request);
      } catch (std::exception const &exc) {
        REL_ERROR("archive_if_needed: put_record error {}", exc.what());
        return list_uri;
      }

      // Full list has been archived. Reset its in-store membership and create
      // the new active list. The new list will have a different URI.
      _active_list_members_for_group.erase(list_members);
      _list_lookup.erase(list_group_name);
      return load_or_create_list(list_group_name);

    } else {
      // continue using this list until it is over maximum size
      return list_uri;
    }
  } else {
    REL_WARNING("Membership for list group {} not found, unexpected");
    return list_uri;
  }
}

// TODO add metrics
atproto::at_uri list_manager::add_account_to_list_and_group(
    std::string const &did, std::string const &list_group_name) {
  record_account_in_list_and_group(did, list_group_name);
  if (_dry_run) {
    REL_INFO("Dry-run Added {} to list group {}", did, list_group_name);
    return atproto::at_uri::empty();
  }
  atproto::at_uri list_uri(ensure_list_group_is_available(list_group_name));
  list_uri = archive_if_needed(list_group_name, list_uri);

  try {
    atproto::create_record_listitem_request request;
    request.repo = _client_did;

    bsky::listitem item;
    item.subject = did;
    item.list = std::string(list_uri);

    request.record = item;
    atproto::create_record_response response =
        _client->create_record<atproto::create_record_listitem_request>(
            request);
    metrics_factory::instance()
        .get_counter("automation")
        .Get({{"block_list", "list_group"}, {"added", list_group_name}})
        .Increment();
  } catch (std::exception &) {
    metrics_factory::instance()
        .get_counter("automation")
        .Get({{"block_list", "list_group"}, {"add_failed", list_group_name}})
        .Increment();
  }
  return list_uri;
}
