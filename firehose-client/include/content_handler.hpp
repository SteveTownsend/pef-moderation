#ifndef __content_handler_hpp__
#define __content_handler_hpp__
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

#include "content_handler.hpp"
#include "log_wrapper.hpp"
#include "matcher.hpp"
#include "post_processor.hpp"
#include <boost/beast/core.hpp>

namespace beast = boost::beast; // from <boost/beast.hpp>

template <typename PAYLOAD> class content_handler {
public:
  content_handler() : _is_ready(false), _matcher(new matcher) {}
  ~content_handler() = default;

  void set_filter(std::string const &filter_file) {
    _filter_file = filter_file;
    _matcher->set_filter(_filter_file);
    _is_ready = true;
    _post_processor.set_matcher(_matcher);
  }

  std::string get_filter() const { return _filter_file; }

  void handle(beast::flat_buffer const &beast_data) {
    if (!_is_ready)
      return;
    auto matches(_matcher->find_all_matches(beast_data));
    // No match, or all eliminated by contingent match processing
    if (matches.empty()) {
      return;
    }
    std::string json_msg(boost::beast::buffers_to_string(beast_data.data()));

    _post_processor.wait_enqueue(PAYLOAD(json_msg, matches));
  }

private:
  bool _is_ready;
  std::string _filter_file;
  std::shared_ptr<matcher> _matcher;
  post_processor<PAYLOAD> _post_processor;
};

template <>
void content_handler<firehose_payload>::handle(
    beast::flat_buffer const &beast_data) {
  if (!_is_ready)
    return;
  parser my_parser;
  my_parser.get_candidates_from_flat_buffer(beast_data);
  _post_processor.wait_enqueue(firehose_payload(my_parser));
}

#endif