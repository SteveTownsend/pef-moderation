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

// app.bsky.actor.getProfiles
BOOST_FUSION_ADAPT_STRUCT(bsky::profile_view_detailed, (std::string, did),
                          (std::string, handle))
BOOST_FUSION_ADAPT_STRUCT(bsky::get_profiles_response,
                          (std::vector<bsky::profile_view_detailed>, profiles))

// tools.ozone.moderation.emitEvent
// Shared
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_subject,
                          (std::string, _type), (std::string, did))
// Label
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::label_event, (std::string, _type),
                          (std::vector<std::string>, createLabelVals),
                          (std::vector<std::string>, negateLabelVals))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::emit_event_label_request,
                          (bsky::moderation::label_event, event),
                          (bsky::moderation::report_subject, subject),
                          (std::string, createdBy))
// Acknowledge
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::acknowledge_event_comment,
                          (std::string, descriptor), (std::string, context),
                          (std::string, did), (std::string, path))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::acknowledge_event,
                          (std::string, _type), (std::string, comment),
                          (bool, acknowledgeAccountSubjects))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::emit_event_acknowledge_request,
                          (bsky::moderation::acknowledge_event, event),
                          (bsky::moderation::report_subject, subject),
                          (std::string, createdBy))
// Tag
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::tag_event_comment,
                          (std::string, descriptor))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::tag_event, (std::string, _type),
                          (std::string, comment),
                          (std::vector<std::string>, add),
                          (std::vector<std::string>, remove))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::emit_event_tag_request,
                          (bsky::moderation::tag_event, event),
                          (bsky::moderation::report_subject, subject),
                          (std::string, createdBy))
// Comment
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::comment_event_comment,
                          (std::string, descriptor), (std::string, context),
                          (std::string, reason))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::comment_event, (std::string, _type),
                          (std::string, comment))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::emit_event_comment_request,
                          (bsky::moderation::comment_event, event),
                          (bsky::moderation::report_subject, subject),
                          (std::string, createdBy))
// Response
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::emit_event_response,
                          (std::string, createdAt), (int64_t, id),
                          (std::string, createdBy))

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

std::string client::raw_post(std::string const &relative_path,
                             const std::string &&body) {
  std::string response;
  size_t retries(0);
  while (retries < 5) {
    try {
      _session->check_refresh();
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
  bsky::moderation::emit_event_label_request request;
  request.subject.did = did;
  request.createdBy = _did;
  request.event.createLabelVals = labels;
  try {
    bsky::moderation::emit_event_response response =
        emit_event<bsky::moderation::emit_event_label_request>(request);
    REL_INFO("Labeled {} as {} at {}", did, format_vector(labels),
             response.createdAt);
  } catch (std::exception const &exc) {
    REL_ERROR("Label {} as {} error {}", did, format_vector(labels),
              exc.what());
  }
}

void client::add_comment_for_subject(
    std::string const &did,
    bsky::moderation::comment_event_comment const &comment,
    std::string const &path) {
  std::ostringstream oss;
  restc_cpp::SerializeToJson(comment, oss);
  if (_dry_run) {
    REL_INFO("Dry-run Comment on {} for {}", did, oss.str());
    return;
  }
  if (comment.context.empty()) {
    REL_ERROR("Comment on moderation subject must have context in {}",
              oss.str());
    return;
  }
  if (!path.empty()) {
    REL_WARNING("Comment on moderation subject for content not yet supported");
    return;
  }
  bsky::moderation::emit_event_comment_request request;
  request.subject.did = did;
  request.createdBy = _did;
  request.event.comment = oss.str();
  try {
    bsky::moderation::emit_event_response response =
        emit_event<bsky::moderation::emit_event_comment_request>(request);
    REL_INFO("Comment {} with {}", did, oss.str(), response.createdAt);
  } catch (std::exception const &exc) {
    REL_ERROR("Comment {} with {} error {}", did, oss.str(), exc.what());
  }
}

void client::acknowledge_subject(
    std::string const &did,
    bsky::moderation::acknowledge_event_comment const &comment,
    std::string const &path) {
  std::ostringstream oss;
  restc_cpp::SerializeToJson(comment, oss);
  if (_dry_run) {
    REL_INFO("Dry-run acknowledge of subject {}/{} reason {}", did, path,
             oss.str());
    return;
  }
  if (comment.context.empty()) {
    REL_ERROR(
        "Acknowledge of moderation subject must have comment context in {}",
        oss.str());
    return;
  }
  if (!path.empty()) {
    REL_WARNING(
        "Acknowledge of moderation subject for content not yet supported");
    return;
  }
  try {
    // TODO use a variant to branch for account/content
    bsky::moderation::emit_event_acknowledge_request request;
    request.subject.did = did;
    request.createdBy = _did;
    request.event.comment = oss.str();

    bsky::moderation::emit_event_response response = emit_event(request);
    REL_INFO("Acknowledge OK: subject {}/{} reason {} at {}", did, path,
             oss.str(), response.createdAt);
  } catch (std::exception const &exc) {
    REL_ERROR("Acknowledge error: subject {}/{} reason {} error {}", did, path,
              oss.str(), exc.what());
  }
}

void client::tag_report_subject(
    std::string const &did, std::string const &path,
    bsky::moderation::tag_event_comment const &comment,
    std::vector<std::string> const &add_tags,
    std::vector<std::string> const &remove_tags) {
  std::ostringstream oss;
  restc_cpp::SerializeToJson(comment, oss);
  if (_dry_run) {
    REL_INFO("Dry-run Tag of {} add: {} remove: {} comment: {}", did,
             format_vector(add_tags), format_vector(remove_tags), oss.str());
    return;
  }
  if (!path.empty()) {
    REL_WARNING("Tag of moderation subject for content not yet supported");
    return;
  }
  bsky::moderation::emit_event_tag_request request;
  request.subject.did = did;
  request.createdBy = _did;
  request.event.add = add_tags;
  request.event.remove = remove_tags;
  try {
    bsky::moderation::emit_event_response response =
        emit_event<bsky::moderation::emit_event_tag_request>(request);
    REL_INFO("Tagged {} add: {} remove: {} comment: {} at {}", did,
             format_vector(add_tags), format_vector(remove_tags), oss.str(),
             response.createdAt);
  } catch (std::exception const &exc) {
    REL_ERROR("Tagged {} add: {} remove: {} comment: {} error {}", did,
              format_vector(add_tags), format_vector(remove_tags), oss.str(),
              exc.what());
  }
}

std::unordered_set<bsky::profile_view_detailed>
client::get_profiles(std::unordered_set<std::string> const &dids) {
  std::unordered_set<bsky::profile_view_detailed> profiles;
  profiles.reserve(dids.size());
  size_t next(0);
  std::vector<std::string> batch;
  batch.reserve(bsky::GetProfilesMax);
  bsky::client::get_callback_t callback =
      [&](restc_cpp::RequestBuilder &builder) {
        for (auto &actor : batch) {
          builder.Argument("actors[]", actor);
        }
      };
  for (std::string const &did : dids) {
    batch.emplace_back(did);
    if (++next == bsky::GetProfilesMax) {
      // request batch from Bluesky API
      next = 0;
      bsky::get_profiles_response response =
          do_get<bsky::get_profiles_response>("app.bsky.actor.getProfiles",
                                              callback);
      REL_TRACE("getProfiles request for {} returned {}", batch.size(),
                response.profiles.size());
      batch.clear();
#ifdef __GNUC__
      profiles.insert(response.profiles.cbegin(), response.profiles.cend());
#else
      profiles.insert_range(response.profiles);
#endif
    }
  }
  REL_INFO("get_profiles request for {} returned {}", dids.size(),
           profiles.size());
  return profiles;
}

bsky::profile_view_detailed client::get_profile(std::string const &did) {
  bsky::client::get_callback_t callback =
      [&](restc_cpp::RequestBuilder &builder) {
        builder.Argument("actor", did);
      };
  return do_get<bsky::profile_view_detailed>("app.bsky.actor.getProfile",
                                             callback);
}

} // namespace bsky
