#ifndef __content_handler_hpp__
#define __content_handler_hpp__
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

#include <boost/beast/core.hpp>

#include "common/log_wrapper.hpp"
#include "matcher.hpp"
#include "moderation/action_router.hpp"
#include "moderation/embed_checker.hpp"
#include "post_processor.hpp"

namespace beast = boost::beast;  // from <boost/beast.hpp>

template <typename PAYLOAD>
class content_handler {
 public:
  content_handler() = default;
  ~content_handler() = default;

  void handle(beast::flat_buffer const &beast_data) {
    auto matches(matcher::shared().find_all_matches(beast_data));
    // No match, or all eliminated by contingent match processing
    if (matches.empty()) {
      return;
    }
    std::string json_msg(boost::beast::buffers_to_string(beast_data.data()));

    // With Sync 1.1 firehose, the backlog here grows too fast, blowing up RAM -
    // do work inline
    // _post_processor.wait_enqueue(PAYLOAD(json_msg, matches));
    try {
      PAYLOAD(json_msg, matches).handle(_post_processor);
    } catch (std::exception const &exc) {
      REL_ERROR("content_handler exception {}", exc.what());
      controller::instance().force_stop();
    }
  }

 private:
  post_processor<PAYLOAD> _post_processor;
};

class firehose_payload;
template <>
void content_handler<firehose_payload>::handle(
    beast::flat_buffer const &beast_data);

#endif