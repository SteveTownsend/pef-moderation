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

#include "common/metrics_factory.hpp"
#include "common/log_wrapper.hpp"

metrics_factory::metrics_factory() : _registry(new prometheus::Registry) {}

metrics_factory &metrics_factory::instance() {
  static metrics_factory my_instance;
  return my_instance;
}

void metrics_factory::set_config(std::shared_ptr<config> &settings,
                                 std::string const &project_name) {
  _settings = settings;
  _port = _settings->get_config()[project_name]["metrics"]["port"]
              .as<std::string>();
  _exposer = std::make_unique<prometheus::Exposer>("0.0.0.0:" + _port);
  // ask the exposer to scrape the registry on incoming HTTP requests
  _exposer->RegisterCollectable(_registry);
}

void metrics_factory::add_counter(std::string const &name,
                                  std::string const &help) {
  auto &counter(
      prometheus::BuildCounter().Name(name).Help(help).Register(*_registry));
  if (!_counters.insert({name, {help, counter}}).second) {
    std::string error("cannot create duplicate metric(counter) " + name);
    REL_ERROR(error);
    throw std::invalid_argument(error.c_str());
  }
}

void metrics_factory::add_gauge(std::string const &name,
                                std::string const &help) {
  auto &gauge(
      prometheus::BuildGauge().Name(name).Help(help).Register(*_registry));
  if (!_gauges.insert({name, {help, gauge}}).second) {
    std::string error("cannot create duplicate metric(gauge) " + name);
    REL_ERROR(error);
    throw std::invalid_argument(error.c_str());
  }
}

void metrics_factory::add_histogram(std::string const &name,
                                    std::string const &help) {
  auto &histogram(
      prometheus::BuildHistogram().Name(name).Help(help).Register(*_registry));
  if (!_histograms.insert({name, {help, histogram}}).second) {
    std::string error("cannot create duplicate metric(histogram) " + name);
    REL_ERROR(error);
    throw std::invalid_argument(error.c_str());
  }
}

prometheus::Family<prometheus::Counter> &
metrics_factory::get_counter(std::string const &name) const {
  auto counter(_counters.find(name));
  if (counter == _counters.cend()) {
    std::string error("cannot find metric(counter) " + name);
    REL_ERROR(error);
    throw std::invalid_argument(error.c_str());
  }
  return counter->second.second;
}

prometheus::Family<prometheus::Gauge> &
metrics_factory::get_gauge(std::string const &name) const {
  auto gauge(_gauges.find(name));
  if (gauge == _gauges.cend()) {
    std::string error("cannot find metric(gauge) " + name);
    REL_ERROR(error);
    throw std::invalid_argument(error.c_str());
  }
  return gauge->second.second;
}

prometheus::Family<prometheus::Histogram> &
metrics_factory::get_histogram(std::string const &name) const {
  auto histogram(_histograms.find(name));
  if (histogram == _histograms.cend()) {
    std::string error("cannot find metric(histogram) " + name);
    REL_ERROR(error);
    throw std::invalid_argument(error.c_str());
  }
  return histogram->second.second;
}
