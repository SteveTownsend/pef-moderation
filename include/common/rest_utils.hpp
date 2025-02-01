#ifndef __rest_utils_
#define __rest_utils_
/*************************************************************************
NAFO Forum Moderation Tools
Copyright (c) Steve Townsend 2025

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
#include <nlohmann/json.hpp>
#include <restc-cpp/RequestBody.h>
#include <restc-cpp/SerializeJSON.h>

namespace json {
extern restc_cpp::JsonFieldMapping TypeFieldMapping;
extern std::map<std::string_view, std::vector<nlohmann::json::json_pointer>>
    TargetFieldNames;
} // namespace json

template <typename T>
inline std::string format_vector(std::vector<T> const &vals) {
#ifdef _WIN32
  return std::format("{}", vals);
#else
  std::ostringstream oss;
  oss << '[';
  bool first(true);
  for (auto const &val : vals) {
    if (!first) {
      oss << ", ";
    } else {
      first = false;
    }
    oss << '"';
    oss << std::format("{}", val);
    oss << '"';
  }
  oss << ']';
  return oss.str();
#endif
}

#endif