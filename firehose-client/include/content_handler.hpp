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

#include "matcher.hpp"
#include "post_processor.hpp"
#include <boost/beast/core.hpp>

namespace beast = boost::beast; // from <boost/beast.hpp>

class content_handler {
public:
  content_handler();
  ~content_handler() = default;
  void set_filter(std::string const &filter_file);
  std::string get_filter() const;

  void handle(beast::flat_buffer const &beast_data);

private:
  bool _is_ready;
  std::string _filter_file;
  matcher _matcher;
  post_processor<payload> _verifier;
};
#endif