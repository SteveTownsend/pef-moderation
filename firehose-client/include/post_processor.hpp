#ifndef __post_processor_hpp__
#define __post_processor_hpp__
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

#include "activity/event_recorder.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "common/metrics_factory.hpp"
#include "helpers.hpp"
#include "matcher.hpp"
#include "moderation/embed_checker.hpp"
#include "parser.hpp"
#include "readerwriterqueue.h"
#include <nlohmann/detail/exceptions.hpp>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <thread>
#include <unordered_map>

namespace firehose {

enum class op { error = -1, message = 1 };

enum class op_type {
  account = 1,
  commit,
  handle,
  identity,
  info,
  migrate,
  tombstone,
  invalid = -1
};

constexpr std::string_view OpTypeAccount = "#account";
constexpr std::string_view OpTypeCommit = "#commit";
constexpr std::string_view OpTypeHandle = "#handle";
constexpr std::string_view OpTypeIdentity = "#identity";
constexpr std::string_view OpTypeInfo = "#info";
constexpr std::string_view OpTypeMigrate = "#migrate";
constexpr std::string_view OpTypeTombstone = "#tombstone";

inline op_type op_type_from_string(std::string const &op_type_str) {
  if (op_type_str == OpTypeAccount) {
    return op_type::account;
  } else if (op_type_str == OpTypeCommit) {
    return op_type::commit;
  } else if (op_type_str == OpTypeHandle) {
    return op_type::handle;
  } else if (op_type_str == OpTypeIdentity) {
    return op_type::identity;
  } else if (op_type_str == OpTypeInfo) {
    return op_type::info;
  } else if (op_type_str == OpTypeMigrate) {
    return op_type::migrate;
  } else if (op_type_str == OpTypeTombstone) {
    return op_type::tombstone;
  }
  return op_type::invalid;
}

enum class op_kind { create = 1, delete_, update, invalid = -1 };

constexpr std::string_view OpKindCreate = "create";
constexpr std::string_view OpKindDelete = "delete";
constexpr std::string_view OpKindUpdate = "update";

inline op_kind op_kind_from_string(std::string const &op_kind_str) {
  if (op_kind_str == OpKindCreate) {
    return op_kind::create;
  } else if (op_kind_str == OpKindDelete) {
    return op_kind::delete_;
  } else if (op_kind_str == OpKindUpdate) {
    return op_kind::update;
  }
  return op_kind::invalid;
}

} // namespace firehose

template <typename T> class post_processor {
public:
  static constexpr size_t QueueLimit = 10000;

  post_processor() : _queue(QueueLimit) {
    _thread = std::thread([&, this] {
      try {
        while ((controller::instance().is_active())) {
          T my_payload;
          try {
            _queue.wait_dequeue(my_payload);
            metrics_factory::instance()
                .get_gauge("process_operation")
                .Get({{"message", "backlog"}})
                .Decrement();

            my_payload.handle(*this);
          } catch (nlohmann::detail::exception const &exc) {
            REL_ERROR("post_processor JSON error {} on payload {}", exc.what(),
                      my_payload.to_string());
          }
        }
      } catch (std::exception const &exc) {
        REL_ERROR("post_processor exception {}", exc.what());
        controller::instance().force_stop();
      }
      REL_INFO("post_processor stopping");
    });
  }
  ~post_processor() = default;
  void wait_enqueue(T &&value) {
    _queue.enqueue(value);
    metrics_factory::instance()
        .get_gauge("process_operation")
        .Get({{"message", "backlog"}})
        .Increment();
  }
  inline void request_recording(activity::timed_event &&event) {
    _recorder.wait_enqueue(std::move(event));
  }

private:
  // Declare queue between websocket and match post-processing
  moodycamel::BlockingReaderWriterQueue<T> _queue;
  std::thread _thread;
  activity::event_recorder _recorder;
};

#endif