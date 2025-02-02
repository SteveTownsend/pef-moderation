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
#include "metrics.hpp"
#include "common/metrics_factory.hpp"
#include "firehost_client_config.hpp"
#include "helpers.hpp"

metrics::metrics()
    : _matched_elements(metrics_factory::instance().add_counter(
          "message_field_matches",
          "Number of matches within each field of message")),
      _firehose_stats(metrics_factory::instance().add_counter(
          "firehose", "Statistics about received firehose data")),
      _firehose_facets(metrics_factory::instance().add_histogram(
          "firehose_facets", "Statistics about received firehose facets")),
      _operational_stats(metrics_factory::instance().add_gauge(
          "operational_stats", "Statistics about client internals")),
      _realtime_alerts(metrics_factory::instance().add_counter(
          "realtime_alerts", "Alerts generated for possibly suspect activity")),
      _embed_stats(metrics_factory::instance().add_counter(
          "embed_stats",
          "Checks performed on 'embeds': post, video, image, link")),
      _link_stats(metrics_factory::instance().add_histogram(
          "link_stats", "Statistics from link analysis")),
      _automation_stats(metrics_factory::instance().add_counter(
          "automation_stats",
          "Automated moderation activity - block-list, report")) {
  // Histogram metrics have to be added by hand, on-deman instantiation is not
  // possible
  prometheus::Histogram::BucketBoundaries boundaries = {
      0.0,  1.0,  2.0,  3.0,  4.0,  5.0,  6.0,  7.0,  8.0,  9.0,  10.0, 11.0,
      12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0,
      24.0, 25.0, 26.0, 27.0, 28.0, 29.0, 30.0, 31.0, 32.0, 33.0, 34.0, 35.0};
  _firehose_facets.Add({{"facet", std::string(bsky::AppBskyRichtextFacetLink)}},
                       boundaries);
  _firehose_facets.Add(
      {{"facet", std::string(bsky::AppBskyRichtextFacetMention)}}, boundaries);
  _firehose_facets.Add({{"facet", std::string(bsky::AppBskyRichtextFacetTag)}},
                       boundaries);
  _firehose_facets.Add({{"facet", "total"}}, boundaries);

  prometheus::Histogram::BucketBoundaries hop_count = {
      0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
  _link_stats.Add({{"redirection", "hops"}}, hop_count);
}

metrics &metrics::instance() {
  static metrics my_instance;
  return my_instance;
}
