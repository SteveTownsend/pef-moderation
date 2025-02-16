/*************************************************************************
Public Education Forum Moderation DB Crawler
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

#include "common/bluesky/client.hpp"
#include "common/config.hpp"
#include "common/controller.hpp"
#include "common/log_wrapper.hpp"
#include "project_defs.hpp"
#include "restc-cpp/logging.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace bsky {
namespace moderation {

// PLC GET of labeler service record
struct verification_methods {
  std::string atproto;
  std::string atproto_label;
};
struct labeler_service {
  std::string type;
  std::string endpoint;
};
struct labeler_services {
  labeler_service atproto_pds;
  labeler_service atproto_labeler;
};
struct labeler_definition {
  std::string did;
  verification_methods verificationMethods;
  std::vector<std::string> rotationKeys;
  std::vector<std::string> alsoKnownAs;
  labeler_services services;
};

struct labeler_update {
  std::string token;
  labeler_services services;
};

struct labeler_update_operation {
  std::string prev;
  std::string type;
  labeler_services services;
  std::vector<std::string> rotationKeys;
  std::vector<std::string> alsoKnownAs;
  verification_methods verificationMethods;
  std::string sig;
};

struct labeler_update_signed {
  labeler_update_operation operation;
};

} // namespace moderation

template <>
inline std::string as_string<bsky::moderation::labeler_update_signed>(
    bsky::moderation::labeler_update_signed const &obj,
    restc_cpp::serialize_properties_t properties) {
  std::ostringstream oss;
  static std::set<std::string> omit = {"sig"};
  properties.excluded_names = &omit;
  restc_cpp::SerializeToJson(obj, oss, properties);
  return oss.str();
}
} // namespace bsky

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::labeler_service,
                          (std::string, type), (std::string, endpoint))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::labeler_services,
                          (bsky::moderation::labeler_service, atproto_pds),
                          (bsky::moderation::labeler_service, atproto_labeler))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::verification_methods,
                          (std::string, atproto), (std::string, atproto_label))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::labeler_definition,
                          (std::string, did),
                          (bsky::moderation::verification_methods,
                           verificationMethods),
                          (std::vector<std::string>, rotationKeys),
                          (std::vector<std::string>, alsoKnownAs),
                          (bsky::moderation::labeler_services, services))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::labeler_update,
                          (std::string, token),
                          (bsky::moderation::labeler_services, services))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::labeler_update_operation,
                          (std::string, prev), (std::string, type),
                          (bsky::moderation::labeler_services, services),
                          (std::vector<std::string>, rotationKeys),
                          (std::vector<std::string>, alsoKnownAs),
                          (bsky::moderation::verification_methods,
                           verificationMethods),
                          (std::string, sig))
BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::labeler_update_signed,
                          (bsky::moderation::labeler_update_operation,
                           operation))

int main(int argc, char **argv) {
  bool log_ready(false);
#if _DEBUG
  // std::this_thread::sleep_for(std::chrono::milliseconds(20000));
#endif
  try {
    // Check command line arguments.
    if (argc != 2) {
      std::cerr << "Usage: labeler_update <config-file-name>\n";
      return EXIT_FAILURE;
    }

    std::shared_ptr<config> settings(std::make_shared<config>(argv[1]));
    std::string const log_file(
        settings->get_config()[PROJECT_NAME]["logging"]["filename"]
            .as<std::string>());
    spdlog::level::level_enum log_level(spdlog::level::from_str(
        settings->get_config()[PROJECT_NAME]["logging"]["level"]
            .as<std::string>()));
    if (!init_logging(log_file, PROJECT_NAME, log_level)) {
      return EXIT_FAILURE;
    }
    log_ready = true;

    controller::instance().set_config(settings);
    controller::instance().start();

#if _DEBUG
    restc_cpp::Logger::Instance().SetLogLevel(restc_cpp::LogLevel::WARNING);
    restc_cpp::Logger::Instance().SetHandler(
        [](restc_cpp::LogLevel, const std::string &msg) {
          static const std::array<std::string, 6> levels = {
              "NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
          // REST log filtering is decoupled from app level setting
          logger->info("REST trace {}", msg);
        });
#else
    restc_cpp::Logger::Instance().SetLogLevel(restc_cpp::LogLevel::WARNING);
    restc_cpp::Logger::Instance().SetHandler(
        [](restc_cpp::LogLevel level, const std::string &msg) {
          static const std::array<std::string, 6> levels = {
              "NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
          // REST log filtering is decoupled from app level setting
          logger->info("REST trace {}", msg);
        });
#endif
    REL_INFO("labeler_update v{}.{}.{}", PROJECT_NAME_VERSION_MAJOR,
             PROJECT_NAME_VERSION_MINOR, PROJECT_NAME_VERSION_PATCH);

    // Two-stage process.
    // 1. Request token to sign the update.
    //    Leave token field as empty string for this.
    // 2. Use the token to perform field update(s)
    bsky::client pds_client;
    pds_client.set_config(settings->get_config()[PROJECT_NAME]["pds"]);
    if (!pds_client.is_ready()) {
      return EXIT_FAILURE;
    }

    std::string token(
        settings->get_config()[PROJECT_NAME]["token"].as<std::string>());
    if (token.empty()) {
      // Get a signature from PDS via email for the PLC op to update the fields
      std::string signature(pds_client.raw_post(
          "com.atproto.identity.requestPlcOperationSignature"));
    } else {
      if (!settings->get_config()[PROJECT_NAME]["services"]) {
        REL_ERROR("No update configured, requires '{}/services' YAML node",
                  PROJECT_NAME);
        return EXIT_FAILURE;
      }
      auto service_config(settings->get_config()[PROJECT_NAME]["services"]);

      // token in config - try the update
      bsky::client plc_client;
      plc_client.set_config(
          settings->get_config()[PROJECT_NAME]["plc_directory"]);
      bsky::moderation::labeler_definition labeler(
          plc_client.do_get<bsky::moderation::labeler_definition>(
              plc_client.service_did() + "/data"));

      bsky::moderation::labeler_update update;
      update.token = token;
      update.services = labeler.services;
      bool updated(false);
      if (service_config["atproto_pds"]) {
        update.services.atproto_labeler.endpoint =
            service_config["atproto_pds"]["endpoint"].as<std::string>();
        updated = true;
      }
      if (service_config["atproto_labeler"]) {
        update.services.atproto_labeler.endpoint =
            service_config["atproto_labeler"]["endpoint"].as<std::string>();
        updated = true;
      }
      if (!updated) {
        REL_ERROR(
            "No update configured, requires '{}/services/atproto_pds/endpoint' "
            "or '{}/services/atproto_labeler/endpoint' YAML node",
            PROJECT_NAME, PROJECT_NAME);
        return EXIT_FAILURE;
      }
      constexpr bool use_refresh_token(false);
      constexpr bool no_post_log(true);
      bsky::moderation::labeler_update_signed signed_update(
          pds_client.do_post<bsky::moderation::labeler_update,
                             bsky::moderation::labeler_update_signed>(
              "com.atproto.identity.signPlcOperation", update,
              use_refresh_token, no_post_log));
      bsky::empty ignored(
          pds_client
              .do_post<bsky::moderation::labeler_update_signed, bsky::empty>(
                  "com.atproto.identity.submitPlcOperation", signed_update,
                  use_refresh_token, no_post_log));
    }

    return EXIT_SUCCESS;
  } catch (std::exception const &exc) {
    if (log_ready) {
      REL_CRITICAL("Unhandled exception : {}", exc.what());
    } else {
      std::cerr << "Unhandled exception : " << exc.what() << '\n';
    }
    return EXIT_FAILURE;
  }
}
