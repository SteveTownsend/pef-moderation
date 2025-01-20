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
#include "moderation/embed_checker.hpp"
#include "jwt-cpp/traits/boost-json/traits.h"
#include "log_wrapper.hpp"
#include "matcher.hpp"
#include "metrics.hpp"
#include "moderation/action_router.hpp"
#include "moderation/report_agent.hpp"
#include "post_processor.hpp"
#include "restc-cpp/RequestBuilder.h"

namespace bsky {
namespace moderation {

embed_checker &embed_checker::instance() {
  static embed_checker my_instance;
  return my_instance;
}

embed_checker::embed_checker() : _queue(QueueLimit) {}

void embed_checker::start() {
  restc_cpp::Request::Properties properties;
  properties.maxRedirects = UrlRedirectLimit;
  for (size_t count = 0; count < NumberOfThreads; ++count) {
    _rest_client = restc_cpp::RestClient::Create(properties);
    _threads[count] = std::thread([&] {
      while (true) {
        embed::embed_info_list embed_list;
        _queue.wait_dequeue(embed_list);
        // process the item
        metrics::instance()
            .operational_stats()
            .Get({{"embed_checker", "backlog"}})
            .Decrement();

        // TODO the work
        // add LFU cache of URL/did/rate-limit pairs
        // add LFU cache of content-cid/did/rate-limit
        // add metrics
        for (auto const &next_embed : embed_list._embeds) {
          embed_handler handler(*this, *_rest_client, embed_list._did,
                                embed_list._path);
          _rest_client->GetConnectionProperties()->redirectFn = std::bind(
              &embed_handler::on_url_redirect, &handler, std::placeholders::_1,
              std::placeholders::_2, std::placeholders::_3);
          ;

          std::visit(handler, next_embed);
        }

        // TODO terminate gracefully
      }
    });
  }
}

void embed_checker::wait_enqueue(embed::embed_info_list &&value) {
  _queue.enqueue(value);
  metrics::instance()
      .operational_stats()
      .Get({{"embed_checker", "backlog"}})
      .Increment();
}

bool embed_checker::should_process_uri(std::string const &uri) {
  // ensure well-formed, then check whitelist vs substring after prefix to
  // first '/' or end of string
  // check for platform suffix on URLs
  std::string target;
  constexpr std::string_view UrlSuffix = "\xe2\x80\xa6";
  if (ends_with(uri, UrlSuffix)) {
    target = uri.substr(0, uri.length() - UrlSuffix.length());
  } else {
    target = uri;
  }
  boost::core::string_view url_view = target;
  boost::system::result<boost::urls::url_view> parsed_uri =
      boost::urls::parse_uri(url_view);
  // leftover text may be handleble by websites, this shows in posts as trailing
  // "..."
  if (parsed_uri.has_error()) {
    // TOTO this fails for multilanguage e.g.
    // https://bsky.app/profile/did:plc:j5k6e6hf2rp4bkqk5sao56ad/post/3lg6hohjsg422
    REL_WARNING("Skip malformed URI {}, error {}", uri,
                parsed_uri.error().message());
    return true;
  }
  auto host(parsed_uri->host());
  if (starts_with(host, _uri_host_prefix)) {
    host = host.substr(_uri_host_prefix.length());
  }
  return _whitelist_uris.contains(host);
}

void embed_handler::operator()(embed::external const &value) {
  // check redirects for string match, and for abuse
  if (_checker.uri_seen(value._uri) ||
      _checker.should_process_uri(value._uri)) {
    return;
  }
  _root_url = value._uri;
  bool done(false);
  REL_INFO("Redirect check starting for {}", value._uri);
  while (!done) {
    size_t retries(0);
    while (retries < 5) {
      try {
        auto check_done =
            _rest_client.ProcessWithPromise([=](restc_cpp::Context &ctx) {
              restc_cpp::RequestBuilder builder(ctx);
              // pretend to be a browser, like the web-app
              builder.Get(value._uri)
                  .Header("User-Agent",
                          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                          "AppleWebKit/537.36 (KHTML, like Gecko) "
                          "Chrome/132.0.0.0 Safari/537.36")
                  .Header("Referrer-Policy", "strict-origin-when-cross-origin")
                  .Header(
                      "Accept",
                      "text/html,application/xhtml+xml,application/"
                      "xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8")
                  .Header("Accept-Language", "en-US,en;q=0.9")
                  .Header("Accept-Encoding", "gzip, deflate")
                  .Execute();
            });
        try {
          check_done.get();
        } catch (boost::system::system_error const &exc) {
          if (exc.code().value() == boost::asio::error::eof &&
              exc.code().category() ==
                  boost::asio::error::get_misc_category()) {
            REL_WARNING("Redirect check: asio eof, retry");
            ++retries;
          } else {
            // unrecoverable error
            REL_ERROR("Redirect check {} Boost exception {}", value._uri,
                      exc.what())
          }
        } catch (std::exception const &ex) {
          REL_ERROR("Redirect check for {} error {}", value._uri, ex.what());
        }
        done = true;
        break;
      } catch (restc_cpp::ConstraintException const &) {
        REL_ERROR("Redirect check overflow for {}", value._uri);
        done = true;
        break;
      } catch (std::exception const &exc) {
        REL_ERROR("Redirect check {} exception {}", value._uri, exc.what())
        done = true;
        break;
      }
    }
  }
  REL_INFO("Redirect check complete for {}", value._uri);
  // // TODO bot check avoidance, brute force for now
  // std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

bool embed_handler::on_url_redirect(int code, std::string &url,
                                    const restc_cpp::Reply &reply) {
  REL_INFO("Redirect code {} for {}", code, url);
  // already processed, or whitelisted
  if (_checker.uri_seen(url) || _checker.should_process_uri(url)) {
    return false; // stop following the chain
  };
  candidate_list candidate = {{_root_url, "redirected_url", url}};
  match_results results(
      _checker.get_matcher()->all_matches_for_candidates(candidate));
  if (!results.empty()) {
    action_router::instance().wait_enqueue({_repo, {{_path, results}}});
  }
  return true;
}

} // namespace moderation
} // namespace bsky
