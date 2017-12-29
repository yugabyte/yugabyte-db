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

#include "yb/docdb/doc_key.h"

#include <memory>

#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/full_filter_block.h"

#include "yb/docdb/docdb_test_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/util/bytes_formatter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::unique_ptr;
using strings::Substitute;
using yb::util::ApplyEagerLineContinuation;
using yb::util::FormatBytesAsStr;
using rocksdb::FilterBitsBuilder;
using rocksdb::FilterBitsReader;

static constexpr int kNumDocOrSubDocKeysPerBatch = 1000;
static constexpr int kNumTestDocOrSubDocKeyComparisons = 10000;

static_assert(kNumDocOrSubDocKeysPerBatch < kNumTestDocOrSubDocKeyComparisons,
              "Number of document/subdocument key pairs to compare must be greater than the "
              "number of random document/subdocument keys to choose from.");
static_assert(kNumTestDocOrSubDocKeyComparisons <
                  kNumDocOrSubDocKeysPerBatch * kNumDocOrSubDocKeysPerBatch, // NOLINT
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
                                        UseHash use_hash,
                                        int num_keys);

template<>
std::vector<DocKey> GenRandomDocOrSubDocKeys<DocKey>(RandomNumberGenerator* rng,
                                                     UseHash use_hash,
                                                     int num_keys) {
  return GenRandomDocKeys(rng, use_hash, num_keys);
}

template<>
std::vector<SubDocKey> GenRandomDocOrSubDocKeys<SubDocKey>(RandomNumberGenerator* rng,
                                                           UseHash use_hash,
                                                           int num_keys) {
  return GenRandomSubDocKeys(rng, use_hash, num_keys);
}

template <typename DocOrSubDocKey>
void TestRoundTripDocOrSubDocKeyEncodingDecoding(const DocOrSubDocKey& doc_or_subdoc_key) {
  KeyBytes encoded_key = doc_or_subdoc_key.Encode();
  DocOrSubDocKey decoded_key;
  ASSERT_OK(decoded_key.FullyDecodeFrom(encoded_key.AsSlice()));
  ASSERT_EQ(doc_or_subdoc_key, decoded_key);
  KeyBytes reencoded_doc_key = decoded_key.Encode();
  ASSERT_EQ(encoded_key.ToString(), reencoded_doc_key.ToString());
}

template <typename DocOrSubDocKey>
void TestRoundTripDocOrSubDocKeyEncodingDecoding() {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  for (auto use_hash : UseHash::kValues) {
    auto doc_or_subdoc_keys = GenRandomDocOrSubDocKeys<DocOrSubDocKey>(
        &rng, use_hash, kNumDocOrSubDocKeysPerBatch);
    for (const auto& doc_or_subdoc_key : doc_or_subdoc_keys) {
      TestRoundTripDocOrSubDocKeyEncodingDecoding(doc_or_subdoc_key);
    }
  }
}

template <typename DocOrSubDocKey>
void TestDocOrSubDocKeyComparison() {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  for (auto use_hash : UseHash::kValues) {
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
      "DocKey(0x1234, "
      "[\"hashed_key1\", 123, \"hashed_key2\", 234], [10, \"foo\", 20, \"bar\"])",
      DocKey(0x1234,
             PrimitiveValues("hashed_key1", 123, "hashed_key2", 234),
             PrimitiveValues(10, "foo", 20, "bar")).ToString());
}

TEST(DocKeyTest, TestSubDocKeyToString) {
  ASSERT_EQ(
      "SubDocKey(DocKey([], [\"range_key1\", 1000, \"range_key_3\"]), [HT{ physical: 12345 }])",
      SubDocKey(DocKey(PrimitiveValues("range_key1", 1000, "range_key_3")),
                HybridTime::FromMicros(12345L)).ToString());
  ASSERT_EQ(
      "SubDocKey(DocKey([], [\"range_key1\", 1000, \"range_key_3\"]), "
      "[\"subkey1\"; HT{ physical: 20000 }])",
      SubDocKey(
          DocKey(PrimitiveValues("range_key1", 1000, "range_key_3")),
          PrimitiveValue("subkey1"), HybridTime::FromMicros(20000L)
      ).ToString());

}

TEST(DocKeyTest, TestDocKeyEncoding) {
  // A few points to make it easier to understand the expected binary representations here:
  // - Initial bytes such as 'S', 'I' correspond the ValueType enum.
  // - Strings are terminated with \x00\x00.
  // - Groups of key components in the document key ("hashed" and "range" components) are separated
  //   with '!'.
  // - 64-bit signed integers are encoded using big-endian format with sign bit inverted.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#(
              "Sval1\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x03\xe8\
               Sval2\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x07\xd0\
               !"
          )#"),
      FormatBytesAsStr(DocKey(PrimitiveValues("val1", 1000, "val2", 2000)).Encode().data()));

  InetAddress addr;
  ASSERT_OK(addr.FromString("1.2.3.4"));

  // To get a descending sorting, we store the negative of a decimal type. 100.2 gets converted to
  // -100.2 which in the encoded form is equal to \x1c\xea\xfe\xd7.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#(
            "a\x89\x9e\x93\xce\xff\xff\
             I\x80\x00\x00\x00\x00\x00\x03\xe8\
             b\x7f\xff\xff\xff\xff\xff\xfc\x17\
             a\x89\x9e\x93\xce\xff\xfe\xff\xff\
             .\xfe\xfd\xfc\xfb\xff\xff\
             c\x7f\xff\xff\xff\xff\xff\xfc\x17\
             d\x1c\xea\xfe\xd7\
             E\xdd\x14\
             !"
          )#"),
      FormatBytesAsStr(DocKey({
          PrimitiveValue("val1", SortOrder::kDescending),
          PrimitiveValue(1000),
          PrimitiveValue(1000, SortOrder::kDescending),
          PrimitiveValue(BINARY_STRING("val1""\x00"), SortOrder::kDescending),
          PrimitiveValue(addr, SortOrder::kDescending),
          PrimitiveValue(Timestamp(1000), SortOrder::kDescending),
          PrimitiveValue::Decimal(util::Decimal("100.02").EncodeToComparable(),
                                  SortOrder::kDescending),
          PrimitiveValue::Decimal(util::Decimal("0.001").EncodeToComparable(),
                                  SortOrder::kAscending),
                              }).Encode().data()));

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#("G\
               \xca\xfe\
               Shashed1\x00\x00\
               Shashed2\x00\x00\
               !\
               Srange1\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x03\xe8\
               Srange2\x00\x00\
               I\x80\x00\x00\x00\x00\x00\x07\xd0\
               !")#"),
      FormatBytesAsStr(DocKey(
          0xcafe,
          PrimitiveValues("hashed1", "hashed2"),
          PrimitiveValues("range1", 1000, "range2", 2000)).Encode().data()));
}

TEST(DocKeyTest, TestBasicSubDocKeyEncodingDecoding) {
  const SubDocKey subdoc_key(DocKey({PrimitiveValue("some_doc_key")}),
                             PrimitiveValue("sk1"),
                             PrimitiveValue("sk2"),
                             PrimitiveValue(BINARY_STRING("sk3""\x00"), SortOrder::kDescending),
                             HybridTime::FromMicros(1000));
  const KeyBytes encoded_subdoc_key(subdoc_key.Encode());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      ApplyEagerLineContinuation(
          R"#("Ssome_doc_key\x00\x00\
               !\
               Ssk1\x00\x00\
               Ssk2\x00\x00\
               a\x8c\x94\xcc\xff\xfe\xff\xff\
               #\x80\xff\x05T=\xf7)\xbc\x18\x80K"
          )#"
      ),
      encoded_subdoc_key.ToString()
  );
  SubDocKey decoded_subdoc_key;
  ASSERT_OK(decoded_subdoc_key.FullyDecodeFrom(encoded_subdoc_key.AsSlice()));
  ASSERT_EQ(subdoc_key, decoded_subdoc_key);
  Slice source = encoded_subdoc_key.data();
  boost::container::small_vector<Slice, 20> slices;
  ASSERT_OK(SubDocKey::PartiallyDecode(&source, &slices));
  const DocKey& dockey = subdoc_key.doc_key();
  const auto& range_group = dockey.range_group();
  size_t size = slices.size();
  ASSERT_EQ(range_group.size() + 1, size);
  --size; // the last one is time
  for (size_t i = 0; i != size; ++i) {
    PrimitiveValue value;
    Slice temp = slices[i];
    ASSERT_OK(value.DecodeFromKey(&temp));
    ASSERT_TRUE(temp.empty());
    ASSERT_EQ(range_group[i], value);
  }
  DocHybridTime time;
  Slice temp = slices[size];
  ASSERT_OK(time.DecodeFrom(&temp));
  ASSERT_TRUE(temp.empty());
  ASSERT_EQ(subdoc_key.doc_hybrid_time(), time);
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
  auto subdoc_keys = GenRandomSubDocKeys(&rng, UseHash::kFalse, 1000);
  for (const auto& subdoc_key : subdoc_keys) {
    if (subdoc_key.num_subkeys() > 0) {
      const SubDocKey doc_key_only = SubDocKey(subdoc_key.doc_key());
      const SubDocKey doc_key_only_with_ht =
          SubDocKey(subdoc_key.doc_key(), subdoc_key.hybrid_time());
      ASSERT_TRUE(subdoc_key.StartsWith(doc_key_only));
      ASSERT_FALSE(doc_key_only.StartsWith(subdoc_key));
      SubDocKey with_another_doc_gen_ht(subdoc_key);
      with_another_doc_gen_ht.set_hybrid_time(
          DocHybridTime(subdoc_key.hybrid_time().ToUint64() + 1, 0, kMinWriteId));
      ASSERT_FALSE(with_another_doc_gen_ht.StartsWith(doc_key_only_with_ht));
      ASSERT_FALSE(with_another_doc_gen_ht.StartsWith(subdoc_key));
      ASSERT_FALSE(subdoc_key.StartsWith(with_another_doc_gen_ht));
    }
  }
}

TEST(DocKeyTest, TestNumSharedPrefixComponents) {
  const DocKey doc_key({PrimitiveValue("a"), PrimitiveValue("b")});
  const DocKey doc_key2({PrimitiveValue("aa")});
  const SubDocKey k1(doc_key, PrimitiveValue("value"), PrimitiveValue(1000L),
                     HybridTime::FromMicros(12345));

  // If the document key is different, we have no shared prefix components.
  ASSERT_EQ(0, k1.NumSharedPrefixComponents(SubDocKey(doc_key2)));

  // When only the document key matches, we have one shared prefix component.
  ASSERT_EQ(1, k1.NumSharedPrefixComponents(SubDocKey(doc_key)));
  ASSERT_EQ(1, k1.NumSharedPrefixComponents(SubDocKey(doc_key, PrimitiveValue("another_value"))));

  ASSERT_EQ(2, k1.NumSharedPrefixComponents(SubDocKey(doc_key, PrimitiveValue("value"))));
  ASSERT_EQ(3, k1.NumSharedPrefixComponents(
      SubDocKey(doc_key, PrimitiveValue("value"), PrimitiveValue(1000L))));

  // Can't match more components than there are in one of the SubDocKeys.
  ASSERT_EQ(3, k1.NumSharedPrefixComponents(
      SubDocKey(doc_key,
                PrimitiveValue("value"),
                PrimitiveValue(1000L),
                PrimitiveValue("some_more"))));
}

std::string EncodeSubDocKey(const std::string& hash_key,
    const std::string& range_key, const std::string& sub_key, uint64_t time) {
  DocKey dk(DocKey(0, PrimitiveValues(hash_key), PrimitiveValues(range_key)));
  return SubDocKey(dk, PrimitiveValue(sub_key),
      HybridTime::FromMicros(time)).Encode().AsStringRef();
}

std::string EncodeSimpleSubDocKey(const std::string& hash_key) {
  return EncodeSubDocKey(hash_key, "range_key", "sub_key", 12345L);
}

std::string EncodeSimpleSubDocKeyWithDifferentNonHashPart(const std::string& hash_key) {
  return EncodeSubDocKey(hash_key, "another_range_key", "another_sub_key", 55555L);
}

TEST(DocKeyTest, TestKeyMatching) {
  DocDbAwareFilterPolicy policy(rocksdb::FilterPolicy::kDefaultFixedSizeFilterBits, nullptr);
  std::string keys[] = { "foo", "bar", "test" };
  std::string absent_key = "fake";

  std::unique_ptr<FilterBitsBuilder> builder(policy.GetFilterBitsBuilder());
  ASSERT_NE(builder, nullptr);
  // Policy supports GetFilterBitsBuilder/Reader interface (see description in filter_policy.h) -
  // lets test it.
  for (const auto& key : keys) {
    builder->AddKey(policy.GetKeyTransformer()->Transform(EncodeSimpleSubDocKey(key)));
  }
  std::unique_ptr<const char[]> buf;
  rocksdb::Slice filter = builder->Finish(&buf);

  std::unique_ptr<FilterBitsReader> reader(policy.GetFilterBitsReader(filter));

  auto may_match = [&](const std::string& sub_doc_key_str) {
    return reader->MayMatch(policy.GetKeyTransformer()->Transform(sub_doc_key_str));
  };

  for (const auto &key : keys) {
    ASSERT_TRUE(may_match(EncodeSimpleSubDocKey(key))) << "Key: " << key;
    ASSERT_TRUE(may_match(EncodeSimpleSubDocKeyWithDifferentNonHashPart(key))) << "Key: " << key;
  }
  ASSERT_FALSE(may_match(EncodeSimpleSubDocKey(absent_key))) << "Key: " << absent_key;
}

TEST(DocKeyTest, TestWriteId) {
  SubDocKey subdoc_key(DocKey({PrimitiveValue("a"), PrimitiveValue(135)}),
                       DocHybridTime(1000000, 4091, 135));
  TestRoundTripDocOrSubDocKeyEncodingDecoding(subdoc_key);
}

}  // namespace docdb
}  // namespace yb
