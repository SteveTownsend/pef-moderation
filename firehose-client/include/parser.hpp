#ifndef __parser_hpp__
#define __parser_hpp__
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

#include "common/config.hpp"
#include "common/helpers.hpp"
#include "common/log_wrapper.hpp"
#include "matcher.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <boost/beast/core.hpp>
#include <multiformats/cid.hpp>
#include <string_view>
#include <tuple>
#include <unordered_set>


namespace beast = boost::beast; // from <boost/beast.hpp>
using namespace std::literals;

class parser {
public:
  parser() = default;
  ~parser() = default;

  // Extract UTF-8 string containing the material to be checked,  which is
  // context-dependent
  candidate_list
  get_candidates_from_string(std::string const &full_content) const;
  candidate_list
  get_candidates_from_flat_buffer(beast::flat_buffer const &beast_data);
  candidate_list get_candidates_from_json(nlohmann::json &full_json) const;
  static candidate_list
  get_candidates_from_record(nlohmann::json const &record);

  template <typename IteratorType>
  bool json_from_cbor(IteratorType first, IteratorType last) {
    bool parsed(false);
    try {
      std::function<bool(int /*depth*/, nlohmann::json::parse_event_t /*event*/,
                         nlohmann::json & /*parsed*/)>
          callback =
              std::bind(&parser::cbor_callback, this, std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3);
      parsed = nlohmann::json::from_cbor_sequence(
          first, last, callback, true,
          nlohmann::json::cbor_tag_handler_t::ignore);

    } catch (std::exception const &exc) {
      REL_ERROR("from_cbor_sequence threw: {}", exc.what());
    }
    if (!parsed) {
      std::ostringstream oss;
      oss << std::hex;
      auto os_iter = std::ostream_iterator<int>(oss);
      std::ranges::copy(first, last, os_iter);

      REL_ERROR("from_cbor_sequence parse failed: {}", oss.str());
    }
    return parsed;
  }

  template <typename BasicJsonType, typename InputAdapterType,
            typename SAX = nlohmann::detail::json_sax_dom_parser<
                BasicJsonType, InputAdapterType>>
  class car_reader
      : public nlohmann::detail::binary_reader<BasicJsonType, InputAdapterType,
                                               SAX> {
  public:
    using nlohmann::detail::binary_reader<BasicJsonType, InputAdapterType,
                                          SAX>::binary_reader;
    using char_int_type = typename nlohmann::detail::char_traits<
        typename InputAdapterType::char_type>::int_type;

    template <typename IteratorType>
    bool
    parse_car(IteratorType first, IteratorType last,
              nlohmann::detail::parser_callback_t<BasicJsonType> cb = nullptr,
              const bool allow_exceptions = true, const bool strict = true,
              const nlohmann::detail::cbor_tag_handler_t tag_handler =
                  nlohmann::detail::cbor_tag_handler_t::error) {
      nlohmann::basic_json result;
      _tag_handler = tag_handler;
      _callback = cb;
      auto ia =
          nlohmann::detail::input_adapter(std::move(first), std::move(last));
      nlohmann::detail::json_sax_dom_callback_parser<BasicJsonType,
                                                     decltype(ia)>
          sax(result, cb, allow_exceptions);
      return sax_parse_car(&sax, strict);
    }

  protected:
    uint64_t read_u64_leb128(const bool get_char = true) {
      unsigned char uchar(0);
      uint32_t shift(0);
      uint64_t result(0);
      bool first(true);
      while (true) {
        uchar = static_cast<unsigned char>((first && !get_char) ? this->current
                                                                : this->get());
        first = false;
        if (!(uchar & 0x80)) {
          result |= (uchar << shift);
          return result;
        } else {
          result |= ((uchar & 0x7F) << shift);
        }
      }
    }

    // Decode CAR header
    bool parse_car_header() {
      read_u64_leb128(); // skip header length
      // decode DAG-CBOR
      nlohmann::basic_json result;
      SAX this_pass_sax(result, _callback, _allow_exceptions);
      this->sax = &this_pass_sax;
      if (this->parse_cbor_internal(true, _tag_handler)) {
        // check callback for top-level object parse completion
        return _callback(0, nlohmann::detail::parse_event_t::result, result);
      }
      return false;
    }

    // decode CID
    bool parse_car_cid(const bool get_char = true) {
      uint64_t version(read_u64_leb128(get_char));
      uint64_t codec(read_u64_leb128());
      uint64_t digest_length(0);
      if (version == 0x12 && codec == 0x20) {
        // handle v0 CID - digest is always 32 bytes
        digest_length = 32;
        version = 0;
      } else {
        // arcane knowledge - DAG-PB wrapper on the 32-byte digest
        digest_length = 34;
      }
      std::vector<uint8_t> digest(digest_length);
      for (auto next = digest.begin(); next != digest.end(); ++next) {
        *next = static_cast<uint8_t>(this->get());
      }
      // Caller needs to process according to context
      Multiformats::Cid cid({version}, {codec},
                            {digest.cbegin(), digest.cend()});
      nlohmann::json result(
          {{"__readable_cid__",
            cid.to_string(Multiformats::Multibase::Protocol::Base32)}});
      return _callback(0, nlohmann::detail::parse_event_t::result, result);
    }

    bool parse_car_block(const bool get_char) {
      read_u64_leb128(get_char); // skip block length
      if (!parse_car_cid())
        return false;
      // decode DAG-CBOR block
      nlohmann::basic_json result;
      SAX this_pass_sax(result, _callback, _allow_exceptions);
      this->sax = &this_pass_sax;
      if (this->parse_cbor_internal(true, _tag_handler)) {
        // check callback for top-level object parse completion
        return _callback(0, nlohmann::detail::parse_event_t::result, result);
      }
      return false;
    }

    /*!
    @param[in] sax_    a SAX event processor
    @param[in] strict  whether to expect the input to be consumed completed
    @param[in] tag_handler  how to treat CBOR tags
    @return whether parsing was successful

    Logic per https://ipld.io/specs/transport/car/carv1/
    */
    bool sax_parse_car(SAX *sax_, const bool strict = true) {
      bool result(false);
      this->sax = sax_;
      result = parse_car_header();
      while (true) {
        // decode the rest of the blocks.  Only supporting DAG-CBOR data at this
        // time
        // check for empty stream, or end of well-formed object at EOF
        char_int_type next_char(this->get());
        if (next_char == nlohmann::detail::char_traits<
                             typename InputAdapterType::char_type>::eof())
          return true;
        result = parse_car_block(false);
      }

      // strict mode: next byte must be EOF
      if (result && strict) {
        this->get();

        if (this->current != nlohmann::detail::char_traits<
                                 typename InputAdapterType::char_type>::eof()) {
          return this->sax->parse_error(
              this->chars_read, this->get_token_string(),
              nlohmann::detail::parse_error::create(
                  110, this->chars_read,
                  this->exception_message(
                      this->input_format,
                      nlohmann::detail::concat(
                          "expected end of input; last byte: 0x",
                          this->get_token_string()),
                      "value"),
                  nullptr));
        }
      }

      return result;
    }

    nlohmann::detail::cbor_tag_handler_t _tag_handler;
    // callback function
    nlohmann::detail::parser_callback_t<BasicJsonType> _callback = nullptr;
    bool _allow_exceptions;
  };

  template <typename IteratorType>
  bool json_from_car(IteratorType first, IteratorType last) {
    bool parsed(false);
    try {
      std::function<bool(int /*depth*/, nlohmann::json::parse_event_t /*event*/,
                         nlohmann::json & /*parsed*/)>
          callback =
              std::bind(&parser::cbor_callback, this, std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3);
      auto ia =
          ::nlohmann::detail::input_adapter(std::move(first), std::move(last));
      return car_reader<typename ::nlohmann::json, decltype(ia),
                        ::nlohmann::detail::json_sax_dom_callback_parser<
                            typename ::nlohmann::json, decltype(ia)>>(
                 std::move(ia), nlohmann::detail::input_format_t::cbor)
          .parse_car(first, last, callback, true, true,
                     nlohmann::detail::cbor_tag_handler_t::ignore);
    } catch (std::exception const &exc) {
      REL_ERROR("CAR parse threw: {}", exc.what());
    }
    if (!parsed) {
      std::ostringstream oss;
      oss << std::hex;
      auto os_iter = std::ostream_iterator<int>(oss);
      std::ranges::copy(first, last, os_iter);

      REL_ERROR("CAR parse failed: {}", oss.str());
    } else {
      DBG_INFO("CAR parse success");
    }
    return parsed;
  }

  static void set_config(std::shared_ptr<config> &settings);

  // CAR file in "blocks" contains atproto content indexed by CIDs
  typedef std::vector<std::pair<std::string, nlohmann::json>> indexed_cbors;
  const indexed_cbors &other_cbors() const { return _other_cbors; }
  const indexed_cbors &content_cbors() const { return _content_cbors; }
  const indexed_cbors &matchable_cbors() const { return _matchable_cbors; }
  std::string dump_parse_content() const;
  std::string dump_parse_matched() const;
  std::string dump_parse_other() const;

  inline std::string block_cid() const { return _block_cid; }

private:
  bool cbor_callback(int depth, nlohmann::json::parse_event_t event,
                     nlohmann::json &parsed);

  // CAR file in "blocks" contains atproto content indexed by CIDs
  std::string _block_cid;
  std::unordered_set<std::string> _cids;
  indexed_cbors _other_cbors;
  indexed_cbors _content_cbors;
  indexed_cbors _matchable_cbors;
  static std::shared_ptr<config> _settings;
};
#endif