#include "common/bluesky/client.hpp"
#include "restc-cpp/restc-cpp.h"
#include <boost/fusion/adapted.hpp>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

BOOST_FUSION_ADAPT_STRUCT(bsky::moderation::report_subject,
                          (std::string, _type), (std::string, did),
                          (std::string, uri), (std::string, cid))

TEST(JsonTest, ReportSubject) {
  const std::string did("did:plc:gagfmlbeslz6gkbaawi4oz47");
  const std::string path("app.bsky.feed.post/3lk3q6l64pc2f");
  const std::string cid(
      "bafyreibj24izwxhykohnx4giqhja4q2fcgmhsxk7fg7eu5zx47xf6ydaxe");
  constexpr bool ignore_empty(false);
  {
    restc_cpp::serialize_properties_t properties(ignore_empty);
    bsky::moderation::report_subject did_subject(did);
    static const std::set<std::string> omit_fields = {"uri", "cid"};
    properties.excluded_names = &omit_fields;
    std::ostringstream oss;
    restc_cpp::SerializeToJson(did_subject, oss, properties);
    EXPECT_EQ(oss.str(),
              std::string("{\"_type\":\"com.atproto.admin.defs#repoRef\","
                          "\"did\":\"did:plc:gagfmlbeslz6gkbaawi4oz47\"}"));
  }
  {
    restc_cpp::serialize_properties_t properties(ignore_empty);
    bsky::moderation::report_subject post_subject(did, path, cid);
    static const std::set<std::string> omit_fields = {"did"};
    properties.excluded_names = &omit_fields;
    std::ostringstream oss;
    restc_cpp::SerializeToJson(post_subject, oss, properties);
    EXPECT_EQ(
        oss.str(),
        std::string("{\"_type\":\"com.atproto.repo.strongRef\",\"uri\":\"at://"
                    "did:plc:gagfmlbeslz6gkbaawi4oz47/app.bsky.feed.post/"
                    "3lk3q6l64pc2f\",\"cid\":"
                    "\"bafyreibj24izwxhykohnx4giqhja4q2fcgmhsxk7fg7eu5zx47xf6yd"
                    "axe\"}"));
  }
}
