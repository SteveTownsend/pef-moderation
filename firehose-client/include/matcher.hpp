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
#include "common/rest_utils.hpp"
#include "helpers.hpp"
#include <aho_corasick/aho_corasick.hpp>
#include <boost/beast/core.hpp>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <yaml-cpp/yaml.h>


namespace beast = boost::beast; // from <boost/beast.hpp>

// filter match candidate
struct candidate {
  std::string _type;
  std::string _field;
  std::string _value;
  bool operator==(candidate const &rhs) const;
};
// Path->candidate association
typedef std::vector<candidate> candidate_list;
typedef std::vector<std::pair<std::string, candidate_list>> path_candidate_list;

// Stores context that matched one or more filters, and the matches
struct match_result {
  candidate _candidate;
  aho_corasick::wtrie::emit_collection _matches;
};
typedef std::vector<match_result> match_results;
typedef std::vector<std::pair<std::string, match_results>> path_match_results;

struct account_filter_matches {
  std::string _did;
  path_match_results _matches;
};

inline bool candidate::operator==(candidate const &rhs) const {
  return _type == rhs._type && _field == rhs._field && _value == rhs._value;
}

class matcher {
public:
  inline static matcher &shared() {
    static matcher instance;
    return instance;
  }
  matcher();
  ~matcher() = default;

  inline bool is_ready() const { return _is_ready; }
  void set_config(const YAML::Node &filter_config);
  void load_filter_file(std::string const &filename);
  void refresh_rules(matcher &&replacement);

  bool matches_any(std::string const &candidate) const;
  bool matches_any(beast::flat_buffer const &beast_data) const;
  bool add_rule(std::string const &match_rule);
  bool add_rule(std::string const &filter, std::string const &labels,
                std::string const &actions, std::string const &contingent);
  bool check_candidates(candidate_list const &candidates) const;

  match_results find_all_matches(beast::flat_buffer const &beast_data) const;
  match_results
  all_matches_for_candidates(candidate_list const &candidates) const;
  path_match_results all_matches_for_path_candidates(
      path_candidate_list const &path_candidates) const;

  void report_if_needed(account_filter_matches &matches);
  inline bool use_db_for_rules() const { return _use_db_for_rules; }

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

    // for load from file
    rule(std::string const &rule_string);
    // for load from DB
    rule(std::string const &filter, std::string const &labels,
         std::string const &actions, std::string const &contingent);
    rule(rule const &);
    inline std::string to_string() const {
      std::ostringstream oss;
      oss << _target << '|' << format_vector(_labels) << '|' << _raw_actions
          << '|' << _contingent;
      return oss.str();
    }
    std::string _target;
    std::vector<std::string> _labels;
    std::string _raw_actions;
    bool _track = false;
    bool _report = false;
    bool _label = false;
    content_scope _content_scope = content_scope::any;
    std::string _block_list_name;
    match_type _match_type = match_type::substring;
    std::string _contingent;

    static constexpr size_t field_count = 4;
    bool matches_any_contingent(std::string const &candidate) const;

  private:
    void store_actions(std::string_view actions);
    mutable aho_corasick::wtrie _substring_trie;
  };

  rule find_rule(std::wstring const &key) const;

private:
  bool insert_rule(rule &&new_rule);
  rule find_rule_unchecked(std::wstring const &key) const;

  mutable std::mutex _lock;
  bool _is_ready = false;
  bool _use_db_for_rules = false;
  mutable aho_corasick::wtrie _substring_trie;
  mutable aho_corasick::wtrie _whole_word_trie;
  std::unordered_map<std::wstring, rule> _rule_lookup;
};
#endif