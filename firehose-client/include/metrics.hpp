#ifndef __metrics_hpp__
#define __metrics_hpp__
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

#include "common/config.hpp"
#include <prometheus/counter.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>


class metrics {
public:
  static metrics &instance();

  inline prometheus::Family<prometheus::Counter> &matched_elements() {
    return _matched_elements;
  }
  inline prometheus::Family<prometheus::Counter> &firehose_stats() {
    return _firehose_stats;
  }
  inline prometheus::Family<prometheus::Histogram> &firehose_facets() {
    return _firehose_facets;
  }
  inline prometheus::Family<prometheus::Gauge> &operational_stats() {
    return _operational_stats;
  }
  inline prometheus::Family<prometheus::Counter> &realtime_alerts() {
    return _realtime_alerts;
  }
  inline prometheus::Family<prometheus::Counter> &embed_stats() {
    return _embed_stats;
  }
  inline prometheus::Family<prometheus::Histogram> &link_stats() {
    return _link_stats;
  }
  inline prometheus::Family<prometheus::Counter> &automation_stats() {
    return _automation_stats;
  }

private:
  metrics();
  ~metrics() = default;

  // Cardinality =
  //   (number of rules) times (number of elements - profile/post -
  //                            times number of fields per element)
  prometheus::Family<prometheus::Counter> &_matched_elements;
  prometheus::Family<prometheus::Counter> &_firehose_stats;
  prometheus::Family<prometheus::Gauge> &_operational_stats;
  prometheus::Family<prometheus::Histogram> &_firehose_facets;
  prometheus::Family<prometheus::Counter> &_realtime_alerts;
  prometheus::Family<prometheus::Counter> &_embed_stats;
  prometheus::Family<prometheus::Histogram> &_link_stats;
  prometheus::Family<prometheus::Counter> &_automation_stats;
};
#endif