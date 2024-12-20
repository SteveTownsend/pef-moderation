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

jetstream_payload::jetstream_payload() {}
jetstream_payload::jetstream_payload(std::string json_msg,
                                     match_results matches)
    : _json_msg(json_msg), _matches(matches) {}

void jetstream_payload::handle(
    post_processor<jetstream_payload> &processor) const {
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
void firehose_payload::handle(
    post_processor<firehose_payload> &processor) const {}
