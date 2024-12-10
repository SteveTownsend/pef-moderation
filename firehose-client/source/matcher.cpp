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

#include "matcher.hpp"
#include "helpers.hpp"
#include "log_wrapper.hpp"
#include <exception>
#include <fstream>
#include <ranges>
#include <string_view>

matcher::matcher() : _is_ready(false) { _whole_word_trie.only_whole_words(); }

matcher::matcher(std::string const &filename) { set_filter(filename); }

void matcher::set_filter(std::string const &filename) {
  std::ifstream file;
  file.open(filename);
  if (!file.is_open())
    throw std::invalid_argument("Cannot open " + filename);

  std::string str;
  size_t line(0);
  while (std::getline(file, str)) {
    // Skip header line
    ++line;
    if (str.length() < 2) {
      REL_WARNING("Malformed rule at line {}: '{}'", line, str);
      continue;
    }
    // comment
    if (str[0] == '#' && str[1] == '#') {
      REL_INFO("Comment skipped at line {}: '{}'", line, str);
      continue;
    }

    if (!add_rule(str)) {
      REL_WARNING("Duplicate rule at line {}: '{}'", line, str);
    } else {
      REL_INFO("Stored rule at line {}: '{}'", line, str);
    }
  }
  _is_ready = true;
}

bool matcher::add_rule(std::string const &match_rule) {
  rule new_rule(match_rule);
  std::wstring canonical_form(to_canonical(new_rule._target));
  // use ICU canonical form for multilanguage support
  if (new_rule._match_type == match_type::substring)
    _substring_trie.insert(canonical_form);
  else if (new_rule._match_type == match_type::whole_word)
    _whole_word_trie.insert(canonical_form);
  _is_ready = true;
  return _rule_lookup.insert({canonical_form, new_rule}).second;
}

bool matcher::matches_any(std::string const &candidate) const {
  auto candidates(parser().get_candidates_from_string(candidate));
  return check_candidates(candidates);
}

bool matcher::matches_any(beast::flat_buffer const &beast_data) const {
  auto candidates(parser().get_candidates_from_flat_buffer(beast_data));
  return check_candidates(candidates);
}

bool matcher::check_candidates(parser::candidate_list const &candidates) const {
  for (auto &next : candidates) {
    if (next.second.empty())
      continue;
    // use ICU canonical form for multilanguage support
    auto result = _substring_trie.parse_text(to_canonical(next.second));
    if (!result.empty())
      return true;
  }
  return false;
}

matcher::match_results
matcher::find_all_matches(beast::flat_buffer const &beast_data) const {
  auto candidates(parser().get_candidates_from_flat_buffer(beast_data));
  return all_matches_for_candidates(candidates);
}

matcher::match_results matcher::all_matches_for_candidates(
    parser::candidate_list const &candidates) const {
  match_results results;
  for (auto &next : candidates) {
    if (next.second.empty())
      continue;
    // use ICU canonical form for multilanguage support
    std::wstring canonical_form(to_canonical(next.second));
    aho_corasick::basic_trie<wchar_t>::emit_collection all_matches(
        _substring_trie.parse_text(canonical_form));
    aho_corasick::basic_trie<wchar_t>::emit_collection whole_words(
        _whole_word_trie.parse_text(canonical_form));
    if (!whole_words.empty())
      all_matches.insert(all_matches.end(), whole_words.cbegin(),
                         whole_words.cend());
    if (!all_matches.empty()) {
      results.emplace_back(next.second, all_matches);
    }
  }
  return results;
}

matcher::match_results
matcher::find_whole_word_matches(std::string const &string_data) const {
  // use ICU canonical form for multilanguage support
  auto matches(_whole_word_trie.parse_text(to_canonical(string_data)));
  if (!matches.empty()) {
    return match_results({{string_data, matches}});
  }
  return match_results{};
}

matcher::rule::rule(std::string const &rule_string) {
  size_t count = 0;
  if (rule_string.empty() || rule_string[0] == '|')
    throw std::invalid_argument("Malformed rule, missing filter string " +
                                rule_string);
  for (const auto token : std::views::split(rule_string, '|')) {
    // with string_view's C++23 range constructor:
    std::string_view field(token);
    switch (count) {
    case 0:
      if (field.empty())
        throw std::invalid_argument("Blank target in filter rule " +
                                    rule_string);
      _target = field;
      break;
    case 1:
      if (field.empty())
        throw std::invalid_argument("Blank labels in filter rule " +
                                    rule_string);
      _labels = field;
      break;
    case 2:
      _report = report_from_string(field);
      break;
    case 3:
      _match_type = match_type_from_string(field);
      if (_match_type == match_type::invalid)
        throw std::invalid_argument("Invalid matchtype in filter rule " +
                                    rule_string);
      break;
    case 4:
      if (field.empty())
        continue;
      _contingent = field;
      // make a trie that is used to confirm the rule match
      for (const auto subtoken : std::views::split(_contingent, ',')) {
        _substring_trie.insert(to_canonical(std::string_view(subtoken)));
      }
      break;
    default:
      throw std::invalid_argument("More than " + std::to_string(field_count) +
                                  " fields in filter rule " + rule_string);
      break;
    }
    ++count;
  }
  // final field, contingent strings to match, is currently optional
  if (count < field_count - 1)
    throw std::invalid_argument("Less than " + std::to_string(field_count) +
                                " fields in filter rule " + rule_string);
}

matcher::rule::rule(matcher::rule const &rhs)
    : _target(rhs._target), _labels(rhs._labels), _report(rhs._report),
      _match_type(rhs._match_type), _contingent(rhs._contingent) {
  // make a trie that is used to confirm the rule match
  for (const auto subtoken : std::views::split(_contingent, ',')) {
    _substring_trie.insert(to_canonical(std::string_view(subtoken)));
  }
}

bool matcher::rule::matches_any_contingent(std::string const &candidate) const {
  if (_contingent.empty())
    return true;
  // use ICU canonical form for multilanguage support
  auto result = _substring_trie.parse_text(to_canonical(candidate));
  return !result.empty();
}

matcher::rule matcher::find_rule(std::wstring const &key) const {
  auto result(_rule_lookup.find(key));
  if (result != _rule_lookup.cend())
    return result->second;

  std::ostringstream oss;
  oss << "Rule lookup failed for key " << wstring_to_utf8(key);
  throw std::runtime_error(oss.str());
}
