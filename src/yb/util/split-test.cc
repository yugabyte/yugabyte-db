// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <gmock/gmock.h>

#include "yb/util/split.h"
#include "yb/util/status.h"
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
