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
#include <algorithm>
#include <boost/asio/buffers_iterator.hpp>
#include <sstream>

std::shared_ptr<config> parser::_settings;

// Extract UTF-8 string containing the material to be checked,  which is
// context-dependent
candidate_list
parser::get_candidates_from_string(std::string const &full_content) const {
  nlohmann::json full_json(nlohmann::json::parse(full_content));
  return get_candidates_from_json(full_json);
}

candidate_list
parser::get_candidates_from_flat_buffer(beast::flat_buffer const &beast_data) {
  auto buffer(beast_data.data());
  if (_settings->is_full()) {
    bool parsed(false);
    try {
      std::function<bool(int /*depth*/, nlohmann::json::parse_event_t /*event*/,
                         nlohmann::json & /*parsed*/)>
          callback =
              std::bind(&parser::cbor_callback, this, std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3);
      parsed = nlohmann::json::from_cbor_sequence(
          buffers_begin(buffer), buffers_end(buffer), callback, true,
          nlohmann::json::cbor_tag_handler_t::store);

    } catch (std::exception const &exc) {
      REL_ERROR("from_cbor_sequence threw: {}", exc.what());
    }
    if (!parsed) {
      std::ostringstream oss;
      oss << std::hex;
      auto os_iter = std::ostream_iterator<int>(oss);
      std::ranges::copy(buffers_begin(buffer), buffers_end(buffer), os_iter);

      REL_ERROR("from_cbor_sequence parse failed: {}", oss.str());
    }
    return {};
  } else {
    nlohmann::json full_json(
        nlohmann::json::parse(buffers_begin(buffer), buffers_end(buffer)));
    return get_candidates_from_json(full_json);
  }
}

// TODO Could use SAX parsing down the line
candidate_list
parser::get_candidates_from_json(nlohmann::json &full_json) const {
  // Handle exceptions as they come up.
  // Example: record["type"] can sometimes be a "proxy" object not a string
  try {
    REL_TRACE("Target {}", dump_json(full_json));
    // handle updates
    if (full_json["kind"] == "identity") {
      nlohmann::json record(full_json["identity"]);
      if (record.contains("handle")) {
        return {{"identity", "handle",
                 record["handle"].template get<std::string>()}};
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
    auto const record_fields(json::TargetFieldNames.find(record_type));
    candidate_list results;
    if (record_fields != json::TargetFieldNames.cend()) {
      for (auto &field_name : record_fields->second) {
        try {
          results.emplace_back(
              record_type, field_name,
              record.at(field_name).template get<std::string>());

        } catch (const nlohmann::json::out_of_range &) {
          // not an error
        }
      }
    }
    return results;
  } catch (std::exception const &exc) {
    REL_ERROR("Error {} processing JSON\n{}", exc.what(), dump_json(full_json));
  }
  return {};
}

inline bool parser::cbor_callback(int depth,
                                  nlohmann::json::parse_event_t event,
                                  nlohmann::json &parsed) {
  if (event == nlohmann::json::parse_event_t::result) {
    _cbors.push_back(std::move(parsed));
  }
  return true;
}

void parser::set_config(std::shared_ptr<config> &settings) {
  _settings = settings;
}
