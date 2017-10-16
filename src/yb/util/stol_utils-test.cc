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
 protected:
  void TestStoi(const std::string& str, const int32_t int_val) {
    int32_t decoded_int;
    ASSERT_OK(CheckedStoi(str, &decoded_int));
    ASSERT_EQ(int_val, decoded_int);
  }

  void TestStoll(const std::string& str, const int64_t int_val) {
    int64_t decoded_int;
    ASSERT_OK(CheckedStoll(str, &decoded_int));
    ASSERT_EQ(int_val, decoded_int);
  }

  void TestStold(const std::string& str, const long double double_val) {
    long double decoded_double;
    ASSERT_OK(CheckedStold(str, &decoded_double));
    ASSERT_DOUBLE_EQ(double_val, decoded_double);
  }
};

TEST_F(StolUtilsTest, TestCheckedStoild) {
  int32_t int_val;
  TestStoi("123", 123);
  TestStoi("-123", -123);
  TestStoi("2147483647", 2147483647);
  TestStoi("-2147483648", -2147483648);
  ASSERT_NOK(CheckedStoi("abcd", &int_val));
  ASSERT_NOK(CheckedStoi(" 123", &int_val));
  ASSERT_NOK(CheckedStoi("123abc", &int_val));
  ASSERT_NOK(CheckedStoi("123.1", &int_val));
  ASSERT_NOK(CheckedStoi("123456789011", &int_val));
  ASSERT_NOK(CheckedStoi("123-abc", &int_val));
  ASSERT_NOK(CheckedStoi("123 123", &int_val));
  ASSERT_NOK(CheckedStoi("2147483648", &int_val));
  ASSERT_NOK(CheckedStoi("-2147483649", &int_val));
  ASSERT_NOK(CheckedStoi("", &int_val));
  ASSERT_NOK(CheckedStoi("123 ", &int_val));

  int64_t long_val;
  TestStoll("123", 123);
  TestStoll("-123", -123);
  TestStoll("9223372036854775807", 9223372036854775807LL);
  TestStoll("-9223372036854775808", -9223372036854775808ULL);
  ASSERT_NOK(CheckedStoll("abcd", &long_val));
  ASSERT_NOK(CheckedStoll(" 123", &long_val));
  ASSERT_NOK(CheckedStoll("123abc", &long_val));
  ASSERT_NOK(CheckedStoll("-9223372036854775809", &long_val));
  ASSERT_NOK(CheckedStoll("9223372036854775808", &long_val));
  ASSERT_NOK(CheckedStoll("123.1", &long_val));
  ASSERT_NOK(CheckedStoll("123456789123456789123456789", &long_val));
  ASSERT_NOK(CheckedStoll("123 123", &long_val));
  ASSERT_NOK(CheckedStoll("", &long_val));

  long double double_val;
  TestStold("123", 123);
  TestStold("-123", -123);
  TestStold("123.1", 123.1);
  TestStold("1.7e308", 1.7e308);
  TestStold("-1.7e308", -1.7e308);
  ASSERT_NOK(CheckedStold("abcd", &double_val));
  ASSERT_NOK(CheckedStold(" 123", &double_val));
  ASSERT_NOK(CheckedStold("123abc", &double_val));
  ASSERT_NOK(CheckedStold("123 123", &double_val));
  ASSERT_NOK(CheckedStold("9223372036854775808e9223372036854775808", &double_val));
  ASSERT_NOK(CheckedStold("-9223372036854775808.8e9223372036854775808", &double_val));
  ASSERT_NOK(CheckedStold("", &double_val));
}

} // namespace util
} // namespace yb
