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

#include "parser.hpp"
#include "helpers.hpp"
#include "log_wrapper.hpp"
#include <boost/asio/buffers_iterator.hpp>
#include <sstream>

// Extract UTF-8 string containing the material to be checked,  which is
// context-dependent
parser::candidate_list
parser::get_candidates_from_string(std::string const &full_content) const {
  nlohmann::json full_json(nlohmann::json::parse(full_content));
  return get_candidates_from_json(full_json);
}

parser::candidate_list parser::get_candidates_from_flat_buffer(
    beast::flat_buffer const &beast_data) const {
  auto buffer(beast_data.data());
  nlohmann::json full_json(
      nlohmann::json::parse(buffers_begin(buffer), buffers_end(buffer)));
  return get_candidates_from_json(full_json);
}

// TODO Could use SAX parsing down the line
parser::candidate_list
parser::get_candidates_from_json(nlohmann::json &full_json) const {
  REL_TRACE("Target {}", dump_json(full_json));
  // handle updates
  if (full_json["kind"] == "identity") {
    nlohmann::json record(full_json["identity"]);
    if (record.contains("handle")) {
      return {{"handle", record["handle"].template get<std::string>()}};
    }
    return {};
  }

  // other than handles, only interested in commits
  if (full_json["kind"] != "commit")
    return {};

  auto commit(full_json["commit"]);
  // Skip deletions
  if (commit["operation"] == "delete")
    return {};

  nlohmann::json record(commit["record"]);
  auto record_type(record["$type"].template get<std::string>());
  if (record_type == PostId && record.contains("text")) {
    return {{"text", record["text"].template get<std::string>()}};
  } else if (record_type == ProfileId) {
    parser::candidate_list results;
    if (record.contains("description")) {
      results.emplace_back("description",
                           record["description"].template get<std::string>());
    }
    if (record.contains("displayName")) {
      results.emplace_back("displayName",
                           record["displayName"].template get<std::string>());
    }
    return results;
  }
  return {};
}
