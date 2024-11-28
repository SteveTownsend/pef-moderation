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
// Originally sourced from:

// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: WebSocket SSL client, coroutine
//
//------------------------------------------------------------------------------

#include "datasource.hpp"
#include "matcher.hpp"
#include <iostream>

int main(int argc, char **argv) {
  // Check command line arguments.
  if (argc != 4) {
    std::cerr << "Usage: firehose-client <host> <port> <source>\n"
              << "Example:\n"
              << "    ./firehose-client.exe jetstream1.us-east.bsky.network "
                 "443 /subscribe\n";
    // for profile and post commits:
    // subscribe?wantedCollections=app.bsky.actor.profile&wantedCollections=app.bsky.feed.post
    return EXIT_FAILURE;
  }
  auto const host = argv[1];
  auto const port = argv[2];
  auto const source = argv[3];

  matcher filter("smoke");
  datasource(host, port, source, filter).start();

  return EXIT_SUCCESS;
}
