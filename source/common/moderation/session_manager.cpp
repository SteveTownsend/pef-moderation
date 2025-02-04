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

#include "common/moderation/session_manager.hpp"
#include "common/bluesky/client.hpp"
#include "common/log_wrapper.hpp"
#include "restc-cpp/RequestBuilder.h"
#include <boost/fusion/adapted.hpp>
#include <jwt-cpp/traits/boost-json/traits.h>

// com.atproto.server.createSession
BOOST_FUSION_ADAPT_STRUCT(bsky::session_tokens,
                          (std::string, accessJwt)(std::string, refreshJwt))
BOOST_FUSION_ADAPT_STRUCT(bsky::login_info,
                          (std::string, identifier)(std::string, password))

namespace bsky {
pds_session::pds_session(bsky::client &client, std::string const &host)
    : _client(client), _host(host) {}

void pds_session::connect(bsky::login_info const &credentials) {
  _tokens = _client.do_post<bsky::login_info, bsky::session_tokens>(
      "com.atproto.server.createSession", credentials);

  auto access_token = jwt::decode<jwt::traits::boost_json>(_tokens.accessJwt);
  _access_expiry = access_token.get_expires_at();
  REL_INFO("bsky session access token expires at {}", _access_expiry);
  auto refresh_token = jwt::decode<jwt::traits::boost_json>(_tokens.refreshJwt);
  _refresh_expiry = refresh_token.get_expires_at();
  REL_INFO("bsky session refresh token expires at {}", _refresh_expiry);
}

void pds_session::check_refresh() {
  auto now(std::chrono::system_clock::now());
  auto time_to_expiry(std::chrono::duration_cast<std::chrono::milliseconds>(
                          _access_expiry - now)
                          .count());
  if (time_to_expiry <
      static_cast<decltype(time_to_expiry)>(AccessExpiryBuffer.count())) {
    REL_INFO("Refresh access token,expiry in {} ms", time_to_expiry);
    bsky::empty empty_body;
    _tokens = _client.do_post<bsky::empty, bsky::session_tokens>(
        "com.atproto.server.refreshSession", empty_body,
        restc_cpp::serialize_properties_t(), true);
    // assumes refresh and access JWTs have expiry, we are out of luck
    // otherwise
    auto access_token = jwt::decode<jwt::traits::boost_json>(_tokens.accessJwt);
    _access_expiry = access_token.get_expires_at();
    REL_INFO("bsky session access token now expires at {}", _access_expiry);
    auto refresh_token =
        jwt::decode<jwt::traits::boost_json>(_tokens.refreshJwt);
    _refresh_expiry = refresh_token.get_expires_at();
    REL_INFO("bsky session refresh token now expires at {}", _refresh_expiry);
  }
}

} // namespace bsky
