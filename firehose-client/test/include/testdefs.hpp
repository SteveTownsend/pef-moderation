#include "nlohmann/json.hpp"
#include <fstream>
#include <gtest/gtest.h>

constexpr const char *DataPath = "../data/";

inline nlohmann::json load_json_from_file(const std::string &filename) {
  std::string decorated = DataPath + filename;
  std::ifstream ifs(decorated);
  EXPECT_TRUE(ifs.is_open());
  return nlohmann::json::parse(ifs);
}

inline std::string load_from_file(const std::string &filename) {
  std::string decorated = DataPath + filename;
  std::ifstream ifs(decorated);
  EXPECT_TRUE(ifs.is_open());
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  return buffer.str();
}