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

#include <gtest/gtest.h>

#include "kudu/common/scan_predicate.h"
#include "kudu/common/rowblock.h"
#include "kudu/util/test_util.h"

namespace kudu {

class TestPredicate : public KuduTest {
 public:
  TestPredicate() :
    arena_(1024, 4096),
    n_rows_(100),
    schema_({ ColumnSchema("col0", UINT32),
              ColumnSchema("col1", UINT32),
              ColumnSchema("col2", STRING) },
            1),
    row_block_(schema_, n_rows_, &arena_)
  {}

  // Set up a block of data with two columns:
  // col0   col1
  // ----   ------
  // 0      0
  // 1      10
  // ...    ...
  // N      N * 10
  //
  // The third STRING column is left unset.
  void SetUp() OVERRIDE {
    KuduTest::SetUp();

    ColumnBlock col0 = row_block_.column_block(0, n_rows_);
    ColumnBlock col1 = row_block_.column_block(1, n_rows_);

    for (uint32_t i = 0; i < n_rows_; i++) {
      uint32_t i1 = i * 10;
      col0.SetCellValue(i, &i);
      col1.SetCellValue(i, &i1);
    }
  }

 protected:
  Arena arena_;
  const size_t n_rows_;
  Schema schema_;
  RowBlock row_block_;
};

TEST_F(TestPredicate, TestSelectionVector) {
  SelectionVector selvec(10);
  selvec.SetAllTrue();
  ASSERT_TRUE(selvec.IsRowSelected(0));
  ASSERT_TRUE(selvec.IsRowSelected(9));
  ASSERT_EQ(10, selvec.CountSelected());
  ASSERT_TRUE(selvec.AnySelected());

  for (int i = 0; i < 10; i++) {
    BitmapClear(selvec.mutable_bitmap(), i);
  }

  ASSERT_FALSE(selvec.AnySelected());

  // Test Resize()
  selvec.SetAllTrue();
  for (int i = 10; i > 0; --i) {
    selvec.Resize(i);
    ASSERT_EQ(selvec.CountSelected(), i);
    ASSERT_TRUE(selvec.AnySelected());
  }
  selvec.Resize(0);
  ASSERT_EQ(selvec.CountSelected(), 0);
  ASSERT_FALSE(selvec.AnySelected());
}

TEST_F(TestPredicate, TestColumnRange) {
  SelectionVector selvec(n_rows_);
  selvec.SetAllTrue();
  ASSERT_EQ(100, selvec.CountSelected());

  // Apply predicate 20 <= col0 <= 29
  uint32_t col0_lower = 20;
  uint32_t col0_upper = 29;
  ColumnRangePredicate pred1(schema_.column(0), &col0_lower, &col0_upper);
  ASSERT_EQ("(`col0` BETWEEN 20 AND 29)", pred1.ToString());
  pred1.Evaluate(&row_block_, &selvec);
  ASSERT_EQ(10, selvec.CountSelected()) << "Only 10 rows should be left (20-29)";

  // Apply predicate col1 >= 250
  uint32_t col1_lower = 250;
  ColumnRangePredicate pred2(schema_.column(1), &col1_lower, nullptr);
  ASSERT_EQ("(`col1` >= 250)", pred2.ToString());
  pred2.Evaluate(&row_block_, &selvec);
  ASSERT_EQ(5, selvec.CountSelected()) << "Only 5 rows should be left (25-29)";
}

// Regression test for KUDU-54: should not try to access rows for which the
// selection vector is 0.
TEST_F(TestPredicate, TestDontEvalauteOnUnselectedRows) {
  SelectionVector selvec(n_rows_);
  selvec.SetAllFalse();

  // Fill the STRING column with garbage data.
  OverwriteWithPattern(reinterpret_cast<char*>(row_block_.column_block(2).data()),
                       row_block_.column_block(2).stride() * row_block_.nrows(),
                       "JUNKDATA");

  Slice lower("lower");
  ColumnRangePredicate p(schema_.column(2), &lower, nullptr);
  p.Evaluate(&row_block_, &selvec);
  ASSERT_EQ(0, selvec.CountSelected());
}

} // namespace kudu
