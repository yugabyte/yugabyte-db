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
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

class StolUtilsTest : public YBTest {
 protected:
  void TestStoi(const std::string& str, const int32_t int_val) {
    auto decoded_int = CheckedStoi(str);
    ASSERT_OK(decoded_int);
    ASSERT_EQ(int_val, *decoded_int);
  }

  void TestStoll(const std::string& str, const int64_t int_val) {
    auto decoded_int = CheckedStoll(str);
    ASSERT_OK(decoded_int);
    ASSERT_EQ(int_val, *decoded_int);
  }

  void TestStold(const std::string& str, const long double double_val) {
    auto decoded_double = CheckedStold(str);
    ASSERT_OK(decoded_double);
    ASSERT_DOUBLE_EQ(double_val, *decoded_double);
  }
};

TEST_F(StolUtilsTest, TestCheckedStoild) {
  TestStoi("123", 123);
  TestStoi("-123", -123);
  TestStoi("2147483647", 2147483647);
  TestStoi("-2147483648", -2147483648);
  ASSERT_NOK(CheckedStoi("abcd"));
  ASSERT_NOK(CheckedStoi(" 123"));
  ASSERT_NOK(CheckedStoi("123abc"));
  ASSERT_NOK(CheckedStoi("123.1"));
  ASSERT_NOK(CheckedStoi("123456789011"));
  ASSERT_NOK(CheckedStoi("123-abc"));
  ASSERT_NOK(CheckedStoi("123 123"));
  ASSERT_NOK(CheckedStoi("2147483648"));
  ASSERT_NOK(CheckedStoi("-2147483649"));
  ASSERT_NOK(CheckedStoi(""));
  ASSERT_NOK(CheckedStoi("123 "));

  TestStoll("123", 123);
  TestStoll("-123", -123);
  TestStoll("9223372036854775807", 9223372036854775807LL);
  TestStoll("-9223372036854775808", -9223372036854775808ULL);
  ASSERT_NOK(CheckedStoll("abcd"));
  ASSERT_NOK(CheckedStoll(" 123"));
  ASSERT_NOK(CheckedStoll("123abc"));
  ASSERT_NOK(CheckedStoll("-9223372036854775809"));
  ASSERT_NOK(CheckedStoll("9223372036854775808"));
  ASSERT_NOK(CheckedStoll("123.1"));
  ASSERT_NOK(CheckedStoll("123456789123456789123456789"));
  ASSERT_NOK(CheckedStoll("123 123"));
  ASSERT_NOK(CheckedStoll(""));

  TestStold("123", 123);
  TestStold("-123", -123);
  TestStold("123.1", 123.1);
  TestStold("1.7e308", 1.7e308);
  TestStold("-1.7e308", -1.7e308);
  ASSERT_NOK(CheckedStold("abcd"));
  ASSERT_NOK(CheckedStold(" 123"));
  ASSERT_NOK(CheckedStold("123abc"));
  ASSERT_NOK(CheckedStold("123 123"));
  ASSERT_NOK(CheckedStold("9223372036854775808e9223372036854775808"));
  ASSERT_NOK(CheckedStold("-9223372036854775808.8e9223372036854775808"));
  ASSERT_NOK(CheckedStold(""));
}

} // namespace util
} // namespace yb
