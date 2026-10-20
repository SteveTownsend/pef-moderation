#ifndef __payload_hpp__
#define __payload_hpp__
/*************************************************************************
Public Education Forum Moderation Firehose Client
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

#include <unordered_map>

#include "common/activity/event_recorder.hpp"
#include "common/helpers.hpp"
#include "common/parser.hpp"
#include "matcher.hpp"
#include "post_processor.hpp"
class firehose_payload {
 public:
  firehose_payload() = default;
  void handle(beast::flat_buffer const &beast_data);
  inline std::string to_string(parser &my_parser) const {
    auto const &header(my_parser.other_cbors().front().second);
    auto const &message(my_parser.other_cbors().back().second);
    std::ostringstream oss;
    oss << "header (" << dump_json(header) << ") message ("
        << dump_json(message) << ')';
    return oss.str();
  }

 private:
  struct context {
    inline context(post_processor<firehose_payload> &processor,
                   nlohmann::json const &content)
        : _processor(processor), _content(content) {}
    std::string _repo;
    std::string _this_path;
    std::string _embed_type_str;
    bsky::tracked_event _event_type = bsky::tracked_event::invalid;
    bool _recorded = false;
    bsky::embed_type process_embed(nlohmann::json const &content);

    void add_embed(embed::embed_info &&new_embed) {
      _embeds.emplace_back(std::move(new_embed));
    }
    auto const &get_embeds() const { return _embeds; }

   private:
    post_processor<firehose_payload> &_processor;
    nlohmann::json const &_content;
    std::vector<embed::embed_info> _embeds;
  };
  void handle_content(const int64_t seq, std::string const &repo,
                      std::string const &cid, nlohmann::json const &content);
  void handle_matchable_content(const int64_t seq, std::string const &repo,
                                std::string const &cid,
                                nlohmann::json const &content);

  post_processor<firehose_payload> _post_processor;
  path_candidate_list _path_candidates;
  std::unordered_map<std::string, std::string> _path_by_cid;
};

#endif