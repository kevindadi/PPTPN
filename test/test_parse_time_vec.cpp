//
// Created by 张凯文 on 2024/6/22.
//
#include "clap.h" // 包含 TDG 类定义和 parse_time_vec 方法
#include <gtest/gtest.h>
#include <string>
#include <vector>

// 定义一个测试夹具类
class ParseTimeVecTest : public ::testing::Test {
protected:
  TDG tdg;
};

// 测试 parse_time_vec 方法
TEST_F(ParseTimeVecTest, HandlesEmptyString) {
  std::vector<int> result = tdg.parse_time_vec("");
  EXPECT_TRUE(result.empty());
}

TEST_F(ParseTimeVecTest, HandlesSingleTimePair) {
  std::vector<int> result = tdg.parse_time_vec("[10,20]");
  std::vector<int> expected = {10, 20};
  EXPECT_EQ(result, expected);
}

TEST_F(ParseTimeVecTest, HandlesMultipleTimePairs) {
  std::vector<int> result = tdg.parse_time_vec("[10,20][30,40][50,60]");
  std::vector<int> expected = {10, 20, 30, 40, 50, 60};
  EXPECT_EQ(result, expected);
}

TEST_F(ParseTimeVecTest, HandlesNonMatchingStrings) {
  std::vector<int> result = tdg.parse_time_vec("non-matching string");
  EXPECT_TRUE(result.empty());
}

TEST_F(ParseTimeVecTest, HandlesMixedContent) {
  std::vector<int> result =
      tdg.parse_time_vec("[10,20] random text [30,40] more text [50,60]");
  std::vector<int> expected = {10, 20, 30, 40, 50, 60};
  EXPECT_EQ(result, expected);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
