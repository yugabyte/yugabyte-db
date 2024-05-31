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

#include <vector>

#include "yb/util/range.h"
#include "yb/util/test_util.h"
#include "yb/util/tostring.h"

namespace yb {

class RangeTest : public YBTest {
};

// Test ascending range (positive step).
TEST_F(RangeTest, TestAsc) {
  ASSERT_EQ(AsString(Range(5)), "[0, 1, 2, 3, 4]");
  ASSERT_EQ(AsString(Range(13, 34, 5)), "[13, 18, 23, 28, 33]");
}

// Test descending range (negative step).
TEST_F(RangeTest, TestDesc) {
  ASSERT_EQ(AsString(Range(4, -1, -1)), "[4, 3, 2, 1, 0]");
  ASSERT_EQ(AsString(Range(34, 13, -5)), "[34, 29, 24, 19, 14]");
}

// Test reverse range.
TEST_F(RangeTest, TestReverse) {
  constexpr auto kCount = 5;
  const auto r = Range(kCount);
  const auto v = std::vector{0, 1, 2, 3, 4};
  const auto expected_r_str = AsString(v);
  ASSERT_EQ(AsString(r), expected_r_str);
  const auto reversed_r = r.Reversed();
  ASSERT_EQ(AsString(reversed_r), AsString(std::vector(v.rbegin(), v.rend())));
  const auto double_reversed_r = reversed_r.Reversed();
  ASSERT_EQ(AsString(double_reversed_r), expected_r_str);
}

} // namespace yb
