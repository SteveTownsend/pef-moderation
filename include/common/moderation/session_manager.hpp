#ifndef __session_manager__
#define __session_manager__
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
// #include "helpers.hpp"
#include "jwt-cpp/jwt.h"
#include "restc-cpp/RequestBody.h"
#include "restc-cpp/restc-cpp.h"

namespace bsky {

// com.atproto.server.createSession
// com.atproto.server.refreshSession
struct session_tokens {
  std::string accessJwt;
  std::string refreshJwt;
};

struct login_info {
  std::string identifier;
  std::string password;
};

class pds_session {
public:
  pds_session(restc_cpp::RestClient &client, std::string const &host);
  pds_session() = delete;

  void connect(login_info const &credentials);
  void check_refresh();
  inline std::string access_token() const { return _tokens.accessJwt; }

  static constexpr std::chrono::milliseconds AccessExpiryBuffer =
      std::chrono::milliseconds(60000 * 2);
  static constexpr std::chrono::milliseconds RefreshExpiryBuffer =
      std::chrono::milliseconds(60000 * 30);

private:
  restc_cpp::RestClient &_client;
  std::string _host;
  session_tokens _tokens;
  //  Log excerpt:
  //    2025-01-10 17:50:33.778218500     info  36816 bsky session access token
  //      expires at 2025-01-11 00:50:34.0000000
  //    2025-01-10 17:50:33.778475000     info  36816 bsky session refresh token
  //      expires at 2025-04-10 22:50:34.0000000

  jwt::date _access_expiry;  // a few hours
  jwt::date _refresh_expiry; // a few months
};

} // namespace bsky

#endif