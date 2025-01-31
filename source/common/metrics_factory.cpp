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

#include "common/metrics_factory.hpp"

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

prometheus::Family<prometheus::Counter> &
metrics_factory::add_counter(std::string const &name, std::string const &help) {
  return prometheus::BuildCounter().Name(name).Help(help).Register(*_registry);
}

prometheus::Family<prometheus::Gauge> &
metrics_factory::add_gauge(std::string const &name, std::string const &help) {
  return prometheus::BuildGauge().Name(name).Help(help).Register(*_registry);
}

prometheus::Family<prometheus::Histogram> &
metrics_factory::add_histogram(std::string const &name,
                               std::string const &help) {
  return prometheus::BuildHistogram().Name(name).Help(help).Register(
      *_registry);
}
