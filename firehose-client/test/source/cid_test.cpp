#include "multiformats/cid.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(CIDTest, RoundTrip) {
  const std::string mod_service_banner(
      "bafyreifd275x5ujvzarnxzwmztn32ncrtewmtp4ne4bb73opkflvdabere");
  Multiformats::Cid cid(mod_service_banner);
  // Extract as string, should be the same
  std::string readable(
      cid.to_string(Multiformats::Multibase::Protocol::Base32));
  EXPECT_EQ(readable, mod_service_banner);
  auto decoded(Multiformats::Multibase::decode(mod_service_banner));
  EXPECT_EQ(decoded.size(), 36);
}
