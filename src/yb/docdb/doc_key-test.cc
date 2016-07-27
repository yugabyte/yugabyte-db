// Copyright (c) YugaByte, Inc.

#include "yb/docdb/doc_key.h"

#include <random>

#include "yb/util/bytes_formatter.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/gutil/strings/substitute.h"

using strings::Substitute;
using yb::util::FormatBytesAsStr;
using yb::util::ApplyEagerLineContinuation;

using RandomNumberGenerator = std::mt19937_64;

static constexpr int kMaxNumDocKeyParts = 10;
static constexpr int kMaxNumSubKeys = 10;
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

// Note: test data generator methods below are using a non-const reference for the random number
// generator for simplicity, even though it is against Google C++ Style Guide. If we used a pointer,
// we would have to invoke the RNG as (*rng)().

PrimitiveValue GenRandomPrimitiveValue(RandomNumberGenerator& rng) {
  switch (rng() % 5) {
    case 0:
      return PrimitiveValue(static_cast<int64_t>(rng()));
    case 1: {
      string s;
      for (int j = 0; j < rng() % 50; ++j) {
        s.push_back(rng() & 0xff);
      }
      return PrimitiveValue(s);
    }
    case 2: return PrimitiveValue::kNull;
    case 3: return PrimitiveValue::kTrue;
    case 4: return PrimitiveValue::kFalse;
  }
  LOG(FATAL) << "Should never get here";
  return PrimitiveValue(); // to make the compiler happy
}

// Generate a vector of random primitive values.
vector<PrimitiveValue> GenRandomPrimitiveValues(RandomNumberGenerator& rng) {
  vector<PrimitiveValue> result;
  for (int i = 0; i < rng() % (kMaxNumDocKeyParts + 1); ++i) {
    result.push_back(GenRandomPrimitiveValue(rng));
  }
  return result;
}

DocKey GenRandomDocKey(RandomNumberGenerator& rng, bool use_hash) {
  if (use_hash) {
    return DocKey(
        static_cast<uint32_t>(rng()),  // this is just a random value, not a hash function result
        GenRandomPrimitiveValues(rng),
        GenRandomPrimitiveValues(rng));
  } else {
    return GenRandomPrimitiveValues(rng);
  }
}

DocKey CreateMinimalDocKey(RandomNumberGenerator& rng, bool use_hash) {
  return use_hash ? DocKey(static_cast<DocKeyHash>(rng()), {}, {}) : DocKey();
}

template<typename T>
vector<T> GenRandomDocOrSubDocKeys(RandomNumberGenerator& rng, bool use_hash);

template<>
vector<DocKey> GenRandomDocOrSubDocKeys<DocKey>(RandomNumberGenerator& rng, bool use_hash) {
  vector<DocKey> result;
  result.push_back(CreateMinimalDocKey(rng, use_hash));
  for (int iteration = 0; iteration < 1000; ++iteration) {
    result.push_back(GenRandomDocKey(rng, use_hash));
  }
  return result;
}

template<>
vector<SubDocKey> GenRandomDocOrSubDocKeys<SubDocKey>(RandomNumberGenerator& rng, bool use_hash) {
  vector<SubDocKey> result;
  result.push_back(SubDocKey(CreateMinimalDocKey(rng, use_hash), Timestamp(rng())));
  for (int iteration = 0; iteration < kNumDocOrSubDocKeysPerBatch; ++iteration) {
    result.push_back(SubDocKey(GenRandomDocKey(rng, use_hash), Timestamp(rng())));
    for (int i = 0; i < rng() % (kMaxNumSubKeys + 1); ++i) {
      result.back().AppendSubKeysAndTimestamps(GenRandomPrimitiveValue(rng), Timestamp(rng()));
    }
  }
  return result;
}

template <typename DocOrSubDocKey>
void TestRoundTripDocOrSubDocKeyEncodingDecoding() {
  RandomNumberGenerator rng;  // Use the default seed to keep it deterministic.
  for (int use_hash = 0; use_hash <= 1; ++use_hash) {
    auto keys = GenRandomDocOrSubDocKeys<DocOrSubDocKey>(rng, use_hash);
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
    auto keys = GenRandomDocOrSubDocKeys<DocOrSubDocKey>(rng, use_hash);
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

}
}
