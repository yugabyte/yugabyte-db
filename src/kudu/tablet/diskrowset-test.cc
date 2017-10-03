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

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>
#include <time.h>

#include "kudu/common/row.h"
#include "kudu/common/schema.h"
#include "kudu/gutil/algorithm.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/tablet/delta_compaction.h"
#include "kudu/tablet/diskrowset.h"
#include "kudu/tablet/diskrowset-test-base.h"
#include "kudu/tablet/tablet-test-util.h"
#include "kudu/util/env.h"
#include "kudu/util/status.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_macros.h"

DEFINE_double(update_fraction, 0.1f, "fraction of rows to update");
DECLARE_bool(cfile_lazy_open);
DECLARE_int32(cfile_default_block_size);
DECLARE_double(tablet_delta_store_major_compact_min_ratio);
DECLARE_int32(tablet_delta_store_minor_compact_max);

using std::is_sorted;
using std::shared_ptr;
using std::unordered_set;

namespace kudu {
namespace tablet {

// TODO: add test which calls CopyNextRows on an iterator with no more
// rows - i think it segfaults!

// Test round-trip writing and reading back a rowset with
// multiple columns. Does not test any modifications.
TEST_F(TestRowSet, TestRowSetRoundTrip) {
  WriteTestRowSet();

  // Now open the DiskRowSet for read
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));

  // First iterate over all columns
  LOG_TIMING(INFO, "Iterating over all columns") {
    IterateProjection(*rs, schema_, n_rows_);
  }

  // Now iterate only over the key column
  Schema proj_key;
  ASSERT_OK(schema_.CreateProjectionByNames({ "key" }, &proj_key));

  LOG_TIMING(INFO, "Iterating over only key column") {
    IterateProjection(*rs, proj_key, n_rows_);
  }


  // Now iterate only over the non-key column
  Schema proj_val;
  ASSERT_OK(schema_.CreateProjectionByNames({ "val" }, &proj_val));
  LOG_TIMING(INFO, "Iterating over only val column") {
    IterateProjection(*rs, proj_val, n_rows_);
  }

  // Test that CheckRowPresent returns correct results
  ProbeStats stats;

  // 1. Check a key which comes before all keys in rowset
  {
    RowBuilder rb(schema_.CreateKeyProjection());
    rb.AddString(Slice("h"));
    RowSetKeyProbe probe(rb.row());
    bool present;
    ASSERT_OK(rs->CheckRowPresent(probe, &present, &stats));
    ASSERT_FALSE(present);
  }

  // 2. Check a key which comes after all keys in rowset
  {
    RowBuilder rb(schema_.CreateKeyProjection());
    rb.AddString(Slice("z"));
    RowSetKeyProbe probe(rb.row());
    bool present;
    ASSERT_OK(rs->CheckRowPresent(probe, &present, &stats));
    ASSERT_FALSE(present);
  }

  // 3. Check a key which is not present, but comes between present
  // keys
  {
    RowBuilder rb(schema_.CreateKeyProjection());
    rb.AddString(Slice("hello 00000000000049x"));
    RowSetKeyProbe probe(rb.row());
    bool present;
    ASSERT_OK(rs->CheckRowPresent(probe, &present, &stats));
    ASSERT_FALSE(present);
  }

  // 4. Check a key which is present
  {
    char buf[256];
    RowBuilder rb(schema_.CreateKeyProjection());
    FormatKey(49, buf, sizeof(buf));
    rb.AddString(Slice(buf));
    RowSetKeyProbe probe(rb.row());
    bool present;
    ASSERT_OK(rs->CheckRowPresent(probe, &present, &stats));
    ASSERT_TRUE(present);
  }
}

// Test writing a rowset, and then updating some rows in it.
TEST_F(TestRowSet, TestRowSetUpdate) {
  WriteTestRowSet();

  // Now open the DiskRowSet for read
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));

  // Add an update to the delta tracker for a number of keys
  // which exist. These updates will change the value to
  // equal idx*5 (whereas in the original data, value = idx)
  unordered_set<uint32_t> updated;
  UpdateExistingRows(rs.get(), FLAGS_update_fraction, &updated);
  ASSERT_EQ(static_cast<int>(n_rows_ * FLAGS_update_fraction),
            rs->delta_tracker_->dms_->Count());

  // Try to add a mutation for a key not in the file (but which falls
  // between two valid keys)
  faststring buf;
  RowChangeListEncoder enc(&buf);
  enc.SetToDelete();

  Timestamp timestamp(0);
  RowBuilder rb(schema_.CreateKeyProjection());
  rb.AddString(Slice("hello 00000000000049x"));
  RowSetKeyProbe probe(rb.row());

  OperationResultPB result;
  ProbeStats stats;
  Status s = rs->MutateRow(timestamp, probe, enc.as_changelist(), op_id_, &stats, &result);
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ(0, result.mutated_stores_size());

  // Now read back the value column, and verify that the updates
  // are visible.
  VerifyUpdates(*rs, updated);
}

TEST_F(TestRowSet, TestRandomRead) {
  // Write 100 rows.
  WriteTestRowSet(100);
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));

  // Read un-updated row.
  VerifyRandomRead(*rs, "hello 000000000000050",
                   "(string key=hello 000000000000050, uint32 val=50)");
  NO_FATALS();

  // Update the row.
  OperationResultPB result;
  ASSERT_OK(UpdateRow(rs.get(), 50, 12345, &result));

  // Read it again -- should see the updated value.
  VerifyRandomRead(*rs, "hello 000000000000050",
                   "(string key=hello 000000000000050, uint32 val=12345)");
  NO_FATALS();

  // Try to read a row which comes before the first key.
  // This should return no rows.
  VerifyRandomRead(*rs, "aaaaa", "");
  NO_FATALS();

  // Same with a row which falls between keys.
  VerifyRandomRead(*rs, "hello 000000000000050_between_keys", "");
  NO_FATALS();

  // And a row which falls after the last key.
  VerifyRandomRead(*rs, "hello 000000000000101", "");
  NO_FATALS();
}

// Test Delete() support within a DiskRowSet.
TEST_F(TestRowSet, TestDelete) {
  // Write and open a DiskRowSet with 2 rows.
  WriteTestRowSet(2);
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));
  MvccSnapshot snap_before_delete(mvcc_);

  // Delete one of the two rows
  OperationResultPB result;
  ASSERT_OK(DeleteRow(rs.get(), 0, &result));
  ASSERT_EQ(1, result.mutated_stores_size());
  ASSERT_EQ(0L, result.mutated_stores(0).rs_id());
  ASSERT_EQ(0L, result.mutated_stores(0).dms_id());
  MvccSnapshot snap_after_delete(mvcc_);

  vector<string> rows;
  Status s;

  for (int i = 0; i < 2; i++) {
    // Reading the MVCC snapshot prior to deletion should show the row.
    ASSERT_OK(DumpRowSet(*rs, schema_, snap_before_delete, &rows));
    ASSERT_EQ(2, rows.size());
    EXPECT_EQ("(string key=hello 000000000000000, uint32 val=0)", rows[0]);
    EXPECT_EQ("(string key=hello 000000000000001, uint32 val=1)", rows[1]);

    // Reading the MVCC snapshot after the deletion should hide the row.
    ASSERT_OK(DumpRowSet(*rs, schema_, snap_after_delete, &rows));
    ASSERT_EQ(1, rows.size());
    EXPECT_EQ("(string key=hello 000000000000001, uint32 val=1)", rows[0]);

    // Trying to delete or update the same row again should fail.
    OperationResultPB result;
    s = DeleteRow(rs.get(), 0, &result);
    ASSERT_TRUE(s.IsNotFound()) << "bad status: " << s.ToString();
    ASSERT_EQ(0, result.mutated_stores_size());
    result.Clear();
    s = UpdateRow(rs.get(), 0, 12345, &result);
    ASSERT_TRUE(s.IsNotFound()) << "bad status: " << s.ToString();
    ASSERT_EQ(0, result.mutated_stores_size());

    // CheckRowPresent should return false.
    bool present;
    ASSERT_OK(CheckRowPresent(*rs, 0, &present));
    EXPECT_FALSE(present);

    if (i == 1) {
      // Flush DMS. The second pass through the loop will re-verify that the
      // externally visible state of the layer has not changed.
      // deletions now in a DeltaFile.
      ASSERT_OK(rs->FlushDeltas());
    }
  }
}


TEST_F(TestRowSet, TestDMSFlush) {
  WriteTestRowSet();

  unordered_set<uint32_t> updated;

  // Now open the DiskRowSet for read
  {
    shared_ptr<DiskRowSet> rs;
    ASSERT_OK(OpenTestRowSet(&rs));

    // Add an update to the delta tracker for a number of keys
    // which exist. These updates will change the value to
    // equal idx*5 (whereas in the original data, value = idx)
    UpdateExistingRows(rs.get(), FLAGS_update_fraction, &updated);
    ASSERT_EQ(static_cast<int>(n_rows_ * FLAGS_update_fraction),
              rs->delta_tracker_->dms_->Count());

    ASSERT_OK(rs->FlushDeltas());

    // Check that the DiskRowSet's DMS has now been emptied.
    ASSERT_EQ(0, rs->delta_tracker_->dms_->Count());

    // Now read back the value column, and verify that the updates
    // are visible.
    SCOPED_TRACE("before reopen");
    VerifyUpdates(*rs, updated);
  }

  LOG(INFO) << "Reopening rowset ===============";
  // Close and re-open the rowset and ensure that the updates were
  // persistent.
  {
    shared_ptr<DiskRowSet> rs;
    ASSERT_OK(OpenTestRowSet(&rs));

    // Now read back the value column, and verify that the updates
    // are visible.
    SCOPED_TRACE("after reopen");
    VerifyUpdates(*rs, updated);
  }
}

// Test that when a single row is updated multiple times, we can query the
// historical values using MVCC, even after it is flushed.
TEST_F(TestRowSet, TestFlushedUpdatesRespectMVCC) {
  const Slice key_slice("row");

  // Write a single row into a new DiskRowSet.
  LOG_TIMING(INFO, "Writing rowset") {
    DiskRowSetWriter drsw(rowset_meta_.get(), &schema_,
                          BloomFilterSizing::BySizeAndFPRate(32*1024, 0.01f));

    ASSERT_OK(drsw.Open());

    RowBuilder rb(schema_);
    rb.AddString(key_slice);
    rb.AddUint32(1);
    ASSERT_OK_FAST(WriteRow(rb.data(), &drsw));
    ASSERT_OK(drsw.Finish());
  }


  // Reopen the rowset.
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));

  // Take a snapshot of the pre-update state.
  vector<MvccSnapshot> snaps;
  snaps.push_back(MvccSnapshot(mvcc_));


  // Update the single row multiple times, taking an MVCC snapshot
  // after each update.
  faststring update_buf;
  RowChangeListEncoder update(&update_buf);
  for (uint32_t i = 2; i <= 5; i++) {
    {
      ScopedTransaction tx(&mvcc_);
      tx.StartApplying();
      update.Reset();
      update.AddColumnUpdate(schema_.column(1), schema_.column_id(1), &i);
      RowBuilder rb(schema_.CreateKeyProjection());
      rb.AddString(key_slice);
      RowSetKeyProbe probe(rb.row());
      OperationResultPB result;
      ProbeStats stats;
      ASSERT_OK_FAST(rs->MutateRow(tx.timestamp(),
                                          probe,
                                          RowChangeList(update_buf),
                                          op_id_,
                                          &stats,
                                          &result));
      ASSERT_EQ(1, result.mutated_stores_size());
      ASSERT_EQ(0L, result.mutated_stores(0).rs_id());
      ASSERT_EQ(0L, result.mutated_stores(0).dms_id());
      tx.Commit();
    }
    snaps.push_back(MvccSnapshot(mvcc_));
  }

  // Ensure that MVCC is respected by reading the value at each of the stored
  // snapshots.
  ASSERT_EQ(5, snaps.size());
  for (int i = 0; i < 5; i++) {
    SCOPED_TRACE(i);
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(rs->NewRowIterator(&schema_, snaps[i], &iter));
    string data = InitAndDumpIterator(iter.Pass());
    EXPECT_EQ(StringPrintf("(string key=row, uint32 val=%d)", i + 1), data);
  }

  // Flush deltas to disk and ensure that the historical versions are still
  // accessible.
  ASSERT_OK(rs->FlushDeltas());

  for (int i = 0; i < 5; i++) {
    SCOPED_TRACE(i);
    gscoped_ptr<RowwiseIterator> iter;
    ASSERT_OK(rs->NewRowIterator(&schema_, snaps[i], &iter));
    string data = InitAndDumpIterator(iter.Pass());
    EXPECT_EQ(StringPrintf("(string key=row, uint32 val=%d)", i + 1), data);
  }

}

// Similar to TestDMSFlush above, except does not actually verify
// the results (since the verification step is expensive). Additionally,
// loops the "read" side of the benchmark a number of times, so that
// the speed of applying deltas during read can be micro-benchmarked.
//
// This is most usefully run with an invocation like:
// ./rowset-test --gtest_filter=\*Performance --roundtrip_num_rows=1000000
//    --n_read_passes=1000 --update_fraction=0.01
TEST_F(TestRowSet, TestDeltaApplicationPerformance) {
  WriteTestRowSet();

  // Now open the DiskRowSet for read
  {
    shared_ptr<DiskRowSet> rs;
    ASSERT_OK(OpenTestRowSet(&rs));

    BenchmarkIterationPerformance(*rs.get(),
      StringPrintf("Reading %zd rows prior to updates %d times",
                   n_rows_, FLAGS_n_read_passes));

    UpdateExistingRows(rs.get(), FLAGS_update_fraction, nullptr);

    BenchmarkIterationPerformance(*rs.get(),
      StringPrintf("Reading %zd rows with %.2f%% updates %d times (updates in DMS)",
                   n_rows_, FLAGS_update_fraction * 100.0f,
                   FLAGS_n_read_passes));
    ASSERT_OK(rs->FlushDeltas());

    BenchmarkIterationPerformance(*rs.get(),
      StringPrintf("Reading %zd rows with %.2f%% updates %d times (updates on disk)",
                   n_rows_, FLAGS_update_fraction * 100.0f,
                   FLAGS_n_read_passes));
  }
}

TEST_F(TestRowSet, TestRollingDiskRowSetWriter) {
  // Set small block size so that we can roll frequently. Otherwise
  // we couldn't output such small files.
  google::FlagSaver saver;
  FLAGS_cfile_default_block_size = 4096;

  RollingDiskRowSetWriter writer(tablet()->metadata(), schema_,
                                 BloomFilterSizing::BySizeAndFPRate(32*1024, 0.01f),
                                 64 * 1024); // roll every 64KB
  DoWriteTestRowSet(10000, &writer);

  // Should have rolled 4 times.
  vector<shared_ptr<RowSetMetadata> > metas;
  writer.GetWrittenRowSetMetadata(&metas);
  EXPECT_EQ(4, metas.size());
  for (const shared_ptr<RowSetMetadata>& meta : metas) {
    ASSERT_TRUE(meta->HasDataForColumnIdForTests(schema_.column_id(0)));
  }
}

TEST_F(TestRowSet, TestMakeDeltaIteratorMergerUnlocked) {
  WriteTestRowSet();

  // Now open the DiskRowSet for read
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));
  UpdateExistingRows(rs.get(), FLAGS_update_fraction, nullptr);
  ASSERT_OK(rs->FlushDeltas());
  DeltaTracker *dt = rs->delta_tracker();
  int num_stores = dt->redo_delta_stores_.size();
  vector<shared_ptr<DeltaStore> > compacted_stores;
  vector<BlockId> compacted_blocks;
  shared_ptr<DeltaIterator> merge_iter;
  ASSERT_OK(dt->MakeDeltaIteratorMergerUnlocked(0, num_stores - 1, &schema_,
                                                       &compacted_stores,
                                                       &compacted_blocks, &merge_iter));
  vector<string> results;
  ASSERT_OK(DebugDumpDeltaIterator(REDO, merge_iter.get(), schema_,
                                          ITERATE_OVER_ALL_ROWS,
                                          &results));
  for (const string &str : results) {
    VLOG(1) << str;
  }
  ASSERT_EQ(compacted_stores.size(), num_stores);
  ASSERT_EQ(compacted_blocks.size(), num_stores);
  ASSERT_TRUE(is_sorted(results.begin(), results.end()));
}

void BetweenZeroAndOne(double to_check) {
  ASSERT_LT(0, to_check);
  ASSERT_GT(1, to_check);
}

TEST_F(TestRowSet, TestCompactStores) {
  // With this setting, we want major compactions to basically always have a score.
  FLAGS_tablet_delta_store_major_compact_min_ratio = 0.0001;
  // With this setting, the perf improvement will be 0 until we have two files, at which point
  // it will be the expected ratio, then with three files we get the maximum improvement.
  FLAGS_tablet_delta_store_minor_compact_max = 3;
  // Turning this off so that we can call DeltaStoresCompactionPerfImprovementScore without having
  // to open the files after creating them.
  FLAGS_cfile_lazy_open = false;


  WriteTestRowSet();
  shared_ptr<DiskRowSet> rs;
  ASSERT_OK(OpenTestRowSet(&rs));
  ASSERT_EQ(0, rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MINOR_DELTA_COMPACTION));
  ASSERT_EQ(0, rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MAJOR_DELTA_COMPACTION));

  // Write a first delta file.
  UpdateExistingRows(rs.get(), FLAGS_update_fraction, nullptr);
  ASSERT_OK(rs->FlushDeltas());
  // One file isn't enough for minor compactions, but a major compaction can run.
  ASSERT_EQ(0, rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MINOR_DELTA_COMPACTION));
  BetweenZeroAndOne(rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MAJOR_DELTA_COMPACTION));

  // Write a second delta file.
  UpdateExistingRows(rs.get(), FLAGS_update_fraction, nullptr);
  ASSERT_OK(rs->FlushDeltas());
  // Two files is enough for all delta compactions.
  BetweenZeroAndOne(rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MINOR_DELTA_COMPACTION));
  BetweenZeroAndOne(rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MAJOR_DELTA_COMPACTION));

  // Write a third delta file.
  UpdateExistingRows(rs.get(), FLAGS_update_fraction, nullptr);
  ASSERT_OK(rs->FlushDeltas());
  // We're hitting the max for minor compactions but not for major compactions.
  ASSERT_EQ(1, rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MINOR_DELTA_COMPACTION));
  BetweenZeroAndOne(rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MAJOR_DELTA_COMPACTION));

  // Compact the deltafiles
  DeltaTracker *dt = rs->delta_tracker();
  int num_stores = dt->redo_delta_stores_.size();
  VLOG(1) << "Number of stores before compaction: " << num_stores;
  ASSERT_EQ(num_stores, 3);
  ASSERT_OK(dt->CompactStores(0, num_stores - 1));
  num_stores = dt->redo_delta_stores_.size();
  VLOG(1) << "Number of stores after compaction: " << num_stores;
  ASSERT_EQ(1,  num_stores);
  // Back to one store, can't minor compact.
  ASSERT_EQ(0, rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MINOR_DELTA_COMPACTION));
  BetweenZeroAndOne(rs->DeltaStoresCompactionPerfImprovementScore(RowSet::MAJOR_DELTA_COMPACTION));

  // Verify that the resulting deltafile is valid
  vector<shared_ptr<DeltaStore> > compacted_stores;
  vector<BlockId> compacted_blocks;
  shared_ptr<DeltaIterator> merge_iter;
  ASSERT_OK(dt->MakeDeltaIteratorMergerUnlocked(0, num_stores - 1, &schema_,
                                                       &compacted_stores,
                                                       &compacted_blocks, &merge_iter));
  vector<string> results;
  ASSERT_OK(DebugDumpDeltaIterator(REDO, merge_iter.get(), schema_,
                                          ITERATE_OVER_ALL_ROWS,
                                          &results));
  for (const string &str : results) {
    VLOG(1) << str;
  }
  ASSERT_TRUE(is_sorted(results.begin(), results.end()));
}

} // namespace tablet
} // namespace kudu
