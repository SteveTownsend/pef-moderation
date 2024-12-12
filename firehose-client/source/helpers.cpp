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

#include "helpers.hpp"
#include <unicode/errorcode.h>
#include <unicode/stringoptions.h>
#include <unicode/ustring.h>

namespace json {
std::map<std::string, std::vector<std::string>> TargetFieldNames = {
    {"app.bsky.feed.post", {"text"}},
    {"app.bsky.actor.profile", {"description", "displayName"}}};
}

// convert UTF-8 input to canonical form where case differences are erased
std::wstring to_canonical(std::string_view const input) {
  int32_t capacity((static_cast<int32_t>(input.length()) * 3));
  int32_t new_size;
  std::unique_ptr<UChar> workspace(new UChar[capacity]);
  icu::ErrorCode error_code;
  u_strFromUTF8(workspace.get(), capacity, &new_size, input.data(),
                static_cast<int32_t>(input.length()), (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "to_canonical: u_strFromUTF8 error " << error_code.errorName();
    throw std::runtime_error(oss.str());
  }
  if (new_size > capacity - 1) {
    std::ostringstream oss;
    oss << "UTF-8 to UCHAR overflow for " << input << ", capacity=" << capacity
        << ", length required=" << new_size;
    throw std::runtime_error(oss.str());
  }
  std::unique_ptr<UChar> case_folded(new UChar[capacity]);
  new_size =
      u_strFoldCase(case_folded.get(), capacity, workspace.get(), new_size,
                    U_FOLD_CASE_DEFAULT, (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "to_canonical: u_strFoldCase error " << error_code.errorName();
    throw std::runtime_error(oss.str());
  }
  if (new_size > capacity - 1) {
    std::ostringstream oss;
    oss << "Case-fold overflow for " << input << ", capacity=" << capacity
        << ", length required=" << new_size;
    throw std::runtime_error(oss.str());
  }
  return std::wstring(case_folded.get(), case_folded.get() + new_size);
}

std::string wstring_to_utf8(std::wstring const &rc_string) {
  return wstring_to_utf8(
      std::wstring_view(rc_string.c_str(), rc_string.length()));
}

std::string wstring_to_utf8(std::wstring_view rc_string) {
  if (rc_string.empty())
    return std::string();

  std::vector<UChar> buffer(rc_string.size() * 3, 0);
  std::string result(rc_string.size() * 2, 0);

  icu::ErrorCode error_code;
  int32_t len = 0;

  u_strFromWCS(&buffer[0], static_cast<int32_t>(buffer.size()), &len,
               &rc_string[0], static_cast<int32_t>(rc_string.size()),
               (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "wstring_to_utf8: u_strFromWCS error " << error_code.errorName();
    throw std::runtime_error(oss.str());
  }
  buffer.resize(len);

  u_strToUTF8(&result[0], static_cast<int32_t>(result.size()), &len, &buffer[0],
              static_cast<int32_t>(buffer.size()), (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "wstring_to_utf8: u_strToUTF8 error " << error_code.errorName();
    throw std::runtime_error(oss.str());
  }
  result.resize(len);

  return result;
}

std::string print_emits(const aho_corasick::wtrie::emit_collection &c) {
  std::wostringstream oss;
  bool first(true);
  for (auto const &emit : c) {
    if (!first)
      oss << ',';
    else
      first = false;
    oss << '"' << emit.get_keyword() << '"';
  }
  return wstring_to_utf8(oss.str());
}
