#ifndef __parser_hpp__
#define __parser_hpp__
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

#include "config.hpp"
#include "helpers.hpp"
#include "nlohmann/json.hpp"
#include <boost/beast/core.hpp>
#include <string_view>
#include <tuple>

namespace beast = boost::beast; // from <boost/beast.hpp>
using namespace std::literals;

class parser {
public:
  parser() = default;
  ~parser() = default;

  // Extract UTF-8 string containing the material to be checked,  which is
  // context-dependent
  candidate_list
  get_candidates_from_string(std::string const &full_content) const;
  candidate_list
  get_candidates_from_flat_buffer(beast::flat_buffer const &beast_data);
  candidate_list get_candidates_from_json(nlohmann::json &full_json) const;

  static void set_config(std::shared_ptr<config> &settings);

private:
  bool cbor_callback(int depth, nlohmann::json::parse_event_t event,
                     nlohmann::json &parsed);

  std::vector<nlohmann::json> _cbors;
  static std::shared_ptr<config> _settings;
};
#endif