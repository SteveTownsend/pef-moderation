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

#include "metrics.hpp"
#include "firehost_client_config.hpp"

metrics::metrics() : _registry(new prometheus::Registry) {}

metrics &metrics::instance() {
  static metrics my_instance;
  return my_instance;
}

void metrics::set_config(std::shared_ptr<config> &settings) {
  _settings = settings;
  _port = _settings->get_config()[PROJECT_NAME]["metrics"]["port"]
              .as<std::string>();
  _exposer = std::make_unique<prometheus::Exposer>("0.0.0.0:" + _port);
  // ask the exposer to scrape the registry on incoming HTTP requests
  _exposer->RegisterCollectable(_registry);
}

prometheus::Family<prometheus::Counter> &
metrics::add_counter(std::string const &name, std::string const &help) {
  return prometheus::BuildCounter().Name(name).Help(help).Register(*_registry);
}
