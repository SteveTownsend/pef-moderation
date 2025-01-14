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

#include "moderation/list_manager.hpp"
#include "jwt-cpp/traits/boost-json/traits.h"
#include "log_wrapper.hpp"
#include "matcher.hpp"
#include "metrics.hpp"
#include "restc-cpp/RequestBuilder.h"
#include <boost/fusion/adapted.hpp>
#include <functional>

// app.bsky.graph.getLists
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::list_definition, (std::string, uri),
                          (std::string, name), (int32_t, listItemCount))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::get_lists_response,
                          (std::string, cursor),
                          (std::vector<bsky::moderation::list_definition>,
                           lists))

// app.bsky.graph.getList
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::item_definition, (std::string, uri))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::get_list_response,
                          (std::string, cursor),
                          (std::vector<bsky::moderation::item_definition>,
                           items))

// com.atproto.repo.createRecord (any)
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::create_record_response,
                          (std::string, uri))

// com.atproto.repo.createRecord (app.bsky.graph.list)
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::list, (std::string, _type),
                          (std::string, purpose), (std::string, name),
                          (std::string, description), (std::string, createdAt))

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::create_list_request,
                          (std::string, repo), (std::string, collection),
                          (bsky::moderation::list, record))

// com.atproto.repo.createRecord (app.bsky.graph.listitem)
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::listitem, (std::string, _type),
                          (std::string, subject), (std::string, list),
                          (std::string, createdAt))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::create_listitem_request,
                          (std::string, repo), (std::string, collection),
                          (bsky::moderation::listitem, record))

list_manager &list_manager::instance() {
  static list_manager my_instance;
  return my_instance;
}

list_manager::list_manager() : _queue(QueueLimit) {}

void list_manager::set_config(YAML::Node const &settings) {
  _handle = settings["handle"].as<std::string>();
  _password = settings["password"].as<std::string>();
  _host = settings["host"].as<std::string>();
  _port = settings["port"].as<std::string>();
  _dry_run = settings["dry_run"].as<bool>();
  _client_did = settings["client_did"].as<std::string>();
}

void list_manager::start() {
  _thread = std::thread([&] {
    // create client
    _rest_client = restc_cpp::RestClient::Create();

    // create session
    // bootstrap self-managed session from the returned tokens
    _session = std::make_unique<bsky::pds_session>(*_rest_client, _host);
    _session->connect(bsky::login_info({_handle, _password}));

    // this requires HTTP lookups and could take a while. Allow backlog while we
    // are doing this.
    lazy_load_managed_lists();

    while (true) {
      block_list_addition to_block;
      if (_queue.wait_dequeue_timed(to_block, DequeueTimeout)) {
        // process the item
        metrics::instance()
            .operational_stats()
            .Get({{"list_manager", "backlog"}})
            .Decrement();

        // do not process same account/list pair twice
        if (is_account_in_list(to_block._did, to_block._list_name)) {
          continue;
        }

        add_account_to_list(to_block._did, to_block._list_name);
        metrics::instance()
            .realtime_alerts()
            .Get({{"accounts", "block_listed"}})
            .Increment();

        // crude rate limit obedience, wait 7 seconds between high-frequency
        // create ops 86400 (seconds per day) / 16667 (creates per day) -> 7.406
        // seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(7000));
      }
      // check session status
      _session->check_refresh();

      // TODO terminate gracefully
    }
  });
}

void list_manager::wait_enqueue(block_list_addition &&value) {
  _queue.wait_enqueue(value);
  metrics::instance()
      .operational_stats()
      .Get({{"list_manager", "backlog"}})
      .Increment();
}

void list_manager::lazy_load_managed_lists() {
  // Get my lists
  REL_INFO("List load starting");
  try {
    std::string current_cursor;
    while (true) {
      bsky::moderation::get_lists_response response =
          _rest_client
              ->ProcessWithPromiseT<bsky::moderation::get_lists_response>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a report_response structure.
                    bsky::moderation::get_lists_response response;
                    bsky::moderation::get_lists_request request;
                    request.actor = _client_did;
                    if (!current_cursor.empty()) {
                      request.cursor = current_cursor;
                    }

                    // Construct a request to the server
                    restc_cpp::RequestBuilder builder(ctx);
                    builder.Get(_host + "app.bsky.graph.getLists")
                        .Header("Content-Type", "application/json")
                        .Header(
                            "Authorization",
                            std::string("Bearer " + _session->access_token()))
                        .Argument("actor", _client_did)
                        .Argument("limit", 50);
                    if (!current_cursor.empty()) {
                      builder.Argument("cursor", current_cursor);
                    }
                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(response,
                                                 // Send the request
                                                 builder.Execute());

                    // Return the session instance through C++ future<>
                    return response;
                  })

              // Get the Post instance from the future<>, or any C++ exception
              // thrown within the lambda.
              .get();
      for (auto const &next_list : response.lists) {
        REL_INFO("List load processing {}", next_list.name);
        make_known_list_available(next_list.name, next_list.uri);
        load_or_create_list(next_list.name);
      }
      if (!response.cursor.empty()) {
        current_cursor = response.cursor;
        REL_INFO("List load found next {} lists, cursor {}",
                 response.lists.size(), current_cursor);

      } else {
        REL_INFO("List load found final {} lists", response.lists.size());
        break;
      }
    }
    REL_INFO("List load complete");
  } catch (std::exception const &exc) {
    REL_ERROR("Get Lists {} exception {}", _handle, exc.what())
  }
}

// creates empty list if if does not exist
atproto::at_uri
list_manager::load_or_create_list(std::string const &list_name) {
  // assumes list was not created by hand after we loaded the startup list
  atproto::at_uri list_uri(list_is_available(list_name));
  if (!list_uri) {
    // Need to create the list before we add to it
    if (_dry_run) {
      REL_INFO("Dry-run creation of list {}", list_name);
      return list_uri;
    }
    REL_INFO("Create new list {}", list_name);
    try {
      restc_cpp::serialize_properties_t properties;
      properties.name_mapping = &json::TypeFieldMapping;
      bsky::moderation::create_record_response response =
          _rest_client
              ->ProcessWithPromiseT<bsky::moderation::create_record_response>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a report_response structure.
                    bsky::moderation::create_record_response response;
                    bsky::moderation::create_list_request request;
                    request.repo = _client_did;
                    request.record.name = list_name;
                    // for rule enforcement
                    request.record.description = block_reasons(list_name);

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(
                        response,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "com.atproto.repo.createRecord")
                            .Header("Content-Type", "application/json")
                            .Header("Authorization",
                                    std::string("Bearer " +
                                                _session->access_token()))
                            .Data(body.str())
                            // Send the request
                            .Execute());

                    // Return the created list through C++ future<>
                    return response;
                  })

              // Get the Post instance from the future<>, or any C++ exception
              // thrown within the lambda.
              .get();
      REL_INFO("create_record(list={}) yielded uri {}", list_name,
               response.uri);
      make_known_list_available(list_name, response.uri);
      list_uri = response.uri;
    } catch (std::exception const &exc) {
      REL_ERROR("create_record(list={}) exception  {}", list_name, exc.what())
    }
    // List was created and is ready for use
    return list_uri;
  }

  // Existing list - load by iterating using cursor
  try {
    std::string current_cursor;
    while (true) {
      restc_cpp::SerializeProperties properties;
      properties.ignore_empty_fileds = true;
      bsky::moderation::get_list_response response =
          _rest_client
              ->ProcessWithPromiseT<bsky::moderation::get_list_response>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a report_response structure.
                    bsky::moderation::get_list_response response;

                    // Construct a request to the server
                    restc_cpp::RequestBuilder builder(ctx);
                    builder.Get(_host + "app.bsky.graph.getList")
                        .Header("Content-Type", "application/json")
                        .Header(
                            "Authorization",
                            std::string("Bearer " + _session->access_token()))
                        .Argument("list", std::string(list_uri))
                        .Argument("limit", 50);
                    if (!current_cursor.empty()) {
                      builder.Argument("cursor", current_cursor);
                    }

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(response,

                                                 // Send the request
                                                 builder.Execute());
                    // TODO work out clean code path if it does not exist

                    // Return the list members through C++ future<>
                    return response;
                  })

              // Get the Post instance from the future<>, or any C++ exception
              // thrown within the lambda.
              .get();

      for (auto const &item : response.items) {
        // store list member using its DID
        record_account_in_list(atproto::at_uri(item.uri)._authority, list_name);
      }
      if (!response.cursor.empty()) {
        current_cursor = response.cursor;
        REL_INFO("List load get_list returned next {} items, cursor={}",
                 response.items.size(), current_cursor);
      } else {
        REL_INFO("List load get_list returned final {} items",
                 response.items.size());
        break;
      }
    }
  } catch (std::exception const &exc) {
    REL_ERROR("Get List {} exception  {}", list_name, exc.what())
  }
  return list_uri;
}

atproto::at_uri
list_manager::ensure_list_is_available(std::string const &list_name) {
  atproto::at_uri list_uri(list_is_available(list_name));
  if (list_uri) {
    return list_uri;
  }
  // check if platform knows about the list - it might have been created
  // manually since we loaded all known lists during on startup
  return load_or_create_list(list_name);
}

// TODO add metrics
atproto::at_uri
list_manager::add_account_to_list(std::string const &did,
                                  std::string const &list_name) {
  atproto::at_uri list_uri(record_account_in_list(did, list_name));
  if (_dry_run) {
    REL_INFO("Dry-run Added {} to list {}", did, list_name);
    return list_uri;
  }
  list_uri = ensure_list_is_available(list_name);

  size_t retries(0);
  while (retries < 5) {
    try {
      restc_cpp::SerializeProperties properties;
      properties.name_mapping = &json::TypeFieldMapping;
      bsky::moderation::create_record_response response =
          _rest_client
              ->ProcessWithPromiseT<bsky::moderation::create_record_response>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a report_response structure.
                    bsky::moderation::create_record_response response;
                    bsky::moderation::create_listitem_request request;
                    request.repo = _client_did;

                    bsky::moderation::listitem item;
                    item.subject = did;
                    item.list = std::string(list_uri);

                    request.record = item;

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(
                        response,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "com.atproto.repo.createRecord")
                            .Header("Content-Type", "application/json")
                            .Header("Authorization",
                                    std::string("Bearer " +
                                                _session->access_token()))
                            .Data(body.str())
                            // Send the request
                            .Execute());

                    // Return the session instance through C++ future<>
                    return response;
                  })

              // Get the Post instance from the future<>, or any C++ exception
              // thrown within the lambda.
              .get();
      REL_INFO("create-record(listitem={}|{}) yielded uri {}", did, list_name,
               response.uri);
      break;
    } catch (boost::system::system_error const &exc) {
      if (exc.code().value() == boost::asio::error::eof &&
          exc.code().category() == boost::asio::error::get_misc_category()) {
        REL_WARNING("IoReaderImpl::ReadSome: asio eof, retry");
        ++retries;
      } else {
        // unrecoverable error
        break;
      }
    } catch (std::exception const &exc) {
      REL_ERROR("create-record(listitem={}|{}) exception {}", did, list_name,
                exc.what())
      break;
    }
  }
  return list_uri;
}
