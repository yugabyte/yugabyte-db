// Copyright (c) YugabyteDB, Inc.
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
#include <vector>

#include <gtest/gtest.h>

#include "yb/dockv/doc_bson.h"
#include "yb/dockv/docdb_key_comparator.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/comparator.h"

#include "yb/util/test_macros.h"

using std::string;
using std::vector;

namespace yb::dockv {

namespace {

// Helper to construct a BSON document from field/value specifications.
class BsonBuilder {
 public:
  void AddInt32(const string& name, int32_t value) {
    data_.push_back(0x10);  // int32 type
    AppendCString(name);
    AppendLE32(value);
  }

  void AddInt64(const string& name, int64_t value) {
    data_.push_back(0x12);  // int64 type
    AppendCString(name);
    AppendLE64(value);
  }

  void AddDouble(const string& name, double value) {
    data_.push_back(0x01);  // double type
    AppendCString(name);
    AppendLEDouble(value);
  }

  void AddString(const string& name, const string& value) {
    data_.push_back(0x02);  // string type
    AppendCString(name);
    // String value: int32 length (including null) + string + null.
    int32_t len = static_cast<int32_t>(value.size()) + 1;
    AppendLE32(len);
    data_.insert(data_.end(), value.begin(), value.end());
    data_.push_back(0x00);
  }

  void AddBoolean(const string& name, bool value) {
    data_.push_back(0x08);  // boolean type
    AppendCString(name);
    data_.push_back(value ? 0x01 : 0x00);
  }

  void AddNull(const string& name) {
    data_.push_back(0x0A);  // null type
    AppendCString(name);
  }

  void AddObjectId(const string& name, const uint8_t oid[12]) {
    data_.push_back(0x07);
    AppendCString(name);
    data_.insert(data_.end(), oid, oid + 12);
  }

  void AddDatetime(const string& name, int64_t millis_since_epoch) {
    data_.push_back(0x09);
    AppendCString(name);
    AppendLE64(millis_since_epoch);
  }

  void AddTimestamp(const string& name, uint32_t seconds, uint32_t increment) {
    data_.push_back(0x11);
    AppendCString(name);
    AppendLEU32(increment);
    AppendLEU32(seconds);
  }

  void AddMinKey(const string& name) {
    data_.push_back(0xFF);
    AppendCString(name);
  }

  void AddMaxKey(const string& name) {
    data_.push_back(0x7F);
    AppendCString(name);
  }

  // Adds a nested BSON document as a field value.
  void AddDocument(const string& name, const string& doc_bytes) {
    data_.push_back(0x03);  // document type
    AppendCString(name);
    data_.insert(data_.end(), doc_bytes.begin(), doc_bytes.end());
  }

  // Adds a BSON array as a field value.
  void AddArray(const string& name, const string& array_bytes) {
    data_.push_back(0x04);  // array type
    AppendCString(name);
    data_.insert(data_.end(), array_bytes.begin(), array_bytes.end());
  }

  string Build() const {
    // Total size = 4 (size) + data + 1 (terminator).
    int32_t total_size = static_cast<int32_t>(4 + data_.size() + 1);
    string result;
    result.resize(4);
    memcpy(result.data(), &total_size, 4);
    result.append(data_.begin(), data_.end());
    result.push_back(0x00);  // Document terminator.
    return result;
  }

 private:
  void AppendCString(const string& s) {
    data_.insert(data_.end(), s.begin(), s.end());
    data_.push_back(0x00);
  }

  void AppendLE32(int32_t value) {
    uint8_t buf[4];
    memcpy(buf, &value, 4);
    data_.insert(data_.end(), buf, buf + 4);
  }

  void AppendLEU32(uint32_t value) {
    uint8_t buf[4];
    memcpy(buf, &value, 4);
    data_.insert(data_.end(), buf, buf + 4);
  }

  void AppendLE64(int64_t value) {
    uint8_t buf[8];
    memcpy(buf, &value, 8);
    data_.insert(data_.end(), buf, buf + 8);
  }

  void AppendLEDouble(double value) {
    uint8_t buf[8];
    memcpy(buf, &value, 8);
    data_.insert(data_.end(), buf, buf + 8);
  }

  vector<uint8_t> data_;
};

// Helper to build a DocDB key with a BSON entry as a range component.
string BuildKeyWithBson(const string& bson_bytes, SortOrder sort_order = SortOrder::kAscending) {
  KeyBytes key;
  key.AppendKeyEntryType(KeyEntryType::kUInt16Hash);
  key.AppendUInt16(0x1234);
  key.AppendGroupEnd();
  auto bson_val = KeyEntryValue::MakeBson(Slice(bson_bytes), sort_order);
  bson_val.AppendToKey(&key);
  key.AppendGroupEnd();
  return key.ToStringBuffer();
}

// Helper to build a DocDB key with an int32 range component followed by BSON.
string BuildKeyWithInt32AndBson(int32_t int_val, const string& bson_bytes) {
  KeyBytes key;
  key.AppendKeyEntryType(KeyEntryType::kUInt16Hash);
  key.AppendUInt16(0x1234);
  key.AppendGroupEnd();
  auto int_entry = KeyEntryValue::Int32(int_val);
  int_entry.AppendToKey(&key);
  auto bson_val = KeyEntryValue::MakeBson(Slice(bson_bytes));
  bson_val.AppendToKey(&key);
  key.AppendGroupEnd();
  return key.ToStringBuffer();
}

// Helper to build a DocDB key with only non-BSON components.
string BuildKeyWithInts(int32_t a, int64_t b) {
  KeyBytes key;
  key.AppendKeyEntryType(KeyEntryType::kUInt16Hash);
  key.AppendUInt16(0x5678);
  key.AppendGroupEnd();
  auto int32_entry = KeyEntryValue::Int32(a);
  int32_entry.AppendToKey(&key);
  auto int64_entry = KeyEntryValue::Int64(b);
  int64_entry.AppendToKey(&key);
  key.AppendGroupEnd();
  return key.ToStringBuffer();
}

}  // namespace

// ----- Tests for CompareBson -----

class CompareBsonTest : public ::testing::Test {};

TEST(CompareBsonTest, Int32Positive) {
  BsonBuilder a, b;
  a.AddInt32("x", 42);
  b.AddInt32("x", 100);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
  EXPECT_GT(CompareBson(b.Build(), a.Build()), 0);
}

TEST(CompareBsonTest, Int32Equal) {
  BsonBuilder a, b;
  a.AddInt32("x", 42);
  b.AddInt32("x", 42);
  EXPECT_EQ(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, Int32Negative) {
  BsonBuilder a, b;
  a.AddInt32("x", -1);
  b.AddInt32("x", 1);
  // BSON comparison: -1 < 1.
  // But byte-wise (little-endian): -1 = FF FF FF FF > 01 00 00 00 = 1.
  // This is exactly the case where byte-wise comparison is WRONG.
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
  EXPECT_GT(CompareBson(b.Build(), a.Build()), 0);
}

TEST(CompareBsonTest, Int32NegativeValues) {
  BsonBuilder a, b;
  a.AddInt32("x", -100);
  b.AddInt32("x", -1);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, Int64Values) {
  BsonBuilder a, b;
  a.AddInt64("x", -1000000);
  b.AddInt64("x", 1000000);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, DoubleValues) {
  BsonBuilder a, b;
  a.AddDouble("x", 1.5);
  b.AddDouble("x", 2.5);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, DoubleNegative) {
  BsonBuilder a, b;
  a.AddDouble("x", -1.5);
  b.AddDouble("x", 1.5);
  // BSON: -1.5 < 1.5. Byte-wise would give wrong result for doubles.
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, CrossTypeNumericInt32VsInt64) {
  BsonBuilder a, b;
  a.AddInt32("x", 42);
  b.AddInt64("x", 42);
  // Same numeric value, different types - should be equal.
  EXPECT_EQ(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, CrossTypeNumericInt32VsDouble) {
  BsonBuilder a, b;
  a.AddInt32("x", 42);
  b.AddDouble("x", 42.0);
  EXPECT_EQ(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, CrossTypeNumericInt32VsLargerDouble) {
  BsonBuilder a, b;
  a.AddInt32("x", 42);
  b.AddDouble("x", 42.5);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, StringValues) {
  BsonBuilder a, b;
  a.AddString("x", "apple");
  b.AddString("x", "banana");
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, StringEqual) {
  BsonBuilder a, b;
  a.AddString("x", "hello");
  b.AddString("x", "hello");
  EXPECT_EQ(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, BooleanValues) {
  BsonBuilder a, b;
  a.AddBoolean("x", false);
  b.AddBoolean("x", true);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, NullValues) {
  BsonBuilder a, b;
  a.AddNull("x");
  b.AddNull("x");
  EXPECT_EQ(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, TypeOrdering) {
  // null < number < string < boolean
  BsonBuilder null_doc, int_doc, str_doc, bool_doc;
  null_doc.AddNull("x");
  int_doc.AddInt32("x", 0);
  str_doc.AddString("x", "");
  bool_doc.AddBoolean("x", false);

  EXPECT_LT(CompareBson(null_doc.Build(), int_doc.Build()), 0);
  EXPECT_LT(CompareBson(int_doc.Build(), str_doc.Build()), 0);
  EXPECT_LT(CompareBson(str_doc.Build(), bool_doc.Build()), 0);
}

TEST(CompareBsonTest, MinKeyMaxKey) {
  BsonBuilder min_doc, max_doc, int_doc;
  min_doc.AddMinKey("x");
  max_doc.AddMaxKey("x");
  int_doc.AddInt32("x", 0);

  EXPECT_LT(CompareBson(min_doc.Build(), int_doc.Build()), 0);
  EXPECT_GT(CompareBson(max_doc.Build(), int_doc.Build()), 0);
  EXPECT_LT(CompareBson(min_doc.Build(), max_doc.Build()), 0);
}

TEST(CompareBsonTest, MultipleFields) {
  BsonBuilder a, b;
  a.AddInt32("x", 1);
  a.AddInt32("y", 10);
  b.AddInt32("x", 1);
  b.AddInt32("y", 20);
  // First fields equal, second fields differ.
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, DatetimeValues) {
  BsonBuilder a, b;
  a.AddDatetime("x", 1000);
  b.AddDatetime("x", 2000);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, DatetimeNegative) {
  BsonBuilder a, b;
  a.AddDatetime("x", -1000);
  b.AddDatetime("x", 1000);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, TimestampValues) {
  BsonBuilder a, b;
  a.AddTimestamp("x", 100, 1);  // seconds=100, increment=1
  b.AddTimestamp("x", 100, 2);  // seconds=100, increment=2
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, TimestampSecondsDiffer) {
  BsonBuilder a, b;
  a.AddTimestamp("x", 100, 999);
  b.AddTimestamp("x", 200, 1);
  // Seconds compared first: 100 < 200.
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, NestedDocument) {
  BsonBuilder inner_a, inner_b;
  inner_a.AddInt32("y", -5);
  inner_b.AddInt32("y", 5);

  BsonBuilder a, b;
  a.AddDocument("x", inner_a.Build());
  b.AddDocument("x", inner_b.Build());
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, FewerElements) {
  BsonBuilder a, b;
  a.AddInt32("x", 1);
  b.AddInt32("x", 1);
  b.AddInt32("y", 2);
  // Document with fewer elements is less.
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

TEST(CompareBsonTest, ObjectId) {
  uint8_t oid_a[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  uint8_t oid_b[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12};
  BsonBuilder a, b;
  a.AddObjectId("x", oid_a);
  b.AddObjectId("x", oid_b);
  EXPECT_LT(CompareBson(a.Build(), b.Build()), 0);
}

// ----- Tests for DocDBKeyComparator -----

class DocDBKeyComparatorTest : public ::testing::Test {
 protected:
  const rocksdb::Comparator* comparator_ = DocDBKeyComparatorInstance();
  const rocksdb::Comparator* bytewise_ = rocksdb::BytewiseComparator();
};

TEST_F(DocDBKeyComparatorTest, NonBsonKeysMatchBytewise) {
  // For keys without BSON, the custom comparator should give the same results
  // as the BytewiseComparator.
  auto key_a = BuildKeyWithInts(10, 100);
  auto key_b = BuildKeyWithInts(20, 200);
  auto key_c = BuildKeyWithInts(10, 100);

  EXPECT_EQ(comparator_->Compare(key_a, key_c), 0);
  EXPECT_LT(comparator_->Compare(key_a, key_b), 0);
  EXPECT_GT(comparator_->Compare(key_b, key_a), 0);

  // Verify matches BytewiseComparator.
  EXPECT_EQ(
      comparator_->Compare(key_a, key_b) < 0,
      bytewise_->Compare(key_a, key_b) < 0);
}

TEST_F(DocDBKeyComparatorTest, BsonNegativeIntSortOrder) {
  // This is the key test: BSON with negative integers should sort correctly
  // even though their byte-wise representation would sort incorrectly.
  BsonBuilder bson_neg1, bson_pos1;
  bson_neg1.AddInt32("a", -1);
  bson_pos1.AddInt32("a", 1);

  auto key_neg1 = BuildKeyWithBson(bson_neg1.Build());
  auto key_pos1 = BuildKeyWithBson(bson_pos1.Build());

  // Custom comparator: -1 < 1 (correct BSON ordering).
  EXPECT_LT(comparator_->Compare(key_neg1, key_pos1), 0);
  EXPECT_GT(comparator_->Compare(key_pos1, key_neg1), 0);

  // BytewiseComparator would give the WRONG result for this case because
  // little-endian -1 (0xFFFFFFFF) > little-endian 1 (0x01000000).
  EXPECT_GT(bytewise_->Compare(key_neg1, key_pos1), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonEqualValues) {
  BsonBuilder bson_a, bson_b;
  bson_a.AddInt32("x", 42);
  bson_b.AddInt32("x", 42);

  auto key_a = BuildKeyWithBson(bson_a.Build());
  auto key_b = BuildKeyWithBson(bson_b.Build());

  EXPECT_EQ(comparator_->Compare(key_a, key_b), 0);
  EXPECT_TRUE(comparator_->Equal(key_a, key_b));
}

TEST_F(DocDBKeyComparatorTest, BsonDescendingSortOrder) {
  BsonBuilder bson_small, bson_large;
  bson_small.AddInt32("a", -10);
  bson_large.AddInt32("a", 10);

  auto key_small = BuildKeyWithBson(bson_small.Build(), SortOrder::kDescending);
  auto key_large = BuildKeyWithBson(bson_large.Build(), SortOrder::kDescending);

  // Descending: larger value should sort first (smaller key).
  EXPECT_GT(comparator_->Compare(key_small, key_large), 0);
  EXPECT_LT(comparator_->Compare(key_large, key_small), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonWithPrecedingInt32) {
  // Test BSON comparison when there's a non-BSON component before it.
  BsonBuilder bson_neg, bson_pos;
  bson_neg.AddInt32("a", -5);
  bson_pos.AddInt32("a", 5);

  // Same int32 prefix, different BSON values.
  auto key_a = BuildKeyWithInt32AndBson(100, bson_neg.Build());
  auto key_b = BuildKeyWithInt32AndBson(100, bson_pos.Build());

  EXPECT_LT(comparator_->Compare(key_a, key_b), 0);

  // Different int32 prefix - should compare by int32 first.
  auto key_c = BuildKeyWithInt32AndBson(50, bson_pos.Build());
  auto key_d = BuildKeyWithInt32AndBson(200, bson_neg.Build());

  EXPECT_LT(comparator_->Compare(key_c, key_d), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonDoubleNegative) {
  BsonBuilder bson_neg, bson_pos;
  bson_neg.AddDouble("a", -1.5);
  bson_pos.AddDouble("a", 1.5);

  auto key_neg = BuildKeyWithBson(bson_neg.Build());
  auto key_pos = BuildKeyWithBson(bson_pos.Build());

  EXPECT_LT(comparator_->Compare(key_neg, key_pos), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonCrossTypeNumeric) {
  BsonBuilder bson_int32, bson_int64;
  bson_int32.AddInt32("a", 42);
  bson_int64.AddInt64("a", 43);

  auto key_32 = BuildKeyWithBson(bson_int32.Build());
  auto key_64 = BuildKeyWithBson(bson_int64.Build());

  EXPECT_LT(comparator_->Compare(key_32, key_64), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonTypeOrdering) {
  BsonBuilder bson_null, bson_int, bson_str;
  bson_null.AddNull("a");
  bson_int.AddInt32("a", 0);
  bson_str.AddString("a", "");

  auto key_null = BuildKeyWithBson(bson_null.Build());
  auto key_int = BuildKeyWithBson(bson_int.Build());
  auto key_str = BuildKeyWithBson(bson_str.Build());

  EXPECT_LT(comparator_->Compare(key_null, key_int), 0);
  EXPECT_LT(comparator_->Compare(key_int, key_str), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonStringComparison) {
  BsonBuilder bson_a, bson_b;
  bson_a.AddString("a", "apple");
  bson_b.AddString("a", "banana");

  auto key_a = BuildKeyWithBson(bson_a.Build());
  auto key_b = BuildKeyWithBson(bson_b.Build());

  EXPECT_LT(comparator_->Compare(key_a, key_b), 0);
}

TEST_F(DocDBKeyComparatorTest, ComparatorNameIsStable) {
  EXPECT_STREQ(comparator_->Name(), "yb.DocDBKeyComparator.v1");
}

TEST_F(DocDBKeyComparatorTest, EmptyKeys) {
  EXPECT_EQ(comparator_->Compare(Slice(), Slice()), 0);
  Slice non_empty("abc", 3);
  EXPECT_LT(comparator_->Compare(Slice(), non_empty), 0);
  EXPECT_GT(comparator_->Compare(non_empty, Slice()), 0);
}

TEST_F(DocDBKeyComparatorTest, PrefixKeys) {
  string key = "abc";
  string prefix = "ab";
  EXPECT_GT(comparator_->Compare(key, prefix), 0);
  EXPECT_LT(comparator_->Compare(prefix, key), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonMultipleFieldsNegativeValues) {
  // Test with multiple fields where negative values matter.
  BsonBuilder a, b;
  a.AddInt32("x", 1);
  a.AddInt32("y", -10);
  b.AddInt32("x", 1);
  b.AddInt32("y", 10);

  auto key_a = BuildKeyWithBson(a.Build());
  auto key_b = BuildKeyWithBson(b.Build());

  EXPECT_LT(comparator_->Compare(key_a, key_b), 0);
}

TEST_F(DocDBKeyComparatorTest, BsonNestedDocument) {
  BsonBuilder inner_a, inner_b;
  inner_a.AddInt32("y", -100);
  inner_b.AddInt32("y", 100);

  BsonBuilder a, b;
  a.AddDocument("x", inner_a.Build());
  b.AddDocument("x", inner_b.Build());

  auto key_a = BuildKeyWithBson(a.Build());
  auto key_b = BuildKeyWithBson(b.Build());

  EXPECT_LT(comparator_->Compare(key_a, key_b), 0);
}

}  // namespace yb::dockv
