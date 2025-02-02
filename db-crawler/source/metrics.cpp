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

metrics::metrics()
    : _tagged_records(metrics_factory::instance().add_counter(
          "tagged_records", "Number of records flagged with a given tag")),
      _operational_stats(metrics_factory::instance().add_gauge(
          "operational_stats", "Statistics about client internals")),
      _realtime_alerts(metrics_factory::instance().add_counter(
          "realtime_alerts", "Alerts generated for possibly suspect activity")),
      _automation_stats(metrics_factory::instance().add_counter(
          "automation_stats",
          "Automated moderation activity - block-list, report")) {}

metrics &metrics::instance() {
  static metrics my_instance;
  return my_instance;
}
