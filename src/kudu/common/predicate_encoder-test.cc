// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/common/predicate_encoder.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

namespace kudu {

class TestRangePredicateEncoder : public KuduTest {
 public:
  explicit TestRangePredicateEncoder(const Schema& s)
    : arena_(1024, 256 * 1024),
      schema_(s),
      enc_(&schema_, &arena_) {}

  enum ComparisonOp {
    GE,
    EQ,
    LE
  };

  template<class T>
  void AddPredicate(ScanSpec* spec, StringPiece col,
                    ComparisonOp op, T val) {
    int idx = schema_.find_column(col);
    CHECK_GE(idx, 0);

    void* upper = nullptr;
    void* lower = nullptr;
    void* val_void = arena_.AllocateBytes(sizeof(val));
    memcpy(val_void, &val, sizeof(val));

    switch (op) {
      case GE:
        lower = val_void;
        break;
      case EQ:
        lower = upper = val_void;
        break;
      case LE:
        upper = val_void;
        break;
    }

    ColumnRangePredicate pred(schema_.column(idx), lower, upper);
    spec->AddPredicate(pred);
  }


 protected:
  Arena arena_;
  Schema schema_;
  RangePredicateEncoder enc_;
};

class CompositeIntKeysTest : public TestRangePredicateEncoder {
 public:
  CompositeIntKeysTest() :
    TestRangePredicateEncoder(
        Schema({ ColumnSchema("a", UINT8),
                 ColumnSchema("b", UINT8),
                 ColumnSchema("c", UINT8) },
               3)) {
  }
};

// Test that multiple predicates on a column are collapsed by
// RangePredicateEncoder::Simplify()
TEST_F(CompositeIntKeysTest, TestSimplify) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 255);
  AddPredicate<uint8_t>(&spec, "b", GE, 3);
  AddPredicate<uint8_t>(&spec, "b", LE, 255);
  AddPredicate<uint8_t>(&spec, "b", LE, 200);
  AddPredicate<uint8_t>(&spec, "c", LE, 128);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  vector<RangePredicateEncoder::SimplifiedBounds> bounds;
  enc_.SimplifyBounds(spec, &bounds);
  ASSERT_EQ(3, bounds.size());
  ASSERT_EQ("(`a` BETWEEN 255 AND 255)",
            ColumnRangePredicate(schema_.column(0), bounds[0].lower, bounds[0].upper).ToString());

  ASSERT_EQ("(`b` BETWEEN 3 AND 200)",
            ColumnRangePredicate(schema_.column(1), bounds[1].lower, bounds[1].upper).ToString());
  ASSERT_EQ("(`c` <= 128)",
            ColumnRangePredicate(schema_.column(2), bounds[2].lower, bounds[2].upper).ToString());
}

// Predicate: a == 128
TEST_F(CompositeIntKeysTest, TestPrefixEquality) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 128);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  // Expect: key >= (128, 0, 0) AND key < (129, 0, 0)
  EXPECT_EQ("PK >= (uint8 a=128, uint8 b=0, uint8 c=0) AND "
            "PK < (uint8 a=129, uint8 b=0, uint8 c=0)",
            spec.ToStringWithSchema(schema_));
}

// Predicate: a <= 254
TEST_F(CompositeIntKeysTest, TestPrefixUpperBound) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", LE, 254);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK < (uint8 a=255, uint8 b=0, uint8 c=0)",
            spec.ToStringWithSchema(schema_));
}

// Predicate: a >= 254
TEST_F(CompositeIntKeysTest, TestPrefixLowerBound) {
  // Predicate: a >= 254
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", GE, 254);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK >= (uint8 a=254, uint8 b=0, uint8 c=0)", spec.ToStringWithSchema(schema_));
}

// Test a predicate on a non-prefix part of the key. Can't be pushed.
//
// Predicate: b == 128
TEST_F(CompositeIntKeysTest, TestNonPrefix) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "b", EQ, 128);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  // Expect: nothing pushed (predicate is still on `b`, not PK)
  EXPECT_EQ("(`b` BETWEEN 128 AND 128)",
            spec.ToStringWithSchema(schema_));
}

// Test what happens when an upper bound on a cell is equal to the maximum
// value for the cell. In this case, the preceding cell is also at the maximum
// value as well, so we eliminate the upper bound entirely.
//
// Predicate: a == 255 AND b BETWEEN 3 AND 255
TEST_F(CompositeIntKeysTest, TestRedundantUpperBound) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 255);
  AddPredicate<uint8_t>(&spec, "b", GE, 3);
  AddPredicate<uint8_t>(&spec, "b", LE, 255);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK >= (uint8 a=255, uint8 b=3, uint8 c=0)", spec.ToStringWithSchema(schema_));
}

// A similar test, but in this case we still have an equality prefix
// that needs to be accounted for, so we can't eliminate the upper bound
// entirely.
//
// Predicate: a == 1 AND b BETWEEN 3 AND 255
TEST_F(CompositeIntKeysTest, TestRedundantUpperBound2) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 1);
  AddPredicate<uint8_t>(&spec, "b", GE, 3);
  AddPredicate<uint8_t>(&spec, "b", LE, 255);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK >= (uint8 a=1, uint8 b=3, uint8 c=0) AND "
            "PK < (uint8 a=2, uint8 b=0, uint8 c=0)",
            spec.ToStringWithSchema(schema_));
}

// Test that, if so desired, pushed predicates are not erased.
//
// Predicate: a == 254
TEST_F(CompositeIntKeysTest, TestNoErasePredicates) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 254);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, false));
  EXPECT_EQ("PK >= (uint8 a=254, uint8 b=0, uint8 c=0) AND "
            "PK < (uint8 a=255, uint8 b=0, uint8 c=0)\n"
            "(`a` BETWEEN 254 AND 254)", spec.ToStringWithSchema(schema_));
}

// Test that, if pushed predicates are erased, that we don't
// erase non-pushed predicates.
// Because we have no predicate on column 'b', we can't push a
// a range predicate that includes 'c'.
//
// Predicate: a == 254 AND c == 254
TEST_F(CompositeIntKeysTest, TestNoErasePredicates2) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 254);
  AddPredicate<uint8_t>(&spec, "c", EQ, 254);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  // The predicate on column A should be pushed while "c" remains.
  EXPECT_EQ("PK >= (uint8 a=254, uint8 b=0, uint8 c=0) AND "
            "PK < (uint8 a=255, uint8 b=0, uint8 c=0)\n"
            "(`c` BETWEEN 254 AND 254)", spec.ToStringWithSchema(schema_));
}

// Test that predicates added out of key order are OK.
//
// Predicate: b == 254 AND a == 254
TEST_F(CompositeIntKeysTest, TestPredicateOrderDoesntMatter) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "b", EQ, 254);
  AddPredicate<uint8_t>(&spec, "a", EQ, 254);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK >= (uint8 a=254, uint8 b=254, uint8 c=0) AND "
            "PK < (uint8 a=254, uint8 b=255, uint8 c=0)",
            spec.ToStringWithSchema(schema_));
}

// Tests for String parts in composite keys
//------------------------------------------------------------
class CompositeIntStringKeysTest : public TestRangePredicateEncoder {
 public:
  CompositeIntStringKeysTest() :
    TestRangePredicateEncoder(
        Schema({ ColumnSchema("a", UINT8),
                 ColumnSchema("b", STRING),
                 ColumnSchema("c", STRING) },
               3)) {
  }
};


// Predicate: a == 128
TEST_F(CompositeIntStringKeysTest, TestPrefixEquality) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 128);
  SCOPED_TRACE(spec.ToStringWithSchema(schema_));
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  // Expect: key >= (128, "", "") AND key < (129, "", "")
  EXPECT_EQ("PK >= (uint8 a=128, string b=, string c=) AND "
            "PK < (uint8 a=129, string b=, string c=)",
            spec.ToStringWithSchema(schema_));
}

// Predicate: a == 128 AND b = "abc"
TEST_F(CompositeIntStringKeysTest, TestPrefixEqualityWithString) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 128);
  AddPredicate<Slice>(&spec, "b", EQ, Slice("abc"));
  SCOPED_TRACE(spec.ToString());
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK >= (uint8 a=128, string b=abc, string c=) AND "
            "PK < (uint8 a=128, string b=abc\\000, string c=)",
            spec.ToStringWithSchema(schema_));
}

// Tests for non-composite int key
//------------------------------------------------------------
class SingleIntKeyTest : public TestRangePredicateEncoder {
 public:
  SingleIntKeyTest() :
    TestRangePredicateEncoder(
        Schema({ ColumnSchema("a", UINT8) }, 1)) {
    }
};

TEST_F(SingleIntKeyTest, TestEquality) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 128);
  SCOPED_TRACE(spec.ToString());
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK >= (uint8 a=128) AND "
            "PK < (uint8 a=129)",
            spec.ToStringWithSchema(schema_));
}

TEST_F(SingleIntKeyTest, TestRedundantUpperBound) {
  ScanSpec spec;
  AddPredicate<uint8_t>(&spec, "a", EQ, 255);
  SCOPED_TRACE(spec.ToString());
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("PK >= (uint8 a=255)",
            spec.ToStringWithSchema(schema_));
}

TEST_F(SingleIntKeyTest, TestNoPredicates) {
  ScanSpec spec;
  SCOPED_TRACE(spec.ToString());
  ASSERT_NO_FATAL_FAILURE(enc_.EncodeRangePredicates(&spec, true));
  EXPECT_EQ("", spec.ToStringWithSchema(schema_));
}

} // namespace kudu
