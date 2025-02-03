#ifndef __metrics_factory_hpp__
#define __metrics_factory_hpp__
/*************************************************************************
Public Education Forum Moderation Firehose Client
Copyright (c) Steve Townsend 2025

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
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/info.h>
#include <prometheus/registry.h>
#include <prometheus/summary.h>
#include <unordered_map>

class metrics_factory {
public:
  static metrics_factory &instance();
  void set_config(std::shared_ptr<config> &settings,
                  std::string const &project_name);

  void add_counter(std::string const &name, std::string const &help);
  void add_gauge(std::string const &name, std::string const &help);
  void add_histogram(std::string const &name, std::string const &help);

  prometheus::Family<prometheus::Counter> &
  get_counter(std::string const &name) const;
  prometheus::Family<prometheus::Gauge> &
  get_gauge(std::string const &name) const;
  prometheus::Family<prometheus::Histogram> &
  get_histogram(std::string const &name) const;

private:
  metrics_factory();
  ~metrics_factory() = default;

  std::string _port;
  std::shared_ptr<config> _settings;
  std::unique_ptr<prometheus::Exposer> _exposer;
  std::shared_ptr<prometheus::Registry> _registry;

  std::unordered_map<
      std::string,
      std::pair<std::string, prometheus::Family<prometheus::Counter> &>>
      _counters;
  std::unordered_map<
      std::string,
      std::pair<std::string, prometheus::Family<prometheus::Gauge> &>>
      _gauges;
  std::unordered_map<
      std::string,
      std::pair<std::string, prometheus::Family<prometheus::Histogram> &>>
      _histograms;
};
#endif