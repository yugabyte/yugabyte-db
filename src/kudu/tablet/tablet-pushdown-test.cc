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

#include <algorithm>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet-test-base.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

namespace kudu {
namespace tablet {

enum Setup {
  ALL_IN_MEMORY,
  SPLIT_MEMORY_DISK,
  ALL_ON_DISK
};

class TabletPushdownTest : public KuduTabletTest,
                           public ::testing::WithParamInterface<Setup> {
 public:
  TabletPushdownTest()
    : KuduTabletTest(Schema({ ColumnSchema("key", INT32),
                              ColumnSchema("int_val", INT32),
                              ColumnSchema("string_val", STRING) }, 1)) {
  }

  virtual void SetUp() OVERRIDE {
    KuduTabletTest::SetUp();

    FillTestTablet();
  }

  void FillTestTablet() {
    RowBuilder rb(client_schema_);

    nrows_ = 2100;
    if (AllowSlowTests()) {
      nrows_ = 100000;
    }

    LocalTabletWriter writer(tablet().get(), &client_schema_);
    KuduPartialRow row(&client_schema_);
    for (int64_t i = 0; i < nrows_; i++) {
      CHECK_OK(row.SetInt32(0, i));
      CHECK_OK(row.SetInt32(1, i * 10));
      CHECK_OK(row.SetStringCopy(2, StringPrintf("%08" PRId64, i)));
      ASSERT_OK_FAST(writer.Insert(row));

      if (i == 205 && GetParam() == SPLIT_MEMORY_DISK) {
        ASSERT_OK(tablet()->Flush());
      }
    }

    if (GetParam() == ALL_ON_DISK) {
      ASSERT_OK(tablet()->Flush());
    }
  }

  // The predicates tested in the various test cases all yield
  // the same set of rows. Run the scan and verify that the
  // expected rows are returned.
  void TestScanYieldsExpectedResults(ScanSpec spec) {
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(tablet()->NewRowIterator(client_schema_, &iter));
    ASSERT_OK(iter->Init(&spec));
    ASSERT_TRUE(spec.predicates().empty()) << "Should have accepted all predicates";

    vector<string> results;
    LOG_TIMING(INFO, "Filtering by int value") {
      ASSERT_OK(IterateToStringList(iter.get(), &results));
    }
    std::sort(results.begin(), results.end());
    for (const string &str : results) {
      LOG(INFO) << str;
    }
    ASSERT_EQ(11, results.size());
    ASSERT_EQ("(int32 key=200, int32 int_val=2000, string string_val=00000200)",
              results[0]);
    ASSERT_EQ("(int32 key=210, int32 int_val=2100, string string_val=00000210)",
              results[10]);

    int expected_blocks_from_disk;
    int expected_rows_from_disk;
    bool check_stats = true;
    switch (GetParam()) {
      case ALL_IN_MEMORY:
        expected_blocks_from_disk = 0;
        expected_rows_from_disk = 0;
        break;
      case SPLIT_MEMORY_DISK:
        expected_blocks_from_disk = 1;
        expected_rows_from_disk = 206;
        break;
      case ALL_ON_DISK:
        // If AllowSlowTests() is true and all data is on disk
        // (vs. first 206 rows -- containing the values we're looking
        // for -- on disk and the rest in-memory), then the number
        // of blocks and rows we will scan through can't be easily
        // determined (as it depends on default cfile block size, the
        // size of cfile header, and how much data each column takes
        // up).
        if (AllowSlowTests()) {
          check_stats = false;
        } else {
          // If AllowSlowTests() is false, then all of the data fits
          // into a single cfile.
          expected_blocks_from_disk = 1;
          expected_rows_from_disk = nrows_;
        }
        break;
    }
    if (check_stats) {
      vector<IteratorStats> stats;
      iter->GetIteratorStats(&stats);
      for (const IteratorStats& col_stats : stats) {
        EXPECT_EQ(expected_blocks_from_disk, col_stats.data_blocks_read_from_disk);
        EXPECT_EQ(expected_rows_from_disk, col_stats.cells_read_from_disk);
      }
    }
  }

  // Test that a scan with an empty projection and the given spec
  // returns the expected number of rows. The rows themselves
  // should be empty.
  void TestCountOnlyScanYieldsExpectedResults(ScanSpec spec) {
    Schema empty_schema(std::vector<ColumnSchema>(), 0);
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(tablet()->NewRowIterator(empty_schema, &iter));
    ASSERT_OK(iter->Init(&spec));
    ASSERT_TRUE(spec.predicates().empty()) << "Should have accepted all predicates";

    vector<string> results;
    ASSERT_OK(IterateToStringList(iter.get(), &results));
    ASSERT_EQ(11, results.size());
    for (const string& result : results) {
      ASSERT_EQ("()", result);
    }
  }
 private:
  uint64_t nrows_;
};

TEST_P(TabletPushdownTest, TestPushdownIntKeyRange) {
  ScanSpec spec;
  int32_t lower = 200;
  int32_t upper = 210;
  ColumnRangePredicate pred0(schema_.column(0), &lower, &upper);
  spec.AddPredicate(pred0);

  TestScanYieldsExpectedResults(spec);
  TestCountOnlyScanYieldsExpectedResults(spec);
}

TEST_P(TabletPushdownTest, TestPushdownIntValueRange) {
  // Push down a double-ended range on the integer value column.

  ScanSpec spec;
  int32_t lower = 2000;
  int32_t upper = 2100;
  ColumnRangePredicate pred1(schema_.column(1), &lower, &upper);
  spec.AddPredicate(pred1);

  TestScanYieldsExpectedResults(spec);

  // TODO: support non-key predicate pushdown on columns which aren't
  // part of the projection. The following line currently would crash.
  // TestCountOnlyScanYieldsExpectedResults(spec);

  // TODO: collect IO statistics per column, verify that most of the string blocks
  // were not read.
}

INSTANTIATE_TEST_CASE_P(AllMemory, TabletPushdownTest, ::testing::Values(ALL_IN_MEMORY));
INSTANTIATE_TEST_CASE_P(SplitMemoryDisk, TabletPushdownTest, ::testing::Values(SPLIT_MEMORY_DISK));
INSTANTIATE_TEST_CASE_P(AllDisk, TabletPushdownTest, ::testing::Values(ALL_ON_DISK));

} // namespace tablet
} // namespace kudu
