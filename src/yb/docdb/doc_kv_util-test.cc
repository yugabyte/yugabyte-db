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

#include "yb/docdb/doc_kv_util.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/value.h"

#include "yb/rocksdb/util/random.h"

#include "yb/util/monotime.h"
#include "yb/util/slice.h"
#include "yb/util/test_macros.h"

using namespace std::literals;
using std::string;

namespace yb {
namespace docdb {

TEST(DocKVUtilTest, KeyBelongsToDocKeyInTest) {
  string actual_key = "mydockey";
  actual_key.push_back('\x0');
  const string actual_key_with_one_zero = actual_key;
  actual_key.push_back('\x0');
  ASSERT_TRUE(KeyBelongsToDocKeyInTest(rocksdb::Slice(actual_key), "mydockey"));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(rocksdb::Slice(actual_key_with_one_zero), "mydockey"));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(rocksdb::Slice("mydockey"), "mydockey"));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(rocksdb::Slice(""), ""));

  string just_two_zeros;
  just_two_zeros.push_back('\x0');
  just_two_zeros.push_back('\x0');
  ASSERT_TRUE(KeyBelongsToDocKeyInTest(rocksdb::Slice(just_two_zeros), ""));
  ASSERT_FALSE(KeyBelongsToDocKeyInTest(rocksdb::Slice(just_two_zeros), just_two_zeros));
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
        rocksdb::Slice slice(buf);
        DocHybridTime decoded_ht;
        ASSERT_OK(DecodeHybridTimeFromEndOfKey(slice, &decoded_ht));
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

TEST(DocKVUtilTest, ZeroEncodingAndDecoding) {
  rocksdb::Random rng(12345); // initialize with a fixed seed
  for (int i = 0; i < 1000; ++i) {
    int len = rng.Next() % 200;
    string s;
    s.reserve(len);
    for (int j = 0; j < len; ++j) {
      s.push_back(static_cast<char>(rng.Next()));
    }
    string encoded_str = ZeroEncodeStr(s);
    size_t expected_size_when_no_zeros = s.size() + kEncodedKeyStrTerminatorSize;
    if (s.find('\0') == string::npos) {
      ASSERT_EQ(expected_size_when_no_zeros, encoded_str.size());
    } else {
      ASSERT_LT(expected_size_when_no_zeros, encoded_str.size());
    }
    string decoded_str = DecodeZeroEncodedStr(encoded_str);
    ASSERT_EQ(s, decoded_str);
  }
}

TEST(DocKVUtilTest, TableTTL) {
  Schema schema;
  EXPECT_TRUE(TableTTL(schema).Equals(Value::kMaxTtl));

  schema.SetDefaultTimeToLive(1000);
  EXPECT_TRUE(MonoDelta::FromMilliseconds(1000).Equals(TableTTL(schema)));
}

TEST(DocKVUtilTest, ComputeTTL) {
  Schema schema;
  schema.SetDefaultTimeToLive(1000);

  MonoDelta value_ttl = MonoDelta::FromMilliseconds(2000);

  EXPECT_TRUE(MonoDelta::FromMilliseconds(2000).Equals(ComputeTTL(value_ttl, schema)));
  EXPECT_TRUE(MonoDelta::FromMilliseconds(1000).Equals(ComputeTTL(Value::kMaxTtl, schema)));

  MonoDelta reset_ttl = MonoDelta::FromMilliseconds(0);
  EXPECT_TRUE(ComputeTTL(reset_ttl, schema).Equals(Value::kMaxTtl));
}

TEST(DocKVUtilTest, FileExpirationFromValueTTL) {
  HybridTime key_hybrid_time = 1000_usec_ht;

  MonoDelta value_ttl = Value::kMaxTtl;
  EXPECT_EQ(FileExpirationFromValueTTL(key_hybrid_time, value_ttl), kUseDefaultTTL);

  value_ttl = Value::kResetTtl;
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
  table_ttl = Value::kMaxTtl;
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
  for (int i = 0; i < numbers.size(); i++) {
    string s;
    util::AppendFloatToKey(numbers[i], &s);
    strings.push_back(s);
    EXPECT_EQ(numbers[i], util::DecodeFloatFromKey(rocksdb::Slice(s)));
  }
  for (int i = 1; i < numbers.size(); i++) {
    EXPECT_LT(strings[i-1], strings[i]);
  }
}

TEST(DocKVUtilTest, DoubleEncoding) {
  vector<double> numbers = {-123.45f, -0.00123f, -0.0f, 0.0f, 0.00123f, 123.45f};
  vector<string> strings;
  for (int i = 0; i < numbers.size(); i++) {
    string s;
    util::AppendDoubleToKey(numbers[i], &s);
    strings.push_back(s);
    EXPECT_EQ(numbers[i], util::DecodeDoubleFromKey(rocksdb::Slice(s)));
  }
  for (int i = 1; i < numbers.size(); i++) {
    EXPECT_LT(strings[i-1], strings[i]);
  }
}

TEST(DocKVUtilTest, UInt64Encoding) {
  vector<uint64_t> numbers = {0, 1, 100, 9223372036854775807ULL, 18446744073709551615ULL};
  vector<string> strings;
  for (int i = 0; i < numbers.size(); i++) {
    string s;
    AppendUInt64ToKey(numbers[i], &s);
    strings.push_back(s);
    EXPECT_EQ(numbers[i], BigEndian::Load64(s.c_str()));
  }
  for (int i = 1; i < numbers.size(); i++) {
    EXPECT_LT(strings[i-1], strings[i]);
  }
}

}  // namespace docdb
}  // namespace yb
