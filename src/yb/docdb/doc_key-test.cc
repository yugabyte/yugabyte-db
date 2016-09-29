// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_key.h"

#include <memory>

#include "yb/docdb/docdb_test_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::unique_ptr;
using strings::Substitute;
using yb::util::ApplyEagerLineContinuation;
using yb::util::FormatBytesAsStr;

static constexpr int kNumDocOrSubDocKeysPerBatch = 1000;
static constexpr int kNumTestDocOrSubDocKeyComparisons = 10000;

static_assert(kNumDocOrSubDocKeysPerBatch < kNumTestDocOrSubDocKeyComparisons,
              "Number of document/subdocument key pairs to compare must be greater than the "
              "number of random document/subdocument keys to choose from.");
static_assert(kNumTestDocOrSubDocKeyComparisons <
                  kNumDocOrSubDocKeysPerBatch * kNumDocOrSubDocKeysPerBatch,
              "Number of document/subdocument key pairs to compare must be less than the maximum "
              "theoretical number of such pairs given how many keys we generate to choose from.");

namespace yb {
namespace docdb {

namespace {

int Sign(int x) {
  if (x < 0) return -1;
  if (x > 0) return 1;
  return 0;
}

template<typename T>
std::vector<T> GenRandomDocOrSubDocKeys(RandomNumberGenerator* rng,
                                        bool use_hash,
                                        int num_keys);

template<>
std::vector<DocKey> GenRandomDocOrSubDocKeys<DocKey>(RandomNumberGenerator* rng,
                                                     bool use_hash,
                                                     int num_keys) {
  return GenRandomDocKeys(rng, use_hash, num_keys);
}

template<>
std::vector<SubDocKey> GenRandomDocOrSubDocKeys<SubDocKey>(RandomNumberGenerator* rng,
                                                           bool use_hash,
                                                           int num_keys) {
  return GenRandomSubDocKeys(rng, use_hash, num_keys);
}

template <typename DocOrSubDocKey>
void TestRoundTripDocOrSubDocKeyEncodingDecoding() {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  for (int use_hash = 0; use_hash <= 1; ++use_hash) {
    auto keys = GenRandomDocOrSubDocKeys<DocOrSubDocKey>(
        &rng, use_hash, kNumDocOrSubDocKeysPerBatch);
    for (const auto& key : keys) {
      KeyBytes encoded_key = key.Encode();
      DocOrSubDocKey decoded_key;
      ASSERT_OK(decoded_key.FullyDecodeFrom(encoded_key.AsSlice()));
      ASSERT_EQ(key, decoded_key);
      KeyBytes reencoded_doc_key = decoded_key.Encode();
      ASSERT_EQ(encoded_key.ToString(), reencoded_doc_key.ToString());
    }
  }
}

template <typename DocOrSubDocKey>
void TestDocOrSubDocKeyComparison() {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  for (int use_hash = 0; use_hash <= 1; ++use_hash) {
    auto keys = GenRandomDocOrSubDocKeys<DocOrSubDocKey>(
        &rng, use_hash, kNumDocOrSubDocKeysPerBatch);
    for (int k = 0; k < kNumTestDocOrSubDocKeyComparisons; ++k) {
      const auto& a = keys[rng() % keys.size()];
      const auto& b = keys[rng() % keys.size()];
      ASSERT_EQ(a == b, !(a != b));
      ASSERT_EQ(a == b, a.ToString() == b.ToString());

      const int object_comparison = a.CompareTo(b);
      const int reverse_object_comparison = b.CompareTo(a);

      const KeyBytes a_encoded = a.Encode();
      const KeyBytes b_encoded = b.Encode();
      const int encoded_comparison = a_encoded.CompareTo(b_encoded);
      const int reverse_encoded_comparison = b_encoded.CompareTo(a_encoded);

      ASSERT_EQ(Sign(object_comparison), Sign(encoded_comparison))
          << "Object comparison inconsistent with encoded byte sequence comparison:\n"
          << "a: " << a.ToString() << "\n"
          << "b: " << b.ToString() << "\n"
          << "a.Encode(): " << a.Encode().ToString() << "\n"
          << "b.Encode(): " << b.Encode().ToString() << "\n"
          << "a.CompareTo(b): " << object_comparison << "\n"
          << "a.Encode().CompareTo(b.Encode()): " << encoded_comparison;

      ASSERT_EQ(0, object_comparison + reverse_object_comparison);
      ASSERT_EQ(0, encoded_comparison + reverse_encoded_comparison);
    }
  }
}

}  // unnamed namespace

TEST(DocKeyTest, TestDocKeyToString) {
  ASSERT_EQ(
      "DocKey([], [10, \"foo\", 20, \"bar\"])",
      DocKey(PrimitiveValues(10, "foo", 20, "bar")).ToString());
  ASSERT_EQ(
      "DocKey(0x12345678, "
      "[\"hashed_key1\", 123, \"hashed_key2\", 234], [10, \"foo\", 20, \"bar\"])",
      DocKey(0x12345678,
             PrimitiveValues("hashed_key1", 123, "hashed_key2", 234),
             PrimitiveValues(10, "foo", 20, "bar")).ToString());
}

TEST(DocKeyTest, TestSubDocKeyToString) {
  ASSERT_EQ(
      "SubDocKey(DocKey([], [\"range_key1\", 1000, \"range_key_3\"]), [TS(12345)])",
      SubDocKey(DocKey(PrimitiveValues("range_key1", 1000, "range_key_3")),
                Timestamp(12345L)).ToString());
  ASSERT_EQ(
      "SubDocKey(DocKey([], [\"range_key1\", 1000, \"range_key_3\"]), "
      "[TS(12345), \"subkey1\", TS(20000)])",
      SubDocKey(
          DocKey(PrimitiveValues("range_key1", 1000, "range_key_3")),
          Timestamp(12345L), PrimitiveValue("subkey1"), Timestamp(20000L)
      ).ToString());

}

TEST(DocKeyTest, TestDocKeyEncoding) {
  // A few points to make it easier to understand the expected binary representations here:
  // - Initial bytes such as \x04, \x05 correspond to instances of the enum ValueType
  // - Strings are terminated with \x00\x00
  // - Groups of key components in the document key ("hashed" and "range" components) are separated
  //   terminated with another \x00.
  // - 64-bit signed integers are encoded using big-endian format with sign bit inverted.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#("\x04val1\x00\
               \x00\
               \x05\x80\x00\x00\x00\x00\x00\x03\xe8\
               \x04val2\x00\x00\
               \x05\x80\x00\x00\x00\x00\x00\x07\xd0\
               \x00")#"),
      FormatBytesAsStr(DocKey(PrimitiveValues("val1", 1000, "val2", 2000)).Encode().data()));

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#("\x09\
               \xca\xfeg\x89\
               \x04hashed1\x00\x00\
               \x04hashed2\x00\x00\
               \x00\
               \x04range1\x00\x00\
               \x05\x80\x00\x00\x00\x00\x00\x03\xe8\
               \x04range2\x00\x00\
               \x05\x80\x00\x00\x00\x00\x00\x07\xd0\
               \x00")#"),
      FormatBytesAsStr(DocKey(
          0xcafe6789,
          PrimitiveValues("hashed1", "hashed2"),
          PrimitiveValues("range1", 1000, "range2", 2000)).Encode().data()));
}

TEST(DocKeyTest, TestRandomizedDocKeyRoundTripEncodingDecoding) {
  TestRoundTripDocOrSubDocKeyEncodingDecoding<DocKey>();
}

TEST(DocKeyTest, TestRandomizedSubDocKeyRoundTripEncodingDecoding) {
  TestRoundTripDocOrSubDocKeyEncodingDecoding<SubDocKey>();
}

TEST(DocKeyTest, TestDocKeyComparison) {
  TestDocOrSubDocKeyComparison<DocKey>();
}

TEST(DocKeyTest, TestSubDocKeyComparison) {
  TestDocOrSubDocKeyComparison<SubDocKey>();
}

TEST(DocKeyTest, TestFromKuduKey) {
  Schema schema({ ColumnSchema("number1", DataType::INT64),
                  ColumnSchema("string1", DataType::STRING),
                  ColumnSchema("number2", DataType::INT64),
                  ColumnSchema("string2", DataType::STRING) }, 4);
  EncodedKeyBuilder builder(&schema);

  int64_t number1 = 123123123;
  Slice string1("mystring1");
  int64_t number2 = -100020003000;
  Slice string2("mystring2");

  builder.AddColumnKey(&number1);
  builder.AddColumnKey(&string1);
  builder.AddColumnKey(&number2);
  builder.AddColumnKey(&string2);

  auto encoded_key = unique_ptr<EncodedKey>(builder.BuildEncodedKey());
  ASSERT_EQ("(123123123,mystring1,-100020003000,mystring2)",
            encoded_key->Stringify(schema));

  auto doc_key = DocKey::FromKuduEncodedKey(*encoded_key, schema);
  ASSERT_EQ("DocKey([], [123123123, \"mystring1\", -100020003000, \"mystring2\"])",
            doc_key.ToString());
}

TEST(DocKeyTest, TestSubDocKeyStartsWith) {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  auto subdoc_keys = GenRandomSubDocKeys(&rng, /* use_hash = */ false, 1000);
  for (const auto& subdoc_key : subdoc_keys) {
    if (subdoc_key.num_subkeys() > 0) {
      const SubDocKey doc_key_only = SubDocKey(subdoc_key.doc_key(), subdoc_key.doc_gen_ts());
      ASSERT_TRUE(subdoc_key.StartsWith(doc_key_only));
      ASSERT_FALSE(doc_key_only.StartsWith(subdoc_key));
      SubDocKey with_another_doc_gen_ts(subdoc_key);
      with_another_doc_gen_ts.set_doc_gen_ts(Timestamp(subdoc_key.doc_gen_ts().ToUint64() + 1));
      ASSERT_FALSE(with_another_doc_gen_ts.StartsWith(doc_key_only));
      ASSERT_FALSE(with_another_doc_gen_ts.StartsWith(subdoc_key));
      ASSERT_FALSE(subdoc_key.StartsWith(with_another_doc_gen_ts));
    }
  }
}

}  // namespace docdb
}  // namespace yb
