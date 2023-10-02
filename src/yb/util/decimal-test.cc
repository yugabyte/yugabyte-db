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

#include "yb/util/decimal.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

class DecimalTest : public YBTest {
 protected:
  // Note that the following test cases are only used for testing encodings. The other tests should
  // verify that Decimal representation is perfect, and only the conversion to the encoding and
  // its comparison need to be tested.
  const std::vector<std::string> test_cases = {
      // The purpose of these tests is to verify various aspects of comparisons for different cases.
      // The priority order for comparing two decimals is sign > exponent > mantissa. The mantissa
      // must be compared lexicographically while exponent must be compared in absolute value.

      // -2147483648 is the smallest signed int, so BigDecimal Encoding fails below this scale.
      "-9847.236776e+2147483654", // Note that the scale is -2147483648.
      "-9847.236780e+2147483653",
      // Testing numbers with close by digits to make sure comparison is correct.
      "-1.34",
      "-13.37e-1",
      "-13.34e-1",
      "-13.3e-1",
      // Checking the higher boundary of the scale.
      "-1.36e-2147483645", // Note that the scale is 2147483647, largest signed int.
      "-0",
      "0.05",
      "1.15",
      "1.2",
      "120e0",
      "1.2e+100",
      "2638.2e+3624"
  };

  const std::vector<size_t> kComparableEncodingLengths =
      {10, 10, 3, 3, 3, 3, 7, 1, 2, 3, 2, 2, 3, 6};
  const std::vector<size_t> kBigDecimalEncodingLengths =
      {9, 8, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 6};
};

TEST_F(DecimalTest, TestToStringFunctions) {
  std::string string;
  Decimal decimal0({}, VarInt(0), /* is_positive = */ false);
  Decimal decimal1({9, 0, 1, 2}, VarInt(-2), false);
  Decimal decimal2({9, 0, 1, 2}, VarInt(2), true);
  Decimal decimal3({9, 0, 1, 2}, VarInt(8), false);
  Decimal decimal4(
      {9, 0, 1, 2}, ASSERT_RESULT(VarInt::CreateFromString("-36546632732954564789")), true);
  Decimal decimal5(
      {9, 0, 1, 2}, ASSERT_RESULT(VarInt::CreateFromString("+36546632732954564789")), true);

  EXPECT_EQ("[ + 10^+0 * 0. ]", decimal0.ToDebugString());
  EXPECT_OK(decimal0.ToPointString(&string));
  EXPECT_EQ("0", string);
  EXPECT_EQ("0", decimal0.ToScientificString());
  EXPECT_EQ("0", decimal0.ToString());

  EXPECT_EQ("[ - 10^-2 * 0.9012 ]", decimal1.ToDebugString());
  EXPECT_OK(decimal1.ToPointString(&string));
  EXPECT_EQ("-0.009012", string);
  EXPECT_EQ("-9.012e-3", decimal1.ToScientificString());
  EXPECT_EQ("-0.009012", decimal1.ToString());

  EXPECT_EQ("[ + 10^+2 * 0.9012 ]", decimal2.ToDebugString());
  EXPECT_OK(decimal2.ToPointString(&string));
  EXPECT_EQ("90.12", string);
  EXPECT_EQ("9.012e+1", decimal2.ToScientificString());
  EXPECT_EQ("90.12", decimal2.ToString());

  EXPECT_EQ("[ - 10^+8 * 0.9012 ]", decimal3.ToDebugString());
  EXPECT_OK(decimal3.ToPointString(&string));
  EXPECT_EQ("-90120000", string);
  EXPECT_EQ("-9.012e+7", decimal3.ToScientificString());
  EXPECT_EQ("-90120000", decimal3.ToString());

  EXPECT_EQ("[ + 10^-36546632732954564789 * 0.9012 ]", decimal4.ToDebugString());
  EXPECT_FALSE(decimal4.ToPointString(&string).ok());
  EXPECT_EQ("9.012e-36546632732954564790", decimal4.ToScientificString());
  EXPECT_EQ("9.012e-36546632732954564790", decimal4.ToString());

  EXPECT_EQ("[ + 10^+36546632732954564789 * 0.9012 ]", decimal5.ToDebugString());
  EXPECT_FALSE(decimal5.ToPointString(&string).ok());
  EXPECT_EQ("9.012e+36546632732954564788", decimal5.ToScientificString());
  EXPECT_EQ("9.012e+36546632732954564788", decimal5.ToString());
}

TEST_F(DecimalTest, TestFromStringFunctions) {
  Decimal decimal;

  EXPECT_OK(decimal.FromString("0"));
  EXPECT_EQ("[ + 10^+0 * 0. ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("+0"));
  EXPECT_EQ("[ + 10^+0 * 0. ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("+00"));
  EXPECT_EQ("[ + 10^+0 * 0. ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("0.1"));
  EXPECT_EQ("[ + 10^+0 * 0.1 ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString(".1"));
  EXPECT_EQ("[ + 10^+0 * 0.1 ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("0.02"));
  EXPECT_EQ("[ + 10^-1 * 0.2 ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("12.02"));
  EXPECT_EQ("[ + 10^+2 * 0.1202 ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("+0120."));
  EXPECT_EQ("[ + 10^+3 * 0.12 ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("-0"));
  EXPECT_EQ("[ + 10^+0 * 0. ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("-0.0"));
  EXPECT_EQ("[ + 10^+0 * 0. ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("-9.012e-4"));
  EXPECT_EQ("[ - 10^-3 * 0.9012 ]", decimal.ToDebugString());
  EXPECT_OK(decimal.FromString("9.012e-36546632732954564791"));
  EXPECT_EQ("[ + 10^-36546632732954564790 * 0.9012 ]", decimal.ToDebugString());

  EXPECT_FALSE(decimal.FromString("").ok());
  EXPECT_FALSE(decimal.FromString("-").ok());
  EXPECT_FALSE(decimal.FromString("1.1a").ok());
  EXPECT_FALSE(decimal.FromString("1.1a1").ok());
  EXPECT_FALSE(decimal.FromString("1.1e").ok());
  EXPECT_FALSE(decimal.FromString("1.1e1a2").ok());
}

TEST_F(DecimalTest, IsIntegerTest) {
  EXPECT_TRUE(Decimal({}, VarInt(0), false).is_integer());

  EXPECT_FALSE(Decimal({3}, VarInt(-1), false).is_integer());
  EXPECT_FALSE(Decimal({3}, VarInt(0), false).is_integer());
  EXPECT_TRUE(Decimal({3}, VarInt(1), false).is_integer());
  auto big_positive = ASSERT_RESULT(VarInt::CreateFromString("328763771921201932786301"));
  auto big_negative = ASSERT_RESULT(VarInt::CreateFromString("-328763771921201932786301"));
  EXPECT_TRUE(Decimal({3}, big_positive, false).is_integer());
  EXPECT_FALSE(Decimal(
      {3}, big_negative, false).is_integer());

  EXPECT_FALSE(Decimal({3, 0, 7, 8}, VarInt(-1), false).is_integer());
  EXPECT_FALSE(Decimal({3, 0, 7, 8}, VarInt(3), false).is_integer());
  EXPECT_TRUE(Decimal({3, 0, 7, 8}, VarInt(4), false).is_integer());
  EXPECT_TRUE(Decimal({3, 0, 7, 8}, big_positive, false).is_integer());
  EXPECT_FALSE(Decimal({3, 0, 7, 8}, big_negative, false).is_integer());
}

TEST_F(DecimalTest, TestDoubleConversions) {
  // Note: Rounding errors are expected

  auto dbl = Decimal("12.301").ToDouble();
  EXPECT_OK(dbl);
  EXPECT_EQ("1.2301000000000000156e+1", Decimal(*dbl).ToString());

  EXPECT_OK(dbl = Decimal("-0").ToDouble());
  EXPECT_EQ("0", Decimal(*dbl).ToString());

  EXPECT_OK(dbl = Decimal("1236.8642261937127309271040921").ToDouble());
  EXPECT_EQ("1.2368642261937127387e+3", Decimal(*dbl).ToString());

  EXPECT_OK(dbl = Decimal("1.236864226e3").ToDouble());
  EXPECT_EQ("1.2368642259999999169e+3", Decimal(*dbl).ToString());

  // Test large exponent
  EXPECT_OK(dbl = Decimal("1.236864226e-33").ToDouble());
  EXPECT_EQ("1.2368642260000000385e-33", Decimal(*dbl).ToString());

  // Exponent too large
  EXPECT_NOK(Decimal("1.236864226e-782323").ToDouble());

  Decimal decimal;

  EXPECT_OK(decimal.FromDouble(std::numeric_limits<double>::epsilon()));
  EXPECT_OK(dbl = decimal.ToDouble());
  EXPECT_EQ(std::numeric_limits<double>::epsilon(), static_cast<double>(*dbl));
  EXPECT_EQ("2.2204460492503130808e-16", decimal.ToString());

  EXPECT_OK(decimal.FromDouble(std::numeric_limits<double>::lowest()));
  EXPECT_OK(dbl = decimal.ToDouble());
  EXPECT_EQ(std::numeric_limits<double>::lowest(), static_cast<double>(*dbl));
  EXPECT_EQ("-1.7976931348623157081e+308", decimal.ToString());

  EXPECT_OK(decimal.FromDouble(std::numeric_limits<double>::max()));
  EXPECT_OK(dbl = decimal.ToDouble());
  EXPECT_EQ(std::numeric_limits<double>::max(), static_cast<double>(*dbl));
  EXPECT_EQ("1.7976931348623157081e+308", decimal.ToString());

  // Can convert from denorm values.
  EXPECT_OK(decimal.FromDouble(std::numeric_limits<double>::denorm_min()));
  // Can convert to denorm values.
  EXPECT_OK(decimal.ToDouble());
  EXPECT_EQ("4.9406564584124654418e-324", decimal.ToString());

  EXPECT_TRUE(decimal.FromDouble(std::numeric_limits<double>::infinity()).IsCorruption());
  EXPECT_TRUE(decimal.FromDouble(-std::numeric_limits<double>::infinity()).IsCorruption());
  EXPECT_TRUE(decimal.FromDouble(std::numeric_limits<double>::signaling_NaN()).IsCorruption());
  EXPECT_TRUE(decimal.FromDouble(std::numeric_limits<double>::quiet_NaN()).IsCorruption());
}

TEST_F(DecimalTest, TestComparableEncoding) {
  std::vector<Decimal> test_decimals;
  std::vector<std::string> encoded_strings;
  std::vector<Decimal> decoded_decimals;
  for (size_t i = 0; i < test_cases.size(); i++) {
    SCOPED_TRACE(Format("Index: $0, value: $1", i, test_cases[i]));
    test_decimals.emplace_back(test_cases[i]);
    encoded_strings.push_back(test_decimals[i].EncodeToComparable());
    EXPECT_EQ(kComparableEncodingLengths[i], encoded_strings[i].size());
    decoded_decimals.emplace_back();
    size_t length;
    EXPECT_OK(decoded_decimals[i].DecodeFromComparable(encoded_strings[i], &length));
    EXPECT_EQ(kComparableEncodingLengths[i], length);
    EXPECT_EQ(test_decimals[i], decoded_decimals[i]);
    if (i > 0) {
      EXPECT_GT(decoded_decimals[i], decoded_decimals[i-1]);
      EXPECT_GT(decoded_decimals[i], test_decimals[i-1]);
      EXPECT_GT(test_decimals[i], decoded_decimals[i-1]);
      EXPECT_GT(test_decimals[i], test_decimals[i-1]);
      EXPECT_GT(encoded_strings[i], encoded_strings[i-1]);
    }
  }
}

TEST_F(DecimalTest, TestBigDecimalEncoding) {
  std::vector<Decimal> test_decimals;
  std::vector<std::string> encoded_strings;
  std::vector<Decimal> decoded_decimals;
  bool is_out_of_range = false;
  for (size_t i = 0; i < test_cases.size(); i++) {
    SCOPED_TRACE(Format("Index: $0, value: $1", i, test_cases[i]));
    test_decimals.emplace_back(test_cases[i]);
    encoded_strings.push_back(test_decimals[i].EncodeToSerializedBigDecimal(&is_out_of_range));
    EXPECT_FALSE(is_out_of_range);
    EXPECT_EQ(kBigDecimalEncodingLengths[i], encoded_strings[i].size());
    decoded_decimals.emplace_back();
    EXPECT_OK(decoded_decimals[i].DecodeFromSerializedBigDecimal(encoded_strings[i]));
    EXPECT_EQ(decoded_decimals[i], test_decimals[i]);
    if (i > 0) {
      EXPECT_GT(decoded_decimals[i], decoded_decimals[i-1]);
      EXPECT_GT(decoded_decimals[i], test_decimals[i-1]);
      EXPECT_GT(test_decimals[i], decoded_decimals[i-1]);
      EXPECT_GT(test_decimals[i], test_decimals[i-1]);
      // This is not necessarily true for BigDecimal Serialization
      // EXPECT_TRUE(encoded_strings[i] > encoded_strings[i-1]);
    }
  }

  // Testing just outside the scale limits.
  Decimal("-9847.236780e+2147483654").EncodeToSerializedBigDecimal(&is_out_of_range);
  EXPECT_TRUE(is_out_of_range);
  Decimal("-1.36e-2147483646").EncodeToSerializedBigDecimal(&is_out_of_range);
  EXPECT_TRUE(is_out_of_range);
}

TEST_F(DecimalTest, TestFloatDoubleCanonicalization) {
  const float float_nan_0 = CreateFloat(1, 0b11111111, (1 << 22));
  const float float_nan_1 = CreateFloat(0, 0b11111111, 1);
  const float float_not_nan_0 = CreateFloat(0, 0b11111110, 1);
  const float float_not_nan_1 = CreateFloat(0, 0b11111111, 0);

  const double double_nan_0 = CreateDouble(1, 0b11111111111, (1l << 51));
  const double double_nan_1 = CreateDouble(0, 0b11111111111, 1);
  const double double_not_nan_0 = CreateDouble(0, 0b11111111110, 1);
  const double double_not_nan_1 = CreateDouble(0, 0b11111111111, 0);

  EXPECT_TRUE(IsNanFloat(float_nan_0));
  EXPECT_TRUE(IsNanFloat(float_nan_1));
  EXPECT_FALSE(IsNanFloat(float_not_nan_0));
  EXPECT_FALSE(IsNanFloat(float_not_nan_1));

  EXPECT_TRUE(IsNanDouble(double_nan_0));
  EXPECT_TRUE(IsNanDouble(double_nan_1));
  EXPECT_FALSE(IsNanDouble(double_not_nan_0));
  EXPECT_FALSE(IsNanDouble(double_not_nan_1));

  float f1 = CanonicalizeFloat(float_nan_0);
  float f2 = CanonicalizeFloat(float_nan_1);
  EXPECT_EQ(*(reinterpret_cast<int32_t *>(&f1)), *(reinterpret_cast<int32_t *>(&f2)));

  double d1 = CanonicalizeDouble(double_nan_0);
  double d2 = CanonicalizeDouble(double_nan_1);
  EXPECT_EQ(*(reinterpret_cast<int64_t *>(&d1)), *(reinterpret_cast<int64_t *>(&d2)));
}

} // namespace util
} // namespace yb
