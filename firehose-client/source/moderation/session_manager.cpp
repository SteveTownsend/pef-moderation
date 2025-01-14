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

#include "moderation/session_manager.hpp"
#include "log_wrapper.hpp"
#include "restc-cpp/RequestBuilder.h"
#include <boost/fusion/adapted.hpp>
#include <jwt-cpp/traits/boost-json/traits.h>

// com.atproto.server.createSession
BOOST_FUSION_ADAPT_STRUCT(bsky::session_tokens,
                          (std::string, accessJwt)(std::string, refreshJwt))
BOOST_FUSION_ADAPT_STRUCT(bsky::login_info,
                          (std::string, identifier)(std::string, password))

namespace bsky {
pds_session::pds_session(restc_cpp::RestClient &client, std::string const &host)
    : _client(client), _host(host) {}

void pds_session::connect(bsky::login_info const &credentials) {
  // Create and instantiate a Post from data received from the server.
  size_t retries(0);
  while (retries < 5) {
    try {
      _tokens =
          _client
              .ProcessWithPromiseT<bsky::session_tokens>(
                  [&](restc_cpp::Context &ctx) {
                    // This is a co-routine, running in a worker-thread

                    // Instantiate a session_tokens structure.
                    bsky::session_tokens session;
                    // Serialize it asynchronously. The asynchronously part
                    // does not really matter here, but it may if you receive
                    // huge data structures.
                    restc_cpp::SerializeFromJson(
                        session,

                        // Construct a request to the server
                        restc_cpp::RequestBuilder(ctx)
                            .Post(_host + "com.atproto.server.createSession")
                            .Header("Content-Type", "application/json")
                            .Data(credentials)
                            // Send the request
                            .Execute());

                    // Return the session instance through C++ future<>
                    return session;
                  })

              // Get the Post instance from the future<>, or any C++ exception
              // thrown within the lambda.
              .get();

      // store the response refresh and access JWTs
      auto access_token =
          jwt::decode<jwt::traits::boost_json>(_tokens.accessJwt);
      _access_expiry = access_token.get_expires_at();
      REL_INFO("bsky session access token expires at {}", _access_expiry);
      auto refresh_token =
          jwt::decode<jwt::traits::boost_json>(_tokens.refreshJwt);
      _refresh_expiry = refresh_token.get_expires_at();
      REL_INFO("bsky session refresh token expires at {}", _refresh_expiry);
      break;
    } catch (std::exception const &exc) {
      REL_ERROR("create-session failed for {} exception {}, retry", _host,
                exc.what());
      std::this_thread::sleep_for(std::chrono::seconds(5));
      ++retries;
    }
  }
}

void pds_session::check_refresh() {
  // TODO handle session refresh when access or refresh token expiry is near
  auto now(std::chrono::system_clock::now());
  auto time_to_expiry(std::chrono::duration_cast<std::chrono::milliseconds>(
                          _access_expiry - now)
                          .count());
  if (time_to_expiry <
      static_cast<decltype(time_to_expiry)>(AccessExpiryBuffer.count())) {
    REL_INFO("Refresh access token,expiry in {} ms", time_to_expiry);
    size_t retries(0);
    while (retries < 5) {
      try {
        _tokens =
            _client
                .ProcessWithPromiseT<bsky::session_tokens>(
                    [&](restc_cpp::Context &ctx) {
                      // This is a co-routine, running in a worker-thread

                      // Instantiate a session_tokens structure.
                      bsky::session_tokens session;
                      // Serialize it asynchronously. The asynchronously part
                      // does not really matter here, but it may if you receive
                      // huge data structures.
                      restc_cpp::SerializeFromJson(
                          session,

                          // Construct a request to the server
                          restc_cpp::RequestBuilder(ctx)
                              .Post(_host + "com.atproto.server.refreshSession")
                              .Header("Content-Type", "application/json")
                              .Header(
                                  "Authorization",
                                  std::string("Bearer " + _tokens.refreshJwt))
                              // Send the request
                              .Execute());

                      // Return the session instance through C++ future<>
                      return session;
                    })

                // Get the Post instance from the future<>, or any C++ exception
                // thrown within the lambda.
                .get();

        // assumes refresh and access JWTs have expiry, we are out of luck
        // otherwise
        auto access_token =
            jwt::decode<jwt::traits::boost_json>(_tokens.accessJwt);
        _access_expiry = access_token.get_expires_at();
        REL_INFO("bsky session access token now expires at {}", _access_expiry);
        auto refresh_token =
            jwt::decode<jwt::traits::boost_json>(_tokens.refreshJwt);
        _refresh_expiry = refresh_token.get_expires_at();
        REL_INFO("bsky session refresh token now expires at {}",
                 _refresh_expiry);
        break;
      } catch (std::exception const &exc) {
        REL_ERROR("refresh-session failed for {} exception {}, retry", _host,
                  exc.what());
        std::this_thread::sleep_for(std::chrono::seconds(5));
        ++retries;
      }
    }
  }
}

} // namespace bsky
