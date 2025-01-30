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
#include "controller.hpp"
#include "jwt-cpp/traits/boost-json/traits.h"
#include "log_wrapper.hpp"
#include "matcher.hpp"
#include "metrics.hpp"
#include "restc-cpp/RequestBuilder.h"
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
    try {
      // create client
      _rest_client = restc_cpp::RestClient::Create();

      // create session
      // bootstrap self-managed session from the returned tokens
      _session = std::make_unique<bsky::pds_session>(*_rest_client, _host);
      _session->connect(bsky::login_info({_handle, _password}));

      // this requires HTTP lookups and could take a while. Allow backlog while
      // we are doing this.
      lazy_load_managed_lists();

      while (controller::instance().is_active()) {
        block_list_addition to_block;
        if (_queue.wait_dequeue_timed(to_block, DequeueTimeout)) {
          // process the item
          metrics::instance()
              .operational_stats()
              .Get({{"list_manager", "backlog"}})
              .Decrement();

          // do not process if whitelisted
          if (_moderation_data->already_processed(to_block._did)) {
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
        // check session status
        _session->check_refresh();
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
  metrics::instance()
      .operational_stats()
      .Get({{"list_manager", "backlog"}})
      .Increment();
}

void list_manager::lazy_load_managed_lists() {
  // Get my lists
  REL_INFO("List load starting");
  std::string current_cursor;
  bool done(false);
  while (!done) {
    bsky::get_lists_response response;
    size_t retries(0);
    while (retries < 5) {
      try {
        response =
            _rest_client
                ->ProcessWithPromiseT<bsky::get_lists_response>(
                    [&](restc_cpp::Context &ctx) {
                      // This is a co-routine, running in a worker-thread

                      // Instantiate a report_response structure.
                      bsky::get_lists_response response;
                      bsky::get_lists_request request;
                      request.actor = _client_did;
                      if (!current_cursor.empty()) {
                        request.cursor = current_cursor;
                      }

                      // Construct a request to the server
                      restc_cpp::RequestBuilder builder(ctx);
                      builder.Get(_host + "app.bsky.graph.getLists")
                          .Header(
                              "Authorization",
                              std::string("Bearer " + _session->access_token()))
                          .Argument("actor", _client_did)
                          .Argument("limit", 50);
                      if (!current_cursor.empty()) {
                        builder.Argument("cursor", current_cursor);
                      }
                      // Serialize it asynchronously. The asynchronously part
                      // does not really matter here, but it may if you receive
                      // huge data structures.
                      restc_cpp::SerializeFromJson(response,
                                                   // Send the request
                                                   builder.Execute(),
                                                   &json::TypeFieldMapping);

                      // Return the session instance through C++ future<>
                      return response;
                    })

                // Get the Post instance from the future<>, or any C++ exception
                // thrown within the lambda.
                .get();
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
          retries = 0;
        } else {
          REL_INFO("List load found final {} lists", response.lists.size());
          done = true;
          break;
        }
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING("IoReaderImpl::ReadSome(getLists): asio eof, retry");
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("Get Lists Boost exception {}", exc.what())
          done = true;
          break;
        }
      } catch (std::exception const &exc) {
        REL_ERROR("Get Lists {} exception {}", _handle, exc.what())
        done = true;
        break;
      }
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
    size_t retries(0);
    atproto::create_record_response response;
    while (retries < 5) {
      try {
        restc_cpp::serialize_properties_t properties;
        properties.name_mapping = &json::TypeFieldMapping;
        response =
            _rest_client
                ->ProcessWithPromiseT<atproto::create_record_response>(
                    [&](restc_cpp::Context &ctx) {
                      // This is a co-routine, running in a worker-thread

                      // Instantiate a report_response structure.
                      atproto::create_record_response response;
                      atproto::create_record_list_request request;
                      request.repo = _client_did;
                      request.record.name = list_name;
                      // for rule enforcement
                      request.record.description = block_reasons(list_name);

                      std::ostringstream body;
                      restc_cpp::SerializeToJson(request, body, properties);

                      // Serialize it asynchronously. The asynchronously part
                      // does not really matter here, but it may if you
                      // receive huge data structures.
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

                // Get the Post instance from the future<>, or any C++
                // exception thrown within the lambda.
                .get();
        REL_INFO("create_record(list={}) yielded uri {}", list_name,
                 response.uri);
        make_known_list_available(list_name, response.uri);
        list_uri = response.uri;
        metrics::instance()
            .automation_stats()
            .Get({{"block_list", "list_group"},
                  {"list_count", as_list_group_name(list_name)}})
            .Increment();
        break;
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING(
              "IoReaderImpl::ReadSome(create_record(list={})): asio eof, retry",
              list_name);
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("create_record(list={}) Boost exception {}", list_name,
                    exc.what())
          break;
        }

      } catch (std::exception const &exc) {
        REL_ERROR("create_record(list={}) exception  {}", list_name, exc.what())
        break;
      }
    }
    // List was created, is empty, and is ready for use
    return list_uri;
  }

  // Existing list - load by iterating using cursor
  std::string current_cursor;
  bool done(false);
  while (!done) {
    bsky::get_list_response response;
    restc_cpp::SerializeProperties properties;
    properties.ignore_empty_fileds = true;
    size_t retries(0);
    while (retries < 5) {
      try {
        response = _rest_client
                       ->ProcessWithPromiseT<bsky::get_list_response>(
                           [&](restc_cpp::Context &ctx) {
                             // This is a co-routine, running in a worker-thread

                             // Instantiate a report_response structure.
                             bsky::get_list_response response;

                             // Construct a request to the server
                             restc_cpp::RequestBuilder builder(ctx);
                             builder.Get(_host + "app.bsky.graph.getList")
                                 .Header("Authorization",
                                         std::string("Bearer " +
                                                     _session->access_token()))
                                 .Argument("list", std::string(list_uri))
                                 .Argument("limit", 50);
                             if (!current_cursor.empty()) {
                               builder.Argument("cursor", current_cursor);
                             }

                             // Serialize it asynchronously. The asynchronously
                             // part does not really matter here, but it may if
                             // you receive huge data structures.
                             restc_cpp::SerializeFromJson(response,
                                                          // Send the request
                                                          builder.Execute());
                             // Return the list members through C++ future<>
                             return response;
                           })

                       // Get the Post instance from the future<>, or any C++
                       // exception thrown within the lambda.
                       .get();

        for (auto const &item : response.items) {
          // store list member using its DID
          record_account_in_list_and_group(item.subject.did, list_name);
        }
        if (!response.cursor.empty()) {
          current_cursor = response.cursor;
          REL_INFO("List load get_list returned next {} items, cursor={}",
                   response.items.size(), current_cursor);
          retries = 0;
        } else {
          REL_INFO("List load get_list returned final {} items",
                   response.items.size());
          done = true;
          metrics::instance()
              .automation_stats()
              .Get({{"block_list", "list_group"},
                    {"list_count", as_list_group_name(list_name)}})
              .Increment();
          break;
        }
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING("IoReaderImpl::ReadSome(getLists): asio eof, retry");
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("Get Lists Boost exception {}", exc.what())
          done = true;
          break;
        }
      } catch (std::exception const &exc) {
        REL_ERROR("Get List {} exception  {}", list_name, exc.what())
        done = true;
        break;
      }
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
      size_t retries(0);
      // Instantiate a report_response structure, we will edit the record with
      // updates and put it back
      atproto::get_record_list_response response;

      while (retries < 5) {
        try {
          restc_cpp::SerializeProperties properties;
          properties.name_mapping = &json::TypeFieldMapping;
          response =
              _rest_client
                  ->ProcessWithPromiseT<atproto::get_record_list_response>(
                      [&](restc_cpp::Context &ctx) {
                        // This is a co-routine, running in a worker-thread

                        // Serialize it asynchronously. The asynchronously
                        // part does not really matter here, but it may if you
                        // receive huge data structures.
                        restc_cpp::SerializeFromJson(
                            response,

                            // Construct a request to the server
                            restc_cpp::RequestBuilder(ctx)
                                .Get(_host + "com.atproto.repo.getRecord")
                                .Header("Authorization",
                                        std::string("Bearer " +
                                                    _session->access_token()))
                                .Argument("repo", list_uri._authority)
                                .Argument("collection", list_uri._collection)
                                .Argument("rkey", list_uri._rkey)
                                // Send the request
                                .Execute(),
                            &json::TypeFieldMapping);

                        // Return the list record instance through C++
                        // future<>
                        return response;
                      })

                  // Get the Post instance from the future<>, or any C++
                  // exception thrown within the lambda.
                  .get();
          REL_INFO("get-record(list) OK for {}", std::string(list_uri));
          break;
        } catch (boost::system::system_error const &exc) {
          if (exc.code().value() == boost::asio::error::eof &&
              exc.code().category() ==
                  boost::asio::error::get_misc_category()) {
            REL_WARNING(
                "IoReaderImpl::ReadSome(get-record(list)): asio eof, retry");
            ++retries;
          } else {
            // unrecoverable error
            break;
          }
        } catch (std::exception const &exc) {
          REL_ERROR("get-record(list) for list_uri exception {}",
                    std::string(list_uri), exc.what())
          break;
        }
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
      retries = 0;
      atproto::put_record_response put_response;
      while (retries < 5) {
        try {
          restc_cpp::SerializeProperties properties;
          properties.name_mapping = &json::TypeFieldMapping;
          put_response =
              _rest_client
                  ->ProcessWithPromiseT<atproto::put_record_response>(
                      [&](restc_cpp::Context &ctx) {
                        // This is a co-routine, running in a worker-thread

                        std::ostringstream body;
                        restc_cpp::SerializeToJson(request, body, properties);

                        // Serialize it asynchronously. The asynchronously
                        // part does not really matter here, but it may if you
                        // receive huge data structures.
                        restc_cpp::SerializeFromJson(
                            put_response,

                            // Construct a request to the server
                            restc_cpp::RequestBuilder(ctx)
                                .Post(_host + "com.atproto.repo.putRecord")
                                .Header("Content-Type", "application/json")
                                .Header("Authorization",
                                        std::string("Bearer " +
                                                    _session->access_token()))
                                .Data(body.str())
                                // Send the request
                                .Execute());

                        // Return the list record instance through C++
                        // future<>
                        return put_response;
                      })

                  // Get the Post instance from the future<>, or any C++
                  // exception thrown within the lambda.
                  .get();
          REL_INFO("put-record(list) OK for {}", std::string(list_uri));
          break;
        } catch (boost::system::system_error const &exc) {
          if (exc.code().value() == boost::asio::error::eof &&
              exc.code().category() ==
                  boost::asio::error::get_misc_category()) {
            REL_WARNING(
                "IoReaderImpl::ReadSome(put-record(list)): asio eof, retry");
            ++retries;
          } else {
            // unrecoverable error
            break;
          }
        } catch (std::exception const &exc) {
          REL_ERROR("put-record(list) for list_uri {} exception {}",
                    std::string(list_uri), exc.what())
          break;
        }
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

  bool done(false);
  size_t retries(0);
  atproto::create_record_response response;
  while (retries < 5) {
    try {
      restc_cpp::SerializeProperties properties;
      properties.name_mapping = &json::TypeFieldMapping;
      response =
          _rest_client
              ->ProcessWithPromiseT<atproto::create_record_response>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a report_response structure.
                    atproto::create_record_listitem_request request;
                    request.repo = _client_did;

                    bsky::listitem item;
                    item.subject = did;
                    item.list = std::string(list_uri);

                    request.record = item;

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part
                    // does not really matter here, but it may if you receive
                    // huge data structures.
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
      REL_INFO("create-record(listitem={}|{}) yielded uri {}", did,
               list_group_name, response.uri);
      done = true;
      break;
    } catch (boost::system::system_error const &exc) {
      if (exc.code().value() == boost::asio::error::eof &&
          exc.code().category() == boost::asio::error::get_misc_category()) {
        REL_WARNING("IoReaderImpl::ReadSome(createListItem): asio eof, retry");
        ++retries;
      } else {
        // unrecoverable error
        break;
      }
    } catch (std::exception const &exc) {
      REL_ERROR("create-record(listitem={}|{}) exception {}", did,
                list_group_name, exc.what())
      break;
    }
  }
  if (done) {
    metrics::instance()
        .automation_stats()
        .Get({{"block_list", "list_group"}, {"added", list_group_name}})
        .Increment();
  } else {
    metrics::instance()
        .automation_stats()
        .Get({{"block_list", "list_group"}, {"add_failed", list_group_name}})
        .Increment();
  }
  return list_uri;
}
