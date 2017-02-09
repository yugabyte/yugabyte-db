// Copyright (c) YugaByte, Inc.

#include "yb/util/varint.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace util {

class VarIntTest : public YBTest {
  friend class VarInt;

 protected:

  VarInt EncodeToBase256(const VarInt &v) {
    return v.EncodeToBase256();
  }

  std::string ToBinaryDebugString(const VarInt& v) {
    return v.ToBinaryDebugString();
  }
};

TEST_F(VarIntTest, TestBaseConversion) {
  VarInt decimal_number({3, 4, 2}, 10);

  ASSERT_TRUE(decimal_number.is_identical_to(VarInt("243")));
  VarInt binary_number({1, 1, 0, 0, 1, 1, 1, 1}, 2);

  ASSERT_TRUE(binary_number.is_identical_to(decimal_number.convert_to_base(2)));
  ASSERT_TRUE(decimal_number.is_identical_to(binary_number.convert_to_base(10)));
  ASSERT_TRUE(binary_number.is_identical_to(binary_number.convert_to_base(2)));

  VarInt base26({23, 6, 23, 12}, 26);
  ASSERT_EQ("[ + radix 26 digits 23 6 23 12 ]", base26.ToDebugString());
  ASSERT_EQ("[ + radix 3 digits 1 0 0 0 2 2 1 1 1 2 0 1 ]",
      base26.convert_to_base(3).ToDebugString());

  VarInt base3({1, 0, 0, 0, 2, 2, 1, 1, 1, 2, 0, 1}, 3);
  ASSERT_EQ("[ + radix 3 digits 1 0 0 0 2 2 1 1 1 2 0 1 ]", base3.ToDebugString());
  ASSERT_EQ("[ + radix 26 digits 23 6 23 12 ]",
      base3.convert_to_base(26).ToDebugString());
}

TEST_F(VarIntTest, TestNegatives) {
  VarInt positive_decimal_number({3, 4, 2}, 10, true);
  VarInt negative_decimal_number({3, 4, 2}, 10, false);
  VarInt positive_binary_number({1, 1, 0, 0, 1, 1, 1, 1}, 2, true);
  VarInt negative_binary_number({1, 1, 0, 0, 1, 1, 1, 1}, 2, false);

  ASSERT_TRUE(positive_decimal_number.is_identical_to(VarInt("243")));
  ASSERT_TRUE(negative_decimal_number.is_identical_to(VarInt("-243")));
  ASSERT_FALSE(positive_decimal_number.is_identical_to(VarInt("-243")));
  ASSERT_TRUE(positive_decimal_number.is_identical_to(-negative_decimal_number));
  ASSERT_TRUE(positive_decimal_number.is_identical_to(+positive_decimal_number));
  ASSERT_TRUE(negative_binary_number.is_identical_to(-(-negative_binary_number)));

  ASSERT_EQ("[ + radix 10 digits 3 4 2 ]", positive_decimal_number.ToDebugString());
  ASSERT_EQ("[ - radix 10 digits 3 4 2 ]", negative_decimal_number.ToDebugString());

  ASSERT_EQ("[ - radix 10 digits 3 4 2 ]",
      negative_binary_number.convert_to_base(10).ToDebugString());
  ASSERT_EQ("[ + radix 10 digits 3 4 2 ]",
      positive_binary_number.convert_to_base(10).ToDebugString());
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

  ASSERT_OK(VarInt(max).ToInt64(&val));
  ASSERT_EQ(max, val);

  ASSERT_OK(VarInt(min).ToInt64(&val));
  ASSERT_EQ(min, val);

  ASSERT_OK(VarInt(zero).ToInt64(&val));
  ASSERT_EQ(zero, val);

  ASSERT_OK(VarInt(negative).ToInt64(&val));
  ASSERT_EQ(negative, val);

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
  // TODO (akashnil):Arithmetic is not fully supported / tested yet. When it is needed,
  // we should test it.
}

TEST_F(VarIntTest, TestEncodingDecoding) {
  VarInt negative_decimal_zero({}, 10, false);
  VarInt positive_binary_zero({}, 2, true);
  VarInt negative_small("-23");
  VarInt negative_big("-37618632178637216379216387");
  VarInt positive_small("38");
  VarInt positive_big("8429024091289482183283928321");

  EXPECT_EQ("[ 10000000 ]", ToBinaryDebugString(EncodeToBase256(negative_decimal_zero)));
  EXPECT_EQ("[ 10000000 ]", ToBinaryDebugString(EncodeToBase256(positive_binary_zero)));
  EXPECT_EQ("[ 01101000 ]", ToBinaryDebugString(EncodeToBase256(negative_small)));
  EXPECT_EQ(
      "[ 00000000 00000111 11100000 11100001 11110001 11011101 00000001 00110000 "
          "11011100 11111011 11101101 01011101 11111100 ]",
      ToBinaryDebugString(EncodeToBase256(negative_big)));
  EXPECT_EQ("[ 10100110 ]", ToBinaryDebugString(EncodeToBase256(positive_small)));
  EXPECT_EQ(
      "[ 11111111 11111100 00011011 00111100 01010011 01000111 10010101 11011111 "
          "00011011 00001010 01111011 11111101 01101101 00000001 ]",
      ToBinaryDebugString(EncodeToBase256(positive_big)));

  VarInt decoded;
  size_t size;

  EXPECT_OK(decoded.DecodeFrom(negative_decimal_zero.Encode(), &size));
  EXPECT_EQ(1, size);
  EXPECT_TRUE(negative_decimal_zero == decoded);

  EXPECT_OK(decoded.DecodeFrom(negative_small.Encode(), &size));
  EXPECT_EQ(1, size);
  EXPECT_TRUE(negative_small == decoded);

  EXPECT_OK(decoded.DecodeFrom(negative_big.Encode(), &size));
  EXPECT_EQ(13, size);
  EXPECT_TRUE(negative_big == decoded);

  EXPECT_OK(decoded.DecodeFrom(positive_small.Encode(), &size));
  EXPECT_EQ(1, size);
  EXPECT_TRUE(positive_small == decoded);

  EXPECT_OK(decoded.DecodeFrom(positive_big.Encode(), &size));
  EXPECT_EQ(14, size);
  EXPECT_TRUE(positive_big == decoded);
}

TEST_F(VarIntTest, TestEncodingComparison) {
  std::string negative_big = VarInt("-37618632178637216379216387").Encode();
  std::string negative_small = VarInt("-23").Encode();
  std::string negative_zero = VarInt("-0").Encode();
  std::string zero = VarInt("0").Encode();
  std::string positive_small = VarInt("38").Encode();
  std::string positive_big = VarInt("8429024091289482183283928321").Encode();

  EXPECT_TRUE(negative_big < negative_small);
  EXPECT_TRUE(negative_small < zero);
  EXPECT_TRUE(negative_zero == zero);
  EXPECT_TRUE(zero < positive_small);
  EXPECT_TRUE(positive_small < positive_big);
}

} // namespace util
} // namespace yb
