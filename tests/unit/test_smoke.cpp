#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <picosha2.h>
#include <yaml-cpp/yaml.h>

TEST(Smoke, ThirdPartyLinks) {
  EXPECT_EQ(YAML::Load("a: 1")["a"].as<int>(), 1);
  EXPECT_EQ(nlohmann::json::parse("{\"a\":2}")["a"].get<int>(), 2);
  EXPECT_EQ(picosha2::hash256_hex_string(std::string("abc")).substr(0, 8), "ba7816bf");
}
