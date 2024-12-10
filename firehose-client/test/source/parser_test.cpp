#include "parser.hpp"
#include "testdefs.hpp"
#include <aho_corasick/aho_corasick.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <ios>

// Detection logic for switching
TEST(ParseTest, DetectPostInJSON) {
  auto post = load_json_from_file("post.json");
  parser::candidate_list expected = {
      {"text", "You have been immensely kind to me as a smaller creator and "
               "have shown me empathy and decency when I've discussed "
               "personal "
               "feelings with you. You're an incredibly skilled creative on "
               "top of it, too. You deserve to be happy and to be living your "
               "absolute best life!"}};
  EXPECT_THAT(parser().get_candidates_from_json(post),
              ::testing::ContainerEq(expected));
}

TEST(ParseTest, DetectProfileInJSON) {
  auto profile = load_json_from_file("profile.json");
  parser::candidate_list expected = {
      {"description", "News and information for the Epping Forest district in "
                      "Essex on social media and by email. Featured on Sky "
                      "News, BBC Radio 4's 'You and Yours' and BBC Essex. "
                      "\n\nContact email: everythingeppingforest@gmail.com"},
      {"displayName", "Everything Epping Forest"}};
  EXPECT_THAT(parser().get_candidates_from_json(profile),
              ::testing::ContainerEq(expected));
}

TEST(ParseTest, DetectPostInString) {
  const auto post = load_from_file("post.json");
  parser::candidate_list expected = {
      {"text", "You have been immensely kind to me as a smaller creator and "
               "have shown me empathy and decency when I've discussed "
               "personal "
               "feelings with you. You're an incredibly skilled creative on "
               "top of it, too. You deserve to be happy and to be living your "
               "absolute best life!"}};
  EXPECT_THAT(parser().get_candidates_from_string(post),
              ::testing::ContainerEq(expected));
}

TEST(ParseTest, DetectProfileInString) {
  const auto profile = load_from_file("profile.json");
  parser::candidate_list expected = {
      {"description", "News and information for the Epping Forest district in "
                      "Essex on social media and by email. Featured on Sky "
                      "News, BBC Radio 4's 'You and Yours' and BBC Essex. "
                      "\n\nContact email: everythingeppingforest@gmail.com"},
      {"displayName", "Everything Epping Forest"}};
  EXPECT_THAT(parser().get_candidates_from_string(profile),
              ::testing::ContainerEq(expected));
}

TEST(ParseTest, DetectUkrainian) {
  const auto profile = load_from_file("abusive_profile.json");
  {
    parser::candidate_list expected = {
        {"description",
         "russians use \xD1\x85\xD0\xBE\xD1\x85\xD0\xBE\xD0\xBB as a slur"},
        {"displayName",
         "russian slur \xD1\x85\xD0\xBE\xD1\x85\xD0\xBE\xD0\xBB"}};
    EXPECT_THAT(parser().get_candidates_from_string(profile),
                ::testing::ContainerEq(expected));
  }
  {
    parser::candidate_list expected = {
        {"description",
         "russians use \xD1\x85\xD0\xBE\xD1\x85\xD0\xBE\xD0\xBB as a slur"},
        {"displayName",
         "russian slur \xD1\x85\xD0\xBE\xD1\x85\xD0\xBE\xD0\xBB"}};
    EXPECT_THAT(parser().get_candidates_from_string(profile),
                ::testing::ContainerEq(expected));
  }
}
