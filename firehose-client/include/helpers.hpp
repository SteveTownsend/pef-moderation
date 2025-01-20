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
#include "log_wrapper.hpp"
#include "nlohmann/json.hpp"
#include "restc-cpp/RequestBody.h"
#include "restc-cpp/SerializeJson.h"
#include "restc-cpp/restc-cpp.h"
#include <algorithm>
#include <boost/functional/hash.hpp>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

namespace bsky {
constexpr std::string_view AppBskyFeedLike = "app.bsky.feed.like";
constexpr std::string_view AppBskyFeedPost = "app.bsky.feed.post";
constexpr std::string_view AppBskyFeedRepost = "app.bsky.feed.repost";

constexpr std::string_view AppBskyGraphBlock = "app.bsky.graph.block";
constexpr std::string_view AppBskyGraphFollow = "app.bsky.graph.follow";
constexpr std::string_view AppBskyGraphList = "app.bsky.graph.list";
constexpr std::string_view AppBskyGraphListItem = "app.bsky.graph.listitem";
constexpr std::string_view AppBskyGraphDefsModlist =
    "app.bsky.graph.defs#modlist";

constexpr std::string_view AppBskyActorProfile = "app.bsky.actor.profile";

constexpr std::string_view AppBskyEmbedExternal = "app.bsky.embed.external";
constexpr std::string_view AppBskyEmbedImages = "app.bsky.embed.images";
constexpr std::string_view AppBskyEmbedRecord = "app.bsky.embed.record";
constexpr std::string_view AppBskyEmbedRecordWithMedia =
    "app.bsky.embed.recordWithMedia";
constexpr std::string_view AppBskyEmbedVideo = "app.bsky.embed.video";

constexpr std::string_view AppBskyRichtextFacet = "app.bsky.richtext.facet";
constexpr std::string_view AppBskyRichtextFacetLink =
    "app.bsky.richtext.facet#link";
constexpr std::string_view AppBskyRichtextFacetMention =
    "app.bsky.richtext.facet#mention";
constexpr std::string_view AppBskyRichtextFacetTag =
    "app.bsky.richtext.facet#tag";

namespace moderation {
constexpr std::string_view ReasonOther =
    "com.atproto.moderation.defs#reasonOther";
} // namespace moderation

constexpr std::string_view DownReasonDeactivated = "deactivated";
constexpr std::string_view DownReasonDeleted = "deleted";
constexpr std::string_view DownReasonSuspended = "suspended";
constexpr std::string_view DownReasonTakenDown = "takendown";
constexpr std::string_view DownReasonTombstone = "#tombstone";

enum class down_reason {
  invalid = -1,
  unknown,
  deactivated,
  deleted,
  suspended,
  taken_down,
  tombstone
};
down_reason down_reason_from_string(std::string_view down_reason_str);

enum class tracked_event {
  invalid = -1,
  post,
  repost,
  quote,
  reply,
  like,
  follow,
  block,
  activate,
  deactivate,
  handle,
  profile
};

tracked_event event_type_from_collection(std::string const &collection);

enum class embed_type {
  invalid = -1,
  external,
  images,
  record,
  record_with_media,
  video
};

embed_type embed_type_from_string(std::string_view embed_type_str);

// Compact internal representation
typedef std::chrono::sys_time<std::chrono::milliseconds> time_stamp;
// Permissive parse based on real-world observation
typedef std::chrono::sys_time<std::chrono::nanoseconds> parse_time_stamp;

// optimal format for UTC offset 'Z'
constexpr const char *UtcDefault = "%FT%TZ";

inline bsky::time_stamp current_time() {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now());
}

// Parse ISO8601 time
bsky::time_stamp time_stamp_from_iso_8601(std::string const &date_time);

template <typename T>
inline bool alert_needed(const T count, const size_t factor) {
  std::div_t divide_result(std::div(int(count), int(factor)));
  return (divide_result.rem == 0) &&
         !(divide_result.quot & (divide_result.quot - 1));
}

} // namespace bsky

namespace atproto {

constexpr std::string_view RepoStrongRef = "com.atproto.repo.strongRef";
constexpr std::string_view AdminDefsRepoRef = "com.atproto.admin.defs#repoRef";
constexpr std::string_view ProxyLabelerSuffix = "#atproto_labeler";
constexpr std::string_view AcceptLabelersPrefix =
    "did:plc:ar7c4by46qjdydhdevvrndac;redact, ";

constexpr std::string_view SyncSubscribeReposOpCreate = "create";
constexpr std::string_view SyncSubscribeReposOpDelete = "delete";
constexpr std::string_view SyncSubscribeReposOpUpdate = "update";

// URI holder per https://atproto.com/specs/at-uri-scheme
constexpr std::string_view URIPrefix = "at://";
inline std::string make_at_uri(std::string const &authority,
                               std::string const &collection = {},
                               std::string const &rkey = {}) {
  if (collection.empty()) {
    return std::string(URIPrefix) + authority;
  } else if (rkey.empty()) {
    return std::string(URIPrefix) + authority + '/' + collection;
  } else {
    return std::string(URIPrefix) + authority + '/' + collection + '/' + rkey;
  }
}
struct at_uri {
  at_uri(std::string const &uri_str);
  at_uri(at_uri const &uri);
  at_uri &operator=(at_uri const &uri);
  at_uri(at_uri &&uri);
  std::string _authority; // in practice, this is a DID
  std::string _collection;
  std::string _rkey; // optional
  bool _empty = false;
  inline operator std::string() const {
    return make_at_uri(_authority, _collection, _rkey);
  }
  inline operator bool() const { return !_empty; }
  static inline at_uri &empty() {
    static at_uri empty_uri("");
    return empty_uri;
  }

private:
  inline at_uri() : _empty(true) {}
};

struct at_uri_hash {
  std::size_t operator()(const at_uri &uri) const {
    size_t result(0);
    boost::hash_combine(result, std::hash<std::string>()(uri._authority));
    boost::hash_combine(result, std::hash<std::string>()(uri._collection));
    if (!uri._rkey.empty()) {
      boost::hash_combine(result, std::hash<std::string>()(uri._rkey));
    }
    return result;
  }
};

template <typename IteratorType> class cid_decoder {
public:
  cid_decoder(IteratorType begin, IteratorType end)
      : _begin(begin), _end(end), _current(begin) {}

  // decode CID
  inline nlohmann::json decode() {
    uint64_t version(read_u64_leb128());
    // TODO account for two unexplained bytes
    version = read_u64_leb128();
    version = read_u64_leb128();
    uint64_t codec(read_u64_leb128());
    uint64_t digest_length(0);
    if (version == 0x12 && codec == 0x20) {
      // handle v0 CID - digest is always 32 bytes
      digest_length = 32;
      version = 0;
    } else {
      // read Multihash
      digest_length = read_u64_leb128();
    }
    std::vector<unsigned char> digest(digest_length);
    for (auto next = digest.begin(); next != digest.end(); ++next) {
      *next = get();
    }
    // Caller needs to process according to context
    return nlohmann::json(
        {{"digest", std::string(digest.cbegin(), digest.cend())},
         {"version", version},
         {"codec", codec}});
  }

private:
  unsigned char get() const { return *_current++; }

  uint64_t read_u64_leb128() const {
    unsigned char uchar(0);
    uint32_t shift(0);
    uint64_t result(0);
    while (true) {
      uchar = get();
      if (!(uchar & 0x80)) {
        result |= (uchar << shift);
        return result;
      } else {
        result |= ((uchar & 0x7F) << shift);
      }
    }
  }
  IteratorType _begin;
  IteratorType _end;
  mutable IteratorType _current;
};

struct at_uri_less {
  bool operator()(const at_uri &lhs, const at_uri &rhs) const {
    if (lhs._authority == rhs._authority) {
      if (lhs._collection == rhs._collection) {
        return lhs._rkey < rhs._rkey;
      }
      return lhs._collection < rhs._collection;
    }
    return lhs._authority < rhs._authority;
  }
};

inline bool operator==(const at_uri &lhs, const at_uri &rhs) {
  return lhs._authority == rhs._authority &&
         lhs._collection == rhs._collection && lhs._rkey == rhs._rkey;
}

} // namespace atproto

inline std::string print_current_time() {
  return std::format("{0:%F}T{0:%T}Z", std::chrono::utc_clock::now());
}

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

namespace json {
extern restc_cpp::JsonFieldMapping TypeFieldMapping;
extern std::map<std::string_view, std::vector<nlohmann::json::json_pointer>>
    TargetFieldNames;
} // namespace json

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
