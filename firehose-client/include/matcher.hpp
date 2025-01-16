#ifndef __matcher_hpp__
#define __matcher_hpp__
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
#include "parser.hpp"
#include <aho_corasick/aho_corasick.hpp>
#include <boost/beast/core.hpp>
#include <string>
#include <tuple>
#include <unordered_map>

namespace beast = boost::beast; // from <boost/beast.hpp>

class matcher {
public:
  matcher();
  ~matcher() = default;
  matcher(std::string const &filename);

  void set_filter(std::string const &filename);
  bool matches_any(std::string const &candidate) const;
  bool matches_any(beast::flat_buffer const &beast_data) const;
  bool add_rule(std::string const &match_rule);
  bool check_candidates(candidate_list const &candidates) const;

  match_results find_all_matches(beast::flat_buffer const &beast_data) const;
  match_results
  all_matches_for_candidates(candidate_list const &candidates) const;
  path_match_results all_matches_for_path_candidates(
      path_candidate_list const &path_candidates) const;

  class rule {
  public:
    enum class match_type { substring, whole_word };
    enum class content_scope { profile, any };

    inline content_scope content_scope_from_string(std::string_view str) {
      if (str == "profile")
        return content_scope::profile;
      if (str == "any")
        return content_scope::any;
      std::ostringstream err;
      err << "Bad content scope " << str;
      throw std::invalid_argument(err.str());
    }

    inline match_type match_type_from_string(std::string_view str) {
      if (str == "substring")
        return match_type::substring;
      if (str == "word")
        return match_type::whole_word;
      std::ostringstream err;
      err << "Bad match type " << str;
      throw std::invalid_argument(err.str());
    }

    inline std::string match_type_to_string(match_type my_match_type) {
      if (my_match_type == match_type::substring)
        return "substring";
      if (my_match_type == match_type::whole_word)
        return "word";
      return std::string{};
    }

    rule(std::string const &rule_string);
    rule(rule const &);
    std::string _target;
    std::string _labels;
    bool _track;
    bool _report;
    content_scope _content_scope;
    std::string _block_list_name;
    match_type _match_type;
    std::string _contingent;

    static constexpr size_t field_count = 4;
    bool matches_any_contingent(std::string const &candidate) const;

  private:
    void store_actions(std::string_view actions);
    mutable aho_corasick::wtrie _substring_trie;
  };

  rule find_rule(std::wstring const &key) const;

private:
  bool _is_ready;
  mutable aho_corasick::wtrie _substring_trie;
  mutable aho_corasick::wtrie _whole_word_trie;
  std::unordered_map<std::wstring, rule> _rule_lookup;
};
#endif