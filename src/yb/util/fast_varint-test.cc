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

#include <iostream>

#include "yb/gutil/strings/substitute.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/cast.h"
#include "yb/util/fast_varint.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/varint.h"

using namespace std::literals;
using strings::Substitute;
using yb::util::FormatBytesAsStr;

namespace yb {
namespace util {

namespace {

void CheckEncoding(int64_t v) {
  SCOPED_TRACE(Substitute("v=$0", v));

  const string correct_encoded(VarInt(v).EncodeToComparable(true, 0));
  uint8_t buf[16];
  size_t encoded_size = 0;

  FastEncodeSignedVarInt(v, buf, &encoded_size);
  ASSERT_EQ(correct_encoded.size(), encoded_size);
  ASSERT_EQ(correct_encoded, string(to_char_ptr(buf), encoded_size));
  int64_t decoded = 0;
  int decoded_size = 0;
  ASSERT_OK(FastDecodeSignedVarInt(correct_encoded, &decoded, &decoded_size));
  ASSERT_EQ(v, decoded);
  ASSERT_EQ(decoded_size, correct_encoded.size());

  {
    // Provide a way to generate examples for checking value validity during decoding. These could
    // be copied and pasted into TestDecodeIncorrectValues.
    int64_t unused_decoded_value ATTRIBUTE_UNUSED;
    int unused_decoded_size ATTRIBUTE_UNUSED;

    constexpr bool kGenerateInvalidDecodingExamples = false;
    if (kGenerateInvalidDecodingExamples &&
        !FastDecodeSignedVarInt(
            buf + 1, encoded_size - 1, &unused_decoded_value, &unused_decoded_size).ok()) {
      std::cout << "ASSERT_FALSE(FastDecodeSignedVarInt("
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
    FastEncodeDescendingSignedVarInt(-v, &encoded_dest);
    const int encoded_size = encoded_dest.size() - kPrefix.size();
    ASSERT_EQ(correct_encoded,
              encoded_dest.substr(kPrefix.size(), encoded_size));

    Slice slice_for_decoding(encoded_dest.c_str() + kPrefix.size(), encoded_size);
    int64_t decoded_value = 0;
    ASSERT_OK(FastDecodeDescendingSignedVarInt(&slice_for_decoding, &decoded_value));
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

template<class T>
std::vector<T> GenerateRandomValues() {
  std::mt19937_64 rng(123456);
  const int kMaxLength = 62;
  const int kValuesPerLength = 500000;
  std::vector<T> values;
  values.reserve(kMaxLength * kValuesPerLength);
  for (int i = 1; i <= kMaxLength; ++i) {
    uint64_t max_value = (1ull << i) - 1;
    std::uniform_int_distribution<T> distribution(0, max_value);
    for (int j = 0; j < kValuesPerLength; ++j) {
      values.push_back(distribution(rng));
    }
  }
  return values;
}

TEST(FastVarIntTest, TestEncodePerformance) {
  const std::vector<int64_t> values = GenerateRandomValues<int64_t>();

  uint8_t buf[kMaxVarIntBufferSize];
  size_t encoded_size = 0;
  std::clock_t start_time = std::clock();
  for (auto value : values) {
    FastEncodeSignedVarInt(value, buf, &encoded_size);
  }
  std::clock_t end_time = std::clock();
  LOG(INFO) << std::fixed << std::setprecision(2) << "CPU time used: "
            << 1000.0 * (end_time - start_time) / CLOCKS_PER_SEC << " ms\n";
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

const std::vector<std::string>& IncorrectValues() {
  static std::vector<std::string> result = {
      ""s,
      "0"s,
      "1"s,
      "<"s,
      "="s,
      ">"s,
      " "s,
      "-"s,
      ","s,
      ";"s,
      ":"s,
      "!"s,
      "?"s,
      "/"s,
      "."s,
      "'"s,
      "("s,
      ")"s,
      "$"s,
      "*"s,
      "\""s,
      "&"s,
      "#"s,
      "%"s,
      "+"s,
      "2"s,
      "3"s,
      "4"s,
      "5"s,
      "6"s,
      "7"s,
      "8"s,
      "9"s,
      "\x00"s,
      "\x00\x00"s,
      "\x00\x00\x00"s,
      "\x00\x00\x00\x00"s,
      "\x00\x00\x00\x00\x00"s,
      "\x00\x00\x00\x00\x01"s,
      "\x00\x00\x00\x01"s,
      "\x00\x00\x01"s,
      "\x00\x01"s,
      "\x01"s,
      "\x02"s,
      "\x03"s,
      "\x04"s,
      "\x05"s,
      "\x06"s,
      "\x07"s,
      "\x08"s,
      "\x09"s,
      "\x0a"s,
      "\x0b"s,
      "\x0c"s,
      "\x0d"s,
      "\x0e"s,
      "\x0f"s,
      "\x10"s,
      "\x11"s,
      "\x12"s,
      "\x13"s,
      "\x14"s,
      "\x15"s,
      "\x16"s,
      "\x17"s,
      "\x18"s,
      "\x19"s,
      "\x1a"s,
      "\x1b"s,
      "\x1c"s,
      "\x1d"s,
      "\x1e"s,
      "\x1f"s,
      "\xc0"s,
      "\xc1"s,
      "\xc2"s,
      "\xc3"s,
      "\xc4"s,
      "\xc5"s,
      "\xc6"s,
      "\xc7"s,
      "\xc8"s,
      "\xc9"s,
      "\xca"s,
      "\xcb"s,
      "\xcc"s,
      "\xcd"s,
      "\xce"s,
      "\xcf"s,
      "\xd0"s,
      "\xd1"s,
      "\xd2"s,
      "\xd3"s,
      "\xd4"s,
      "\xd5"s,
      "\xd6"s,
      "\xd7"s,
      "\xd8"s,
      "\xd9"s,
      "\xda"s,
      "\xdb"s,
      "\xdc"s,
      "\xdd"s,
      "\xde"s,
      "\xdf"s,
      "\xe0"s,
      "\xe1"s,
      "\xe2"s,
      "\xe3"s,
      "\xe4"s,
      "\xe5"s,
      "\xe6"s,
      "\xe7"s,
      "\xe8"s,
      "\xe9"s,
      "\xea"s,
      "\xeb"s,
      "\xec"s,
      "\xed"s,
      "\xee"s,
      "\xef"s,
      "\xf0"s,
      "\xf1"s,
      "\xf2"s,
      "\xf3"s,
      "\xf4"s,
      "\xf5"s,
      "\xf6"s,
      "\xf7"s,
      "\xf8"s,
      "\xf9"s,
      "\xfa"s,
      "\xfb"s,
      "\xfc"s,
      "\xfd"s,
      "\xfe"s,
      "\xff"s,
      "\xff\xff"s,
      "\xff\xff\xff"s,
      "\xff\xff\xff\xff"s,
      "\xff\xff\xff\xff\xff"s,
      "\xff\xff\xff\xff\xff\xff"s,
      "\x00\x00\x00\x00\x00+g!J"s
  };
  return result;
}

TEST(FastVarIntTest, TestDecodeIncorrectValues) {
  int64_t v;
  int n;
  const auto& incorrect_values = IncorrectValues();
  for (const auto& value : incorrect_values) {
    ASSERT_NOK(FastDecodeSignedVarInt(value, &v, &n))
        << "Input: " << Slice(value).ToDebugHexString();
  }
}

void CheckUnsignedEncoding(uint64_t value) {
  uint8_t buf[kMaxVarIntBufferSize];
  size_t size = 0;
  FastEncodeUnsignedVarInt(value, buf, &size);
  uint64_t decoded_value;
  size_t decoded_size = 0;
  ASSERT_OK(FastDecodeUnsignedVarInt(buf, size, &decoded_value, &decoded_size));
  ASSERT_EQ(value, decoded_value);
  ASSERT_EQ(size, decoded_size) << "Value is: " << value;
}

TEST(FastVarIntTest, Unsigned) {
  std::mt19937_64 rng(123456);
  const int kTotalValues = 1000000;

  std::vector<uint64_t> values(kTotalValues);
  std::vector<uint8_t> big_buffer(kTotalValues * kMaxVarIntBufferSize);
  std::vector<Slice> encoded_values(kTotalValues);

  VarInt varint;
  for (int i = 0; i != kTotalValues; ++i) {
    uint64_t len = std::uniform_int_distribution<uint64_t>(1, 10)(rng);
    uint64_t max_value = len == 10 ? std::numeric_limits<uint64_t>::max() : (1ULL << (7 * len)) - 1;
    uint64_t value = std::uniform_int_distribution<uint64_t>(0, max_value)(rng);
    values[i] = value;
    uint8_t* buf = big_buffer.data() + kMaxVarIntBufferSize * i;
    size_t size = 0;
    FastEncodeUnsignedVarInt(value, buf, &size);
    encoded_values[i] = Slice(buf, size);
    uint64_t decoded_value;
    size_t decoded_size = 0;
    ASSERT_OK(FastDecodeUnsignedVarInt(buf, size, &decoded_value, &decoded_size));
    ASSERT_EQ(value, decoded_value);
    ASSERT_EQ(size, decoded_size) << "Value is: " << value;

    varint.FromUInt64(value);
    auto encoded_varint = varint.EncodeToComparable(false, 0);
    ASSERT_EQ(encoded_varint, std::string(buf, buf + size)) << "Value is: " << value;
  }
  CheckUnsignedEncoding(numeric_limits<uint64_t>::max());
  CheckUnsignedEncoding(numeric_limits<uint64_t>::max() - 1);

  std::sort(values.begin(), values.end());
  auto compare_slices = [](const Slice& lhs, const Slice& rhs) {
    return lhs.compare(rhs) < 0;
  };
  std::sort(encoded_values.begin(), encoded_values.end(), compare_slices);
  for (size_t i = 0; i != kTotalValues; ++i) {
    auto decoded_value = FastDecodeUnsignedVarInt(encoded_values[i]);
    ASSERT_OK(decoded_value);
    ASSERT_EQ(values[i], *decoded_value);
  }
}

TEST(FastVarIntTest, DecodeUnsignedIncorrect) {
  const auto& incorrect_values = IncorrectValues();
  for (const auto& value : incorrect_values) {
    // Values with leading zero bit are correctly decoded in unsigned mode.
    if (value.size() == 1 && (value[0] & 0x80) == 0) {
      continue;
    }
    ASSERT_NOK(FastDecodeUnsignedVarInt(value)) << "Input: " << Slice(value).ToDebugHexString();
  }
}

TEST(FastVarIntTest, EncodeUnsignedPerformance) {
  const std::vector<uint64_t> values = GenerateRandomValues<uint64_t>();

  uint8_t buf[kMaxVarIntBufferSize];
  size_t encoded_size = 0;
  std::clock_t start_time = std::clock();
  for (auto value : values) {
    FastEncodeUnsignedVarInt(value, buf, &encoded_size);
  }
  std::clock_t end_time = std::clock();
  LOG(INFO) << std::fixed << std::setprecision(2) << "CPU time used: "
            << 1000.0 * (end_time - start_time) / CLOCKS_PER_SEC << " ms\n";
}

}  // namespace util
}  // namespace yb
