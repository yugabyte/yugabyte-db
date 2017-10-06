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

#include "yb/util/stol_utils.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

class StolUtilsTest : public YBTest {
};

TEST_F(StolUtilsTest, TestCheckedStoild) {
  int32_t int_val;
  ASSERT_OK(CheckedStoi("123", &int_val));
  ASSERT_OK(CheckedStoi("-123", &int_val));
  ASSERT_NOK(CheckedStoi("123.1", &int_val));
  ASSERT_NOK(CheckedStoi("123456789011", &int_val));
  ASSERT_NOK(CheckedStoi("123-abc", &int_val));
  ASSERT_NOK(CheckedStoi("123 123", &int_val));

  int64_t long_val;
  ASSERT_OK(CheckedStoll("123", &long_val));
  ASSERT_OK(CheckedStoll("-123", &long_val));
  ASSERT_OK(CheckedStoll("9223372036854775807", &long_val));
  ASSERT_OK(CheckedStoll("-9223372036854775808", &long_val));
  ASSERT_NOK(CheckedStoll("-9223372036854775809", &long_val));
  ASSERT_NOK(CheckedStoll("9223372036854775808", &long_val));
  ASSERT_NOK(CheckedStoll("123.1", &long_val));
  ASSERT_NOK(CheckedStoll("123456789123456789123456789", &long_val));
  ASSERT_NOK(CheckedStoll("123 123", &long_val));

  long double double_val;
  ASSERT_OK(CheckedStold("123", &double_val));
  ASSERT_OK(CheckedStold("-123", &double_val));
  ASSERT_OK(CheckedStold("123.1", &double_val));
  ASSERT_OK(CheckedStold("1.7e308", &double_val));
  ASSERT_OK(CheckedStold("-1.7e308", &double_val));
  ASSERT_NOK(CheckedStold("123 123", &double_val));
  ASSERT_NOK(CheckedStold("9223372036854775808e9223372036854775808", &double_val));
  ASSERT_NOK(CheckedStold("-9223372036854775808.8e9223372036854775808", &double_val));
}

} // namespace util
} // namespace yb
