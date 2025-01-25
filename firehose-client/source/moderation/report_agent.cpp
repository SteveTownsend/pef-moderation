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

#include "moderation/report_agent.hpp"
#include "log_wrapper.hpp"
#include "metrics.hpp"
#include "restc-cpp/RequestBuilder.h"
#include <algorithm>
#include <boost/fusion/adapted.hpp>
#include <functional>

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::filter_match_info,
                          (std::string,
                           descriptor)(std::vector<std::string>,
                                       filters)(std::vector<std::string>,
                                                paths))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::link_redirection_info,
                          (std::string,
                           descriptor)(std::string,
                                       path)(std::vector<std::string>, uris))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_subject,
                          (std::string, _type), (std::string, did))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_request,
                          (std::string, reasonType), (std::string, reason),
                          (bsky::moderation::report_subject, subject))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_response,
                          (std::string, createdAt), (int64_t, id),
                          (std::string, reportedBy))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::label_event, (std::string, _type),
                          (std::vector<std::string>, createLabelVals),
                          (std::vector<std::string>, negateLabelVals))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::emit_event_label_request,
                          (bsky::moderation::label_event, event),
                          (bsky::moderation::report_subject, subject),
                          (std::string, createdBy))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::emit_event_label_response,
                          (std::string, createdAt), (int64_t, id),
                          (std::string, createdBy))

namespace bsky {
namespace moderation {

report_agent &report_agent::instance() {
  static report_agent my_instance;
  return my_instance;
}

report_agent::report_agent() : _queue(QueueLimit) {}

void report_agent::set_config(YAML::Node const &settings) {
  _handle = settings["handle"].as<std::string>();
  _password = settings["password"].as<std::string>();
  _did = settings["did"].as<std::string>();
  _host = settings["host"].as<std::string>();
  _port = settings["port"].as<std::string>();
  _service_did = settings["service_did"].as<std::string>();
  _dry_run = settings["dry_run"].as<bool>();
}

void report_agent::start() {
  _thread = std::thread([&] {
    // create client
    _rest_client = restc_cpp::RestClient::Create();

    // create session
    // bootstrap self-managed session from the returned tokens
    _session = std::make_unique<bsky::pds_session>(*_rest_client, _host);
    _session->connect(bsky::login_info({_handle, _password}));

    while (true) {
      account_report report;
      if (_queue.wait_dequeue_timed(report, DequeueTimeout)) {
        // process the item
        metrics::instance()
            .operational_stats()
            .Get({{"report_agent", "backlog"}})
            .Decrement();

        // Don't reprocess previously-labeled accounts
        if (_moderation_data->already_processed(report._did) ||
            is_reported(report._did)) {
          REL_INFO("Report of {} skipped, already known", report._did);
          metrics::instance()
              .realtime_alerts()
              .Get({{"auto_reports", "skipped"}})
              .Increment();
          continue;
        }

        std::visit(report_content_visitor(*this, report._did), report._content);
      }
      // check session status
      _session->check_refresh();

      // TODO terminate gracefully
    }
  });
}

void report_agent::wait_enqueue(account_report &&value) {
  _queue.enqueue(value);
  metrics::instance()
      .operational_stats()
      .Get({{"report_agent", "backlog"}})
      .Increment();
  metrics::instance()
      .realtime_alerts()
      .Get({{"auto_reports", "submitted"}})
      .Increment();
}

// TODO add metrics
void report_agent::string_match_report(std::string const &did,
                                       std::vector<std::string> const &filters,
                                       std::vector<std::string> const &paths) {
  restc_cpp::serialize_properties_t properties;
  properties.name_mapping = &json::TypeFieldMapping;
  reported(did);
  if (_dry_run) {
    REL_INFO("Dry-run Report of {} for rules {} on paths {}", did,
             format_vector(filters), format_vector(paths));
    return;
  }
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
                    request.subject.did = did;

                    bsky::moderation::filter_match_info reason;
                    reason.filters = filters;
                    reason.paths = paths;
                    std::ostringstream oss;
                    restc_cpp::SerializeToJson(reason, oss);
                    request.reason = oss.str();

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(
                        response,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "com.atproto.moderation.createReport")
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
      REL_INFO("Report of {} for rules {} on paths {} recorded at {}, reporter "
               "{} id={}",
               did, format_vector(filters), format_vector(paths),
               response.createdAt, response.reportedBy, response.id);
      break;
    } catch (boost::system::system_error const &exc) {
      if (exc.code().value() == boost::asio::error::eof &&
          exc.code().category() == boost::asio::error::get_misc_category()) {
        REL_WARNING("IoReaderImpl::ReadSome(createReport): asio eof, retry");
        ++retries;
      } else {
        // unrecoverable error
        REL_ERROR(
            "Create report of {} for rules {} on paths {} Boost exception {}",
            did, format_vector(filters), format_vector(paths), exc.what());
        break;
      }
    } catch (std::exception const &exc) {
      REL_ERROR("Create report of {} for rules {} on paths {} exception {}",
                did, format_vector(filters), format_vector(paths), exc.what());
      break;
    }
  }
}

// TODO add metrics
void report_agent::link_redirection_report(
    std::string const &did, std::string const &path,
    std::vector<std::string> const &uri_chain) {
  restc_cpp::serialize_properties_t properties;
  properties.name_mapping = &json::TypeFieldMapping;
  reported(did);
  if (_dry_run) {
    REL_INFO("Dry-run Report of {} for link-redirection chain {}", did,
             format_vector(uri_chain));
    return;
  }
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
                    request.subject.did = did;

                    bsky::moderation::link_redirection_info reason;
                    reason.path = path;
                    reason.uris = uri_chain;
                    std::ostringstream oss;
                    restc_cpp::SerializeToJson(reason, oss);
                    request.reason = oss.str();

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(
                        response,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "com.atproto.moderation.createReport")
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
      REL_INFO(
          "Report of {} {} for link redirection {} recorded at {}, reporter "
          "{} id={}",
          did, path, format_vector(uri_chain), response.createdAt,
          response.reportedBy, response.id);
      break;
    } catch (boost::system::system_error const &exc) {
      if (exc.code().value() == boost::asio::error::eof &&
          exc.code().category() == boost::asio::error::get_misc_category()) {
        REL_WARNING("IoReaderImpl::ReadSome(createReport): asio eof, retry");
        ++retries;
      } else {
        // unrecoverable error
        REL_ERROR(
            "Create report of {} {} for link redirection {} Boost exception {}",
            did, path, format_vector(uri_chain), exc.what());
        break;
      }
    } catch (std::exception const &exc) {
      REL_ERROR("Create report of {} {} for link redirection {} exception {}",
                did, path, format_vector(uri_chain), exc.what());
      break;
    }
  }
}

// TODO add metrics
void report_agent::blocks_moderation_report(std::string const &did) {
  restc_cpp::serialize_properties_t properties;
  properties.name_mapping = &json::TypeFieldMapping;
  reported(did);
  if (_dry_run) {
    REL_INFO("Dry-run Report of {} as blocks-moderation", did);
    return;
  }
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
                    request.subject.did = did;
                    request.reason = "Auto-report: blocks moderation";

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(
                        response,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "com.atproto.moderation.createReport")
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
      REL_INFO("Report of {} as blocks-moderation recorded at {}, reporter "
               "{} id={}",
               did, response.createdAt, response.reportedBy, response.id);
      break;
    } catch (boost::system::system_error const &exc) {
      if (exc.code().value() == boost::asio::error::eof &&
          exc.code().category() == boost::asio::error::get_misc_category()) {
        REL_WARNING("IoReaderImpl::ReadSome(createReport): asio eof, retry");
        ++retries;
      } else {
        // unrecoverable error
        REL_ERROR("Create report of {} as blocks-moderation Boost exception {}",
                  did, exc.what());
        break;
      }
    } catch (std::exception const &exc) {
      REL_ERROR("Create report of {} as blocks-moderation exception {}", did,
                exc.what());
      break;
    }
  }
}

// TODO add metrics
void report_agent::label_account(std::string const &did,
                                 std::vector<std::string> const &labels) {
  restc_cpp::serialize_properties_t properties;
  properties.name_mapping = &json::TypeFieldMapping;
  properties.ignore_empty_fileds =
      0; // negateLabelsVals is mandatory but unused
  if (_dry_run) {
    REL_INFO("Dry-run Label of {} for {}", did, format_vector(labels));
    return;
  }
  size_t retries(0);
  bsky::moderation::emit_event_label_response response;
  while (retries < 5) {
    try {
      response =
          _rest_client
              ->ProcessWithPromiseT<
                  bsky::moderation::emit_event_label_response>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a report_response structure.
                    bsky::moderation::emit_event_label_request request;
                    request.subject.did = did;
                    request.createdBy = _did;
                    request.event.createLabelVals = labels;

                    std::ostringstream body;
                    restc_cpp::SerializeToJson(request, body, properties);

                    // Serialize it asynchronously. The asynchronously part does
                    // not really matter here, but it may if you receive huge
                    // data structures.
                    restc_cpp::SerializeFromJson(
                        response,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "tools.ozone.moderation.emitEvent")
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
      REL_INFO("Label of {} for {} recorded at {}, reporter "
               "{} id={}",
               did, format_vector(labels), response.createdAt,
               response.createdBy, response.id);
      break;
    } catch (boost::system::system_error const &exc) {
      if (exc.code().value() == boost::asio::error::eof &&
          exc.code().category() == boost::asio::error::get_misc_category()) {
        REL_WARNING("IoReaderImpl::ReadSome(emitEvent): asio eof, retry");
        ++retries;
      } else {
        // unrecoverable error
        REL_ERROR("emitEvent(label) of {} for {} Boost exception {}", did,
                  format_vector(labels), exc.what());
        break;
      }
    } catch (std::exception const &exc) {
      REL_ERROR("emitEvent(label) of {} for {} exception {}", did,
                format_vector(labels), exc.what());
      break;
    }
  }
}

void report_content_visitor::operator()(filter_matches const &value) {
  _agent.string_match_report(_did, value._filters, value._paths);
  if (!value._labels.empty()) {
    // auto-label request augments the report
    _agent.label_account(_did, value._labels);
  }
}
void report_content_visitor::operator()(link_redirection const &value) {
  _agent.link_redirection_report(_did, value._path, value._uri_chain);
}
void report_content_visitor::operator()(blocks_moderation const &value) {
  _agent.blocks_moderation_report(_did);
  // auto-label request augments the report
  _agent.label_account(_did, {"blocks"});
}
} // namespace moderation
} // namespace bsky
