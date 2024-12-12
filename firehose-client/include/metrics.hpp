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

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/info.h>
#include <prometheus/registry.h>
#include <prometheus/summary.h>

class metrics {
public:
  static metrics &instance();

  prometheus::Family<prometheus::Counter> &add_counter(std::string const &name,
                                                       std::string const &help);

private:
  metrics();
  ~metrics() = default;

  prometheus::Exposer _exposer;
  std::shared_ptr<prometheus::Registry> _registry;
};
#endif