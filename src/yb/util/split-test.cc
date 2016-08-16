// Copyright (c) YugaByte, Inc.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "yb/util/split.h"
#include "yb/util/test_macros.h"


namespace yb {
namespace util {

TEST(TestSplitArgs, Simple) {
  Slice input("one two three");
  std::vector<Slice> result;

  ASSERT_OK(SplitArgs(input, &result));
  ASSERT_EQ(3, result.size());
  ASSERT_THAT(result, ::testing::ElementsAre("one", "two", "three"));
}

TEST(TestSplitArgs, SimpleWithSpaces) {
  Slice input(" one  two     three   ");
  std::vector<Slice> result;

  ASSERT_OK(SplitArgs(input, &result));
  ASSERT_EQ(3, result.size());
  ASSERT_THAT(result, ::testing::ElementsAre("one", "two", "three"));
}

TEST(TestSplitArgs, SimpleWithQuotes) {
  Slice input("one \"2a 2b\" '3a 3b'");
  std::vector<Slice> result;

  ASSERT_OK(SplitArgs(input, &result));
  ASSERT_EQ(3, result.size());
  ASSERT_THAT(result, ::testing::ElementsAre("one", "2a 2b", "3a 3b"));
}

TEST(TestSplitArgs, BadWithQuotes) {
  Slice input("one\"2a 2b\" '3a 3b'");
  std::vector<Slice> result;

  ASSERT_FALSE(SplitArgs(input, &result).ok());
}

TEST(TestSplitArgs, Empty) {
  Slice input("");
  std::vector<Slice> result;

  ASSERT_OK(SplitArgs(input, &result));
  ASSERT_EQ(0, result.size());
}

TEST(TestSplitArgs, Error) {
  Slice input("one \"2a 2b\"three");
  std::vector<Slice> result;

  ASSERT_FALSE(SplitArgs(input, &result).ok());
  ASSERT_EQ(0, result.size());
}

TEST(TestSplitArgs, UnbalancedSingle) {
  Slice input("one '2a 2b three");
  std::vector<Slice> result;

  ASSERT_FALSE(SplitArgs(input, &result).ok());
  ASSERT_EQ(0, result.size());
}

TEST(TestSplitArgs, UnbalancedDouble) {
  Slice input("one \"2a 2b three");
  std::vector<Slice> result;

  ASSERT_FALSE(SplitArgs(input, &result).ok());
  ASSERT_EQ(0, result.size());
}

}  // namespace util
}  // namespace yb
