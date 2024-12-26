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
  auto &cbors(_parser.cbors());
  if (cbors.size() != 2) {
    std::ostringstream oss;
    for (auto &cbor : _parser.cbors()) {
      oss << cbor.dump();
    }
    REL_ERROR("Malformed firehose message {}", oss.str());
    return;
  }
  auto header(cbors.front());
  auto message(cbors.back());
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
    if (op_type == firehose::OpTypeCommit) {
      repo = message["repo"].template get<std::string>();
      if (message.contains("blocks")) {
        // CAR file - nested in-situ parse to extract as JSON
        auto blocks(message["blocks"].template get<nlohmann::json::binary_t>());
        parser block_parser;
        bool parsed(block_parser.json_from_car(blocks.cbegin(), blocks.cend()));
        if (parsed) {
          DBG_DEBUG("Commit blocks: {}", block_parser.dump_parse_results());
          auto const content_cbors(block_parser.content_cbors());
          for (auto const &cbor : content_cbors) {
            auto candidates(block_parser.get_candidates_from_record(cbor));
            _candidates.insert(_candidates.end(), candidates.cbegin(),
                               candidates.cend());
          }
        } else {
          // TODO error handling
        }
      }
      for (auto const &op : message["ops"]) {
        size_t count = 0;
        auto path(op["path"].template get<std::string>());
        auto kind(op["action"].template get<std::string>());
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
    if (!_candidates.empty()) {
      auto matches(
          processor.get_matcher().all_matches_for_candidates(_candidates));
      if (!matches.empty()) {
        // Publish metrics for matches
        for (auto &result : matches) {
          // this is the substring of the full JSON that matched one or more
          // desired strings
          REL_INFO("Candidate {}|{}|{}|{}\nmatches {}\non message:{}", repo,
                   result._candidate._type, result._candidate._field,
                   result._candidate._value, result._matches, message.dump());
          for (auto const &match : result._matches) {
            prometheus::Labels labels(
                {{"type", result._candidate._type},
                 {"field", result._candidate._field},
                 {"filter", wstring_to_utf8(match.get_keyword())}});
            processor.metrics().Get(labels).Increment();
          }
        }
      }
    }
  }
}
