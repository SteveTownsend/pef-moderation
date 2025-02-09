/*************************************************************************
Public Education Forum Moderation Firehose Client
Copyright (c) Steve Townsend 2024, 2025

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

#include "payload.hpp"
#include "activity/account_events.hpp"
#include "moderation/action_router.hpp"
#include "moderation/embed_checker.hpp"
#include "parser.hpp"
#include "payload.hpp"
#include <multiformats/cid.hpp>

jetstream_payload::jetstream_payload() {}
jetstream_payload::jetstream_payload(std::string json_msg,
                                     match_results matches)
    : _json_msg(json_msg), _matches(matches) {}

void jetstream_payload::handle(post_processor<jetstream_payload> &) {
  // TODO almost identical to jetstream_payload::handle
  // Publish metrics for matches
  for (auto &result : _matches) {
    // this is the substring of the full JSON that matched one or more
    // desired strings
    REL_INFO("Candidate {}|{}|{}\nmatches {}\non message:{}",
             result._candidate._type, result._candidate._field,
             result._candidate._value, result._matches, _json_msg);
    for (auto const &match : result._matches) {
      prometheus::Labels labels(
          {{"type", result._candidate._type},
           {"field", result._candidate._field},
           {"filter", wstring_to_utf8(match.get_keyword())}});
      metrics_factory::instance()
          .get_counter("message_string_matches")
          .Get(labels)
          .Increment();
    }
  }
}

firehose_payload::firehose_payload() {}
firehose_payload::firehose_payload(parser &my_parser)
    : _parser(std::move(my_parser)) {}

void firehose_payload::handle(post_processor<firehose_payload> &processor) {
  auto const &other_cbors(_parser.other_cbors());
  if (other_cbors.size() != 2) {
    std::ostringstream oss;
    for (auto const &cbor : _parser.other_cbors()) {
      oss << cbor.second.dump();
    }
    REL_ERROR("Malformed firehose message {}", oss.str());
    return;
  }
  auto const &header(other_cbors.front().second);
  auto const &message(other_cbors.back().second);
  REL_DEBUG("Firehose header:  {}", dump_json(header));
  REL_DEBUG("         message: {}", dump_json(message));
  int op(header["op"].template get<int>());
  if (op == static_cast<int>(firehose::op::error)) {
    metrics_factory::instance()
        .get_counter("firehose_content")
        .Get({{"op", "error"}})
        .Increment();
  } else if (op == static_cast<int>(firehose::op::message)) {
    metrics_factory::instance()
        .get_counter("firehose_content")
        .Get({{"op", "message"}})
        .Increment();
    std::string op_type(header["t"].template get<std::string>());
    metrics_factory::instance()
        .get_counter("firehose_content")
        .Get({{"op", "message"}, {"type", op_type}})
        .Increment();
    std::string repo;
    parser block_parser;
    if (op_type == firehose::OpTypeCommit) {
      repo = message["repo"].template get<std::string>();
      if (message.contains("blocks")) {
        // CAR file - nested in-situ parse to extract as JSON
        auto blocks(message["blocks"].template get<nlohmann::json::binary_t>());
        bool parsed(block_parser.json_from_car(blocks.cbegin(), blocks.cend()));
        if (parsed) {
          DBG_DEBUG("Commit content blocks: {}",
                    block_parser.dump_parse_content());
          DBG_DEBUG("Commit other blocks: {}", block_parser.dump_parse_other());
        } else {
          // TODO error handling
        }
      }
      for (auto const &oper : message["ops"]) {
        size_t count = 0;
        auto path(oper["path"].template get<std::string>());
        auto kind(oper["action"].template get<std::string>());
        firehose::op_kind oper_kind(firehose::op_kind_from_string(kind));
        for (const auto token : std::views::split(path, '/')) {
          // with string_view's C++23 range constructor:
          std::string field(token.cbegin(), token.cend());
          switch (count) {
          case 0:
            if (field.empty())
              throw std::invalid_argument("Blank collection in op.path " +
                                          path);
            metrics_factory::instance()
                .get_counter("firehose_content")
                .Get({{"op", "message"},
                      {"type", op_type},
                      {"collection", field},
                      {"kind", kind}})
                .Increment();
            break;
          case 1:
            if (field.empty())
              throw std::invalid_argument("Blank key in op.path " + path);
            break;
          }
          ++count;
        }
        // track deletions
        if (oper_kind == firehose::op_kind::delete_) {
          processor.request_recording(
              {repo,
               bsky::time_stamp_from_iso_8601(
                   message["time"].template get<std::string>()),
               activity::deleted(path)});
        } else if (oper.contains("cid") && !oper["cid"].is_null()) {
          auto cid(oper["cid"].template get<nlohmann::json::binary_t>());
          try {
            // nlhomann parser gives us a leading zero
            atproto::cid_decoder decoder(cid.cbegin() + 1, cid.cend());
            std::string friendly_cid(decoder.as_string());
            auto insertion(_path_by_cid.insert({friendly_cid, path}));
            if (!insertion.second) {
              // We see this for Block operations very rarely. Log to try to
              // track it down
              REL_ERROR(
                  "Duplicate cid {} at op.path {}, already used for path {}",
                  friendly_cid, path, insertion.first->second);
              REL_ERROR("Firehose header:  {}", dump_json(header));
              REL_ERROR("         message: {}", dump_json(message));
              REL_ERROR("Content CBORs:  {}",
                        block_parser.dump_parse_content());
              REL_ERROR("Matched CBORs:  {}",
                        block_parser.dump_parse_matched());
              REL_ERROR("Other CBORs:    {}", block_parser.dump_parse_other());
            }
          } catch (std::exception const &exc) {
            REL_ERROR("CID parse error {} in message {}", exc.what(),
                      dump_json(message));
          }
        }
      }
      // handle all the CBORs with content, metrics, checking
      for (auto const &content_cbor : block_parser.content_cbors()) {
        handle_content(processor, repo, content_cbor.first,
                       content_cbor.second);
      }
      for (auto const &matchable_cbor : block_parser.matchable_cbors()) {
        handle_matchable_content(processor, repo, matchable_cbor.first,
                                 matchable_cbor.second);
      }
    } else if (op_type == firehose::OpTypeIdentity ||
               op_type == firehose::OpTypeHandle) {
      repo = message["did"].template get<std::string>();
      if (message.contains("handle")) {
        std::string handle(message["handle"].template get<std::string>());
        _path_candidates.emplace_back(
            std::make_pair<std::string, candidate_list>(
                "handle", {{op_type, "handle", handle}}));
        processor.request_recording(
            {repo,
             bsky::time_stamp_from_iso_8601(
                 message["time"].template get<std::string>()),
             activity::handle(handle)});
      }
      REL_INFO("{} {}", op_type.c_str(), dump_json(message));
    } else if (op_type == firehose::OpTypeAccount) {
      repo = message["did"].template get<std::string>();
      bool active(message["active"].template get<bool>());
      metrics_factory::instance()
          .get_counter("firehose_content")
          .Get({{"op", "message"},
                {"type", op_type},
                {"status", active ? "active" : "inactive"}})
          .Increment();
      if (active) {
        processor.request_recording(
            {repo,
             bsky::time_stamp_from_iso_8601(
                 message["time"].template get<std::string>()),
             activity::active()});
      } else if (message.contains("status")) {
        processor.request_recording(
            {repo,
             bsky::time_stamp_from_iso_8601(
                 message["time"].template get<std::string>()),
             activity::inactive(bsky::down_reason_from_string(
                 message["status"].template get<std::string>()))});
      } else {
        processor.request_recording(
            {repo,
             bsky::time_stamp_from_iso_8601(
                 message["time"].template get<std::string>()),
             activity::inactive(bsky::down_reason::unknown)});
      }
      REL_INFO("{} {}", op_type.c_str(), dump_json(message));
    } else if (op_type == firehose::OpTypeTombstone) {
      repo = message["did"].template get<std::string>();
      processor.request_recording(
          {repo,
           bsky::time_stamp_from_iso_8601(
               message["time"].template get<std::string>()),
           activity::inactive(bsky::down_reason::tombstone)});
      REL_INFO("{} {}", op_type.c_str(), dump_json(message));
    } else if (op_type == firehose::OpTypeMigrate ||
               op_type == firehose::OpTypeInfo) {
      // no-op
    }
    REL_TRACE("{} {}", header.dump(), message.dump());
    if (!_path_candidates.empty()) {
      auto matches(
          matcher::shared().all_matches_for_path_candidates(_path_candidates));
      if (!matches.empty()) {
        // Publish metrics for matches
        size_t count(0);
        for (auto const &result : matches) {
          for (auto const &next_match : result.second) {
            // this is the substring of the full JSON that matched one or more
            // desired strings
            REL_INFO("{} matched candidate {}|{}|{}|{}", next_match._matches,
                     repo, next_match._candidate._type,
                     next_match._candidate._field,
                     next_match._candidate._value);
            count += next_match._matches.size();
            for (auto const &match : next_match._matches) {
              prometheus::Labels labels(
                  {{"type", next_match._candidate._type},
                   {"field", next_match._candidate._field},
                   {"filter", wstring_to_utf8(match.get_keyword())}});
              metrics_factory::instance()
                  .get_counter("message_string_matches")
                  .Get(labels)
                  .Increment();
            }
          }
        }
        // only log message once - might be interleaved with other thread output
        if (op_type == firehose::OpTypeCommit) {
          // curate a smaller version of the full message for correlation
          REL_INFO("in message: {} {} {}", repo, dump_json(message["ops"]),
                   block_parser.dump_parse_content());
        } else {
          REL_INFO("in message: {} {}", repo, dump_json(message));
        }
        // record suspect activity as a special-case event
        processor.request_recording(
            {repo, bsky::current_time(), activity::matches(count)});

        // forward account and its matched records for possible auto-moderation
        action_router::instance().wait_enqueue({repo, std::move(matches)});
      }
    }
  }
}

bsky::embed_type
firehose_payload::context::process_embed(nlohmann::json const &embed) {
  // TODO pass along the embeds for checking
  std::string uri;
  std::string cid;
  nlohmann::json::binary_t encoded_cid;
  bsky::embed_type embed_type = bsky::embed_type_from_string(_embed_type_str);
  switch (embed_type) {
  case bsky::embed_type::record:
  case bsky::embed_type::record_with_media:
    // Embedded record, this is a quote post
    _event_type = bsky::tracked_event::quote;
    _recorded = true;
    uri = embed_type == bsky::embed_type::record
              ? embed["record"]["uri"].template get<std::string>()
              : embed["record"]["record"]["uri"].template get<std::string>();
    _processor.request_recording(
        {_repo,
         bsky::time_stamp_from_iso_8601(
             _content["createdAt"].template get<std::string>()),
         activity::quote(_this_path, uri)});
    // nested media must be checked
    if (embed_type == bsky::embed_type::record_with_media) {
      // TODO fix recursive checking
      //      process_embed(embed["media"]);
      add_embed(embed::record(uri));
    }
    break;
  case bsky::embed_type::external:
    add_embed(
        embed::external(embed["external"]["uri"].template get<std::string>()));
    if (embed["external"].contains("thumb")) {
      encoded_cid = embed["external"]["thumb"]["ref"]
                        .template get<nlohmann::json::binary_t>();
      // nlohmann parser leaves a leading zero byte
      cid = atproto::cid_decoder<nlohmann::json::binary_t::const_iterator>(
                encoded_cid.cbegin() + 1, encoded_cid.cend())
                .as_string();
      add_embed(embed::image(cid));
    }
    break;
  case bsky::embed_type::images:
    // pass along the CID in each image
    for (auto const &image : embed["images"]) {
      encoded_cid =
          image["image"]["ref"].template get<nlohmann::json::binary_t>();
      // nlohmann parser leaves a leading zero byte
      cid = atproto::cid_decoder<nlohmann::json::binary_t::const_iterator>(
                encoded_cid.cbegin() + 1, encoded_cid.cend())
                .as_string();
      add_embed(embed::image(cid));
    }
    break;
  case bsky::embed_type::video:
    encoded_cid =
        embed["video"]["ref"].template get<nlohmann::json::binary_t>();
    // nlohmann parser leaves a leading zero byte
    cid = atproto::cid_decoder<nlohmann::json::binary_t::const_iterator>(
              encoded_cid.cbegin() + 1, encoded_cid.cend())
              .as_string();
    add_embed(embed::video(cid));
    break;
  default:
    break;
  }
  return embed_type;
}

void firehose_payload::handle_content(
    post_processor<firehose_payload> &processor, std::string const &repo,
    std::string const &cid, nlohmann::json const &content) {
  context this_context(processor, content);
  this_context._repo = repo;
  if (_path_by_cid.contains(cid)) {
    this_context._this_path = _path_by_cid[cid];
  } else {
    throw std::runtime_error("cannot get URI for cid at " + dump_json(content));
  }
  auto collection(content["$type"].template get<std::string>());
  this_context._event_type = bsky::event_type_from_collection(collection);
  if (this_context._event_type == bsky::tracked_event::post) {
    bool recorded(false);
    // Post create/update - may need qualification
    if (content.contains("reply")) {
      this_context._event_type = bsky::tracked_event::reply;
      recorded = true;
      processor.request_recording(
          {repo,
           bsky::time_stamp_from_iso_8601(
               content["createdAt"].template get<std::string>()),
           activity::reply(
               this_context._this_path,
               content["reply"]["root"]["uri"].template get<std::string>(),
               content["reply"]["parent"]["uri"].template get<std::string>())});
    }
    // Check facets
    // 1. look for Matryoshka post - embed video/images, multiple facet
    // mentions/tags
    // https://github.com/SteveTownsend/pef-forum-moderation/issues/68
    // 2. check URIs for toxic content
    size_t tags(0);
    if (content.contains("tags")) {
      tags = content["tags"].size();
    }
    if (content.contains("embed")) {
      auto const &embed(content["embed"]);
      this_context._embed_type_str = embed["$type"].template get<std::string>();
      bsky::embed_type embed_type = this_context.process_embed(embed);

      if (content.contains("facets")) {
        size_t mentions(0);
        size_t links(0);
        if (embed_type == bsky::embed_type::video && embed.contains("langs")) {
          // count languages in video
          auto langs(embed["langs"].template get<std::vector<std::string>>());
          for (auto const &lang : langs) {
            metrics_factory::instance()
                .get_counter("firehose_content")
                .Get({{"embed", this_context._embed_type_str},
                      {"language", lang}})
                .Increment();
          }
        }
        bool has_facets(false);
        for (auto const &facet : content["facets"]) {
          has_facets = true;
          for (auto const &feature : facet["features"]) {
            auto const &facet_type(
                feature["$type"].template get<std::string>());
            if (facet_type == bsky::AppBskyRichtextFacetMention) {
              ++mentions;
            } else if (facet_type == bsky::AppBskyRichtextFacetTag) {
              ++tags;
              // }
            } else if (facet_type == bsky::AppBskyRichtextFacetLink) {
              _path_candidates.emplace_back(
                  std::make_pair<std::string, candidate_list>(
                      std::string(this_context._this_path),
                      {{collection, std::string(bsky::AppBskyRichtextFacetLink),
                        feature["uri"].template get<std::string>()}}));
              this_context.add_embed(
                  embed::external(feature["uri"].template get<std::string>()));
              ++links;
            }
          }
        }
        // record metrics for facet types by embed type
        if (mentions > 0) {
          metrics_factory::instance()
              .get_histogram("firehose_facets")
              .GetAt(
                  {{"facet", std::string(bsky::AppBskyRichtextFacetMention)}})
              .Observe(static_cast<double>(mentions));
        }
        if (links > 0) {
          metrics_factory::instance()
              .get_histogram("firehose_facets")
              .GetAt({{"facet", std::string(bsky::AppBskyRichtextFacetLink)}})
              .Observe(static_cast<double>(links));
        }
        if (tags > 0) {
          metrics_factory::instance()
              .get_histogram("firehose_facets")
              .GetAt({{"facet", std::string(bsky::AppBskyRichtextFacetTag)}})
              .Observe(static_cast<double>(tags));
        }
        if (has_facets) {
          size_t total(mentions + tags + links);
          metrics_factory::instance()
              .get_histogram("firehose_facets")
              .GetAt({{"facet", "total"}})
              .Observe(static_cast<double>(total));
          processor.request_recording(
              {repo,
               bsky::time_stamp_from_iso_8601(
                   content["createdAt"].template get<std::string>()),
               activity::facets(static_cast<unsigned short>(tags),
                                static_cast<unsigned short>(mentions),
                                static_cast<unsigned short>(links))});
        }
        if (content.contains("langs")) {
          auto langs(content["langs"].template get<std::vector<std::string>>());
          for (auto const &lang : langs) {
            metrics_factory::instance()
                .get_counter("firehose_content")
                .Get({{"collection", collection}, {"language", lang}})
                .Increment();
          }
        }
      }
    }
    if (!recorded) {
      // plain old post, not a reply or quote
      processor.request_recording(
          {repo,
           bsky::time_stamp_from_iso_8601(
               content["createdAt"].template get<std::string>()),
           activity::post(this_context._this_path)});
    }
  } else if (this_context._event_type == bsky::tracked_event::block) {
    processor.request_recording(
        {repo,
         bsky::time_stamp_from_iso_8601(
             content["createdAt"].template get<std::string>()),
         activity::block(this_context._this_path,
                         content["subject"].template get<std::string>())});
  } else if (this_context._event_type == bsky::tracked_event::follow) {
    processor.request_recording(
        {repo,
         bsky::time_stamp_from_iso_8601(
             content["createdAt"].template get<std::string>()),
         activity::follow(this_context._this_path,
                          content["subject"].template get<std::string>())});
  } else if (this_context._event_type == bsky::tracked_event::like) {
    processor.request_recording(
        {repo,
         bsky::time_stamp_from_iso_8601(
             content["createdAt"].template get<std::string>()),
         activity::like(
             this_context._this_path,
             content["subject"]["uri"].template get<std::string>())});
  } else if (this_context._event_type == bsky::tracked_event::profile) {
    processor.request_recording(
        {repo,
         (content.contains("createdAt")
              ? bsky::time_stamp_from_iso_8601(
                    content["createdAt"].template get<std::string>())
              : bsky::current_time()),
         activity::profile(this_context._this_path)});
  } else if (this_context._event_type == bsky::tracked_event::repost) {
    processor.request_recording(
        {repo,
         bsky::time_stamp_from_iso_8601(
             content["createdAt"].template get<std::string>()),
         activity::repost(
             this_context._this_path,
             content["subject"]["uri"].template get<std::string>())});
  }
  // pass along embeds for analysis
  if (!this_context.get_embeds().empty()) {
    bsky::moderation::embed_checker::instance().wait_enqueue(
        {repo, this_context._this_path, this_context.get_embeds()});
  }
}

void firehose_payload::handle_matchable_content(
    post_processor<firehose_payload> &processor, std::string const &repo,
    std::string const &cid, nlohmann::json const &content) {
  // common processing
  handle_content(processor, repo, cid, content);

  // check for matches
  std::string this_path;
  if (_path_by_cid.contains(cid)) {
    this_path = _path_by_cid[cid];
  } else {
    throw std::runtime_error("cannot get URI for cid at " + dump_json(content));
  }
  auto candidates(parser::get_candidates_from_record(content));
  if (!candidates.empty()) {
    _path_candidates.insert(_path_candidates.end(),
                            std::make_pair<std::string, candidate_list>(
                                std::string(this_path), std::move(candidates)));
  }
}
