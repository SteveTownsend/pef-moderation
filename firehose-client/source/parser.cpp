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

#include "parser.hpp"
#include "common/rest_utils.hpp"
#include "helpers.hpp"
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
  if (is_full(*_settings)) {
    bool parsed(json_from_cbor(buffers_begin(buffer), buffers_end(buffer)));
    if (!parsed) {
      // TODO error handling
    }
    return {};
  } else {
    nlohmann::json full_json(
        nlohmann::json::parse(buffers_begin(buffer), buffers_end(buffer)));
    return get_candidates_from_json(full_json);
  }
}

candidate_list
parser::get_candidates_from_record(nlohmann::json const &record) {
  auto record_type(record["$type"].template get<std::string>());
  auto const record_fields(json::TargetFieldNames.find(record_type));
  candidate_list results;
  if (record_fields != json::TargetFieldNames.cend()) {
    for (auto &field_name : record_fields->second) {
      if (record.contains(field_name)) {
        results.emplace_back(record_type, field_name.to_string(),
                             nlohmann::to_string(record[field_name]));
      }
    }
  }

  return results;
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

    return get_candidates_from_record(commit["record"]);
  } catch (std::exception const &exc) {
    REL_ERROR("Error {} processing JSON\n{}", exc.what(), dump_json(full_json));
  }
  return {};
}

inline bool parser::cbor_callback(int /* depth */,
                                  nlohmann::json::parse_event_t event,
                                  nlohmann::json &parsed) {
  if (event == nlohmann::json::parse_event_t::result) {
    // Check for "roots" and decode embedded CIDs is found
    if (parsed.contains("roots")) {
      DBG_TRACE("JSON roots  {}", parsed.dump());
    } else if (parsed.contains("__readable_cid__")) {
      // DBG_TRACE("JSON block cid {}", parsed.dump());
      _block_cid = parsed["__readable_cid__"].template get<std::string>();
    } else {
      DBG_TRACE("JSON Result  {}", parsed.dump());
      if (parsed.contains("$type")) {
        if (_block_cid.empty()) {
          REL_ERROR("Block CID empty, block={}", parsed.dump());
          return false;
        }
        // if this is a potential match source store it for scanning
        // There may be more than one per message if user posted multiple
        // replies to a post, or a new thread.
        std::string block_type(parsed["$type"].template get<std::string>());
        if (json::TargetFieldNames.contains(block_type)) {
          // block may contains string-matching content
          if (!_cids.insert(_block_cid).second) {
            REL_ERROR("Matchable Block CID {} already stored, block={}",
                      _block_cid, parsed.dump());
            return false;
          }
          _matchable_cbors.emplace_back(_block_cid, std::move(parsed));
        } else {
          // Also store other typed CBORs.
          if (!_cids.insert(_block_cid).second) {
            REL_ERROR("Content Block CID {} already stored, block={}",
                      _block_cid, parsed.dump());
            return false;
          }
          _content_cbors.emplace_back(_block_cid, std::move(parsed));
        }
      } else {
        _other_cbors.emplace_back(_block_cid, std::move(parsed));
      }
    }
  } else if (event == nlohmann::json::parse_event_t::key) {
    DBG_TRACE("JSON Key     {}", parsed.dump());
  } else if (event == nlohmann::json::parse_event_t::value) {
    DBG_TRACE("JSON Value   {}", parsed.dump());
  } else if (event == nlohmann::json::parse_event_t::object_start) {
    DBG_TRACE("JSON Object Start");
  } else if (event == nlohmann::json::parse_event_t::object_end) {
    DBG_TRACE("JSON Object End");
  } else if (event == nlohmann::json::parse_event_t::array_start) {
    DBG_TRACE("JSON Array Start");
  } else if (event == nlohmann::json::parse_event_t::array_end) {
    DBG_TRACE("JSON Array End");
  }

  return true;
}

void parser::set_config(std::shared_ptr<config> &settings) {
  _settings = settings;
}

std::string parser::dump_parse_content() const {
  bool first(true);
  std::ostringstream oss;
  for (auto const &cbor : _content_cbors) {
    if (!first) {
      oss << '\n';
    }
    first = false;
    oss << dump_json(cbor.second);
  }
  return oss.str();
}

std::string parser::dump_parse_matched() const {
  bool first(true);
  std::ostringstream oss;
  for (auto const &cbor : _matchable_cbors) {
    if (!first) {
      oss << '\n';
    }
    first = false;
    oss << dump_json(cbor.second);
  }
  return oss.str();
}

std::string parser::dump_parse_other() const {
  bool first(true);
  std::ostringstream oss;
  for (auto const &cbor : _other_cbors) {
    if (!first) {
      oss << '\n';
    }
    first = false;
    oss << dump_json(cbor.second);
  }
  return oss.str();
}
