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

#include <memory>
#include <gtest/gtest.h>
#include <glog/logging.h>

#include "kudu/common/generic_iterators.h"
#include "kudu/tablet/cfile_set.h"
#include "kudu/tablet/diskrowset-test-base.h"
#include "kudu/tablet/tablet-test-base.h"
#include "kudu/util/test_util.h"

DECLARE_int32(cfile_default_block_size);

using std::shared_ptr;

namespace kudu {
namespace tablet {

class TestCFileSet : public KuduRowSetTest {
 public:
  TestCFileSet() :
    KuduRowSetTest(Schema({ ColumnSchema("c0", UINT32),
                            ColumnSchema("c1", UINT32, false, nullptr, nullptr, GetRLEStorage()),
                            ColumnSchema("c2", UINT32) }, 1))
  {}

  virtual void SetUp() OVERRIDE {
    KuduRowSetTest::SetUp();

    // Use a small cfile block size, so that when we skip materializing a given
    // column for 10,000 rows, it can actually skip over a number of blocks.
    FLAGS_cfile_default_block_size = 512;
  }

  // Write out a test rowset with two int columns.
  // The first column contains the row index * 2.
  // The second contains the row index * 10.
  // The third column contains index * 100, but is never read.
  void WriteTestRowSet(int nrows) {
    DiskRowSetWriter rsw(rowset_meta_.get(), &schema_,
                         BloomFilterSizing::BySizeAndFPRate(32*1024, 0.01f));

    ASSERT_OK(rsw.Open());

    RowBuilder rb(schema_);
    for (int i = 0; i < nrows; i++) {
      rb.Reset();
      rb.AddUint32(i * 2);
      rb.AddUint32(i * 10);
      rb.AddUint32(i * 100);
      ASSERT_OK_FAST(WriteRow(rb.data(), &rsw));
    }
    ASSERT_OK(rsw.Finish());
  }

  // Issue a range scan between 'lower' and 'upper', and verify that all result
  // rows indeed fall inside that predicate.
  void DoTestRangeScan(const shared_ptr<CFileSet> &fileset,
                       uint32_t lower,
                       uint32_t upper) {
    // Create iterator.
    shared_ptr<CFileSet::Iterator> cfile_iter(fileset->NewIterator(&schema_));
    gscoped_ptr<RowwiseIterator> iter(new MaterializingIterator(cfile_iter));

    // Create a scan with a range predicate on the key column.
    ScanSpec spec;
    ColumnRangePredicate pred1(
      schema_.column(0),
      lower != kNoBound ? &lower : nullptr,
      upper != kNoBound ? &upper : nullptr);
    spec.AddPredicate(pred1);
    ASSERT_OK(iter->Init(&spec));

    // Check that the range was respected on all the results.
    Arena arena(1024, 1024);
    RowBlock block(schema_, 100, &arena);
    while (iter->HasNext()) {
      ASSERT_OK_FAST(iter->NextBlock(&block));
      for (size_t i = 0; i < block.nrows(); i++) {
        if (block.selection_vector()->IsRowSelected(i)) {
          RowBlockRow row = block.row(i);
          if ((lower != kNoBound && *schema_.ExtractColumnFromRow<UINT32>(row, 0) < lower) ||
              (upper != kNoBound && *schema_.ExtractColumnFromRow<UINT32>(row, 0) > upper)) {
            FAIL() << "Row " << schema_.DebugRow(row) << " should not have "
                   << "passed predicate " << pred1.ToString();
          }
        }
      }
    }
  }

 private:
  ColumnStorageAttributes GetRLEStorage() const {
    ColumnStorageAttributes attr;
    attr.encoding = RLE;
    return attr;
  }

 protected:
  static const uint32_t kNoBound;
  google::FlagSaver saver;
};

const uint32_t TestCFileSet::kNoBound = kuint32max;

TEST_F(TestCFileSet, TestPartiallyMaterialize) {
  const int kCycleInterval = 10000;
  const int kNumRows = 100000;
  WriteTestRowSet(kNumRows);

  shared_ptr<CFileSet> fileset(new CFileSet(rowset_meta_));
  ASSERT_OK(fileset->Open());

  gscoped_ptr<CFileSet::Iterator> iter(fileset->NewIterator(&schema_));
  ASSERT_OK(iter->Init(nullptr));

  Arena arena(4096, 1024*1024);
  RowBlock block(schema_, 100, &arena);
  rowid_t row_idx = 0;
  while (iter->HasNext()) {
    arena.Reset();

    size_t n = block.nrows();
    ASSERT_OK_FAST(iter->PrepareBatch(&n));
    block.Resize(n);

    // Cycle between:
    // 0: materializing just column 0
    // 1: materializing just column 1
    // 2: materializing both column 0 and 1
    // NOTE: column 2 ("c2") is never materialized, even though it was part of
    // the projection. It should thus do no IO.
    int cycle = (row_idx / kCycleInterval) % 3;
    if (cycle == 0 || cycle == 2) {
      ColumnBlock col(block.column_block(0));
      ASSERT_OK_FAST(iter->MaterializeColumn(0, &col));

      // Verify
      for (int i = 0; i < n; i++) {
        uint32_t got = *reinterpret_cast<const uint32_t *>(col.cell_ptr(i));
        uint32_t expected = (row_idx + i) * 2;
        if (got != expected) {
          FAIL() << "Failed at row index " << (row_idx + i) << ": expected "
                 << expected << " got " << got;
        }
      }
    }
    if (cycle == 1 || cycle == 2) {
      ColumnBlock col(block.column_block(1));
      ASSERT_OK_FAST(iter->MaterializeColumn(1, &col));

      // Verify
      for (int i = 0; i < n; i++) {
        uint32_t got = *reinterpret_cast<const uint32_t *>(col.cell_ptr(i));
        if (got != 10 * (row_idx + i)) {
          FAIL() << "Failed at row index " << (row_idx + i) << ": expected "
                 << 10 * (row_idx + i) << " got " << got;
        }
      }
    }

    ASSERT_OK_FAST(iter->FinishBatch());
    row_idx += n;
  }

  // Verify through the iterator statistics that IO was saved by not materializing
  // all of the columns.
  vector<IteratorStats> stats;
  iter->GetIteratorStats(&stats);
  ASSERT_EQ(3, stats.size());
  for (int i = 0; i < 3; i++) {
    LOG(INFO) << "Col " << i << " stats: " << stats[i].ToString();
  }

  // Since we pushed down the block size, we expect to have read 100+ blocks of column 0
  ASSERT_GT(stats[0].data_blocks_read_from_disk, 100);

  // Since we didn't ever materialize column 2, we shouldn't have read any data blocks.
  ASSERT_EQ(0, stats[2].data_blocks_read_from_disk);

  // Column 0 and 1 skipped a lot of blocks, so should not have read all of the cells
  // from either column.
  ASSERT_LT(stats[0].cells_read_from_disk, kNumRows * 3 / 4);
  ASSERT_LT(stats[1].cells_read_from_disk, kNumRows * 3 / 4);
}

TEST_F(TestCFileSet, TestIteratePartialSchema) {
  const int kNumRows = 100;
  WriteTestRowSet(kNumRows);

  shared_ptr<CFileSet> fileset(new CFileSet(rowset_meta_));
  ASSERT_OK(fileset->Open());

  Schema new_schema;
  ASSERT_OK(schema_.CreateProjectionByNames({ "c0", "c2" }, &new_schema));
  shared_ptr<CFileSet::Iterator> cfile_iter(fileset->NewIterator(&new_schema));
  gscoped_ptr<RowwiseIterator> iter(new MaterializingIterator(cfile_iter));

  ASSERT_OK(iter->Init(nullptr));

  // Read all the results.
  vector<string> results;
  ASSERT_OK(IterateToStringList(iter.get(), &results));

  VLOG(1) << "Results of iterating over sparse partial schema: ";
  for (const string &str : results) {
    VLOG(1) << str;
  }

  // Ensure that we got the expected rows.
  ASSERT_EQ(results.size(), kNumRows);
  for (int i = 0; i < kNumRows; i++) {
    ASSERT_EQ(StringPrintf("(uint32 c0=%d, uint32 c2=%d)", i * 2, i * 100),
              results[i]);
  }
}

// Add a range predicate on the key column and ensure that only the relevant small number of rows
// are read off disk.
TEST_F(TestCFileSet, TestRangeScan) {
  const int kNumRows = 10000;
  WriteTestRowSet(kNumRows);

  shared_ptr<CFileSet> fileset(new CFileSet(rowset_meta_));
  ASSERT_OK(fileset->Open());

  // Create iterator.
  shared_ptr<CFileSet::Iterator> cfile_iter(fileset->NewIterator(&schema_));
  gscoped_ptr<RowwiseIterator> iter(new MaterializingIterator(cfile_iter));
  Schema key_schema = schema_.CreateKeyProjection();
  Arena arena(1024, 256 * 1024);
  RangePredicateEncoder encoder(&key_schema, &arena);

  // Create a scan with a range predicate on the key column.
  ScanSpec spec;
  uint32_t lower = 2000;
  uint32_t upper = 2009;
  ColumnRangePredicate pred1(schema_.column(0), &lower, &upper);
  spec.AddPredicate(pred1);
  encoder.EncodeRangePredicates(&spec, true);
  ASSERT_OK(iter->Init(&spec));

  // Check that the bounds got pushed as index bounds.
  // Since the key column is the rowidx * 2, we need to divide the integer bounds
  // back down.
  EXPECT_EQ(lower / 2, cfile_iter->lower_bound_idx_);
  // + 1 because the upper bound is exclusive
  EXPECT_EQ(upper / 2 + 1, cfile_iter->upper_bound_idx_);

  // Read all the results.
  vector<string> results;
  ASSERT_OK(IterateToStringList(iter.get(), &results));

  // Ensure that we got the expected rows.
  for (const string &str : results) {
    LOG(INFO) << str;
  }
  ASSERT_EQ(5, results.size());
  EXPECT_EQ("(uint32 c0=2000, uint32 c1=10000, uint32 c2=100000)", results[0]);
  EXPECT_EQ("(uint32 c0=2008, uint32 c1=10040, uint32 c2=100400)", results[4]);

  // Ensure that we only read the relevant range from all of the columns.
  // Since it's a small range, it should be all in one data block in each column.
  vector<IteratorStats> stats;
  iter->GetIteratorStats(&stats);
  EXPECT_EQ(stats[0].data_blocks_read_from_disk, 1);
  EXPECT_EQ(stats[1].data_blocks_read_from_disk, 1);
  EXPECT_EQ(stats[2].data_blocks_read_from_disk, 1);
}

// Several other black-box tests for range scans. These are similar to
// TestRangeScan above, except don't inspect internal state.
TEST_F(TestCFileSet, TestRangePredicates2) {
  const int kNumRows = 10000;
  WriteTestRowSet(kNumRows);

  shared_ptr<CFileSet> fileset(new CFileSet(rowset_meta_));
  ASSERT_OK(fileset->Open());

  // Range scan where rows match on both ends
  DoTestRangeScan(fileset, 2000, 2010);
  // Range scan which falls between rows on both ends
  DoTestRangeScan(fileset, 2001, 2009);
  // Range scan with open lower bound
  DoTestRangeScan(fileset, kNoBound, 2009);
  // Range scan with open upper bound
  DoTestRangeScan(fileset, 2001, kNoBound);
  // Range scan with upper bound coming at end of data
  DoTestRangeScan(fileset, 2001, kNumRows * 2);
  // Range scan with upper bound coming after end of data
  DoTestRangeScan(fileset, 2001, kNumRows * 10);
  // Range scan with lower bound coming at end of data
  DoTestRangeScan(fileset, kNumRows * 2, kNoBound);
  // Range scan with lower bound coming after end of data
  DoTestRangeScan(fileset, kNumRows * 10, kNoBound);
}


} // namespace tablet
} // namespace kudu
