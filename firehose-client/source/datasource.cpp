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

#include "datasource.hpp"
#include "firehost_client_config.hpp"
#include "log_wrapper.hpp"
#include "matcher.hpp"
#include "metrics.hpp"
#include <prometheus/counter.h>

datasource::datasource(std::shared_ptr<config> &settings)
    : _settings(settings),
      _input_messages(metrics::instance().add_counter(
          "websocket_inbound_messages", "Number of inbound messages")),
      _input_message_bytes(metrics::instance().add_counter(
          "websocket_inbound_bytes", "Number of inbound message bytes")) {
  _host = _settings->get_config()[PROJECT_NAME]["datasource"]["hosts"]
              .as<std::string>();
  _port = _settings->get_config()[PROJECT_NAME]["datasource"]["port"]
              .as<std::string>();
  _subscription =
      _settings->get_config()[PROJECT_NAME]["datasource"]["subscription"]
          .as<std::string>();
  _handler.set_filter(
      _settings->get_config()[PROJECT_NAME]["filters"]["filename"]
          .as<std::string>());
}

void datasource::start() {
  REL_INFO("client startup for {}:{} at {}, filters {}", _host, _port,
           _subscription, _handler.get_filter());

  // The io_context is required for all I/O
  net::io_context ioc;

  // The SSL context is required, and holds certificates
  ssl::context ctx{ssl::context::tlsv12_client};

  // Launch the asynchronous operation
  boost::asio::spawn(ioc,
                     std::bind(&datasource::do_work, this, std::ref(ioc),
                               std::ref(ctx), std::placeholders::_1),
                     // on completion, spawn will call this function
                     [](std::exception_ptr ex) {
                       // if an exception occurred in the coroutine,
                       // it's something critical, e.g. out of memory
                       // we capture normal errors in the ec
                       // so we just rethrow the exception here,
                       // which will cause `ioc.run()` to throw
                       if (ex)
                         std::rethrow_exception(ex);
                     });

  // Run the I/O service. The call will return when
  // the socket is closed.
  ioc.run();
}

void datasource::do_work(net::io_context &ioc, ssl::context &ctx,
                         net::yield_context yield) {
  beast::error_code ec;

  // These objects perform our I/O
  tcp::resolver resolver(ioc);
  websocket::stream<ssl::stream<beast::tcp_stream>> ws(ioc, ctx);

  // Look up the domain name
  auto const results = resolver.async_resolve(_host, _port, yield[ec]);
  if (ec)
    return fail(ec, "resolve");

  // Set a timeout on the operation
  beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));

  // Make the connection on the IP address we get from a lookup
  auto ep = beast::get_lowest_layer(ws).async_connect(results, yield[ec]);
  if (ec)
    return fail(ec, "connect");

  // Set SNI Hostname (many hosts need this to handshake successfully)
  if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                _host.c_str())) {
    ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                           net::error::get_ssl_category());
    return fail(ec, "connect");
  }

  // Update the host string. This will provide the value of the
  // Host HTTP header during the WebSocket handshake.
  // See https://tools.ietf.org/html/rfc7230#section-5.4
  auto host = _host + ':' + std::to_string(ep.port());

  // Set a timeout on the operation
  beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(30));

  // Set a decorator to change the User-Agent of the handshake
  ws.set_option(
      websocket::stream_base::decorator([](websocket::request_type &req) {
        req.set(http::field::user_agent,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " websocket-client-coro");
      }));

  // Perform the SSL handshake
  ws.next_layer().async_handshake(ssl::stream_base::client, yield[ec]);
  if (ec)
    return fail(ec, "ssl_handshake");

  // Turn off the timeout on the tcp_stream, because
  // the websocket stream has its own timeout system.
  beast::get_lowest_layer(ws).expires_never();

  // Set suggested timeout settings for the websocket
  ws.set_option(
      websocket::stream_base::timeout::suggested(beast::role_type::client));

  // Perform the websocket handshake
  ws.async_handshake(_host, _subscription, yield[ec]);
  if (ec)
    return fail(ec, "handshake");
  // main processing loop
  while (true) {
    // This buffer will hold the incoming message
    beast::flat_buffer buffer;

    // Read a message into our buffer
    ws.async_read(buffer, yield[ec]);
    if (ec)
      return fail(ec, "read");

    // update stats
    _input_messages.Get({{"host", _host}}).Increment();
    _input_message_bytes.Get({{"host", _host}})
        .Increment(static_cast<double>(buffer.size()));

    _handler.handle(buffer);
  }

  // Close the WebSocket connection
  ws.async_close(websocket::close_code::normal, yield[ec]);
  if (ec)
    return fail(ec, "close");

  // If we get here then the connection is closed gracefully
}

// Report a failure
void datasource::fail(beast::error_code ec, char const *what) {
  std::ostringstream oss;
  oss << what << ": " << ec.message() << "\n";
  REL_ERROR("datasource error: {}", oss.str());
}
