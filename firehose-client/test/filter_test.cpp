#include <gtest/gtest.h>
#include <aho_corasick/aho_corasick.hpp>
#include <ios>

// Demonstrate some basic assertions.
TEST(FilterTest, BasicAssertions)
{
  // Expect two strings not to be equal.
  EXPECT_STRNE("hello", "world");
  // Expect equality.
  EXPECT_EQ(7 * 6, 42);
}

TEST(FilterTest, Parse)
{
  {
    aho_corasick::wtrie trie;
    trie.insert(L"hers");
    trie.insert(L"his");
    trie.insert(L"she");
    trie.insert(L"he");
    auto result = trie.parse_text(L"ushers");
  }

  {
    aho_corasick::wtrie trie;
    trie.remove_overlaps();
    trie.insert(L"hot");
    trie.insert(L"hot chocolate");
    auto result = trie.parse_text(L"hot chocolate");
  }

  {
    aho_corasick::wtrie trie;
    trie.case_insensitive();
    trie.insert(L"casing");
    auto result = trie.parse_text(L"CaSiNg");
  }

  {
    aho_corasick::wtrie trie;
    trie.remove_overlaps()
        .only_whole_words()
        .case_insensitive();
    trie.insert(L"great question");
    trie.insert(L"forty-two");
    trie.insert(L"deep thought");
    auto tokens = trie.tokenise(L"The Answer to the Great Question... Of Life, the Universe and Everything... Is... Forty-two, said Deep Thought, with infinite majesty and calm.");
    std::wstringstream html;
    html << L"<html><body><p>";
    for (const auto &token : tokens)
    {
      if (token.is_match())
        html << L"<i>";
      html << token.get_fragment();
      if (token.is_match())
        html << L"</i>";
    }
    html << L"</p></body></html>";
    std::wcout << html.str() << std::endl;
  }
}
