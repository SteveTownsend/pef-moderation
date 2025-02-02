#ifndef __helpers_hpp__
#define __helpers_hpp__
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
#include "aho_corasick/aho_corasick.hpp"
#include "common/bluesky/platform.hpp"
#include "common/log_wrapper.hpp"
#include "nlohmann/json.hpp"
#include "restc-cpp/RequestBody.h"
#include "restc-cpp/SerializeJson.h"
#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

class config;
bool is_full(config const &settings);

inline bool bool_from_string(std::string_view str) {
  if (str == "false")
    return false;
  if (str == "true")
    return true;
  std::ostringstream err;
  err << "Bad bool value " << str;
  throw std::invalid_argument(err.str());
}

inline bool ends_with(std::string const &value, std::string_view ending) {
  if (ending.size() > value.size())
    return false;
  return std::equal(ending.crbegin(), ending.crend(), value.crbegin());
}

inline bool starts_with(std::string const &value, std::string_view start) {
  if (start.size() > value.size())
    return false;
  return std::equal(start.cbegin(), start.cend(), value.cbegin());
}

template <typename T>
inline bool alert_needed(const T count, const size_t factor) {
  std::div_t divide_result(std::div(int(count), int(factor)));
  return (divide_result.rem == 0) &&
         !(divide_result.quot & (divide_result.quot - 1));
}

inline std::string print_current_time() {
  return std::format("{0:%F}T{0:%T}Z", std::chrono::utc_clock::now());
}

template <> struct std::less<atproto::at_uri> {
  bool operator()(const atproto::at_uri &lhs, const atproto::at_uri &rhs) {
    if (lhs._authority == rhs._authority) {
      if (lhs._collection == rhs._collection) {
        return lhs._rkey < rhs._rkey;
      }
      return lhs._collection < rhs._collection;
    }
    return lhs._authority < rhs._authority;
  }
};

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

inline std::string dump_json(nlohmann::json const &full_json,
                             bool indent = false) {
  return full_json.dump(indent ? 2 : -1);
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
#endif
