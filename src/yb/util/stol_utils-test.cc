// Copyright (c) YugabyteDB, Inc.
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
    auto decoded_int = ASSERT_RESULT(CheckedStoi(str));
    ASSERT_EQ(int_val, decoded_int);
  }

  void TestStoll(const std::string& str, const int64_t int_val) {
    auto decoded_int = ASSERT_RESULT(CheckedStoll(str));
    ASSERT_EQ(int_val, decoded_int);
  }

  void TestStoull(const std::string& str, const uint64_t int_val) {
    auto decoded_int = ASSERT_RESULT(CheckedStoull(str));
    ASSERT_EQ(int_val, decoded_int);
  }

  void TestStold(const std::string& str, const long double double_val) {
    auto decoded_double = ASSERT_RESULT(CheckedStold(str));
    if (std::isnan(double_val) && std::isnan(decoded_double)) {
      // Equality might not work for two NaN values, but we consider them equal here.
      return;
    }
    ASSERT_DOUBLE_EQ(double_val, decoded_double);
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

  TestStoull("0", 0);
  TestStoull("123", 123);
  TestStoull("9223372036854775808", 9223372036854775808ULL);
  TestStoull("18446744073709551615", 18446744073709551615ULL);
  ASSERT_NOK(CheckedStoull("abcd"));
  ASSERT_NOK(CheckedStoull(" 123"));
  ASSERT_NOK(CheckedStoull("123abc"));
  ASSERT_NOK(CheckedStoull("-1"));
  ASSERT_NOK(CheckedStoull("-9223372036854775809"));
  ASSERT_NOK(CheckedStoull("123.1"));
  ASSERT_NOK(CheckedStoull("123456789123456789123456789"));
  ASSERT_NOK(CheckedStoull("123 123"));
  ASSERT_NOK(CheckedStoull(""));

  TestStold("123", 123);
  TestStold("-123", -123);
  TestStold("123.1", 123.1);
  TestStold("1.7e308", 1.7e308);
  TestStold("-1.7e308", -1.7e308);
  TestStold("inf", std::numeric_limits<long double>::infinity());
  TestStold("InF", std::numeric_limits<long double>::infinity());
  TestStold("-inf", -std::numeric_limits<long double>::infinity());
  TestStold("-iNf", -std::numeric_limits<long double>::infinity());
  TestStold("nAn", std::numeric_limits<long double>::quiet_NaN());
  TestStold("nan", std::numeric_limits<long double>::quiet_NaN());
  ASSERT_NOK(CheckedStold("abcd"));
  ASSERT_NOK(CheckedStold(" 123"));
  ASSERT_NOK(CheckedStold("123abc"));
  ASSERT_NOK(CheckedStold("123 123"));
  ASSERT_NOK(CheckedStold("9223372036854775808e9223372036854775808"));
  ASSERT_NOK(CheckedStold("-9223372036854775808.8e9223372036854775808"));
  ASSERT_NOK(CheckedStold(""));
}


TEST_F(StolUtilsTest, TestCheckedParseNumber) {
  // Test with double
  {
    auto result = CheckedParseNumber<double>("123.456");
    ASSERT_OK(result);
    ASSERT_DOUBLE_EQ(123.456, *result);

    result = CheckedParseNumber<double>("-123.456");
    ASSERT_OK(result);
    ASSERT_DOUBLE_EQ(-123.456, *result);

    result = CheckedParseNumber<double>("1.7e308");
    ASSERT_OK(result);
    ASSERT_DOUBLE_EQ(1.7e308, *result);

    result = CheckedParseNumber<double>("inf");
    ASSERT_OK(result);
    ASSERT_EQ(std::numeric_limits<double>::infinity(), *result);

    result = CheckedParseNumber<double>("-inf");
    ASSERT_OK(result);
    ASSERT_EQ(-std::numeric_limits<double>::infinity(), *result);

    result = CheckedParseNumber<double>("nan");
    ASSERT_OK(result);
    ASSERT_TRUE(std::isnan(*result));

    result = CheckedParseNumber<double>("1.8e308");
    ASSERT_NOK(result);  // Out of range (infinite)

    result = CheckedParseNumber<double>("abc");
    ASSERT_NOK(result);  // Invalid input
  }

  // Test with float
  {
    auto result = CheckedParseNumber<float>("123.456");
    ASSERT_OK(result);
    ASSERT_FLOAT_EQ(123.456f, *result);

    result = CheckedParseNumber<float>("-123.456");
    ASSERT_OK(result);
    ASSERT_FLOAT_EQ(-123.456f, *result);

    result = CheckedParseNumber<float>("3.4e38");
    ASSERT_OK(result);
    ASSERT_FLOAT_EQ(3.4e38f, *result);

    result = CheckedParseNumber<float>("inf");
    ASSERT_OK(result);
    ASSERT_EQ(std::numeric_limits<float>::infinity(), *result);

    result = CheckedParseNumber<float>("-inf");
    ASSERT_OK(result);
    ASSERT_EQ(-std::numeric_limits<float>::infinity(), *result);

    result = CheckedParseNumber<float>("nan");
    ASSERT_OK(result);
    ASSERT_TRUE(std::isnan(*result));

    result = CheckedParseNumber<float>("3.5e38");
    ASSERT_NOK(result);  // Out of range (infinite)

    result = CheckedParseNumber<float>("abc");
    ASSERT_NOK(result);  // Invalid input
  }
}

TEST_F(StolUtilsTest, TestParseCommaSeparatedListOfNumbers) {
  // Test with vector<int32_t>
  {
    auto result =
        ParseCommaSeparatedListOfNumbers<int32_t, std::vector<int32_t>>("1,2,3");
    ASSERT_OK(result);
    ASSERT_EQ((std::vector<int32_t>{1, 2, 3}), *result);

    // Test with bounds
    result = ParseCommaSeparatedListOfNumbers<int32_t, std::vector<int32_t>>("4,5,6", 4, 6);
    ASSERT_OK(result);
    ASSERT_EQ((std::vector<int32_t>{4, 5, 6}), *result);

    // Test with out-of-bounds value
    result = ParseCommaSeparatedListOfNumbers<int32_t, std::vector<int32_t>>("3,4,5", 4, 6);
    ASSERT_NOK(result);  // 3 is less than lower bound 4
  }

  // Test with set<double>
  {
    auto result = ParseCommaSeparatedListOfNumbers<double, std::set<double>>("1.1,2.2,3.3");
    ASSERT_OK(result);
    ASSERT_EQ((std::set<double>{1.1, 2.2, 3.3}), *result);

    // Test with bounds
    result = ParseCommaSeparatedListOfNumbers<double, std::set<double>>("2.5,3.5", 2.0, 4.0);
    ASSERT_OK(result);
    ASSERT_EQ((std::set<double>{2.5, 3.5}), *result);

    // Test with out-of-bounds value
    result = ParseCommaSeparatedListOfNumbers<double, std::set<double>>("1.5,2.5", 2.0, 4.0);
    ASSERT_NOK(result);  // 1.5 is less than lower bound 2.0
  }

  // Test with vector<uint64_t>
  {
    auto result =
        ParseCommaSeparatedListOfNumbers<uint64_t, std::vector<uint64_t>>("10000000000,20000000000");
    ASSERT_OK(result);
    ASSERT_EQ((std::vector<uint64_t>{10000000000ULL, 20000000000ULL}), *result);
  }

  // Test with invalid input
  {
    auto result = ParseCommaSeparatedListOfNumbers<int32_t, std::vector<int32_t>>("1,abc,3");
    ASSERT_NOK(result);  // "abc" cannot be parsed as int32_t
  }

  // Test with empty input
  {
    auto result = ParseCommaSeparatedListOfNumbers<int32_t, std::vector<int32_t>>("");
    ASSERT_OK(result);
    ASSERT_TRUE(result->empty());
  }

  // Test with spaces and tabs
  {
    auto result = ParseCommaSeparatedListOfNumbers<int32_t, std::vector<int32_t>>(" 7 ,\t8 , 9 ");
    ASSERT_OK(result);
    ASSERT_EQ((std::vector<int32_t>{7, 8, 9}), *result);
  }
}

} // namespace util
} // namespace yb
