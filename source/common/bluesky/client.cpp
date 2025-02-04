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

#include "common/bluesky/client.hpp"
#include "common/rest_utils.hpp"
#include <algorithm>
#include <boost/fusion/adapted.hpp>
#include <functional>

namespace bsky {

void client::set_config(YAML::Node const &settings) {
  try {
    // leave blank if unauthenticated
    _handle = settings["handle"].as<std::string>("");
    _password = settings["password"].as<std::string>("");
    _did = settings["did"].as<std::string>("");
    _service_did = settings["service_did"].as<std::string>("");

    _host = settings["host"].as<std::string>();
    _port = settings["port"].as<std::string>();
    constexpr bool dry_run(false);
    _dry_run = settings["dry_run"].as<bool>(dry_run);

    // create client
    _rest_client = restc_cpp::RestClient::Create();

    // create session
    // bootstrap self-managed session from the returned tokens
    if (!_password.empty()) {
      _use_token = true;
      _session = std::make_unique<bsky::pds_session>(*this, _host);
      _session->connect(bsky::login_info({_handle, _password}));
    }
    _is_ready = true;
  } catch (std::exception const &exc) {
    REL_ERROR("Error processing Bluesky client config {}", exc.what());
  }
}

inline std::string client::raw_get(std::string const &relative_path,
                                   std::optional<get_callback_t> callback) {
  std::string response;
  size_t retries(0);
  while (retries < 5) {
    try {
      restc_cpp::SerializeProperties properties;
      properties.name_mapping = &json::TypeFieldMapping;
      response =
          _rest_client
              ->ProcessWithPromiseT<std::string>([&](restc_cpp::Context &ctx) {
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
                // Collect overrides if present
                if (callback.has_value()) {
                  callback.value()(builder);
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

std::string client::raw_post(std::string const &relative_path,
                             const std::string &&body) {
  std::string response;
  size_t retries(0);
  while (retries < 5) {
    try {
      restc_cpp::SerializeProperties properties;
      properties.name_mapping = &json::TypeFieldMapping;
      response =
          _rest_client
              ->ProcessWithPromiseT<std::string>([&](restc_cpp::Context &ctx) {
                // This is a co-routine, running in a worker-thread
                // Construct a request to the server
                // Send the request
                restc_cpp::RequestBuilder builder(ctx);
                if (!body.empty()) {
                  builder.Body(restc_cpp::RequestBody::CreateStringBody(body));
                }
                builder.Post(_host + relative_path);
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
      REL_INFO("POST for {} returned '{}'", relative_path, response);
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

void client::label_account(std::string const &did,
                           std::vector<std::string> const &labels) {
  if (_dry_run) {
    REL_INFO("Dry-run Label of {} for {}", did, format_vector(labels));
    return;
  }

  restc_cpp::serialize_properties_t properties;
  properties.name_mapping = &json::TypeFieldMapping;
  properties.ignore_empty_fileds =
      0; // negateLabelsVals is mandatory but unused
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

std::vector<bsky::profile> client::get_profiles(std::vector<std::string> dids) {
  std::vector<bsky::profile> profiles;
  profiles.reserve(dids.size());
  size_t next(0);
  std::vector<std::string> batch;
  batch.reserve(bsky::GetProfilesMax);
  bsky::client::get_callback_t callback =
      [&](restc_cpp::RequestBuilder &builder) {
        // builder.argument
      };
  for (std::string const &did : dids) {
    batch.emplace_back(did);
    if (++next == bsky::GetProfilesMax) {
      // request batch from Bluesky API
      next = 0;
      bsky::get_profiles_response response =
          do_get<bsky::get_profiles_response>("app.bsky.actor.getProfiles",
                                              callback);
    }
  }
  return profiles;
}

} // namespace bsky
