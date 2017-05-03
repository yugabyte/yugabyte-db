// Copyright (c) YugaByte, Inc.

#include <iostream>

#include "yb/gutil/strings/substitute.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/cast.h"
#include "yb/util/fast_varint.h"
#include "yb/util/random.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/varint.h"

using strings::Substitute;
using yb::util::FormatBytesAsStr;

namespace yb {
namespace util {

namespace {

void CheckEncoding(int64_t v) {
  SCOPED_TRACE(Substitute("v=$0", v));

  const string correct_encoded(VarInt(v).EncodeToComparable(true, 0));
  uint8_t buf[16];
  int encoded_size = 0;

  FastEncodeSignedVarInt(v, buf, &encoded_size);
  ASSERT_EQ(correct_encoded.size(), encoded_size);
  ASSERT_EQ(correct_encoded, string(to_char_ptr(buf), encoded_size));
  int64_t decoded = 0;
  int decoded_size = 0;
  ASSERT_OK(FastDecodeVarInt(correct_encoded, &decoded, &decoded_size));
  ASSERT_EQ(v, decoded);
  ASSERT_EQ(decoded_size, correct_encoded.size());

  {
    // Provide a way to generate examples for checking value validity during decoding. These could
    // be copied and pasted into TestDecodeIncorrectValues.
    int64_t unused_decoded_value ATTRIBUTE_UNUSED;
    int unused_decoded_size ATTRIBUTE_UNUSED;

    constexpr bool kGenerateInvalidDecodingExamples = false;
    if (kGenerateInvalidDecodingExamples &&
        !FastDecodeVarInt(
            buf + 1, encoded_size - 1, &unused_decoded_value, &unused_decoded_size).ok()) {
      std::cout << "ASSERT_FALSE(FastDecodeVarInt("
                << FormatBytesAsStr(to_char_ptr(buf) + 1, encoded_size - 1) << ", "
                << encoded_size - 1 << ", "
                << "&v, &n).ok());" << std::endl;
    }
  }

  // Also test "descending varint" encoding. This is only makes sense for numbers that can be
  // negated (not the minimum possible 64-bit integer).
  if (v != std::numeric_limits<int64_t>::min()) {
    // Our "descending varint" encoding is simply the encoding of the negated argument.
    static const string kPrefix = "some_prefix";
    string encoded_dest = kPrefix;
    FastEncodeDescendingVarInt(-v, &encoded_dest);
    const int encoded_size = encoded_dest.size() - kPrefix.size();
    ASSERT_EQ(correct_encoded,
              encoded_dest.substr(kPrefix.size(), encoded_size));

    Slice slice_for_decoding(encoded_dest.c_str() + kPrefix.size(), encoded_size);
    int64_t decoded_value = 0;
    ASSERT_OK(FastDecodeDescendingVarInt(&slice_for_decoding, &decoded_value));
    ASSERT_EQ(0, slice_for_decoding.size());
    ASSERT_EQ(-v, decoded_value);
  }
}

}  // anonymous namespace

TEST(FastVarintTest, TestEncodeDecode) {
  Random rng(SeedRandom());
  CheckEncoding(-1);
  for (int i = 0; i <= 62; ++i) {
    SCOPED_TRACE(Substitute("i (power of 2)=$0", i));
    CheckEncoding(1LL << i);
    CheckEncoding((1LL << i) + 1);
    CheckEncoding((1LL << i) - 1);
  }
  CheckEncoding(numeric_limits<int64_t>::max());
  CheckEncoding(numeric_limits<int64_t>::max() - 1);
  CheckEncoding(numeric_limits<int64_t>::min());
  CheckEncoding(numeric_limits<int64_t>::min() + 1);

  for (int i = 0; i < 10000; ++i) {
    int64_t v = static_cast<int64_t>(rng.Next64());
    for (int m = 0; m < 2; ++m, v = -v) {
      CheckEncoding(v);
    }
  }

  for (int i = -1000; i <= 1000; ++i) {
    CheckEncoding(i);
  }

  ASSERT_EQ(BINARY_STRING("\x80"), FastEncodeSignedVarIntToStr(0));
  ASSERT_EQ(BINARY_STRING("\x81"), FastEncodeSignedVarIntToStr(1));
  // Many people don't know that "~" is \x7e. Did you know that? Very interesting!
  ASSERT_EQ(BINARY_STRING("~"), FastEncodeSignedVarIntToStr(-1));
  ASSERT_EQ(BINARY_STRING("\xc0\x40"), FastEncodeSignedVarIntToStr(64));
  ASSERT_EQ(BINARY_STRING("\xdf\xff"), FastEncodeSignedVarIntToStr(8191));
}

TEST(FastVarIntTest, TestEncodePerformance) {
  Random rng(SeedRandom());
  uint8_t buf[16];
  const MonoTime start_time = MonoTime::Now(MonoTime::Granularity::FINE);
  for (int i = 1; i < 63; ++i) {
    int64_t max_value = static_cast<int64_t>((1ull << i) - 1);
    for (int j = 0; j < 500000; ++j) {
      int64_t v = rng.Next64() % max_value + 1;
      int encoded_size = v;
      FastEncodeSignedVarInt(v, buf, &encoded_size);
    }
  }
  LOG(INFO) << "Elapsed time: "
            << MonoTime::Now(
                MonoTime::Granularity::FINE).GetDeltaSince(start_time).ToMilliseconds() << " ms";
}

TEST(FastVarIntTest, TestSignedPositiveVarIntLength) {
  ASSERT_EQ(1, SignedPositiveVarIntLength(0));
  ASSERT_EQ(1, SignedPositiveVarIntLength(63));
  ASSERT_EQ(2, SignedPositiveVarIntLength(64));

  int n_bits;
  int n_bytes;
  for (n_bits = 6, n_bytes = 1; n_bits <= 62; n_bits += 7, n_bytes += 1) {
    const int64_t max_with_this_n_bytes = (1LL << n_bits) - 1;
    ASSERT_EQ(n_bytes, SignedPositiveVarIntLength(max_with_this_n_bytes));
    ASSERT_EQ(n_bytes + 1, SignedPositiveVarIntLength(max_with_this_n_bytes + 1));
  }
}

TEST(FastVarIntTest, TestDecodeIncorrectValues) {
  int64_t v;
  int n;

  ASSERT_FALSE(FastDecodeVarInt("", 0, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("0", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("1", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("<", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("=", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(">", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(" ", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("-", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(",", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(";", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(":", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("!", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("?", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("/", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(".", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("'", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("(", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(")", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("$", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("*", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\"", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("&", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("#", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("%", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("+", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("2", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("3", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("4", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("5", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("6", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("7", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("8", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("9", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x00", 2, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x00\x00", 3, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x00\x00\x00", 4, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x00\x00\x00\x00", 5, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x00\x00\x00\x01", 5, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x00\x00\x01", 4, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x00\x01", 3, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x00\x01", 2, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x01", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x02", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x03", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x04", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x05", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x06", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x07", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x08", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x09", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x0a", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x0b", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x0c", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x0d", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x0e", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x0f", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x10", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x11", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x12", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x13", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x14", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x15", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x16", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x17", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x18", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x19", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x1a", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x1b", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x1c", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x1d", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x1e", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\x1f", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc0", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc1", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc2", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc3", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc4", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc5", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc6", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc7", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc8", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xc9", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xca", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xcb", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xcc", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xcd", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xce", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xcf", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd0", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd1", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd2", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd3", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd4", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd5", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd6", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd7", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd8", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xd9", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xda", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xdb", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xdc", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xdd", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xde", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xdf", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe0", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe1", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe2", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe3", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe4", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe5", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe6", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe7", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe8", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xe9", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xea", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xeb", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xec", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xed", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xee", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xef", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf0", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf1", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf2", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf3", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf4", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf5", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf6", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf7", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf8", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xf9", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xfa", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xfb", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xfc", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xfd", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xfe", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xff", 1, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xff\xff", 2, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xff\xff\xff", 3, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xff\xff\xff\xff", 4, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xff\xff\xff\xff\xff", 5, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt("\xff\xff\xff\xff\xff\xff", 6, &v, &n).ok());
  ASSERT_FALSE(FastDecodeVarInt(BINARY_STRING("\x00\x00\x00\x00\x00+g!J"), &v, &n).ok());
}

}  // namespace util
}  // namespace yb
