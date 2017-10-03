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

#include <algorithm>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>

#include "kudu/common/partial_row.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/fs/log_block_manager.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/server/logical_clock.h"
#include "kudu/tablet/compaction.h"
#include "kudu/tablet/local_tablet_writer.h"
#include "kudu/tablet/tablet-test-util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"

DEFINE_string(merge_benchmark_input_dir, "",
              "Directory to benchmark merge. The benchmark will merge "
              "all rowsets from this directory, pointed by the super-block "
              "with id 00000 or 1111 and tablet id 'KuduCompactionBenchTablet', "
              "if this is specified. Otherwise, inputs will "
              "be generated as part of the test itself.");
DEFINE_int32(merge_benchmark_num_rowsets, 3,
             "Number of rowsets as input to the merge");
DEFINE_int32(merge_benchmark_num_rows_per_rowset, 500000,
             "Number of rowsets as input to the merge");

DECLARE_string(block_manager);
DECLARE_bool(enable_data_block_fsync);

using std::shared_ptr;

namespace kudu {
namespace tablet {

using consensus::OpId;
using log::LogAnchorRegistry;
using strings::Substitute;

static const char *kRowKeyFormat = "hello %08d";
static const size_t kLargeRollThreshold = 1024 * 1024 * 1024; // 1GB
static const size_t kSmallRollThreshold = 1024; // 1KB

class TestCompaction : public KuduRowSetTest {
 public:
  TestCompaction()
    : KuduRowSetTest(CreateSchema()),
      op_id_(consensus::MaximumOpId()),
      row_builder_(schema_),
      mvcc_(scoped_refptr<server::Clock>(
              server::LogicalClock::CreateStartingAt(Timestamp::kInitialTimestamp))),
      log_anchor_registry_(new log::LogAnchorRegistry()) {
  }

  static Schema CreateSchema() {
    SchemaBuilder builder;
    CHECK_OK(builder.AddKeyColumn("key", STRING));
    CHECK_OK(builder.AddColumn("val", INT32));
    CHECK_OK(builder.AddNullableColumn("nullable_val", INT32));
    return builder.BuildWithoutIds();
  }

  // Insert n_rows rows of data.
  // Each row is the tuple: (string key=hello <n*10 + delta>, val=<n>)
  void InsertRows(MemRowSet *mrs, int n_rows, int delta) {
    for (int32_t i = 0; i < n_rows; i++) {
      InsertRow(mrs, i * 10 + delta, i);
    }
  }

  // Inserts a row.
  // The 'nullable_val' column is set to either NULL (when val is odd)
  // or 'val' (when val is even).
  void InsertRow(MemRowSet *mrs, int row_key, int32_t val) {
    ScopedTransaction tx(&mvcc_);
    tx.StartApplying();
    row_builder_.Reset();
    snprintf(key_buf_, sizeof(key_buf_), kRowKeyFormat, row_key);
    row_builder_.AddString(Slice(key_buf_));
    row_builder_.AddInt32(val);
    if (val % 2 == 0) {
      row_builder_.AddInt32(val);
    } else {
      row_builder_.AddNull();
    }
    if (!mrs->schema().Equals(row_builder_.schema())) {
      // The MemRowSet is not projecting the row, so must be done by the caller
      RowProjector projector(&row_builder_.schema(), &mrs->schema());
      uint8_t rowbuf[ContiguousRowHelper::row_size(mrs->schema())];
      ContiguousRow dst_row(&mrs->schema(), rowbuf);
      ASSERT_OK_FAST(projector.Init());
      ASSERT_OK_FAST(projector.ProjectRowForWrite(row_builder_.row(),
                            &dst_row, static_cast<Arena*>(nullptr)));
      ASSERT_OK_FAST(mrs->Insert(tx.timestamp(), ConstContiguousRow(dst_row), op_id_));
    } else {
      ASSERT_OK_FAST(mrs->Insert(tx.timestamp(), row_builder_.row(), op_id_));
    }
    tx.Commit();
  }

  // Update n_rows rows of data.
  // Each row has the key (string key=hello <n*10 + delta>) and its 'val' column
  // is set to new_val.
  // If 'val' is even, 'nullable_val' is set to NULL. Otherwise, set to 'val'.
  // Note that this is the opposite of InsertRow() above, so that the updates
  // flop NULL to non-NULL and vice versa.
  void UpdateRows(RowSet *rowset, int n_rows, int delta, int32_t new_val) {
    char keybuf[256];
    faststring update_buf;
    ColumnId col_id = schema_.column_id(schema_.find_column("val"));
    ColumnId nullable_col_id = schema_.column_id(schema_.find_column("nullable_val"));
    for (uint32_t i = 0; i < n_rows; i++) {
      SCOPED_TRACE(i);
      ScopedTransaction tx(&mvcc_);
      tx.StartApplying();
      snprintf(keybuf, sizeof(keybuf), kRowKeyFormat, i * 10 + delta);

      update_buf.clear();
      RowChangeListEncoder update(&update_buf);
      update.AddColumnUpdate(schema_.column_by_id(col_id), col_id, &new_val);
      if (new_val % 2 == 0) {
        update.AddColumnUpdate(schema_.column_by_id(nullable_col_id),
                               nullable_col_id, nullptr);
      } else {
        update.AddColumnUpdate(schema_.column_by_id(nullable_col_id),
                               nullable_col_id, &new_val);
      }

      RowBuilder rb(schema_.CreateKeyProjection());
      rb.AddString(Slice(keybuf));
      RowSetKeyProbe probe(rb.row());
      ProbeStats stats;
      OperationResultPB result;
      ASSERT_OK(rowset->MutateRow(tx.timestamp(),
                                         probe,
                                         RowChangeList(update_buf),
                                         op_id_,
                                         &stats,
                                         &result));
      tx.Commit();
    }
  }

  void DeleteRows(RowSet *rowset, int n_rows, int delta) {
    char keybuf[256];
    faststring update_buf;
    for (uint32_t i = 0; i < n_rows; i++) {
      SCOPED_TRACE(i);
      ScopedTransaction tx(&mvcc_);
      tx.StartApplying();
      snprintf(keybuf, sizeof(keybuf), kRowKeyFormat, i * 10 + delta);

      update_buf.clear();
      RowChangeListEncoder update(&update_buf);
      update.SetToDelete();

      RowBuilder rb(schema_.CreateKeyProjection());
      rb.AddString(Slice(keybuf));
      RowSetKeyProbe probe(rb.row());
      ProbeStats stats;
      OperationResultPB result;
      ASSERT_OK(rowset->MutateRow(tx.timestamp(),
                                         probe,
                                         RowChangeList(update_buf),
                                         op_id_,
                                         &stats,
                                         &result));
      tx.Commit();
    }
  }

  // Iterate over the given compaction input, stringifying and dumping each
  // yielded row to *out
  void IterateInput(CompactionInput *input, vector<string> *out) {
    ASSERT_OK(DebugDumpCompactionInput(input, out));
  }

  // Flush the given CompactionInput 'input' to disk with the given snapshot.
  // If 'result_rowsets' is not NULL, reopens the resulting rowset(s) and appends
  // them to the vector.
  void DoFlushAndReopen(
      CompactionInput *input, const Schema& projection, const MvccSnapshot &snap,
      int64_t roll_threshold, vector<shared_ptr<DiskRowSet> >* result_rowsets) {
    // Flush with a large roll threshold so we only write a single file.
    // This simplifies the test so we always need to reopen only a single rowset.
    RollingDiskRowSetWriter rsw(tablet()->metadata(), projection,
                                BloomFilterSizing::BySizeAndFPRate(32*1024, 0.01f),
                                roll_threshold);
    ASSERT_OK(rsw.Open());
    ASSERT_OK(FlushCompactionInput(input, snap, &rsw));
    ASSERT_OK(rsw.Finish());

    vector<shared_ptr<RowSetMetadata> > metas;
    rsw.GetWrittenRowSetMetadata(&metas);
    ASSERT_GE(metas.size(), 1);
    for (const shared_ptr<RowSetMetadata>& meta : metas) {
      ASSERT_TRUE(meta->HasBloomDataBlockForTests());
    }
    if (result_rowsets) {
      // Re-open the outputs
      for (const shared_ptr<RowSetMetadata>& meta : metas) {
        shared_ptr<DiskRowSet> rs;
        ASSERT_OK(DiskRowSet::Open(meta, log_anchor_registry_.get(), &rs));
        result_rowsets->push_back(rs);
      }
    }
  }

  Status BuildCompactionInput(const MvccSnapshot& merge_snap,
                              const vector<shared_ptr<DiskRowSet> >& rowsets,
                              const Schema& projection,
                              gscoped_ptr<CompactionInput>* out) {
    vector<shared_ptr<CompactionInput> > merge_inputs;
    for (const shared_ptr<DiskRowSet> &rs : rowsets) {
      gscoped_ptr<CompactionInput> input;
      RETURN_NOT_OK(CompactionInput::Create(*rs, &projection, merge_snap, &input));
      merge_inputs.push_back(shared_ptr<CompactionInput>(input.release()));
    }
    out->reset(CompactionInput::Merge(merge_inputs, &projection));
    return Status::OK();
  }

  // Compacts a set of DRSs.
  // If 'result_rowsets' is not NULL, reopens the resulting rowset(s) and appends
  // them to the vector.
  Status CompactAndReopen(const vector<shared_ptr<DiskRowSet> >& rowsets,
                          const Schema& projection, int64_t roll_threshold,
                          vector<shared_ptr<DiskRowSet> >* result_rowsets) {
    MvccSnapshot merge_snap(mvcc_);
    gscoped_ptr<CompactionInput> compact_input;
    RETURN_NOT_OK(BuildCompactionInput(merge_snap, rowsets, projection, &compact_input));
    DoFlushAndReopen(compact_input.get(), projection, merge_snap, roll_threshold,
                     result_rowsets);
    return Status::OK();
  }

  // Same as above, but sets a high roll threshold so it only produces a single output.
  void CompactAndReopenNoRoll(const vector<shared_ptr<DiskRowSet> >& input_rowsets,
                              const Schema& projection,
                              shared_ptr<DiskRowSet>* result_rs) {
    vector<shared_ptr<DiskRowSet> > result_rowsets;
    CompactAndReopen(input_rowsets, projection, kLargeRollThreshold, &result_rowsets);
    ASSERT_EQ(1, result_rowsets.size());
    *result_rs = result_rowsets[0];
  }

  // Flush an MRS to disk.
  // If 'result_rowsets' is not NULL, reopens the resulting rowset(s) and appends
  // them to the vector.
  void FlushMRSAndReopen(const MemRowSet& mrs, const Schema& projection,
                         int64_t roll_threshold,
                         vector<shared_ptr<DiskRowSet> >* result_rowsets) {
    MvccSnapshot snap(mvcc_);
    vector<shared_ptr<RowSetMetadata> > rowset_metas;
    gscoped_ptr<CompactionInput> input(CompactionInput::Create(mrs, &projection, snap));
    DoFlushAndReopen(input.get(), projection, snap, roll_threshold, result_rowsets);
  }

  // Same as above, but sets a high roll threshold so it only produces a single output.
  void FlushMRSAndReopenNoRoll(const MemRowSet& mrs, const Schema& projection,
                            shared_ptr<DiskRowSet>* result_rs) {
    vector<shared_ptr<DiskRowSet> > rowsets;
    FlushMRSAndReopen(mrs, projection, kLargeRollThreshold, &rowsets);
    ASSERT_EQ(1, rowsets.size());
    *result_rs = rowsets[0];
  }

  // Test compaction where each of the input rowsets has
  // each of the input schemas. The output rowset will
  // have the 'projection' schema.
  void DoMerge(const Schema& projection, const vector<Schema>& schemas) {
    vector<shared_ptr<DiskRowSet> > rowsets;

    // Create one input rowset for each of the input schemas
    int delta = 0;
    for (const Schema& schema : schemas) {
      // Create a memrowset with a bunch of rows and updates.
      shared_ptr<MemRowSet> mrs(new MemRowSet(delta, schema, log_anchor_registry_.get()));
      InsertRows(mrs.get(), 1000, delta);
      UpdateRows(mrs.get(), 1000, delta, 1);

      // Flush it to disk and re-open it.
      shared_ptr<DiskRowSet> rs;
      FlushMRSAndReopenNoRoll(*mrs, schema, &rs);
      ASSERT_NO_FATAL_FAILURE();
      rowsets.push_back(rs);

      // Perform some updates into DMS
      UpdateRows(rs.get(), 1000, delta, 2);
      delta++;
    }

    // Merge them.
    shared_ptr<DiskRowSet> result_rs;
    ASSERT_NO_FATAL_FAILURE(CompactAndReopenNoRoll(rowsets, projection, &result_rs));

    // Verify the resulting compaction output has the right number
    // of rows.
    rowid_t count = 0;
    ASSERT_OK(result_rs->CountRows(&count));
    ASSERT_EQ(1000 * schemas.size(), count);
  }

  template<bool OVERLAP_INPUTS>
  void DoBenchmark() {
    vector<shared_ptr<DiskRowSet> > rowsets;

    if (FLAGS_merge_benchmark_input_dir.empty()) {
      // Create inputs.
      for (int i = 0; i < FLAGS_merge_benchmark_num_rowsets; i++) {
        // Create a memrowset with a bunch of rows and updates.
        shared_ptr<MemRowSet> mrs(new MemRowSet(i, schema_, log_anchor_registry_.get()));

        for (int n = 0; n < FLAGS_merge_benchmark_num_rows_per_rowset; n++) {

          int row_key;
          if (OVERLAP_INPUTS) {
            // input 0: 0 3 6 9 ...
            // input 1: 1 4 7 10 ...
            // input 2: 2 5 8 11 ...
            row_key = n * FLAGS_merge_benchmark_num_rowsets + i;
          } else {
            // input 0: 0 1 2 3
            // input 1: 1000 1001 1002 1003
            // ...
            row_key = i * FLAGS_merge_benchmark_num_rows_per_rowset + n;
          }
          InsertRow(mrs.get(), row_key, n);
        }
        shared_ptr<DiskRowSet> rs;
        FlushMRSAndReopenNoRoll(*mrs, schema_, &rs);
        ASSERT_NO_FATAL_FAILURE();
        rowsets.push_back(rs);
      }
    } else {
      string tablet_id = "KuduCompactionBenchTablet";
      FsManager fs_manager(env_.get(), FLAGS_merge_benchmark_input_dir);
      scoped_refptr<TabletMetadata> input_meta;
      ASSERT_OK(TabletMetadata::Load(&fs_manager, tablet_id, &input_meta));

      for (const shared_ptr<RowSetMetadata>& meta : input_meta->rowsets()) {
        shared_ptr<DiskRowSet> rs;
        CHECK_OK(DiskRowSet::Open(meta, log_anchor_registry_.get(), &rs));
        rowsets.push_back(rs);
      }

      CHECK(!rowsets.empty()) << "No rowsets found in " << FLAGS_merge_benchmark_input_dir;
    }
    LOG(INFO) << "Beginning compaction";
    LOG_TIMING(INFO, "compacting " +
               std::string((OVERLAP_INPUTS ? "with overlap" : "without overlap"))) {
      MvccSnapshot merge_snap(mvcc_);
      gscoped_ptr<CompactionInput> compact_input;
      ASSERT_OK(BuildCompactionInput(merge_snap, rowsets, schema_, &compact_input));
      // Use a low target row size to increase the number of resulting rowsets.
      RollingDiskRowSetWriter rdrsw(tablet()->metadata(), schema_,
                                    BloomFilterSizing::BySizeAndFPRate(32 * 1024, 0.01f),
                                    1024 * 1024); // 1 MB
      ASSERT_OK(rdrsw.Open());
      ASSERT_OK(FlushCompactionInput(compact_input.get(), merge_snap, &rdrsw));
      ASSERT_OK(rdrsw.Finish());
    }
  }

  Status GetDataDiskSpace(uint64_t* bytes_used) {
    *bytes_used = 0;
    return env_->Walk(fs_manager()->GetDataRootDirs().at(0),
                      Env::PRE_ORDER, Bind(&TestCompaction::GetDataDiskSpaceCb,
                                           Unretained(this), bytes_used));
  }

 protected:
  OpId op_id_;

  RowBuilder row_builder_;
  char key_buf_[256];
  MvccManager mvcc_;

  scoped_refptr<LogAnchorRegistry> log_anchor_registry_;

 private:

  Status GetDataDiskSpaceCb(uint64_t* bytes_used,
                            Env::FileType type,
                            const string& dirname, const string& basename) {
    uint64_t file_bytes_used = 0;
    switch (type) {
      case Env::FILE_TYPE:
        RETURN_NOT_OK(env_->GetFileSizeOnDisk(
            JoinPathSegments(dirname, basename), &file_bytes_used));
        *bytes_used += file_bytes_used;
        break;
      case Env::DIRECTORY_TYPE:
        // Ignore directory space consumption; it varies from filesystem to
        // filesystem and isn't interesting for this test.
        break;
      default:
        LOG(FATAL) << "Unknown file type: " << type;
    }
    return Status::OK();
  }
};

TEST_F(TestCompaction, TestMemRowSetInput) {
  // Create a memrowset with 10 rows and several updates.
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));
  InsertRows(mrs.get(), 10, 0);
  UpdateRows(mrs.get(), 10, 0, 1);
  UpdateRows(mrs.get(), 10, 0, 2);

  // Ensure that the compaction input yields the expected rows
  // and mutations.
  vector<string> out;
  MvccSnapshot snap(mvcc_);
  gscoped_ptr<CompactionInput> input(CompactionInput::Create(*mrs, &schema_, snap));
  IterateInput(input.get(), &out);
  ASSERT_EQ(10, out.size());
  ASSERT_EQ("(string key=hello 00000000, int32 val=0, int32 nullable_val=0) "
      "Undos: [@1(DELETE)] "
      "Redos: [@11(SET val=1, nullable_val=1), @21(SET val=2, nullable_val=NULL)]",
            out[0]);
  ASSERT_EQ("(string key=hello 00000090, int32 val=9, int32 nullable_val=NULL) "
      "Undos: [@10(DELETE)] "
      "Redos: [@20(SET val=1, nullable_val=1), @30(SET val=2, nullable_val=NULL)]",
            out[9]);
}

TEST_F(TestCompaction, TestFlushMRSWithRolling) {
  // Create a memrowset with enough rows so that, when we flush with a small
  // roll threshold, we'll end up creating multiple DiskRowSets.
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));
  InsertRows(mrs.get(), 30000, 0);

  vector<shared_ptr<DiskRowSet> > rowsets;
  FlushMRSAndReopen(*mrs, schema_, kSmallRollThreshold, &rowsets);
  ASSERT_GT(rowsets.size(), 1);

  vector<string> rows;
  rows.reserve(30000 / 2);
  rowsets[0]->DebugDump(&rows);
  EXPECT_EQ("(string key=hello 00000000, int32 val=0, int32 nullable_val=0) "
            "Undos: [@1(DELETE)] Redos: []",
            rows[0]);

  rows.clear();
  rowsets[1]->DebugDump(&rows);
  EXPECT_EQ("(string key=hello 00154700, int32 val=15470, int32 nullable_val=15470) "
            "Undos: [@15471(DELETE)] Redos: []",
            rows[0]);
  EXPECT_EQ("(string key=hello 00154710, int32 val=15471, int32 nullable_val=NULL) "
            "Undos: [@15472(DELETE)] Redos: []",
            rows[1]);
}

TEST_F(TestCompaction, TestRowSetInput) {
  // Create a memrowset with a bunch of rows, flush and reopen.
  shared_ptr<DiskRowSet> rs;
  {
    shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));
    InsertRows(mrs.get(), 10, 0);
    FlushMRSAndReopenNoRoll(*mrs, schema_, &rs);
    ASSERT_NO_FATAL_FAILURE();
  }

  // Update the rows in the rowset.
  UpdateRows(rs.get(), 10, 0, 1);
  UpdateRows(rs.get(), 10, 0, 2);
  // Flush DMS, update some more.
  ASSERT_OK(rs->FlushDeltas());
  UpdateRows(rs.get(), 10, 0, 3);
  UpdateRows(rs.get(), 10, 0, 4);

  // Check compaction input
  vector<string> out;
  gscoped_ptr<CompactionInput> input;
  ASSERT_OK(CompactionInput::Create(*rs, &schema_, MvccSnapshot(mvcc_), &input));
  IterateInput(input.get(), &out);
  ASSERT_EQ(10, out.size());
  EXPECT_EQ("(string key=hello 00000000, int32 val=0, int32 nullable_val=0) "
            "Undos: [@1(DELETE)] "
            "Redos: ["
            "@11(SET val=1, nullable_val=1), "
            "@21(SET val=2, nullable_val=NULL), "
            "@31(SET val=3, nullable_val=3), "
            "@41(SET val=4, nullable_val=NULL)]",
            out[0]);
  EXPECT_EQ("(string key=hello 00000090, int32 val=9, int32 nullable_val=NULL) "
            "Undos: [@10(DELETE)] "
            "Redos: ["
            "@20(SET val=1, nullable_val=1), "
            "@30(SET val=2, nullable_val=NULL), "
            "@40(SET val=3, nullable_val=3), "
            "@50(SET val=4, nullable_val=NULL)]",
            out[9]);
}

// Tests that the same rows, duplicated in three DRSs, ghost in two of them
// appears only once on the compaction output
TEST_F(TestCompaction, TestDuplicatedGhostRowsDontSurviveCompaction) {
  shared_ptr<DiskRowSet> rs1;
  {
    shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));
    InsertRows(mrs.get(), 10, 0);
    FlushMRSAndReopenNoRoll(*mrs, schema_, &rs1);
    ASSERT_NO_FATAL_FAILURE();
  }
  // Now delete the rows, this will make the rs report them as deleted and
  // so we would reinsert them into the MRS.
  DeleteRows(rs1.get(), 10, 0);

  shared_ptr<DiskRowSet> rs2;
  {
    shared_ptr<MemRowSet> mrs(new MemRowSet(1, schema_, log_anchor_registry_.get()));
    InsertRows(mrs.get(), 10, 0);
    UpdateRows(mrs.get(), 10, 0, 1);
    FlushMRSAndReopenNoRoll(*mrs, schema_, &rs2);
    ASSERT_NO_FATAL_FAILURE();
  }
  DeleteRows(rs2.get(), 10, 0);

  shared_ptr<DiskRowSet> rs3;
  {
    shared_ptr<MemRowSet> mrs(new MemRowSet(1, schema_, log_anchor_registry_.get()));
    InsertRows(mrs.get(), 10, 0);
    UpdateRows(mrs.get(), 10, 0, 2);
    FlushMRSAndReopenNoRoll(*mrs, schema_, &rs3);
    ASSERT_NO_FATAL_FAILURE();
  }

  shared_ptr<DiskRowSet> result;
  vector<shared_ptr<DiskRowSet> > all_rss;
  all_rss.push_back(rs3);
  all_rss.push_back(rs1);
  all_rss.push_back(rs2);

  SeedRandom();
  // Shuffle the row sets to make sure we test different orderings
  std::random_shuffle(all_rss.begin(), all_rss.end());

  // Now compact all the drs and make sure we don't get duplicated keys on the output
  CompactAndReopenNoRoll(all_rss, schema_, &result);

  gscoped_ptr<CompactionInput> input;
  ASSERT_OK(CompactionInput::Create(*result,
                                    &schema_,
                                    MvccSnapshot::CreateSnapshotIncludingAllTransactions(),
                                    &input));
  vector<string> out;
  IterateInput(input.get(), &out);
  ASSERT_EQ(out.size(), 10);
  EXPECT_EQ("(string key=hello 00000000, int32 val=2, int32 nullable_val=NULL) "
      "Undos: [@61(SET val=0, nullable_val=0), @51(DELETE)] "
      "Redos: []", out[0]);
  EXPECT_EQ("(string key=hello 00000090, int32 val=2, int32 nullable_val=NULL) "
      "Undos: [@70(SET val=9, nullable_val=NULL), @60(DELETE)] "
      "Redos: []", out[9]);
}

// Test case which doesn't do any merging -- just compacts
// a single input rowset (which may be the memrowset) into a single
// output rowset (on disk).
TEST_F(TestCompaction, TestOneToOne) {
  // Create a memrowset with a bunch of rows and updates.
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));
  InsertRows(mrs.get(), 1000, 0);
  UpdateRows(mrs.get(), 1000, 0, 1);
  MvccSnapshot snap(mvcc_);

  // Flush it to disk and re-open.
  shared_ptr<DiskRowSet> rs;
  FlushMRSAndReopenNoRoll(*mrs, schema_, &rs);
  ASSERT_NO_FATAL_FAILURE();

  // Update the rows with some updates that weren't in the snapshot.
  UpdateRows(mrs.get(), 1000, 0, 2);

  // Catch the updates that came in after the snapshot flush was made.
  MvccSnapshot snap2(mvcc_);
  gscoped_ptr<CompactionInput> input(CompactionInput::Create(*mrs, &schema_, snap2));

  // Add some more updates which come into the new rowset while the "reupdate" is happening.
  UpdateRows(rs.get(), 1000, 0, 3);

  string dummy_name = "";

  ASSERT_OK(ReupdateMissedDeltas(dummy_name, input.get(), snap, snap2, { rs }));

  // If we look at the contents of the DiskRowSet now, we should see the "re-updated" data.
  vector<string> out;
  ASSERT_OK(CompactionInput::Create(*rs, &schema_, MvccSnapshot(mvcc_), &input));
  IterateInput(input.get(), &out);
  ASSERT_EQ(1000, out.size());
  EXPECT_EQ("(string key=hello 00000000, int32 val=1, int32 nullable_val=1) "
      "Undos: [@1001(SET val=0, nullable_val=0), @1(DELETE)] "
      "Redos: [@2001(SET val=2, nullable_val=NULL), "
              "@3001(SET val=3, nullable_val=3)]", out[0]);

  // And compact (1 input to 1 output)
  MvccSnapshot snap3(mvcc_);
  gscoped_ptr<CompactionInput> compact_input;
  ASSERT_OK(CompactionInput::Create(*rs, &schema_, snap3, &compact_input));
  DoFlushAndReopen(compact_input.get(), schema_, snap3, kLargeRollThreshold, nullptr);
}

// Test merging two row sets and the second one has updates, KUDU-102
// We re-create the conditions by providing two DRS that are both the input and the
// output of a compaction, and trying to merge two MRS.
TEST_F(TestCompaction, TestKUDU102) {
  // Create 2 row sets, flush them
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));
  InsertRows(mrs.get(), 10, 0);
  shared_ptr<DiskRowSet> rs;
  FlushMRSAndReopenNoRoll(*mrs, schema_, &rs);
  ASSERT_NO_FATAL_FAILURE();

  shared_ptr<MemRowSet> mrs_b(new MemRowSet(1, schema_, log_anchor_registry_.get()));
  InsertRows(mrs_b.get(), 10, 100);
  MvccSnapshot snap(mvcc_);
  shared_ptr<DiskRowSet> rs_b;
  FlushMRSAndReopenNoRoll(*mrs_b, schema_, &rs_b);
  ASSERT_NO_FATAL_FAILURE();

  // Update all the rows in the second row set
  UpdateRows(mrs_b.get(), 10, 100, 2);

  // Catch the updates that came in after the snapshot flush was made.
  // Note that we are merging two MRS, it's a hack
  MvccSnapshot snap2(mvcc_);
  vector<shared_ptr<CompactionInput> > merge_inputs;
  merge_inputs.push_back(
        shared_ptr<CompactionInput>(CompactionInput::Create(*mrs, &schema_, snap2)));
  merge_inputs.push_back(
        shared_ptr<CompactionInput>(CompactionInput::Create(*mrs_b, &schema_, snap2)));
  gscoped_ptr<CompactionInput> input(CompactionInput::Merge(merge_inputs, &schema_));

  string dummy_name = "";

  // This would fail without KUDU-102
  ASSERT_OK(ReupdateMissedDeltas(dummy_name, input.get(), snap, snap2, { rs, rs_b }));
}


// Test compacting when all of the inputs and the output have the same schema
TEST_F(TestCompaction, TestMerge) {
  vector<Schema> schemas;
  schemas.push_back(schema_);
  schemas.push_back(schema_);
  schemas.push_back(schema_);
  DoMerge(schemas.back(), schemas);
}

// test compacting when the inputs have different base schemas
TEST_F(TestCompaction, TestMergeMultipleSchemas) {
  vector<Schema> schemas;
  SchemaBuilder builder(schema_);
  schemas.push_back(schema_);

  // Add an int column with default
  int32_t default_c2 = 10;
  CHECK_OK(builder.AddColumn("c2", INT32, false, &default_c2, &default_c2));
  schemas.push_back(builder.Build());

  // add a string column with default
  Slice default_c3("Hello World");
  CHECK_OK(builder.AddColumn("c3", STRING, false, &default_c3, &default_c3));
  schemas.push_back(builder.Build());

  DoMerge(schemas.back(), schemas);
}

// Test MergeCompactionInput against MemRowSets. This behavior isn't currently
// used (we never compact in-memory), but this is a regression test for a bug
// encountered during development where the first row of each MRS got dropped.
TEST_F(TestCompaction, TestMergeMRS) {
  shared_ptr<MemRowSet> mrs_a(new MemRowSet(0, schema_, log_anchor_registry_.get()));
  InsertRows(mrs_a.get(), 10, 0);

  shared_ptr<MemRowSet> mrs_b(new MemRowSet(1, schema_, log_anchor_registry_.get()));
  InsertRows(mrs_b.get(), 10, 1);

  MvccSnapshot snap(mvcc_);
  vector<shared_ptr<CompactionInput> > merge_inputs;
  merge_inputs.push_back(
        shared_ptr<CompactionInput>(CompactionInput::Create(*mrs_a, &schema_, snap)));
  merge_inputs.push_back(
        shared_ptr<CompactionInput>(CompactionInput::Create(*mrs_b, &schema_, snap)));
  gscoped_ptr<CompactionInput> input(CompactionInput::Merge(merge_inputs, &schema_));

  vector<string> out;
  IterateInput(input.get(), &out);
  ASSERT_EQ(out.size(), 20);
  EXPECT_EQ("(string key=hello 00000000, int32 val=0, int32 nullable_val=0) "
            "Undos: [@1(DELETE)] Redos: []", out[0]);
  EXPECT_EQ("(string key=hello 00000091, int32 val=9, int32 nullable_val=NULL) "
            "Undos: [@20(DELETE)] Redos: []", out[19]);
}

#ifdef NDEBUG
// Benchmark for the compaction merge input for the case where the inputs
// contain non-overlapping data. In this case the merge can be optimized
// to be block-wise.
TEST_F(TestCompaction, BenchmarkMergeWithoutOverlap) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipped: must enable slow tests.";
    return;
  }
  ASSERT_NO_FATAL_FAILURE(DoBenchmark<false>());
}

// Benchmark for the compaction merge input when the inputs are entirely
// overlapping (i.e the inputs become fully interleaved in the output)
TEST_F(TestCompaction, BenchmarkMergeWithOverlap) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipped: must enable slow tests.";
    return;
  }
  ASSERT_NO_FATAL_FAILURE(DoBenchmark<true>());
}
#endif

TEST_F(TestCompaction, TestCompactionFreesDiskSpace) {
  // On RHEL 6.4 with an ext4 filesystem mounted as ext3, it was observed
  // that freshly created files report st_blocks=0 via stat(2) for several
  // seconds. This appears to be some buggy interaction with ext4 delalloc.
  //
  // Enabling data block fsync appears to work around the problem. We do
  // that here and not for all tests because:
  // 1. fsync is expensive, and
  // 2. This is the only test that cares about disk space usage and can't
  //    explicitly fsync() after writing new files.

  FLAGS_enable_data_block_fsync = true;

  {
    // We must force the LocalTabletWriter out of scope before measuring
    // disk space usage. Otherwise some deleted blocks are kept open for
    // reading and aren't properly deallocated by the block manager.
    LocalTabletWriter writer(tablet().get(), &client_schema());
    KuduPartialRow row(&client_schema());

    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 10; j++) {
        int val = (i * 10) + j;
        ASSERT_OK(row.SetStringCopy("key", Substitute("hello $0", val)));
        ASSERT_OK(row.SetInt32("val", val));
        ASSERT_OK(writer.Insert(row));
      }
      ASSERT_OK(tablet()->Flush());
    }
  }

  uint64_t bytes_before;
  ASSERT_NO_FATAL_FAILURE(GetDataDiskSpace(&bytes_before));

  ASSERT_OK(tablet()->Compact(Tablet::FORCE_COMPACT_ALL));

  // Block deletion may happen asynchronously, so let's loop for a bit until
  // the space becomes free.
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(30));
  while (true) {
    uint64_t bytes_after;
    ASSERT_NO_FATAL_FAILURE(GetDataDiskSpace(&bytes_after));
    LOG(INFO) << Substitute("Data disk space: $0 (before), $1 (after) ",
                            bytes_before, bytes_after);
    if (bytes_after < bytes_before) {
      break;
    } else if (deadline.ComesBefore(MonoTime::Now(MonoTime::FINE))) {
      FAIL() << "Timed out waiting for compaction to reduce data block disk "
             << "space usage";
    }
    SleepFor(MonoDelta::FromMilliseconds(200));
  }
}

// Regression test for KUDU-1237, a bug in which empty flushes or compactions
// would result in orphaning near-empty cfile blocks on the disk.
TEST_F(TestCompaction, TestEmptyFlushDoesntLeakBlocks) {
  if (FLAGS_block_manager != "log") {
    LOG(WARNING) << "Test requires the log block manager";
    return;
  }

  // Fetch the metric for the number of on-disk blocks, so we can later verify
  // that we actually remove data.
  fs::LogBlockManager* lbm = down_cast<fs::LogBlockManager*>(
      harness_->fs_manager()->block_manager());

  int64_t before_count = lbm->CountBlocksForTests();
  ASSERT_OK(tablet()->Flush());
  int64_t after_count = lbm->CountBlocksForTests();

  ASSERT_EQ(after_count, before_count);
}

} // namespace tablet
} // namespace kudu
