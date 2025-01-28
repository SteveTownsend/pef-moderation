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
#include "payload.hpp"

template <>
void content_handler<firehose_payload>::handle(
    beast::flat_buffer const &beast_data) {
  parser my_parser;
  my_parser.get_candidates_from_flat_buffer(beast_data);
  _post_processor.wait_enqueue(firehose_payload(my_parser));
}