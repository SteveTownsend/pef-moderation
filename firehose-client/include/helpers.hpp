#ifndef __helpers_hpp__
#define __helpers_hpp__
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
#include "aho_corasick/aho_corasick.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <format>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace json {
extern std::map<std::string, std::vector<std::string>> TargetFieldNames;
}

// brute force to_lower, not locale-aware
inline std::string to_lower(std::string_view const input) {
  std::string copy;
  copy.reserve(input.length());
  std::transform(input.cbegin(), input.cend(), std::back_inserter(copy),
                 [](unsigned char c) { return std::tolower(c); });
  return copy;
}

inline std::string to_lower(std::string const &input) {
  return to_lower(std::string_view(input.c_str(), input.length()));
}

// convert UTF-8 input to canonical form where case differences are erased
std::wstring to_canonical(std::string_view const input);

inline std::string dump_json(nlohmann::json &full_json) {
  std::ostringstream ostr;
  ostr << std::setw(4) << full_json << std::endl;
  return ostr.str();
}

// convert wstring to UTF-8 string
std::string wstring_to_utf8(std::wstring const &str);
std::string wstring_to_utf8(std::wstring_view str);

std::string print_emits(const aho_corasick::wtrie::emit_collection &c);
// template <>
// struct std::formatter<aho_corasick::wtrie::emit_collection>
//     : public std::formatter<std::string> {
//   auto format(aho_corasick::wtrie::emit_collection const &emits,
//               std::format_context &ctx) const {
//     return std::format("{}", print_emits(emits));
//   }
// };
template <>
struct std::formatter<aho_corasick::wtrie::emit_collection>
    : std::formatter<std::string> {
  auto format(aho_corasick::wtrie::emit_collection emits,
              format_context &ctx) const {
    return std::formatter<string>::format(std::format("{}", print_emits(emits)),
                                          ctx);
  }
};

// filter match candidate
struct candidate {
  std::string _type;
  std::string _field;
  std::string _value;
};
typedef std::vector<candidate> candidate_list;

// Stores context that matched one or more filters, and the matches
struct match_result {
  candidate _candidate;
  aho_corasick::wtrie::emit_collection _matches;
};
typedef std::vector<match_result> match_results;

#endif
