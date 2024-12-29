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

#include "post_processor.hpp"
#include "parser.hpp"

jetstream_payload::jetstream_payload() {}
jetstream_payload::jetstream_payload(std::string json_msg,
                                     match_results matches)
    : _json_msg(json_msg), _matches(matches) {}

void jetstream_payload::handle(post_processor<jetstream_payload> &processor) {
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
      processor.metrics().Get(labels).Increment();
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
      oss << cbor.dump();
    }
    REL_ERROR("Malformed firehose message {}", oss.str());
    return;
  }
  auto const &header(other_cbors.front());
  auto const &message(other_cbors.back());
  REL_DEBUG("Firehose header:  {}", dump_json(header));
  REL_DEBUG("         message: {}", dump_json(message));
  int op(header["op"].template get<int>());
  if (op == static_cast<int>(firehose::op::error)) {
    processor.firehose_stats().Get({{"op", "error"}}).Increment();
  } else if (op == static_cast<int>(firehose::op::message)) {
    processor.firehose_stats().Get({{"op", "message"}}).Increment();
    std::string op_type(header["t"].template get<std::string>());
    processor.firehose_stats()
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
          auto const &matchable_cbors(block_parser.matchable_cbors());
          for (auto const &cbor : matchable_cbors) {
            auto candidates(block_parser.get_candidates_from_record(cbor));
            if (!candidates.empty()) {
              _candidates.insert(_candidates.end(), candidates.cbegin(),
                                 candidates.cend());
            }
          }
        } else {
          // TODO error handling
        }
      }
      for (auto const &oper : message["ops"]) {
        size_t count = 0;
        auto path(oper["path"].template get<std::string>());
        auto kind(oper["action"].template get<std::string>());
        for (const auto token : std::views::split(path, '/')) {
          // with string_view's C++23 range constructor:
          std::string field(token.cbegin(), token.cend());
          switch (count) {
          case 0:
            if (field.empty())
              throw std::invalid_argument("Blank collection in op.path " +
                                          path);
            processor.firehose_stats()
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
      }

    } else if (op_type == firehose::OpTypeIdentity ||
               op_type == firehose::OpTypeHandle) {
      repo = message["did"].template get<std::string>();
      if (message.contains("handle")) {
        _candidates.emplace_back(op_type, "handle",
                                 message["handle"].template get<std::string>());
      }
    } else if (op_type == firehose::OpTypeAccount) {
      repo = message["did"].template get<std::string>();
      bool active(message["active"].template get<bool>());
      processor.firehose_stats()
          .Get({{"op", "message"},
                {"type", op_type},
                {"status", active ? "active" : "inactive"}})
          .Increment();
    } else if (op_type == firehose::OpTypeTombstone ||
               op_type == firehose::OpTypeMigrate ||
               op_type == firehose::OpTypeInfo) {
      // no-op
    }
    REL_TRACE("{} {}", header.dump(), message.dump());
    // handle all the CBORs with content, metrics, checking
    for (auto const &content_cbor : block_parser.content_cbors()) {
      handle_content(processor, content_cbor);
    }
    for (auto const &matchable_cbor : block_parser.matchable_cbors()) {
      handle_content(processor, matchable_cbor);
    }
    if (!_candidates.empty()) {
      auto matches(
          processor.get_matcher().all_matches_for_candidates(_candidates));
      if (!matches.empty()) {
        // Publish metrics for matches
        for (auto const &result : matches) {
          // this is the substring of the full JSON that matched one or more
          // desired strings
          REL_INFO("{} matched candidate {}|{}|{}|{}", result._matches, repo,
                   result._candidate._type, result._candidate._field,
                   result._candidate._value);
          for (auto const &match : result._matches) {
            prometheus::Labels labels(
                {{"type", result._candidate._type},
                 {"field", result._candidate._field},
                 {"filter", wstring_to_utf8(match.get_keyword())}});
            processor.metrics().Get(labels).Increment();
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
      }
    }
  }
}

void firehose_payload::handle_content(
    post_processor<firehose_payload> &processor,
    nlohmann::json const &content) {
  auto collection(content["$type"].template get<std::string>());
  if (collection == bsky::AppBskyFeedPost) {
    // Post create/update
    // Check facets
    // 1. look for Matryoshka post - embed video/images, multiple facet
    // mentions/tags
    // https://github.com/SteveTownsend/nafo-forum-moderation/issues/68
    // 2. check URIs for toxic content
    size_t tags(0);
    if (content.contains("tags")) {
      tags = content["tags"].size();
    }
    if (content.contains("embed") && content.contains("facets")) {
      size_t mentions(0);
      size_t links(0);
      auto const &embed(content["embed"]);
      auto const &embed_type(embed["$type"].template get<std::string>());
      if (embed_type == bsky::AppBskyEmbedVideo && embed.contains("langs")) {
        // count languages in video
        auto langs(embed["langs"].template get<std::vector<std::string>>());
        for (auto const &lang : langs) {
          processor.firehose_stats()
              .Get({{"embed", std::string(bsky::AppBskyEmbedVideo)},
                    {"language", lang}})
              .Increment();
        }
      }
      // bool is_media(embed_type == bsky::AppBskyEmbedImages ||
      //               embed_type == bsky::AppBskyEmbedVideo);
      bool has_facets(false);
      for (auto const &facet : content["facets"]) {
        has_facets = true;
        for (auto const &feature : facet["features"]) {
          auto const &facet_type(feature["$type"].template get<std::string>());
          // if (is_media) {
          if (facet_type == bsky::AppBskyRichtextFacetMention) {
            ++mentions;
          } else if (facet_type == bsky::AppBskyRichtextFacetTag) {
            ++tags;
            // }
          } else if (facet_type == bsky::AppBskyRichtextFacetLink) {
            _candidates.emplace_back(
                collection, std::string(bsky::AppBskyRichtextFacetLink),
                feature["uri"].template get<std::string>());
            ++links;
          }
        }
      }
      if (mentions > 0) {
        processor.firehose_facets()
            .GetAt({{"facet", std::string(bsky::AppBskyRichtextFacetMention)}})
            .Observe(static_cast<double>(mentions));
      }
      if (links > 0) {
        processor.firehose_facets()
            .GetAt({{"facet", std::string(bsky::AppBskyRichtextFacetLink)}})
            .Observe(static_cast<double>(links));
      }
      if (tags > 0) {
        processor.firehose_facets()
            .GetAt({{"facet", std::string(bsky::AppBskyRichtextFacetTag)}})
            .Observe(static_cast<double>(tags));
      }
      if (has_facets) {
        processor.firehose_facets()
            .GetAt({{"facet", "total"}})
            .Observe(static_cast<double>(mentions + tags + links));
      }
      if (content.contains("langs")) {
        auto langs(content["langs"].template get<std::vector<std::string>>());
        for (auto const &lang : langs) {
          processor.firehose_stats()
              .Get({{"collection", collection}, {"language", lang}})
              .Increment();
        }
      }
      // if (mentions == 0 && tags >= bsky::PushyTagCount) {
      //   REL_INFO("pushy_tag ({}) {}|{}", tags, repo, dump_json(record));
      // } else if (tags == 0 && mentions >= bsky::PushyMentionCount) {
      //   REL_INFO("pushy_mention ({}) matched candidate {}|{}", mentions,
      //   repo,
      //            dump_json(record));
      // } else if (mentions + tags >= bsky::PushyTotalCount) {
      //   // arbitrary threshold combined tags and mentions, with at least one
      //   // mention
      //   REL_INFO("pushy_mention_tag ({},{}) matched candidate {}|{}",
      //   mentions,
      //            tags, repo, dump_json(record));
      // }
      // record metrics for facet types by embed type
    }
  }
}
