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

#include "matcher.hpp"
#include "common/helpers.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "common/moderation/report_agent.hpp"
#include "moderation/list_manager.hpp"
#include "parser.hpp"
#include <exception>
#include <fstream>
#include <ranges>
#include <string_view>

matcher::matcher() { _whole_word_trie.only_whole_words(); }

// load from file, or wait for DB to load
void matcher::set_config(const YAML::Node &filter_config) {
  _use_db_for_rules = filter_config["use_db"].as<bool>();
  if (!_use_db_for_rules) {
    load_filter_file(filter_config["filename"].as<std::string>());
  }
}

void matcher::load_filter_file(std::string const &filename) {
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
      REL_WARNING("Skipped rule at line {}: '{}'", line, str);
    } else {
      REL_INFO("Stored rule at line {}: '{}'", line, str);
    }
  }
}

void matcher::refresh_rules(matcher &&replacement) {
  std::lock_guard log(_lock);
  _rule_lookup.swap(replacement._rule_lookup);
  _substring_trie = std::move(replacement._substring_trie);
  _whole_word_trie = std::move(replacement._whole_word_trie);
  _is_ready = true;
}

bool matcher::add_rule(std::string const &match_rule) {
  return insert_rule(rule(match_rule));
}

bool matcher::add_rule(std::string const &filter, std::string const &labels,
                       std::string const &actions,
                       std::string const &contingent) {
  return insert_rule(rule(filter, labels, actions, contingent));
}

// thread-safe by design
bool matcher::insert_rule(rule &&new_rule) {
  // Check for intentionally-skipped rule
  if (!new_rule._track) {
    REL_WARNING("Skipped rule '{}'", new_rule.to_string());
    return false;
  }
  // TODO handle this for refresh case
  if (!new_rule._block_list_name.empty()) {
    list_manager::instance().register_block_reason(new_rule._block_list_name,
                                                   new_rule._target);
  }
  std::wstring canonical_form(to_canonical(new_rule._target));
  // use ICU canonical form for multilanguage support
  if (new_rule._match_type == rule::match_type::substring)
    _substring_trie.insert(canonical_form);
  else if (new_rule._match_type == rule::match_type::whole_word)
    _whole_word_trie.insert(canonical_form);
  if (_rule_lookup.insert({canonical_form, new_rule}).second) {
    REL_INFO("Stored rule '{}'", new_rule.to_string());
  } else {
    REL_WARNING("Duplicate rule '{}'", new_rule.to_string());
  }
  return true;
}

bool matcher::matches_any(std::string const &candidate) const {
  auto candidates(parser().get_candidates_from_string(candidate));
  return check_candidates(candidates);
}

bool matcher::matches_any(beast::flat_buffer const &beast_data) const {
  auto candidates(parser().get_candidates_from_flat_buffer(beast_data));
  return check_candidates(candidates);
}

bool matcher::check_candidates(candidate_list const &candidates) const {
  std::lock_guard lock(_lock);
  for (auto &next : candidates) {
    if (next._value.empty())
      continue;
    // use ICU canonical form for multilanguage support
    auto result = _substring_trie.parse_text(to_canonical(next._value));
    if (!result.empty())
      return true;
  }
  return false;
}

match_results
matcher::find_all_matches(beast::flat_buffer const &beast_data) const {
  auto candidates(parser().get_candidates_from_flat_buffer(beast_data));
  return all_matches_for_candidates(candidates);
}

match_results
matcher::all_matches_for_candidates(candidate_list const &candidates) const {
  std::lock_guard lock(_lock);
  match_results results;
  for (auto &next : candidates) {
    if (next._value.empty())
      continue;
    // use ICU canonical form for multilanguage support
    std::wstring canonical_form(to_canonical(next._value));
    aho_corasick::basic_trie<wchar_t>::emit_collection all_matches(
        _substring_trie.parse_text(canonical_form));
    aho_corasick::basic_trie<wchar_t>::emit_collection whole_words(
        _whole_word_trie.parse_text(canonical_form));
    if (!whole_words.empty())
      all_matches.insert(all_matches.end(), whole_words.cbegin(),
                         whole_words.cend());
    if (!all_matches.empty()) {
      results.emplace_back(next, all_matches);
    }
  }

  // strip out matches which do not pass contingent string matching in rule
  for (auto next_match = results.begin(); next_match != results.end();) {
    for (auto rule_key = next_match->_matches.begin();
         rule_key != next_match->_matches.end();) {
      matcher::rule this_rule = find_rule_unchecked(rule_key->get_keyword());
      if (!this_rule.passes_contingent_checks(next_match->_candidate._value)) {
        rule_key = next_match->_matches.erase(rule_key);
      } else {
        ++rule_key;
      }
    }
    if (next_match->_matches.empty()) {
      next_match = results.erase(next_match);
    } else {
      ++next_match;
    }
  }
  return results;
}

path_match_results matcher::all_matches_for_path_candidates(
    path_candidate_list const &path_candidates) const {
  path_match_results results;
  for (auto &next : path_candidates) {
    match_results this_result_set(all_matches_for_candidates(next.second));
    if (!this_result_set.empty()) {
      results.emplace_back(next.first, this_result_set);
    }
  }
  return results;
}

void matcher::report_if_needed(account_filter_matches &matches) {

  // iterate the match results for any rules that are marked
  // auto-reportable
  std::vector<std::string> paths;
  std::vector<std::string> all_filters;
  std::vector<std::string> labels;
  for (auto const &result : matches._matches) {
    // this is the substring of the full JSON that matched one or more
    // desired strings
    std::string path(result.first);
    std::vector<std::string> filters;
    for (auto const &next_match : result.second) {
      for (auto const &match : next_match._matches) {
        matcher::rule matched_rule(find_rule(match.get_keyword()));
        if (!matched_rule._report && !matched_rule._label) {
          // auto-moderation not requested for this rule
          continue;
        }
        if (matched_rule._label) {
          if (!matched_rule._labels.empty()) {
            labels.insert(labels.end(), matched_rule._labels.cbegin(),
                          matched_rule._labels.cend());
          }
        }
        if (!matched_rule._block_list_name.empty()) {
          list_manager::instance().wait_enqueue(
              {matches._did, matched_rule._block_list_name});
        }
        if (matched_rule._content_scope == matcher::rule::content_scope::any) {
          filters.push_back(matched_rule._target);
        } else if (matched_rule._content_scope ==
                   matcher::rule::content_scope::profile) {
          // report only if seen in profile
          if (next_match._candidate._type == bsky::AppBskyActorProfile) {
            filters.push_back(matched_rule._target);
          }
        }
      }
      if (!filters.empty()) {
        paths.push_back(path);
        all_filters.insert(all_filters.cend(), filters.cbegin(),
                           filters.cend());
      }
    }
  }

  // record the account as a delta to cache for dup detection
  if (!all_filters.empty()) {
    if (!labels.empty()) {
      std::sort(labels.begin(), labels.end());
      labels.erase(std::unique(labels.begin(), labels.end()), labels.end());
    }
    bsky::moderation::report_agent::instance().wait_enqueue(
        bsky::moderation::account_report(
            matches._did,
            bsky::moderation::filter_matches(all_filters, paths, labels)));
  }
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
      for (const auto subtoken : std::views::split(std::string(field), ',')) {
        _labels.push_back(std::string(subtoken.cbegin(), subtoken.cend()));
      }
      break;
    case 2:
      store_actions(field);
      break;
    case 3:
      if (field.empty())
        continue;
      _contingent = field;
      // make a trie of 'contingent strings' to confirm rule_string context
      for (const auto subtoken : std::views::split(_contingent, ',')) {
        std::string next(subtoken.cbegin(), subtoken.cend());
        if (starts_with(next, "!")) {
          next = next.substr(1);
          _absent_substring_trie.insert(
              to_canonical(std::string_view(subtoken)));
        } else {
          _substring_trie.insert(to_canonical(std::string_view(subtoken)));
        }
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

matcher::rule::rule(std::string const &filter, std::string const &labels,
                    std::string const &actions, std::string const &contingent) {
  if (filter.empty())
    throw std::invalid_argument("Blank filter");
  _target = filter;
  if (labels.empty())
    throw std::invalid_argument("Blank labels");
  for (const auto subtoken : std::views::split(std::string(labels), ',')) {
    _labels.push_back(std::string(subtoken.cbegin(), subtoken.cend()));
  }
  store_actions(actions);
  if (contingent.empty())
    return;
  _contingent = contingent;
  // make a trie of 'contingent strings' to confirm rule_string context
  for (const auto subtoken : std::views::split(_contingent, ',')) {
    std::string next(subtoken.cbegin(), subtoken.cend());
    if (starts_with(next, "!")) {
      next = next.substr(1);
      _absent_substring_trie.insert(to_canonical(std::string_view(subtoken)));
    } else {
      _substring_trie.insert(to_canonical(std::string_view(subtoken)));
    }
  }
}

void matcher::rule::store_actions(std::string_view actions) {
  _raw_actions = actions;
  // with string_view's C++23 range constructor:
  for (const auto token : std::views::split(actions, ',')) {
    std::string field(token.cbegin(), token.cend());
    size_t offset(field.find('='));
    if (offset == std::string::npos || offset == 0) {
      throw std::invalid_argument("Invalid rule action " + field +
                                  ", malformed key-value pair");
    }
    std::string value(field, offset + 1);
    if (offset == std::string::npos) {
      throw std::invalid_argument("Invalid rule action " + field +
                                  ", blank value");
    }
    if (starts_with(field, "track=")) {
      _track = bool_from_string(value);
      continue;
    }
    if (starts_with(field, "report=")) {
      _report = bool_from_string(value);
      continue;
    }
    if (starts_with(field, "label=")) {
      _label = bool_from_string(value);
      continue;
    }
    if (starts_with(field, "scope=")) {
      _content_scope = content_scope_from_string(value);
      continue;
    }
    if (starts_with(field, "match=")) {
      _match_type = match_type_from_string(value);
      continue;
    }
    if (starts_with(field, "block=")) {
      if (!list_manager::is_active_list_for_group(value)) {
        throw std::invalid_argument(
            "Invalid rule action " + field +
            ", hyphen not permitted in list-troup name");
      }
      _block_list_name = value;
      continue;
    }
    throw std::invalid_argument("Invalid rule action " + field +
                                ", invalid key");
  }
}

matcher::rule::rule(matcher::rule const &rhs)
    : _target(rhs._target), _labels(rhs._labels), _track(rhs._track),
      _report(rhs._report), _label(rhs._label),
      _content_scope(rhs._content_scope),
      _block_list_name(rhs._block_list_name), _match_type(rhs._match_type),
      _contingent(rhs._contingent) {
  // make a trie that is used to confirm the rule match
  for (const auto subtoken : std::views::split(_contingent, ',')) {
    std::string next(subtoken.cbegin(), subtoken.cend());
    if (starts_with(next, "!")) {
      next = next.substr(1);
      _absent_substring_trie.insert(to_canonical(std::string_view(subtoken)));
    } else {
      _substring_trie.insert(to_canonical(std::string_view(subtoken)));
    }
  }
}

bool matcher::rule::passes_contingent_checks(
    std::string const &candidate) const {
  if (_contingent.empty())
    return true;
  // use ICU canonical form for multilanguage support
  auto normalized(to_canonical(candidate));
  auto required = _substring_trie.parse_text(normalized); // at least one match
  auto disallowed =
      _absent_substring_trie.parse_text(normalized); // zero matches

  return !required.empty() && disallowed.empty();
}

matcher::rule matcher::find_rule(std::wstring const &key) const {
  std::lock_guard lock(_lock);
  return find_rule_unchecked(key);
}

matcher::rule matcher::find_rule_unchecked(std::wstring const &key) const {
  auto result(_rule_lookup.find(key));
  if (result != _rule_lookup.cend())
    return result->second;

  std::ostringstream oss;
  oss << "Rule lookup failed for key " << wstring_to_utf8(key);
  throw std::runtime_error(oss.str());
}
