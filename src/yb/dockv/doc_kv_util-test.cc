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

#include <string>

#include <gtest/gtest.h>

#include "yb/common/schema.h"

#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/doc_ttl_util.h"
#include "yb/dockv/value.h"

#include "yb/rocksdb/util/random.h"

#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/slice.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;
using namespace yb::size_literals;
using std::string;
using std::vector;

namespace yb::dockv {

TEST(DocKVUtilTest, KeyBelongsToDocKeyInTest) {
  string actual_key = "mydockey";
  actual_key.push_back('\x0');
  const string actual_key_with_one_zero = actual_key;
  actual_key.push_back('\x0');
  ASSERT_TRUE(KeyBelongsToDocKeyInTest(Slice(actual_key), "mydockey"));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(Slice(actual_key_with_one_zero), "mydockey"));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(Slice("mydockey"), "mydockey"));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(Slice(""), ""));

  string just_two_zeros;
  just_two_zeros.push_back('\x0');
  just_two_zeros.push_back('\x0');
  ASSERT_TRUE(KeyBelongsToDocKeyInTest(Slice(just_two_zeros), ""));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(Slice(just_two_zeros), just_two_zeros));
}

TEST(DocKVUtilTest, EncodeAndDecodeHybridTimeInKey) {
  string initial_str;
  HybridTimeRepr cur_ht_value = 0;
  for (int i = 0; i < 10; ++i) {
    initial_str.push_back('a');
    string buf = initial_str;
    static constexpr int kNumHTValuesToTry = 10;
    for (int j = 0; j < kNumHTValuesToTry ; ++j) {
      static constexpr int kNumWriteIdsToTry = 10;
      for (int k = 0; k < kNumWriteIdsToTry; ++k) {
        const auto write_id = std::numeric_limits<IntraTxnWriteId>::max() / kNumWriteIdsToTry * k;
        const auto htw = DocHybridTime(HybridTime(cur_ht_value), write_id);
        htw.AppendEncodedInDocDbFormat(&buf);
        Slice slice(buf);
        DocHybridTime decoded_ht = ASSERT_RESULT(DocHybridTime::DecodeFromEnd(slice));
        ASSERT_EQ(htw, decoded_ht);
      }
      cur_ht_value += std::numeric_limits<uint64_t>::max() / kNumHTValuesToTry;
    }
  }
}

TEST(DocKVUtilTest, AppendZeroEncodedStrToKey) {
  KeyBuffer buf("a"s);
  AppendZeroEncodedStrToKey("bc", &buf);
  ASSERT_EQ("abc", buf.ToStringBuffer());
  string str_with_embedded_zeros = "f";
  str_with_embedded_zeros.push_back('\x0');
  str_with_embedded_zeros.push_back('g');
  AppendZeroEncodedStrToKey(str_with_embedded_zeros, &buf);
  ASSERT_EQ(7, buf.size());
  ASSERT_EQ('f', buf[3]);
  ASSERT_EQ('\x00', buf[4]);
  ASSERT_EQ('\x01', buf[5]);
  ASSERT_EQ('g', buf[6]);
}

TEST(DocKVUtilTest, TerminateZeroEncodedKeyStr) {
  KeyBuffer buf("a"s);
  TerminateZeroEncodedKeyStr(&buf);
  ASSERT_EQ(3, buf.size());
  ASSERT_EQ('a', buf[0]);
  ASSERT_EQ('\x0', buf[1]);
  ASSERT_EQ('\x0', buf[2]);
}

namespace {

template <char END_OF_STRING>
void NaiveAppendEncodedStr(const string &s, std::string *dest) {
  static_assert(END_OF_STRING == '\0' || END_OF_STRING == '\xff',
                "Only characters 0x00 and 0xff allowed as a template parameter");
  if (END_OF_STRING == '\0' && s.find('\0') == string::npos) {
    // Fast path: no zero characters, nothing to encode.
    dest->append(s);
  } else {
    for (char c : s) {
      if (c == '\0') {
        dest->push_back(END_OF_STRING);
        dest->push_back(END_OF_STRING ^ 1);
      } else {
        dest->push_back(END_OF_STRING ^ c);
      }
    }
  }
  dest->push_back(END_OF_STRING);
  dest->push_back(END_OF_STRING);
}

std::string NaiveZeroEncodeStr(const string &s) {
  std::string result;
  NaiveAppendEncodedStr<'\0'>(s, &result);
  return result;
}

std::string NaiveComplementZeroEncodeStr(const string &s) {
  std::string result;
  NaiveAppendEncodedStr<'\xff'>(s, &result);
  return result;
}

template<char END_OF_STRING>
Status NaiveDecodeEncodedStr(Slice* slice, string* result) {
  static_assert(END_OF_STRING == '\0' || END_OF_STRING == '\xff',
                "Invalid END_OF_STRING character. Only 0x00 and 0xff accepted");
  constexpr char END_OF_STRING_ESCAPE = END_OF_STRING ^ 1;
  const char* p = slice->cdata();
  const char* end = p + slice->size();

  while (p != end) {
    if (*p == END_OF_STRING) {
      ++p;
      if (p == end) {
        return STATUS(Corruption, StringPrintf("Encoded string ends with only one \\0x%02x ",
                                               END_OF_STRING));
      }
      if (*p == END_OF_STRING) {
        // Found two END_OF_STRING characters, this is the end of the encoded string.
        ++p;
        break;
      }
      if (*p == END_OF_STRING_ESCAPE) {
        // 0 is encoded as 00 01 in ascending encoding and FF FE in descending encoding.
        if (result != nullptr) {
          result->push_back(0);
        }
        ++p;
      } else {
        return STATUS(Corruption, StringPrintf(
            "Invalid sequence in encoded string: "
            R"#(\0x%02x\0x%02x (must be either \0x%02x\0x%02x or \0x%02x\0x%02x))#",
            END_OF_STRING, *p, END_OF_STRING, END_OF_STRING, END_OF_STRING, END_OF_STRING_ESCAPE));
      }
    } else {
      if (result != nullptr) {
        result->push_back((*p) ^ END_OF_STRING);
      }
      ++p;
    }
  }
  if (result != nullptr) {
    result->shrink_to_fit();
  }
  slice->remove_prefix(p - slice->cdata());
  return Status::OK();
}

std::string NaiveDecodeComplementZeroEncodedStr(Slice slice) {
  std::string result;
  CHECK_OK(NaiveDecodeEncodedStr<'\xff'>(&slice, &result));
  return result;
}

std::string NaiveDecodeZeroEncodedStr(Slice slice) {
  std::string result;
  CHECK_OK(NaiveDecodeEncodedStr<'\0'>(&slice, &result));
  return result;
}

} // namespace

template <class Encoder, class Decoder, class NaiveEncoder, class NaiveDecoder, class Cmp>
void TestStringCoding(
    const Encoder& encoder, const Decoder& decoder, const NaiveEncoder& naive_encoder,
    const NaiveDecoder& naive_decoder, const Cmp& cmp) {
  std::mt19937_64 rng(12345); // initialize with a fixed seed
  std::vector<std::string> strings;
  for (int i = 0; i < 1000; ++i) {
    strings.push_back(RandomString(RandomUniformInt(0, 200, &rng), &rng));
  }

  strings.push_back("a");
  strings.push_back("aa");

  size_t kLen = 4;
  for (size_t mask = 0; mask != 1ULL << kLen; ++mask) {
    std::string str;
    for (size_t i = 0; i != kLen; ++i) {
      if (mask & (1 << i)) {
        str.push_back(0);
      } else {
        str.push_back(RandomUniformInt(1, 0xff, &rng));
      }
    }
    strings.push_back(str);
  }

  std::sort(strings.begin(), strings.end(), cmp);
  std::string prev;
  for (const auto& str : strings) {
    auto encoded_str = encoder(str);
    ASSERT_EQ(encoded_str, naive_encoder(str));
    if (!prev.empty()) {
      ASSERT_LE(prev, encoded_str);
    }
    prev = encoded_str;

    size_t expected_size =
        str.size() + kEncodedKeyStrTerminatorSize + std::count(str.begin(), str.end(), '\0');

    ASSERT_EQ(expected_size, encoded_str.size());
    auto decoded_str = ASSERT_RESULT(decoder(encoded_str));
    ASSERT_EQ(str, decoded_str);
    ASSERT_EQ(decoded_str, naive_decoder(encoded_str));
  }
}

TEST(DocKVUtilTest, ZeroEncodingAndDecoding) {
  TestStringCoding(
      &ZeroEncodeStr, [](const auto& s) {
        return DecodeZeroEncodedStr(s);
      },
      &NaiveZeroEncodeStr, &NaiveDecodeZeroEncodedStr, std::less<void>()
  );
}

TEST(DocKVUtilTest, ComplementZeroEncodingAndDecoding) {
  TestStringCoding(
      &ComplementZeroEncodeStr, [](const auto& s) {
        return DecodeComplementZeroEncodedStr(s);
      },
      &NaiveComplementZeroEncodeStr, &NaiveDecodeComplementZeroEncodedStr, std::greater<void>()
  );
}

TEST(DocKVUtilTest, DecodeZeroEncodingMalformed) {
  // no escape character at all
  EXPECT_NOK(DecodeZeroEncodedStr(""s));
  EXPECT_NOK(DecodeZeroEncodedStr("abc"s));

  // string is not terminated
  EXPECT_NOK(DecodeZeroEncodedStr("\0\1 abc"s));

  // escape character followed by end of string
  EXPECT_NOK(DecodeZeroEncodedStr("\0"s));
  EXPECT_NOK(DecodeZeroEncodedStr("abc\0"s));
  EXPECT_NOK(DecodeZeroEncodedStr("\0\1 abc\0"s));

  // escape character followed by incorrect character
  EXPECT_NOK(DecodeZeroEncodedStr("\0\2 abc\0\0"s));

  // reading past end of Slice
  EXPECT_NOK(DecodeZeroEncodedStr(Slice("\0\0", 1)));
  EXPECT_NOK(DecodeZeroEncodedStr(Slice("\0\1", 1)));
  EXPECT_NOK(DecodeZeroEncodedStr(Slice("\0\2", 1)));
}

TEST(DocKVUtilTest, DecodeComplementZeroEncodingMalformed) {
  // no escape character at all
  EXPECT_NOK(DecodeComplementZeroEncodedStr(""s));
  EXPECT_NOK(DecodeComplementZeroEncodedStr("abc"s));

  // string is not terminated
  EXPECT_NOK(DecodeComplementZeroEncodedStr("\xff\xfe abc"s));

  // escape character followed by end of string
  EXPECT_NOK(DecodeComplementZeroEncodedStr("\xff"s));
  EXPECT_NOK(DecodeComplementZeroEncodedStr("abc\xff"s));
  EXPECT_NOK(DecodeComplementZeroEncodedStr("\xff\xfe abc\xff"s));

  // escape character followed by incorrect character
  EXPECT_NOK(DecodeComplementZeroEncodedStr("\xff\2 abc\xff\xff"s));

  // reading past end of Slice
  EXPECT_NOK(DecodeComplementZeroEncodedStr(Slice("\xff\xff", 1)));
  EXPECT_NOK(DecodeComplementZeroEncodedStr(Slice("\xff\xfe", 1)));
  EXPECT_NOK(DecodeComplementZeroEncodedStr(Slice("\xff\2", 1)));
}

template <class Generator, class Coder>
void TestStringCodingPerf(const Generator& generator, const Coder& coder) {
  constexpr size_t kNumStrings = 10000;
  constexpr size_t kNumIterations = RegularBuildVsDebugVsSanitizers(100, 10, 1);
  constexpr size_t kStringLength = 1_KB;
  std::vector<std::string> strings;
  strings.reserve(kNumStrings);
  for (size_t i = 0; i != kNumStrings; ++i) {
    strings.push_back(generator(kStringLength));
  }

  auto start = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i != kNumIterations; ++i) {
    for (const auto& str : strings) {
      coder(str);
    }
  }
  auto stop = std::chrono::high_resolution_clock::now();

  LOG(INFO) << "Time taken: " << MonoDelta(stop - start).ToString();
}

TEST(DocKVUtilTest, ZeroDecodingPerf) {
  std::string buffer;
  TestStringCodingPerf(
      [](size_t len) {
        return ZeroEncodeStr(RandomString(len));
      },
      [&buffer](Slice slice) {
        buffer.clear();
        ASSERT_OK_FAST(DecodeZeroEncodedStr(&slice, &buffer));
      }
  );
}

TEST(DocKVUtilTest, ZeroEncodingPerf) {
  KeyBuffer buffer;
  TestStringCodingPerf(
      [](size_t len) {
        return RandomString(len);
      },
      [&buffer](const Slice& str) {
        buffer.clear();
        AppendZeroEncodedStrToKey(str, &buffer);
      }
  );
}

TEST(DocKVUtilTest, ComplementZeroDecodingPerf) {
  std::string buffer;
  TestStringCodingPerf(
      [](size_t len) {
        return ComplementZeroEncodeStr(RandomString(len));
      },
      [&buffer](Slice slice) {
        buffer.clear();
        ASSERT_OK_FAST(DecodeComplementZeroEncodedStr(&slice, &buffer));
      }
  );
}

TEST(DocKVUtilTest, ComplementZeroEncodingPerf) {
  KeyBuffer buffer;
  TestStringCodingPerf(
      [](size_t len) {
        return RandomString(len);
      },
      [&buffer](const Slice& str) {
        buffer.clear();
        AppendComplementZeroEncodedStrToKey(str, &buffer);
      }
  );
}

TEST(DocKVUtilTest, TableTTL) {
  Schema schema;
  EXPECT_TRUE(TableTTL(schema).Equals(ValueControlFields::kMaxTtl));

  schema.SetDefaultTimeToLive(1000);
  EXPECT_TRUE(MonoDelta::FromMilliseconds(1000).Equals(TableTTL(schema)));
}

TEST(DocKVUtilTest, ComputeTTL) {
  Schema schema;
  schema.SetDefaultTimeToLive(1000);

  MonoDelta value_ttl = MonoDelta::FromMilliseconds(2000);

  EXPECT_TRUE(MonoDelta::FromMilliseconds(2000).Equals(ComputeTTL(value_ttl, schema)));
  EXPECT_TRUE(MonoDelta::FromMilliseconds(1000).Equals(
      ComputeTTL(ValueControlFields::kMaxTtl, schema)));

  MonoDelta reset_ttl = MonoDelta::FromMilliseconds(0);
  EXPECT_TRUE(ComputeTTL(reset_ttl, schema).Equals(ValueControlFields::kMaxTtl));
}

TEST(DocKVUtilTest, FileExpirationFromValueTTL) {
  HybridTime key_hybrid_time = 1000_usec_ht;

  MonoDelta value_ttl = ValueControlFields::kMaxTtl;
  EXPECT_EQ(FileExpirationFromValueTTL(key_hybrid_time, value_ttl), kUseDefaultTTL);

  value_ttl = ValueControlFields::kResetTtl;
  EXPECT_EQ(FileExpirationFromValueTTL(key_hybrid_time, value_ttl), kNoExpiration);

  value_ttl = MonoDelta::FromMicroseconds(1000);
  EXPECT_EQ(FileExpirationFromValueTTL(key_hybrid_time, value_ttl), 2000_usec_ht);

  key_hybrid_time = HybridTime::kInvalid;
  EXPECT_EQ(FileExpirationFromValueTTL(key_hybrid_time, value_ttl), kNoExpiration);

  key_hybrid_time = HybridTime::kMax;
  EXPECT_EQ(FileExpirationFromValueTTL(key_hybrid_time, value_ttl), kNoExpiration);
}

TEST(DocKVUtilTest, MaxExpirationFromValueAndTableTTL) {
  HybridTime key_ht = 1000_usec_ht;
  MonoDelta table_ttl;
  HybridTime val_expiry_ht;

  val_expiry_ht = 2000_usec_ht;
  table_ttl = ValueControlFields::kMaxTtl;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), val_expiry_ht);
  val_expiry_ht = kUseDefaultTTL;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), kNoExpiration);
  val_expiry_ht = kNoExpiration;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), kNoExpiration);

  table_ttl = MonoDelta::FromMicroseconds(500);
  val_expiry_ht = 2000_usec_ht;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), val_expiry_ht);
  val_expiry_ht = 1100_usec_ht;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), 1500_usec_ht);
  val_expiry_ht = kNoExpiration;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), kNoExpiration);
  val_expiry_ht = kUseDefaultTTL;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), 1500_usec_ht);

  key_ht = HybridTime::kMax;
  val_expiry_ht = 2000_usec_ht;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), kNoExpiration);
  key_ht = HybridTime::kInvalid;
  EXPECT_EQ(MaxExpirationFromValueAndTableTTL(key_ht, table_ttl, val_expiry_ht), kNoExpiration);
}

TEST(DocKVUtilTest, FloatEncoding) {
  vector<float> numbers = {-123.45f, -0.00123f, -0.0f, 0.0f, 0.00123f, 123.45f};
  vector<string> strings;
  for (size_t i = 0; i < numbers.size(); i++) {
    string s;
    util::AppendFloatToKey(numbers[i], &s);
    strings.push_back(s);
    EXPECT_EQ(numbers[i], util::DecodeFloatFromKey(Slice(s)));
  }
  for (size_t i = 1; i < numbers.size(); i++) {
    EXPECT_LT(strings[i-1], strings[i]);
  }
}

TEST(DocKVUtilTest, DoubleEncoding) {
  vector<double> numbers = {-123.45f, -0.00123f, -0.0f, 0.0f, 0.00123f, 123.45f};
  vector<string> strings;
  for (size_t i = 0; i < numbers.size(); i++) {
    string s;
    util::AppendDoubleToKey(numbers[i], &s);
    strings.push_back(s);
    EXPECT_EQ(numbers[i], util::DecodeDoubleFromKey(Slice(s)));
  }
  for (size_t i = 1; i < numbers.size(); i++) {
    EXPECT_LT(strings[i-1], strings[i]);
  }
}

TEST(DocKVUtilTest, UInt64Encoding) {
  vector<uint64_t> numbers = {0, 1, 100, 9223372036854775807ULL, 18446744073709551615ULL};
  vector<string> strings;
  for (size_t i = 0; i < numbers.size(); i++) {
    string s;
    AppendUInt64ToKey(numbers[i], &s);
    strings.push_back(s);
    EXPECT_EQ(numbers[i], BigEndian::Load64(s.c_str()));
  }
  for (size_t i = 1; i < numbers.size(); i++) {
    EXPECT_LT(strings[i-1], strings[i]);
  }
}

}  // namespace yb::dockv
