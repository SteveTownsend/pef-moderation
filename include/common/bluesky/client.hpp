#pragma once
/*************************************************************************
Public Education Forum Moderation Firehose Client
Copyright (c) Steve Townsend 2025

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
#include "common/bluesky/platform.hpp"
#include "common/log_wrapper.hpp"
#include "common/moderation/session_manager.hpp"
#include "common/rest_utils.hpp"
#include "metrics.hpp"

// clang-format off
#include "restc-cpp/restc-cpp.h"
// clang-format on
#include "restc-cpp/RequestBody.h"
#include "restc-cpp/RequestBuilder.h"
#include "restc-cpp/SerializeJson.h"
#include "yaml-cpp/yaml.h"
#include <thread>

namespace bsky {
namespace moderation {
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

struct label_event {
  std::string _type = std::string(bsky::moderation::EventLabel);
  std::vector<std::string> createLabelVals;
  std::vector<std::string> negateLabelVals;
};
// Label auto-reported account. Associated eport indicates context
struct emit_event_label_request {
  label_event event;
  report_subject subject;
  std::string createdBy;
};
struct emit_event_label_response {
  // ignore other fields
  std::string createdAt;
  int64_t id;
  std::string createdBy;
};
} // namespace moderation

class client {
public:
  struct empty_response {
    inline std::string as_string() { return {}; }
  };
  client() = default;
  ~client() = default;

  void set_config(YAML::Node const &settings);
  std::string service_did() const { return _service_did; }
  inline bool is_ready() const { return _is_ready; }
  // check session status
  inline void check_session() {
    if (_use_token) {
      _session->check_refresh();
    }
  }

  void label_account(std::string const &did,
                     std::vector<std::string> const &labels);

  template <typename RESPONSE>
  RESPONSE get_record(std::string const &did, std::string const &collection,
                      std::string const &rkey) {
    size_t retries(0);
    // Instantiate a get_record structure, we will edit the record with
    // updates and put it back
    RESPONSE response;

    while (retries < 5) {
      try {
        restc_cpp::SerializeProperties properties;
        properties.name_mapping = &json::TypeFieldMapping;
        response =
            _rest_client
                ->ProcessWithPromiseT<RESPONSE>([&](restc_cpp::Context &ctx) {
                  // This is a co-routine, running in a worker-thread
                  // Construct a request to the server

                  restc_cpp::SerializeFromJson(
                      response,
                      // Send the request
                      restc_cpp::RequestBuilder(ctx)
                          .Get(_host + "com.atproto.repo.getRecord")
                          .Header(
                              "Authorization",
                              std::string("Bearer " + _session->access_token()))
                          .Argument("repo", did)
                          .Argument("collection", collection)
                          .Argument("rkey", rkey)
                          .Execute(),
                      &json::TypeFieldMapping);

                  // Return the list record instance through C++
                  // future<>
                  return response;
                })

                // Get the Post instance from the future<>, or any C++
                // exception thrown within the lambda.
                .get();
        REL_INFO("getRecord OK for {} {} {}", did, collection, rkey);
        return response;
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING("IoReaderImpl::ReadSome(getRecord): asio eof, retry");
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("getRecord for {} {} {} Boost exception {}", did,
                    collection, rkey, exc.what())
          throw;
        }
      } catch (std::exception const &exc) {
        REL_ERROR("getRecord for {} {} {} exception {}", did, collection, rkey,
                  exc.what())
        throw;
      }
    }
  }

  template <typename REPORT>
  void send_report(std::string const &did, REPORT const &report) {
    if (!_is_ready) {
      REL_ERROR("Bluesky client not ready, skip report of {} for {}", did,
                report.as_string());
    }
    restc_cpp::serialize_properties_t properties;
    properties.name_mapping = &json::TypeFieldMapping;
    if (_dry_run) {
      REL_INFO("Dry-run Report of {} {}", did, report.as_string());
      return;
    }
    // check session status
    check_session();

    bool done(false);
    size_t retries(0);
    bsky::moderation::report_response response;
    while (retries < 5) {
      try {
        response =
            _rest_client
                ->ProcessWithPromiseT<bsky::moderation::report_response>(
                    [&](restc_cpp::Context &ctx) {
                      // This is a co-routine, running in a worker-thread

                      // Instantiate a report_response structure.
                      bsky::moderation::report_request request;
                      request.reason = report.reason();

                      std::ostringstream body;
                      restc_cpp::SerializeToJson(request, body, properties);

                      // Serialize it asynchronously. The asynchronously part
                      // does not really matter here, but it may if you receive
                      // huge data structures.
                      restc_cpp::SerializeFromJson(
                          response,

                          // Construct a request to the server
                          restc_cpp::RequestBuilder(ctx)
                              .Post(_host +
                                    "com.atproto.moderation.createReport")
                              .Header("Content-Type", "application/json")
                              .Header("Atproto-Accept-Labelers", _service_did)
                              .Header(
                                  "Atproto-Proxy",
                                  _service_did +
                                      std::string(atproto::ProxyLabelerSuffix))
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
        REL_INFO("Report of {} {} recorded at {}, reporter "
                 "{} id={}",
                 did, report.as_string(), response.createdAt,
                 response.reportedBy, response.id);
        metrics::instance()
            .automation_stats()
            .Get({{"report", report.get_name()}})
            .Increment();
        done = true;
        break;
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING("IoReaderImpl::ReadSome(createReport): asio eof, retry");
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("Create report of {} {} Boost exception {}", did,
                    report.as_string(), exc.what());
          break;
        }
      } catch (std::exception const &exc) {
        REL_ERROR("Create report of {} {} exception {}", did,
                  report.as_string(), exc.what());
        break;
      }
    }
    if (!done) {
      metrics::instance()
          .automation_stats()
          .Get({{"report_error", report.get_name()}})
          .Increment();
    }
  }

  template <typename RESPONSE>
  RESPONSE do_get(std::string const &relative_path) {
    size_t retries(0);
    RESPONSE response;

    while (retries < 5) {
      try {
        restc_cpp::SerializeProperties properties;
        properties.name_mapping = &json::TypeFieldMapping;
        response =
            _rest_client
                ->ProcessWithPromiseT<RESPONSE>([&](restc_cpp::Context &ctx) {
                  // This is a co-routine, running in a worker-thread
                  // Construct a request to the server
                  restc_cpp::RequestBuilder builder(ctx);
                  builder.Get(_host + relative_path);
                  if (_use_token) {
                    builder.Header(
                        "Authorization",
                        std::string("Bearer " + _session->access_token()));
                  }
                  restc_cpp::SerializeFromJson(response,
                                               // Send the request
                                               builder.Execute(),
                                               &json::TypeFieldMapping);

                  // Return the list record instance through C++
                  // future<>
                  return response;
                })

                // Get the Post instance from the future<>, or any C++
                // exception thrown within the lambda.
                .get();
        REL_INFO("GET OK for {}", relative_path);
        break;
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING("IoReaderImpl::ReadSome(GET): asio eof, retry");
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("GET for {} Boost exception {}", relative_path, exc.what())
          throw;
        }
      } catch (std::exception const &exc) {
        REL_ERROR("GET for {} exception {}", relative_path, exc.what())
        throw;
      }
    }
    return response;
  }

  inline std::string raw_get(std::string const &relative_path) {
    std::string response;
    size_t retries(0);
    while (retries < 5) {
      try {
        restc_cpp::SerializeProperties properties;
        properties.name_mapping = &json::TypeFieldMapping;
        response =
            _rest_client
                ->ProcessWithPromiseT<std::string>(
                    [&](restc_cpp::Context &ctx) {
                      // This is a co-routine, running in a worker-thread
                      // Construct a request to the server
                      // Send the request
                      restc_cpp::RequestBuilder builder(ctx);
                      builder.Get(_host + relative_path);
                      if (_use_token) {
                        builder.Header(
                            "Authorization",
                            std::string("Bearer " + _session->access_token()));
                      }
                      auto reply = builder.Execute();

                      // Return the list record instance through C++
                      // future<>
                      return reply->GetBodyAsString();
                    })

                // Get the Post instance from the future<>, or any C++
                // exception thrown within the lambda.
                .get();
        REL_INFO("GET for {} returned '{}'", relative_path, response);
        break;
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING("IoReaderImpl::ReadSome(GET): asio eof, retry");
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("GET for {} Boost exception {}", relative_path, exc.what())
          throw;
        }
      } catch (std::exception const &exc) {
        REL_ERROR("GET for {} exception {}", relative_path, exc.what())
        throw;
      }
    }
    return response;
  }

  std::string raw_post(std::string const &relative_path,
                       const std::string &&body = std::string());

  template <typename BODY, typename RESPONSE>
  RESPONSE do_post(std::string const &relative_path, BODY const &body) {
    RESPONSE response;
    size_t retries(0);
    while (retries < 5) {
      try {
        restc_cpp::SerializeProperties properties;
        properties.name_mapping = &json::TypeFieldMapping;
        response =
            _rest_client
                ->ProcessWithPromiseT<RESPONSE>([&](restc_cpp::Context &ctx) {
                  // This is a co-routine, running in a worker-thread
                  // Construct a request to the server
                  // Send the request
                  restc_cpp::RequestBuilder builder(ctx);
                  if (_use_token) {
                    builder.Header(
                        "Authorization",
                        std::string("Bearer " + _session->access_token()));
                  }
                  std::ostringstream raw_body;
                  restc_cpp::SerializeToJson(body, raw_body, properties);

                  // Serialize it asynchronously. The asynchronously part
                  // does not really matter here, but it may if you receive
                  // huge data structures.
                  restc_cpp::SerializeFromJson(
                      response,
                      // Send the request
                      builder.Post(_host + relative_path)
                          .Header("Content-Type", "application/json")
                          .Data(raw_body.str())
                          .Execute(),
                      &json::TypeFieldMapping);
                  return response;
                })

                // Get the Post instance from the future<>, or any C++
                // exception thrown within the lambda.
                .get();
        REL_INFO("POST for {} returned '{}'", relative_path,
                 response.as_string());
        break;
      } catch (boost::system::system_error const &exc) {
        if (exc.code().value() == boost::asio::error::eof &&
            exc.code().category() == boost::asio::error::get_misc_category()) {
          REL_WARNING("IoReaderImpl::ReadSome(POST): asio eof, retry");
          ++retries;
        } else {
          // unrecoverable error
          REL_ERROR("POST for {} Boost exception {}", relative_path, exc.what())
          throw;
        }
      } catch (std::exception const &exc) {
        REL_ERROR("POST for {} exception {}", relative_path, exc.what())
        throw;
      }
    }
    return response;
  }

private:
  std::unique_ptr<restc_cpp::RestClient> _rest_client;
  std::unique_ptr<pds_session> _session;

  std::string _handle;
  std::string _password;
  std::string _did;
  std::string _host;
  std::string _port;
  std::string _service_did;
  bool _dry_run = true;
  bool _use_token = false;
  bool _is_ready = false;
};

} // namespace bsky
