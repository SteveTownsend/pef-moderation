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

#include "common/rest_utils.hpp"
#include "common/bluesky/platform.hpp"

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
