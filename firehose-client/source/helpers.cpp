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
#include "common/config.hpp"
#include "common/log_wrapper.hpp"
#include "firehost_client_config.hpp"
#include <unicode/errorcode.h>
#include <unicode/stringoptions.h>
#include <unicode/ustring.h>

bool is_full(config const &settings) {
  constexpr std::string_view jetstream = "jetstream";
  return !settings.get_config()[PROJECT_NAME]["datasource"]["hosts"]
              .as<std::string>()
              .contains(jetstream);
}

namespace json {

restc_cpp::JsonFieldMapping TypeFieldMapping({{"_type", "$type"}});

std::map<std::string_view, std::vector<nlohmann::json::json_pointer>>
    TargetFieldNames = {
        {bsky::AppBskyFeedPost,
         {
             // https://github.com/bluesky-social/atproto/blob/main/lexicons/app/bsky/feed/post.json
             // and
             // https://github.com/bluesky-social/atproto/tree/main/lexicons/app/bsky/embed
             "/text"_json_pointer, "/embed/external/description"_json_pointer,
             "/embed/external/title"_json_pointer,
             "/embed/external/uri"_json_pointer,
             "/embed/images/0/alt"_json_pointer,
             "/embed/images/1/alt"_json_pointer,
             "/embed/images/2/alt"_json_pointer,
             "/embed/images/3/alt"_json_pointer,
             "/embed/video/alt"_json_pointer,
             // TODO handle "app.bsky.embed.video" captions blobs
             // TODO handle "app.bsky.embed.record"
             // TODO handle "app.bsky.embed.recordWithMedia"
         }},
        {bsky::AppBskyActorProfile,
         {"/description"_json_pointer, "/displayName"_json_pointer}}};
} // namespace json

namespace bsky {
down_reason down_reason_from_string(std::string_view down_reason_str) {
  if (down_reason_str == DownReasonDeactivated) {
    return down_reason::deactivated;
  }
  if (down_reason_str == DownReasonDeleted) {
    return down_reason::deleted;
  }
  if (down_reason_str == DownReasonSuspended) {
    return down_reason::suspended;
  }
  if (down_reason_str == DownReasonTakenDown) {
    return down_reason::taken_down;
  }
  if (down_reason_str == DownReasonTombstone) {
    return down_reason::tombstone;
  }
  if (down_reason_str == DownReasonDeactivated) {
    return down_reason::deactivated;
  }
  return down_reason::invalid;
}

// best guess from JSON $type, may be corrected later e.g. Post -> Reply
tracked_event event_type_from_collection(std::string const &collection) {
  if (collection == AppBskyFeedLike) {
    return tracked_event::like;
  }
  if (collection == AppBskyGraphFollow) {
    return tracked_event::follow;
  }
  if (collection == AppBskyFeedRepost) {
    return tracked_event::repost;
  }
  if (collection == AppBskyGraphBlock) {
    return tracked_event::block;
  }
  if (collection == AppBskyActorProfile) {
    return tracked_event::profile;
  }
  if (collection == AppBskyFeedPost) {
    return tracked_event::post;
  }
  return tracked_event::invalid;
}

embed_type embed_type_from_string(std::string_view embed_type_str) {
  if (embed_type_str == AppBskyEmbedExternal) {
    return embed_type::external;
  }
  if (embed_type_str == AppBskyEmbedImages) {
    return embed_type::images;
  }
  if (embed_type_str == AppBskyEmbedRecord) {
    return embed_type::record;
  }
  if (embed_type_str == AppBskyEmbedRecordWithMedia) {
    return embed_type::record_with_media;
  }
  if (embed_type_str == AppBskyEmbedVideo) {
    return embed_type::video;
  }
  return embed_type::invalid;
}

// Parse ISO8601 time permissively
bsky::time_stamp time_stamp_from_iso_8601(std::string const &date_time) {
  std::istringstream is(date_time);
  bsky::parse_time_stamp tp;
  // is >> date::parse<bsky::parse_time_stamp, char>(UtcDefault, tp);
  is >> std::chrono::parse(UtcDefault, tp);
  if (!is.fail()) {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(tp);
  }
  // fix and parse for invalid +00:00
  constexpr std::string_view bad_zero = "+00:00";
  constexpr std::string_view good_zero = "Z";
  if (ends_with(date_time, bad_zero)) {
    std::string date_time_new(date_time);
    date_time_new.replace(date_time_new.cend() - bad_zero.length(),
                          date_time_new.cend(), good_zero);
    std::istringstream is_new(date_time_new);
    // is_new >> date::parse<bsky::parse_time_stamp, char>(UtcDefault, tp);
    is_new >> std::chrono::parse(UtcDefault, tp);
    if (!is_new.fail()) {
      return std::chrono::time_point_cast<std::chrono::milliseconds>(tp);
    }
  }
  // fix and parse for alternate form of UTC offset -03:00
  constexpr char alt_utc_marker = ':';
  constexpr const char *UtcWithOffset = "%FT%T%z";
  if (date_time.length() >= 3 &&
      (*(date_time.rbegin() + 2) == alt_utc_marker)) {
    std::string date_time_new(date_time);
    date_time_new.erase(date_time_new.length() - 3, 1);
    std::istringstream is_new(date_time_new);
    // is_new >> date::parse<bsky::parse_time_stamp, char>(UtcWithOffset, tp);
    is_new >> std::chrono::parse(UtcWithOffset, tp);
    if (!is_new.fail()) {
      return std::chrono::time_point_cast<std::chrono::milliseconds>(tp);
    }
  }

  REL_WARNING("Failed to parse {} as ISO8601 date-time", date_time);
  return current_time();
}

} // namespace bsky

namespace atproto {

at_uri::at_uri(std::string const &uri_str) {
  if (uri_str.empty()) {
    _empty = true;
    return;
  }
  if (!starts_with(uri_str, URIPrefix)) {
    REL_ERROR("Malformed at-uri {}", uri_str);
    return;
  }
  std::string_view uri_view(uri_str.cbegin() + URIPrefix.length(),
                            uri_str.cend());
  size_t count(0);
  for (const auto token : std::views::split(uri_view, '/')) {
    // with string_view's C++23 range constructor:
    std::string field(token.cbegin(), token.cend());
    switch (count) {
    case 0:
      if (token.empty()) {
        REL_ERROR("Blank authority in at-uri {}", uri_str);
        return;
      }
      _authority.assign(token.cbegin(), token.cend());
      break;
    case 1:
      if (token.empty()) {
        // this is optional
        return;
      }
      _collection.assign(token.cbegin(), token.cend());
      break;
    case 2:
      if (token.empty()) {
        // this is optional
        return;
      }
      _rkey.assign(token.cbegin(), token.cend());
      break;
    }
    ++count;
  }
}

at_uri::at_uri(at_uri const &uri)
    : _authority(uri._authority), _collection(uri._collection),
      _rkey(uri._rkey), _empty(uri._empty) {}
at_uri &at_uri::operator=(at_uri const &uri) {
  _authority = uri._authority;
  _collection = uri._collection;
  _rkey = uri._rkey;
  _empty = uri._empty;
  return *this;
}

at_uri::at_uri(at_uri &&uri)
    : _authority(std::move(uri._authority)),
      _collection(std::move(uri._collection)), _rkey(std::move(uri._rkey)) {}

} // namespace atproto

// convert UTF-8 input to canonical form where case differences are erased
std::wstring to_canonical(std::string_view const input) {
  if (input.empty())
    return std::wstring();
  int32_t capacity((static_cast<int32_t>(input.length()) * 3));
  int32_t new_size;
  std::unique_ptr<UChar[]> workspace(new UChar[capacity]);
  icu::ErrorCode error_code;
  u_strFromUTF8(workspace.get(), capacity, &new_size, input.data(),
                static_cast<int32_t>(input.length()), (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "to_canonical: u_strFromUTF8 error " << error_code.errorName();
    REL_ERROR(oss.str());
    return std::wstring();
  }
  if (new_size > capacity - 1) {
    std::ostringstream oss;
    oss << "UTF-8 to UCHAR overflow for " << input << ", capacity=" << capacity
        << ", length required=" << new_size;
    REL_ERROR(oss.str());
    return std::wstring();
  }
  std::unique_ptr<UChar> case_folded(new UChar[capacity]);
  new_size =
      u_strFoldCase(case_folded.get(), capacity, workspace.get(), new_size,
                    U_FOLD_CASE_DEFAULT, (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "to_canonical: u_strFoldCase error " << error_code.errorName();
    REL_ERROR(oss.str());
    return std::wstring();
  }
  if (new_size > capacity - 1) {
    std::ostringstream oss;
    oss << "Case-fold overflow for " << input << ", capacity=" << capacity
        << ", length required=" << new_size;
    REL_ERROR(oss.str());
    return std::wstring();
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

  size_t output_max(rc_string.size() * 3);
  std::vector<UChar> buffer(output_max, 0);
  std::string result(output_max, 0);

  icu::ErrorCode error_code;
  int32_t len = 0;

  u_strFromWCS(&buffer[0], static_cast<int32_t>(buffer.size()), &len,
               &rc_string[0], static_cast<int32_t>(rc_string.size()),
               (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "wstring_to_utf8: u_strFromWCS error " << error_code.errorName();
    REL_ERROR(oss.str());
    return std::string();
  }
  buffer.resize(len);

  u_strToUTF8(&result[0], static_cast<int32_t>(result.size()), &len, &buffer[0],
              static_cast<int32_t>(buffer.size()), (UErrorCode *)error_code);
  if (error_code.isFailure()) {
    std::ostringstream oss;
    oss << "wstring_to_utf8: u_strToUTF8 error " << error_code.errorName();
    REL_ERROR(oss.str());
    return std::string();
  }
  result.resize(len);

  return result;
}

std::string print_emits(const aho_corasick::wtrie::emit_collection &c) {
  std::wostringstream oss;
  bool first(true);
  for (auto const &emit : c) {
    if (!first)
      oss << L",";
    else
      first = false;
    oss << L"'" << emit.get_keyword() << L"'";
  }
  return wstring_to_utf8(oss.str());
}
