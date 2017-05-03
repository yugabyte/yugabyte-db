// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_kv_util.h"

#include <string>

#include "yb/docdb/value.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/bytes_formatter.h"

#include "rocksdb/slice.h"
#include "rocksdb/util/random.h"

using std::string;
using yb::util::FormatBytesAsStr;

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
  string buf = "a";
  AppendZeroEncodedStrToKey("bc", &buf);
  ASSERT_EQ("abc", buf);
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
  string buf = "a";
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

}  // namespace docdb
}  // namespace yb
