// Copyright (c) YugaByte, Inc.

#include "yb/util/varint.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

class VarIntTest : public YBTest {

 protected:

  std::string EncodeToComparable(const VarInt& v, bool is_signed = true) {
    return v.EncodeToComparableBytes(is_signed).ToDebugStringFromBase256();
  }

  std::string EncodeToTwosComplement(const VarInt& v, bool* is_out_of_range, size_t num_bytes = 0) {
    return v.EncodeToTwosComplementBytes(is_out_of_range, num_bytes).ToDebugStringFromBase256();
  }

  std::string EncodeToDigitPairs(const VarInt& v) {
    return v.EncodeToDigitPairsBytes().ToDebugStringFromBase256();
  }
};

TEST_F(VarIntTest, TestBaseConversion) {
  VarInt decimal_number({3, 4, 2}, 10);

  ASSERT_TRUE(decimal_number.IsIdenticalTo(VarInt("243")));
  VarInt binary_number({1, 1, 0, 0, 1, 1, 1, 1}, 2);

  ASSERT_TRUE(binary_number.IsIdenticalTo(decimal_number.ConvertToBase(2)));
  ASSERT_TRUE(decimal_number.IsIdenticalTo(binary_number.ConvertToBase(10)));
  ASSERT_TRUE(binary_number.IsIdenticalTo(binary_number.ConvertToBase(2)));

  VarInt base26({23, 6, 23, 12}, 26);
  ASSERT_EQ("[ + radix 26 digits 23 6 23 12 ]", base26.ToDebugString());
  ASSERT_EQ("[ + radix 3 digits 1 0 0 0 2 2 1 1 1 2 0 1 ]",
      base26.ConvertToBase(3).ToDebugString());

  VarInt base3({1, 0, 0, 0, 2, 2, 1, 1, 1, 2, 0, 1}, 3);
  ASSERT_EQ("[ + radix 3 digits 1 0 0 0 2 2 1 1 1 2 0 1 ]", base3.ToDebugString());
  ASSERT_EQ("[ + radix 26 digits 23 6 23 12 ]",
      base3.ConvertToBase(26).ToDebugString());
}

TEST_F(VarIntTest, TestNegatives) {
  VarInt positive_decimal_number({3, 4, 2}, 10, true);
  VarInt negative_decimal_number({3, 4, 2}, 10, false);
  VarInt positive_binary_number({1, 1, 0, 0, 1, 1, 1, 1}, 2, true);
  VarInt negative_binary_number({1, 1, 0, 0, 1, 1, 1, 1}, 2, false);

  ASSERT_TRUE(positive_decimal_number.IsIdenticalTo(VarInt("243")));
  ASSERT_TRUE(negative_decimal_number.IsIdenticalTo(VarInt("-243")));
  ASSERT_FALSE(positive_decimal_number.IsIdenticalTo(VarInt("-243")));
  ASSERT_TRUE(positive_decimal_number.IsIdenticalTo(-negative_decimal_number));
  ASSERT_TRUE(positive_decimal_number.IsIdenticalTo(+positive_decimal_number));
  ASSERT_TRUE(negative_binary_number.IsIdenticalTo(-(-negative_binary_number)));

  ASSERT_EQ("[ + radix 10 digits 3 4 2 ]", positive_decimal_number.ToDebugString());
  ASSERT_EQ("[ - radix 10 digits 3 4 2 ]", negative_decimal_number.ToDebugString());

  ASSERT_EQ("[ - radix 10 digits 3 4 2 ]",
      negative_binary_number.ConvertToBase(10).ToDebugString());
  ASSERT_EQ("[ + radix 10 digits 3 4 2 ]",
      positive_binary_number.ConvertToBase(10).ToDebugString());
}

TEST_F(VarIntTest, TestTypeConversions) {
  int64_t max = std::numeric_limits<int64_t>::max();
  int64_t min = std::numeric_limits<int64_t>::min();
  int64_t zero = 0;
  int64_t negative = -380;

  ASSERT_EQ("9223372036854775807", VarInt(max).ToString());
  ASSERT_EQ("-9223372036854775808", VarInt(min).ToString());
  ASSERT_EQ("0", VarInt(zero).ToString());
  ASSERT_EQ("-380", VarInt(negative).ToString());

  int64_t val;

  // Verify that the largest and smallest values don't cause overflow.
  ASSERT_OK(VarInt(max).ToInt64(&val));
  ASSERT_EQ(max, val);

  ASSERT_OK(VarInt(min).ToInt64(&val));
  ASSERT_EQ(min, val);

  ASSERT_OK(VarInt(zero).ToInt64(&val));
  ASSERT_EQ(zero, val);

  ASSERT_OK(VarInt(negative).ToInt64(&val));
  ASSERT_EQ(negative, val);

  // One more and less than largest value should overflow.
  ASSERT_FALSE(VarInt("9223372036854775808").ToInt64(&val).ok());
  ASSERT_FALSE(VarInt("-9223372036854775809").ToInt64(&val).ok());
}

TEST_F(VarIntTest, TestComparison) {
  VarInt negative_decimal_zero({}, 10, false);
  VarInt positive_binary_zero({}, 2, true);
  VarInt negative_binary_zero({}, 2, false);
  VarInt negative_small("-23");
  VarInt negative_big("-37618632178637216379216387");
  VarInt positive_small("38");
  VarInt positive_big("8429024091289482183283928321");

  ASSERT_TRUE(positive_binary_zero == negative_decimal_zero);
  ASSERT_TRUE(negative_binary_zero == positive_binary_zero);
  ASSERT_FALSE(negative_decimal_zero == negative_small);
  ASSERT_FALSE(positive_binary_zero < negative_decimal_zero);
  ASSERT_TRUE(positive_small < positive_big);
  ASSERT_TRUE(negative_big < negative_small);
  ASSERT_TRUE(negative_big < positive_small);
  ASSERT_TRUE(negative_small <= negative_small);
  ASSERT_TRUE(negative_big <= negative_small);
  ASSERT_FALSE(negative_small <= negative_big);
  ASSERT_TRUE(positive_small > negative_small);
  ASSERT_FALSE(positive_small > positive_big);
  ASSERT_TRUE(negative_decimal_zero >= positive_binary_zero);
  ASSERT_FALSE(negative_small >= positive_big);
}

TEST_F(VarIntTest, TestArithmetic) {
  ASSERT_EQ(VarInt("74"), VarInt("28") + VarInt("46"));
  ASSERT_EQ("-113", (VarInt("28") - VarInt("141")).ToString());
  ASSERT_EQ(VarInt("-113"), VarInt("28") - VarInt("141"));
  ASSERT_EQ(VarInt("0"), VarInt::add({VarInt("23"), VarInt("3"), VarInt("-26")}));
  ASSERT_EQ(VarInt("1"), VarInt::add({VarInt("23"), VarInt("3"), VarInt("-25")}));
  ASSERT_EQ(VarInt("-1"), VarInt::add({VarInt("23"), VarInt("3"), VarInt("-27")}));
  // Test arithmetic even if the numbers are not in the same base
  ASSERT_EQ(VarInt("-112"), VarInt("29").ConvertToBase(7) - VarInt("141"));
}

TEST_F(VarIntTest, TestComparableEncoding) {
  VarInt negative_decimal_zero({}, 10, false);
  VarInt positive_binary_zero({}, 2, true);
  VarInt negative_small("-23");
  VarInt negative_big("-37618632178637216379216387");
  VarInt positive_small("38");
  VarInt positive_big("8429024091289482183283928321");

  EXPECT_EQ("[ 10000000 ]", EncodeToComparable(negative_decimal_zero));
  EXPECT_EQ("[ 10000000 ]", EncodeToComparable(positive_binary_zero));
  EXPECT_EQ("[ 01101000 ]", EncodeToComparable(negative_small));
  EXPECT_EQ("[ 00000000 00000111 11100000 11100001 11110001 11011101 00000001 00110000 "
          "11011100 11111011 11101101 01011101 11111100 ]",
      EncodeToComparable(negative_big));
  EXPECT_EQ("[ 10100110 ]", EncodeToComparable(positive_small));
  EXPECT_EQ("[ 00100110 ]", EncodeToComparable(positive_small, false));
  EXPECT_EQ("[ 11111111 11111100 00011011 00111100 01010011 01000111 10010101 11011111 "
          "00011011 00001010 01111011 11111101 01101101 00000001 ]",
      EncodeToComparable(positive_big));
  EXPECT_EQ("[ 11111111 11111000 00011011 00111100 01010011 01000111 10010101 11011111 "
      "00011011 00001010 01111011 11111101 01101101 00000001 ]",
      EncodeToComparable(positive_big, false));

  VarInt decoded;
  size_t size;

  EXPECT_OK(decoded.DecodeFromComparable(negative_decimal_zero.EncodeToComparable(), &size));
  EXPECT_EQ(1, size);
  EXPECT_EQ(negative_decimal_zero, decoded);

  EXPECT_OK(decoded.DecodeFromComparable(negative_small.EncodeToComparable(), &size));
  EXPECT_EQ(1, size);
  EXPECT_EQ(negative_small, decoded);

  EXPECT_OK(decoded.DecodeFromComparable(negative_big.EncodeToComparable(), &size));
  EXPECT_EQ(13, size);
  EXPECT_EQ(negative_big, decoded);

  EXPECT_OK(decoded.DecodeFromComparable(positive_small.EncodeToComparable(), &size));
  EXPECT_EQ(1, size);
  EXPECT_EQ(positive_small, decoded);

  EXPECT_OK(decoded.DecodeFromComparable(positive_small.EncodeToComparable(false), &size, false));
  EXPECT_EQ(1, size);
  EXPECT_EQ(positive_small, decoded);

  EXPECT_OK(decoded.DecodeFromComparable(positive_big.EncodeToComparable(), &size));
  EXPECT_EQ(14, size);
  EXPECT_EQ(positive_big, decoded);

  EXPECT_OK(decoded.DecodeFromComparable(positive_big.EncodeToComparable(false), &size, false));
  EXPECT_EQ(14, size);
  EXPECT_EQ(positive_big, decoded);
}

TEST_F(VarIntTest, TestEncodingComparison) {
  std::string negative_big = VarInt("-37618632178637216379216387").EncodeToComparable();
  std::string negative_small = VarInt("-23").EncodeToComparable();
  std::string negative_zero = VarInt("-0").EncodeToComparable();
  std::string zero = VarInt("0").EncodeToComparable();
  std::string positive_small = VarInt("38").EncodeToComparable();
  std::string positive_big = VarInt("8429024091289482183283928321").EncodeToComparable();

  EXPECT_LT(negative_big, negative_small);
  EXPECT_LT(negative_small, zero);
  EXPECT_EQ(negative_zero, zero);
  EXPECT_LT(zero, positive_small);
  EXPECT_LT(positive_small, positive_big);
}

TEST_F(VarIntTest, TestTwosComplementEncoding) {
  VarInt negative_decimal_zero({}, 10, false);
  VarInt positive_binary_zero({}, 2, true);
  VarInt negative_small("-23");
  VarInt negative_big("-37618632178637216379216387");
  VarInt positive_small("38");
  VarInt positive_big("8429024091289482183283928321");
  VarInt max_int32 = VarInt(std::numeric_limits<int>::max());
  VarInt min_int32 = VarInt(std::numeric_limits<int>::min());
  VarInt overflow_max = max_int32 + VarInt(1);
  VarInt overflow_min = min_int32 - VarInt(1);

  bool is_out_of_range = false;

  EXPECT_EQ("[ 00000000 ]", EncodeToTwosComplement(negative_decimal_zero, &is_out_of_range));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 00000000 ]", EncodeToTwosComplement(positive_binary_zero, &is_out_of_range));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 11101001 ]", EncodeToTwosComplement(negative_small, &is_out_of_range));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 11111111 11111111 11111111 11101001 ]",
      EncodeToTwosComplement(negative_small, &is_out_of_range, 4));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 11100000 11100001 11110001 11011101 00000001 00110000 11011100 11111011 "
          "11101101 01011101 11111101 ]",
      EncodeToTwosComplement(negative_big, &is_out_of_range));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 00100110 ]",
      EncodeToTwosComplement(positive_small, &is_out_of_range));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 00000000 00000000 00000000 00100110 ]",
      EncodeToTwosComplement(positive_small, &is_out_of_range, 4));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 00011011 00111100 01010011 01000111 10010101 11011111 00011011 00001010 "
          "01111011 11111101 01101101 00000001 ]",
      EncodeToTwosComplement(positive_big, &is_out_of_range));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 01111111 11111111 11111111 11111111 ]",
      EncodeToTwosComplement(max_int32, &is_out_of_range, 4));
  EXPECT_FALSE(is_out_of_range);
  EXPECT_EQ("[ 10000000 00000000 00000000 00000000 ]",
      EncodeToTwosComplement(min_int32, &is_out_of_range, 4));
  EXPECT_FALSE(is_out_of_range);

  EncodeToTwosComplement(overflow_max, &is_out_of_range, 4);
  EXPECT_TRUE(is_out_of_range);
  EncodeToTwosComplement(overflow_min, &is_out_of_range, 4);
  EXPECT_TRUE(is_out_of_range);

  VarInt decoded;

  for (const VarInt &v : {
      negative_decimal_zero, positive_binary_zero, negative_small, negative_big,
      positive_small, positive_big, max_int32, min_int32
  }) {
    EXPECT_OK(decoded.DecodeFromTwosComplement(v.EncodeToTwosComplement(&is_out_of_range)));
    EXPECT_FALSE(is_out_of_range);
    EXPECT_EQ(v, decoded);
  }
}

TEST_F(VarIntTest, TestDigitPairsEncoding) {
  VarInt zero("0");
  VarInt one_digit("3");
  // Note that VarInt("25") actually means the digit aray {5, 2} (in reverse order)
  VarInt two_digit("25");
  VarInt three_digit("272");
  VarInt four_digit("3782");
  VarInt large_odd_digits("123456789012345678901");
  VarInt large_even_digits("1234567890123456789012");

  EXPECT_EQ("[ 00000000 ]", EncodeToDigitPairs(zero));
  EXPECT_EQ("[ 00011110 ]", EncodeToDigitPairs(one_digit)); // 30 = 0b11110
  EXPECT_EQ("[ 00110100 ]", EncodeToDigitPairs(two_digit)); // 52 = 0b110100
  EXPECT_EQ("[ 10011100 01001001 ]", EncodeToDigitPairs(four_digit)); // 28 = 0b11100,73 = 0b1001001
  EXPECT_EQ("[ 10001010 11100010 11001100 10110110 10100000 10001010 11100010 11001100 "
      "10110110 10100000 00001010 ]", EncodeToDigitPairs(large_odd_digits));
  EXPECT_EQ("[ 10010101 10001001 11010111 11000001 10101011 10010101 10001001 11010111 "
      "11000001 10101011 00010101 ]", EncodeToDigitPairs(large_even_digits));

  VarInt decoded;
  size_t size;

  std::vector<VarInt> test_values = {
      zero, one_digit, two_digit, three_digit, four_digit, large_odd_digits, large_even_digits };

  for (VarInt v : test_values) {
    EXPECT_OK(decoded.DecodeFromDigitPairs(v.EncodeToDigitPairs(), &size));
    EXPECT_EQ(v.digits().empty() ? 1 : (v.digits().size()+1) / 2, size);
    EXPECT_EQ(v, decoded);
  }
}

} // namespace util
} // namespace yb
