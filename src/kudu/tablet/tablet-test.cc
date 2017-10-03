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
#include <time.h>

#include "kudu/common/iterator.h"
#include "kudu/common/row.h"
#include "kudu/common/scan_spec.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/tablet/deltafile.h"
#include "kudu/tablet/local_tablet_writer.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet-test-base.h"
#include "kudu/util/slice.h"
#include "kudu/util/test_macros.h"

using std::shared_ptr;
using std::unordered_set;

namespace kudu {
namespace tablet {

using fs::ReadableBlock;

DEFINE_int32(testflush_num_inserts, 1000,
             "Number of rows inserted in TestFlush");
DEFINE_int32(testiterator_num_inserts, 1000,
             "Number of rows inserted in TestRowIterator/TestInsert");
DEFINE_int32(testcompaction_num_rows, 1000,
             "Number of rows per rowset in TestCompaction");

template<class SETUP>
class TestTablet : public TabletTestBase<SETUP> {
  typedef SETUP Type;

 public:
  // Verify that iteration doesn't fail
  void CheckCanIterate() {
    vector<string> out_rows;
    ASSERT_OK(this->IterateToStringList(&out_rows));
  }

};
TYPED_TEST_CASE(TestTablet, TabletTestHelperTypes);

TYPED_TEST(TestTablet, TestFlush) {
  // Insert 1000 rows into memrowset
  uint64_t max_rows = this->ClampRowCount(FLAGS_testflush_num_inserts);
  this->InsertTestRows(0, max_rows, 0);

  // Flush it.
  ASSERT_OK(this->tablet()->Flush());
  TabletMetadata* tablet_meta = this->tablet()->metadata();

  // Make sure the files were created as expected.
  RowSetMetadata* rowset_meta = tablet_meta->GetRowSetForTests(0);
  CHECK(rowset_meta) << "No row set found";
  ASSERT_TRUE(rowset_meta->HasDataForColumnIdForTests(this->schema_.column_id(0)));
  ASSERT_TRUE(rowset_meta->HasDataForColumnIdForTests(this->schema_.column_id(1)));
  ASSERT_TRUE(rowset_meta->HasDataForColumnIdForTests(this->schema_.column_id(2)));
  ASSERT_TRUE(rowset_meta->HasBloomDataBlockForTests());

  // check that undo deltas are present
  vector<BlockId> undo_blocks = rowset_meta->undo_delta_blocks();
  ASSERT_EQ(1, undo_blocks.size());

  // Read the undo delta, we should get one undo mutation (delete) for each row.
  gscoped_ptr<ReadableBlock> block;
  ASSERT_OK(this->fs_manager()->OpenBlock(undo_blocks[0], &block));

  shared_ptr<DeltaFileReader> dfr;
  ASSERT_OK(DeltaFileReader::Open(block.Pass(), undo_blocks[0], &dfr, UNDO));
  // Assert there were 'max_rows' deletions in the undo delta (one for each inserted row)
  ASSERT_EQ(dfr->delta_stats().delete_count(), max_rows);
}

// Test that historical data for a row is maintained even after the row
// is flushed from the memrowset.
TYPED_TEST(TestTablet, TestInsertsAndMutationsAreUndoneWithMVCCAfterFlush) {
  // Insert 5 rows into the memrowset.
  // After the first one, each time we insert a new row we mutate
  // the previous one.

  // Take snapshots after each operation
  vector<MvccSnapshot> snaps;
  snaps.push_back(MvccSnapshot(*this->tablet()->mvcc_manager()));

  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  for (int i = 0; i < 5; i++) {
    this->InsertTestRows(i, 1, 0);
    DVLOG(1) << "Inserted row=" << i << ", row_idx=" << i << ", val=0";
    MvccSnapshot ins_snaphsot(*this->tablet()->mvcc_manager());
    snaps.push_back(ins_snaphsot);
    LOG(INFO) << "After Insert Snapshot: " <<  ins_snaphsot.ToString();
    if (i > 0) {
      ASSERT_OK(this->UpdateTestRow(&writer, i - 1, i));
      DVLOG(1) << "Mutated row=" << i - 1 << ", row_idx=" << i - 1 << ", val=" << i;
      MvccSnapshot mut_snaphsot(*this->tablet()->mvcc_manager());
      snaps.push_back(mut_snaphsot);
      DVLOG(1) << "After Mutate Snapshot: " <<  mut_snaphsot.ToString();
    }
  }

  // Collect the expected rows from the MRS, where there are no
  // undos
  vector<vector<string>* > expected_rows;
  CollectRowsForSnapshots(this->tablet().get(), this->client_schema_,
                          snaps, &expected_rows);

  // Flush the tablet
  ASSERT_OK(this->tablet()->Flush());

  // Now verify that with undos we get the same thing.
  VerifySnapshotsHaveSameResult(this->tablet().get(), this->client_schema_,
                                snaps, expected_rows);

  // Do some more work and flush/compact
  // take a snapshot and mutate the rows so that we have undos and
  // redos
  snaps.push_back(MvccSnapshot(*this->tablet()->mvcc_manager()));
//
  for (int i = 0; i < 4; i++) {
    ASSERT_OK(this->UpdateTestRow(&writer, i, i + 10));
    DVLOG(1) << "Mutated row=" << i << ", row_idx=" << i << ", val=" << i + 10;
    MvccSnapshot mut_snaphsot(*this->tablet()->mvcc_manager());
    snaps.push_back(mut_snaphsot);
    DVLOG(1) << "After Mutate Snapshot: " <<  mut_snaphsot.ToString();
  }

  // also throw a delete in there.
  ASSERT_OK(this->DeleteTestRow(&writer, 4));
  MvccSnapshot delete_snaphsot(*this->tablet()->mvcc_manager());
  snaps.push_back(delete_snaphsot);
  DVLOG(1) << "After Delete Snapshot: " <<  delete_snaphsot.ToString();

  // Collect the expected rows now that we have undos and redos
  STLDeleteElements(&expected_rows);
  CollectRowsForSnapshots(this->tablet().get(), this->client_schema_,
                          snaps, &expected_rows);

  // now flush and the compact everything
  ASSERT_OK(this->tablet()->Flush());
  ASSERT_OK(this->tablet()->Compact(Tablet::FORCE_COMPACT_ALL));

  // Now verify that with undos and redos we get the same thing.
  VerifySnapshotsHaveSameResult(this->tablet().get(), this->client_schema_,
                                snaps, expected_rows);

  STLDeleteElements(&expected_rows);
}

// This tests KUDU-165, a regression where multiple old ghost rows were appearing in
// compaction outputs and sometimes would be selected as the most recent version
// of the row.
// In particular this makes sure that when there is a ghost row in one row set
// and a live one on another the live one is the only one that survives compaction.
TYPED_TEST(TestTablet, TestGhostRowsOnDiskRowSets) {
  // Create a few INSERT/DELETE pairs on-disk by writing and flushing.
  // Each of the resulting rowsets has a single row which is a "ghost" since its
  // redo data has the DELETE.
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);

  for (int i = 0; i < 3; i++) {
    CHECK_OK(this->InsertTestRow(&writer, 0, 0));
    this->DeleteTestRow(&writer, 0);
    ASSERT_OK(this->tablet()->Flush());
  }

  // Create one more rowset on disk which has just an INSERT (ie a non-ghost row).
  CHECK_OK(this->InsertTestRow(&writer, 0, 0));
  ASSERT_OK(this->tablet()->Flush());

  // Compact. This should result in a rowset with just one row in it.
  ASSERT_OK(this->tablet()->Compact(Tablet::FORCE_COMPACT_ALL));

  // Should still be able to update, since the row is live.
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 1));
}

// Test that inserting a row which already exists causes an AlreadyPresent
// error
TYPED_TEST(TestTablet, TestInsertDuplicateKey) {
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);

  CHECK_OK(this->InsertTestRow(&writer, 12345, 0));
  ASSERT_FALSE(writer.last_op_result().has_failed_status());

  // Insert again, should fail!
  Status s = this->InsertTestRow(&writer, 12345, 0);
  ASSERT_STR_CONTAINS(s.ToString(), "entry already present in memrowset");

  ASSERT_EQ(1, this->TabletCount());

  // Flush, and make sure that inserting duplicate still fails
  ASSERT_OK(this->tablet()->Flush());

  ASSERT_EQ(1, this->TabletCount());

  s = this->InsertTestRow(&writer, 12345, 0);
  ASSERT_STR_CONTAINS(s.ToString(), "key already present");
  ASSERT_EQ(1, this->TabletCount());
}


// Test flushes and compactions dealing with deleted rows.
TYPED_TEST(TestTablet, TestDeleteWithFlushAndCompact) {
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  CHECK_OK(this->InsertTestRow(&writer, 0, 0));
  ASSERT_OK(this->DeleteTestRow(&writer, 0));
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).mrs_id());

  // The row is deleted, so we shouldn't see it in the iterator.
  vector<string> rows;
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(0, rows.size());

  // Flush the tablet and make sure the data doesn't re-appear.
  ASSERT_OK(this->tablet()->Flush());
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(0, rows.size());

  // Re-inserting should succeed. This will reinsert into the MemRowSet.
  // Set the int column to '1' this time, so we can differentiate the two
  // versions of the row.
  CHECK_OK(this->InsertTestRow(&writer, 0, 1));
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ(this->setup_.FormatDebugRow(0, 1, false), rows[0]);

  // Flush again, so the DiskRowSet has the row.
  ASSERT_OK(this->tablet()->Flush());
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ(this->setup_.FormatDebugRow(0, 1, false), rows[0]);

  // Delete it again, now that it's in DRS.
  ASSERT_OK(this->DeleteTestRow(&writer, 0));
  ASSERT_EQ(1, writer.last_op_result().mutated_stores_size());
  ASSERT_EQ(1L, writer.last_op_result().mutated_stores(0).rs_id());
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).dms_id());
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(0, rows.size());

  // We now have an INSERT in the MemRowSet and the
  // deleted row in the DiskRowSet. The new version
  // of the row has '2' in the int column.
  CHECK_OK(this->InsertTestRow(&writer, 0, 2));
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ(this->setup_.FormatDebugRow(0, 2, false), rows[0]);

  // Flush - now we have the row in two different DRSs.
  ASSERT_OK(this->tablet()->Flush());
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ(this->setup_.FormatDebugRow(0, 2, false), rows[0]);

  // Compaction should succeed even with the duplicate rows.
  ASSERT_OK(this->tablet()->Compact(Tablet::FORCE_COMPACT_ALL));
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ(this->setup_.FormatDebugRow(0, 2, false), rows[0]);
}

// Test flushes dealing with REINSERT mutations in the MemRowSet.
TYPED_TEST(TestTablet, TestFlushWithReinsert) {
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  // Insert, delete, and re-insert a row in the MRS.

  CHECK_OK(this->InsertTestRow(&writer, 0, 0));
  ASSERT_OK(this->DeleteTestRow(&writer, 0));
  ASSERT_EQ(1, writer.last_op_result().mutated_stores_size());
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).mrs_id());
  CHECK_OK(this->InsertTestRow(&writer, 0, 1));

  // Flush the tablet and make sure the data persists.
  ASSERT_OK(this->tablet()->Flush());
  vector<string> rows;
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ(this->setup_.FormatDebugRow(0, 1, false), rows[0]);
}

// Test flushes dealing with REINSERT mutations if they arrive in the middle
// of a flush.
TYPED_TEST(TestTablet, TestReinsertDuringFlush) {
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  // Insert/delete/insert/delete in MemRowStore.

  CHECK_OK(this->InsertTestRow(&writer, 0, 0));
  ASSERT_OK(this->DeleteTestRow(&writer, 0));
  ASSERT_EQ(1, writer.last_op_result().mutated_stores_size());
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).mrs_id());

  CHECK_OK(this->InsertTestRow(&writer, 0, 1));
  ASSERT_OK(this->DeleteTestRow(&writer, 0));

  ASSERT_EQ(1, writer.last_op_result().mutated_stores_size());
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).mrs_id());

  // During the snapshot flush, insert/delete/insert some more during the flush.
  class MyCommonHooks : public Tablet::FlushCompactCommonHooks {
   public:
    explicit MyCommonHooks(TestFixture *test) : test_(test) {}

    Status PostWriteSnapshot() OVERRIDE {
      LocalTabletWriter writer(test_->tablet().get(), &test_->client_schema());
      test_->InsertTestRow(&writer, 0, 1);
      CHECK_OK(test_->DeleteTestRow(&writer, 0));
      CHECK_EQ(1, writer.last_op_result().mutated_stores_size());
      CHECK_EQ(1L, writer.last_op_result().mutated_stores(0).mrs_id());
      test_->InsertTestRow(&writer, 0, 2);
      CHECK_OK(test_->DeleteTestRow(&writer, 0));
      CHECK_EQ(1, writer.last_op_result().mutated_stores_size());
      CHECK_EQ(1L, writer.last_op_result().mutated_stores(0).mrs_id());
      test_->InsertTestRow(&writer, 0, 3);
      return Status::OK();
    }

   private:
    TestFixture *test_;
  };
  shared_ptr<Tablet::FlushCompactCommonHooks> common_hooks(
    reinterpret_cast<Tablet::FlushCompactCommonHooks *>(new MyCommonHooks(this)));
  this->tablet()->SetFlushCompactCommonHooksForTests(common_hooks);

  // Flush the tablet and make sure the data persists.
  ASSERT_OK(this->tablet()->Flush());
  vector<string> rows;
  ASSERT_OK(this->IterateToStringList(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ(this->setup_.FormatDebugRow(0, 3, false), rows[0]);
}

// Test iterating over a tablet which contains data
// in the memrowset as well as two rowsets. This simple test
// only puts one row in each with no updates.
TYPED_TEST(TestTablet, TestRowIteratorSimple) {
  const int kInRowSet1 = 1;
  const int kInRowSet2 = 2;
  const int kInMemRowSet = 3;

  // Put a row in disk rowset 1 (insert and flush)
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  CHECK_OK(this->InsertTestRow(&writer, kInRowSet1, 0));
  ASSERT_OK(this->tablet()->Flush());

  // Put a row in disk rowset 2 (insert and flush)
  CHECK_OK(this->InsertTestRow(&writer, kInRowSet2, 0));
  ASSERT_OK(this->tablet()->Flush());

  // Put a row in memrowset
  CHECK_OK(this->InsertTestRow(&writer, kInMemRowSet, 0));

  // Now iterate the tablet and make sure the rows show up
  gscoped_ptr<RowwiseIterator> iter;
  ASSERT_OK(this->tablet()->NewRowIterator(this->client_schema_, &iter));
  ASSERT_OK(iter->Init(nullptr));

  ASSERT_TRUE(iter->HasNext());

  RowBlock block(this->schema_, 100, &this->arena_);

  // First call to CopyNextRows should fetch the whole memrowset.
  ASSERT_OK_FAST(iter->NextBlock(&block));
  ASSERT_EQ(1, block.nrows()) << "should get only the one row from memrowset";
  this->VerifyRow(block.row(0), kInMemRowSet, 0);

  // Next, should fetch the older rowset
  ASSERT_TRUE(iter->HasNext());
  ASSERT_OK(iter->NextBlock(&block));
  ASSERT_EQ(1, block.nrows()) << "should get only the one row from rowset 1";
  this->VerifyRow(block.row(0), kInRowSet1, 0);

  // Next, should fetch the newer rowset
  ASSERT_TRUE(iter->HasNext());
  ASSERT_OK(iter->NextBlock(&block));
  ASSERT_EQ(1, block.nrows()) << "should get only the one row from rowset 2";
  this->VerifyRow(block.row(0), kInRowSet2, 0);

  ASSERT_FALSE(iter->HasNext());
}

TYPED_TEST(TestTablet, TestRowIteratorOrdered) {
  // Create interleaved keys in each rowset, so they are clearly not in order
  const int kNumRows = 128;
  const int kNumBatches = 4;
  LOG(INFO) << "Schema: " << this->schema_.ToString();
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  for (int i = 0; i < kNumBatches; i++) {
    ASSERT_OK(this->tablet()->Flush());
    for (int j = 0; j < kNumRows; j++) {
      if (j % kNumBatches == i) {
        LOG(INFO) << "Inserting row " << j;
        CHECK_OK(this->InsertTestRow(&writer, 654321+j, j));
      }
    }
  }

  MvccSnapshot snap(*this->tablet()->mvcc_manager());
  // Iterate through with a few different block sizes.
  for (int numBlocks = 1; numBlocks < 5; numBlocks*=2) {
    const int rowsPerBlock = kNumRows / numBlocks;
    // Make a new ordered iterator for the current snapshot.
    gscoped_ptr<RowwiseIterator> iter;

    ASSERT_OK(this->tablet()->NewRowIterator(this->client_schema_, snap, Tablet::ORDERED, &iter));
    ASSERT_OK(iter->Init(nullptr));

    // Iterate the tablet collecting rows.
    vector<shared_ptr<faststring> > rows;
    for (int i = 0; i < numBlocks; i++) {
      RowBlock block(this->schema_, rowsPerBlock, &this->arena_);
      ASSERT_TRUE(iter->HasNext());
      ASSERT_OK(iter->NextBlock(&block));
      ASSERT_EQ(rowsPerBlock, block.nrows()) << "unexpected number of rows returned";
      for (int j = 0; j < rowsPerBlock; j++) {
        RowBlockRow row = block.row(j);
        shared_ptr<faststring> encoded(new faststring());
        this->client_schema_.EncodeComparableKey(row, encoded.get());
        rows.push_back(encoded);
      }
    }
    // Verify the collected rows, checking that they are sorted.
    for (int j = 1; j < rows.size(); j++) {
      // Use the schema for comparison, since this test is run with different schemas.
      ASSERT_LT((*rows[j-1]).ToString(), (*rows[j]).ToString());
    }
    ASSERT_FALSE(iter->HasNext());
    ASSERT_EQ(kNumRows, rows.size());
  }
}


template<class SETUP>
bool TestSetupExpectsNulls(int32_t key_idx) {
  return false;
}

template<>
bool TestSetupExpectsNulls<NullableValueTestSetup>(int32_t key_idx) {
  // If it's a row that the test updates, then we should expect null
  // based on whether it updated to NULL or away from NULL.
  bool should_update = (key_idx % 2 == 1);
  if (should_update) {
    return (key_idx % 10 == 1);
  }

  // Otherwise, expect whatever was inserted.
  return NullableValueTestSetup::ShouldInsertAsNull(key_idx);
}

// Test iterating over a tablet which has a memrowset
// and several rowsets, each with many rows of data.
TYPED_TEST(TestTablet, TestRowIteratorComplex) {

  uint64_t max_rows = this->ClampRowCount(FLAGS_testiterator_num_inserts);

  // Put a row in disk rowset 1 (insert and flush)
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  for (int32_t i = 0; i < max_rows; i++) {
    ASSERT_OK_FAST(this->InsertTestRow(&writer, i, 0));

    if (i % 300 == 0) {
      LOG(INFO) << "Flushing after " << i << " rows inserted";
      ASSERT_OK(this->tablet()->Flush());
    }
  }
  LOG(INFO) << "Successfully inserted " << max_rows << " rows";

  // At this point, we should have several rowsets as well
  // as some data in memrowset.

  // Update a subset of the rows
  for (int32_t i = 0; i < max_rows; i++) {
    bool should_update = (i % 2 == 1);
    if (!should_update) continue;

    bool set_to_null = TestSetupExpectsNulls<TypeParam>(i);
    if (set_to_null) {
      this->UpdateTestRowToNull(&writer, i);
    } else {
      ASSERT_OK_FAST(this->UpdateTestRow(&writer, i, i));
    }
  }

  // Now iterate the tablet and make sure the rows show up.
  gscoped_ptr<RowwiseIterator> iter;
  const Schema& schema = this->client_schema_;
  ASSERT_OK(this->tablet()->NewRowIterator(schema, &iter));
  ASSERT_OK(iter->Init(nullptr));
  LOG(INFO) << "Created iter: " << iter->ToString();

  vector<bool> seen(max_rows, false);
  int seen_count = 0;

  RowBlock block(schema, 100, &this->arena_);
  while (iter->HasNext()) {
    this->arena_.Reset();
    ASSERT_OK(iter->NextBlock(&block));
    LOG(INFO) << "Fetched batch of " << block.nrows();
    for (size_t i = 0; i < block.nrows(); i++) {
      SCOPED_TRACE(schema.DebugRow(block.row(i)));
      // Verify that we see each key exactly once.
      int32_t key_idx = *schema.ExtractColumnFromRow<INT32>(block.row(i), 1);
      if (seen[key_idx]) {
        FAIL() << "Saw row " << key_idx << " multiple times";
      }
      seen[key_idx] = true;
      seen_count++;

      // Verify that we see the correctly updated value
      const int32_t* val = schema.ExtractColumnFromRow<INT32>(block.row(i), 2);

      bool set_to_null = TestSetupExpectsNulls<TypeParam>(key_idx);
      bool should_update = (key_idx % 2 == 1);
      if (val == nullptr) {
        ASSERT_TRUE(set_to_null);
      } else if (should_update) {
        ASSERT_EQ(key_idx, *val);
      } else {
        ASSERT_EQ(0, *val);
      }
    }
  }

  ASSERT_EQ(seen_count, max_rows)
    << "expected to see all inserted data through iterator.";
}

// Test that, when a tablet has flushed data and is
// reopened, that the data persists
TYPED_TEST(TestTablet, TestInsertsPersist) {
  uint64_t max_rows = this->ClampRowCount(FLAGS_testiterator_num_inserts);

  this->InsertTestRows(0, max_rows, 0);
  ASSERT_EQ(max_rows, this->TabletCount());

  // Flush it.
  ASSERT_OK(this->tablet()->Flush());

  ASSERT_EQ(max_rows, this->TabletCount());

  // Close and re-open tablet
  this->TabletReOpen();

  // Ensure that rows exist
  ASSERT_EQ(max_rows, this->TabletCount());
  this->VerifyTestRows(0, max_rows);

  // TODO: add some more data, re-flush
}

// Test that when a row has been updated many times, it always yields
// the most recent value.
TYPED_TEST(TestTablet, TestMultipleUpdates) {
  // Insert and update several times in MemRowSet
  LocalTabletWriter writer(this->tablet().get(), &this->client_schema_);
  CHECK_OK(this->InsertTestRow(&writer, 0, 0));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 1));
  ASSERT_EQ(1, writer.last_op_result().mutated_stores_size());
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).mrs_id());
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 2));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 3));

  // Should see most recent value.
  vector<string> out_rows;
  ASSERT_OK(this->IterateToStringList(&out_rows));
  ASSERT_EQ(1, out_rows.size());
  ASSERT_EQ(this->setup_.FormatDebugRow(0, 3, false), out_rows[0]);

  // Flush it.
  ASSERT_OK(this->tablet()->Flush());

  // Should still see most recent value.
  ASSERT_OK(this->IterateToStringList(&out_rows));
  ASSERT_EQ(1, out_rows.size());
  ASSERT_EQ(this->setup_.FormatDebugRow(0, 3, false), out_rows[0]);

  // Update the row a few times in DeltaMemStore
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 4));
  ASSERT_EQ(1, writer.last_op_result().mutated_stores_size());
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).rs_id());
  ASSERT_EQ(0L, writer.last_op_result().mutated_stores(0).dms_id());
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 5));
  ASSERT_OK(this->UpdateTestRow(&writer, 0, 6));

  // Should still see most recent value.
  ASSERT_OK(this->IterateToStringList(&out_rows));
  ASSERT_EQ(1, out_rows.size());
  ASSERT_EQ(this->setup_.FormatDebugRow(0, 6, false), out_rows[0]);


  // Force a compaction after adding a new rowset with one row.
  CHECK_OK(this->InsertTestRow(&writer, 1, 0));
  ASSERT_OK(this->tablet()->Flush());
  ASSERT_EQ(2, this->tablet()->num_rowsets());

  ASSERT_OK(this->tablet()->Compact(Tablet::FORCE_COMPACT_ALL));
  ASSERT_EQ(1, this->tablet()->num_rowsets());

  // Should still see most recent value.
  ASSERT_OK(this->IterateToStringList(&out_rows));
  ASSERT_EQ(2, out_rows.size());
  ASSERT_EQ(this->setup_.FormatDebugRow(0, 6, false), out_rows[0]);
  ASSERT_EQ(this->setup_.FormatDebugRow(1, 0, false), out_rows[1]);
}



TYPED_TEST(TestTablet, TestCompaction) {
  uint64_t max_rows = this->ClampRowCount(FLAGS_testcompaction_num_rows);

  uint64_t n_rows = max_rows / 3;
  // Create three rowsets by inserting and flushing
  LOG_TIMING(INFO, "Inserting rows") {
    this->InsertTestRows(0, n_rows, 0);

    LOG_TIMING(INFO, "Flushing rows") {
      ASSERT_OK(this->tablet()->Flush());
    }

    // first MemRowSet had id 0, current one should be 1
    ASSERT_EQ(1, this->tablet()->CurrentMrsIdForTests());
    ASSERT_TRUE(
      this->tablet()->metadata()->GetRowSetForTests(0)->HasDataForColumnIdForTests(
          this->schema_.column_id(0)));
  }

  LOG_TIMING(INFO, "Inserting rows") {
    this->InsertTestRows(n_rows, n_rows, 0);

    LOG_TIMING(INFO, "Flushing rows") {
      ASSERT_OK(this->tablet()->Flush());
    }

    // previous MemRowSet had id 1, current one should be 2
    ASSERT_EQ(2, this->tablet()->CurrentMrsIdForTests());
    ASSERT_TRUE(
      this->tablet()->metadata()->GetRowSetForTests(1)->HasDataForColumnIdForTests(
          this->schema_.column_id(0)));
  }

  LOG_TIMING(INFO, "Inserting rows") {
    this->InsertTestRows(n_rows * 2, n_rows, 0);

    LOG_TIMING(INFO, "Flushing rows") {
      ASSERT_OK(this->tablet()->Flush());
    }

    // previous MemRowSet had id 2, current one should be 3
    ASSERT_EQ(3, this->tablet()->CurrentMrsIdForTests());
    ASSERT_TRUE(
      this->tablet()->metadata()->GetRowSetForTests(2)->HasDataForColumnIdForTests(
          this->schema_.column_id(0)));
  }

  // Issue compaction
  LOG_TIMING(INFO, "Compacting rows") {

    ASSERT_OK(this->tablet()->Compact(Tablet::FORCE_COMPACT_ALL));
    // Compaction does not swap the memrowsets so we should still get 3
    ASSERT_EQ(3, this->tablet()->CurrentMrsIdForTests());
    ASSERT_EQ(n_rows * 3, this->TabletCount());

    const RowSetMetadata *rowset_meta = this->tablet()->metadata()->GetRowSetForTests(3);
    ASSERT_TRUE(rowset_meta != nullptr);
    ASSERT_TRUE(rowset_meta->HasDataForColumnIdForTests(this->schema_.column_id(0)));
    ASSERT_TRUE(rowset_meta->HasBloomDataBlockForTests());
  }

  // Old rowsets should not exist anymore
  for (int i = 0; i <= 2; i++) {
    const RowSetMetadata *rowset_meta = this->tablet()->metadata()->GetRowSetForTests(i);
    ASSERT_TRUE(rowset_meta == nullptr);
  }
}

enum MutationType {
  MRS_MUTATION,
  DELTA_MUTATION,
  DUPLICATED_MUTATION
};

// Hook used by the Test*WithConcurrentMutation tests.
//
// Every time one of these hooks triggers, it inserts a row starting
// at row 20 (and increasing), and updates a row starting at row 10
// (and increasing).
template<class TestFixture>
class MyCommonHooks : public Tablet::FlushCompactCommonHooks {
 public:
  explicit MyCommonHooks(TestFixture *test, bool flushed)
      : test_(test),
        flushed_(flushed),
        i_(0) {
  }
  Status DoHook(MutationType expected_mutation_type) {
    LocalTabletWriter writer(test_->tablet().get(), &test_->client_schema());
    RETURN_NOT_OK(test_->DeleteTestRow(&writer, i_));

    switch (expected_mutation_type) {
      case MRS_MUTATION:
        CHECK_EQ(1, writer.last_op_result().mutated_stores_size());
        CHECK(writer.last_op_result().mutated_stores(0).has_mrs_id());
        break;
      case DELTA_MUTATION:
        CHECK_EQ(1, writer.last_op_result().mutated_stores_size());
        CHECK(writer.last_op_result().mutated_stores(0).has_rs_id());
        CHECK(writer.last_op_result().mutated_stores(0).has_dms_id());
        break;
      case DUPLICATED_MUTATION:
        CHECK_EQ(2, writer.last_op_result().mutated_stores_size());
        break;
    }
    RETURN_NOT_OK(test_->UpdateTestRow(&writer, 10 + i_, 1000 + i_));
    test_->InsertTestRows(20 + i_, 1, 0);
    test_->CheckCanIterate();
    i_++;
    return Status::OK();
  }

  virtual Status PostTakeMvccSnapshot() OVERRIDE {
    // before we flush we update the MemRowSet afterwards we update the
    // DeltaMemStore
    if (!flushed_) {
      return DoHook(MRS_MUTATION);
    } else {
      return DoHook(DELTA_MUTATION);
    }
  }
  virtual Status PostWriteSnapshot() OVERRIDE {
    if (!flushed_) {
      return DoHook(MRS_MUTATION);
    } else {
      return DoHook(DELTA_MUTATION);
    }
  }
  virtual Status PostSwapInDuplicatingRowSet() OVERRIDE {
    return DoHook(DUPLICATED_MUTATION);
  }
  virtual Status PostReupdateMissedDeltas() OVERRIDE {
    return DoHook(DUPLICATED_MUTATION);
  }
  virtual Status PostSwapNewRowSet() OVERRIDE {
    return DoHook(DELTA_MUTATION);
  }
 protected:
  TestFixture *test_;
  bool flushed_;
  int i_;
};

template<class TestFixture>
class MyFlushHooks : public Tablet::FlushFaultHooks, public MyCommonHooks<TestFixture> {
 public:
  explicit MyFlushHooks(TestFixture *test, bool flushed) :
           MyCommonHooks<TestFixture>(test, flushed) {}
  virtual Status PostSwapNewMemRowSet() { return this->DoHook(MRS_MUTATION); }
};

template<class TestFixture>
class MyCompactHooks : public Tablet::CompactionFaultHooks, public MyCommonHooks<TestFixture> {
 public:
  explicit MyCompactHooks(TestFixture *test, bool flushed) :
           MyCommonHooks<TestFixture>(test, flushed) {}
  Status PostSelectIterators() { return this->DoHook(DELTA_MUTATION); }
};

// Test for Flush with concurrent update, delete and insert during the
// various phases.
TYPED_TEST(TestTablet, TestFlushWithConcurrentMutation) {
  this->InsertTestRows(0, 7, 0); // 0-6 inclusive: these rows will be deleted
  this->InsertTestRows(10, 7, 0); // 10-16 inclusive: these rows will be updated
  // Rows 20-26 inclusive will be inserted during the flush

  // Inject hooks which mutate those rows and add more rows at
  // each key stage of flushing.
  shared_ptr<MyFlushHooks<TestFixture> > hooks(new MyFlushHooks<TestFixture>(this, false));
  this->tablet()->SetFlushHooksForTests(hooks);
  this->tablet()->SetFlushCompactCommonHooksForTests(hooks);

  // First hook before we do the Flush
  ASSERT_OK(hooks->DoHook(MRS_MUTATION));

  // Then do the flush with the hooks enabled.
  ASSERT_OK(this->tablet()->Flush());

  // Now verify that the results saw all the mutated_stores.
  vector<string> out_rows;
  ASSERT_OK(this->IterateToStringList(&out_rows));
  std::sort(out_rows.begin(), out_rows.end());

  vector<string> expected_rows;
  expected_rows.push_back(this->setup_.FormatDebugRow(10, 1000, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(11, 1001, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(12, 1002, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(13, 1003, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(14, 1004, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(15, 1005, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(16, 1006, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(20, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(21, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(22, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(23, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(24, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(25, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(26, 0, false));

  std::sort(expected_rows.begin(), expected_rows.end());

  // Verify that all the inserts and updates arrived and persisted.
  LOG(INFO) << "Expected: " << JoinStrings(expected_rows, "\n");

  // Verify that all the inserts and updates arrived and persisted.
  LOG(INFO) << "Results: " << JoinStrings(out_rows, "\n");

  ASSERT_EQ(expected_rows.size(), out_rows.size());
  vector<string>::const_iterator exp_it = expected_rows.begin();
  for (vector<string>::const_iterator out_it = out_rows.begin(); out_it!= out_rows.end();) {
    ASSERT_EQ(*out_it, *exp_it);
    out_it++;
    exp_it++;
  }
}

// Test for compaction with concurrent update and insert during the
// various phases.
TYPED_TEST(TestTablet, TestCompactionWithConcurrentMutation) {
  // Create three rowsets by inserting and flushing.
  // The rows from these layers will get updated or deleted during the flush:
  // - rows 0-6 inclusive will be deleted
  // - rows 10-16 inclusive will be updated

  this->InsertTestRows(0, 2, 0);  // rows 0-1
  this->InsertTestRows(10, 2, 0); // rows 10-11
  ASSERT_OK(this->tablet()->Flush());

  this->InsertTestRows(2, 2, 0);  // rows 2-3
  this->InsertTestRows(12, 2, 0); // rows 12-13
  ASSERT_OK(this->tablet()->Flush());

  this->InsertTestRows(4, 3, 0);  // rows 4-6
  this->InsertTestRows(14, 3, 0); // rows 14-16
  ASSERT_OK(this->tablet()->Flush());

  // Rows 20-26 inclusive will be inserted during the flush.

  shared_ptr<MyCompactHooks<TestFixture> > hooks(new MyCompactHooks<TestFixture>(this, true));
  this->tablet()->SetCompactionHooksForTests(hooks);
  this->tablet()->SetFlushCompactCommonHooksForTests(hooks);

  // First hook pre-compaction.
  ASSERT_OK(hooks->DoHook(DELTA_MUTATION));

  // Issue compaction
  ASSERT_OK(this->tablet()->Compact(Tablet::FORCE_COMPACT_ALL));

  // Grab the resulting data into a vector.
  vector<string> out_rows;
  ASSERT_OK(this->IterateToStringList(&out_rows));
  std::sort(out_rows.begin(), out_rows.end());

  vector<string> expected_rows;
  expected_rows.push_back(this->setup_.FormatDebugRow(10, 1000, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(11, 1001, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(12, 1002, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(13, 1003, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(14, 1004, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(15, 1005, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(16, 1006, true));
  expected_rows.push_back(this->setup_.FormatDebugRow(20, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(21, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(22, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(23, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(24, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(25, 0, false));
  expected_rows.push_back(this->setup_.FormatDebugRow(26, 0, false));

  std::sort(expected_rows.begin(), expected_rows.end());

  ASSERT_EQ(expected_rows.size(), out_rows.size());

  // Verify that all the inserts and updates arrived and persisted.
  LOG(INFO) << "Expected: " << JoinStrings(expected_rows, "\n");

  // Verify that all the inserts and updates arrived and persisted.
  LOG(INFO) << "Results: " << JoinStrings(out_rows, "\n");

  vector<string>::const_iterator exp_it = expected_rows.begin();
  for (vector<string>::const_iterator out_it = out_rows.begin(); out_it!= out_rows.end();) {
    ASSERT_EQ(*out_it, *exp_it);
    out_it++;
    exp_it++;
  }
}

// Test that metrics behave properly during tablet initialization
TYPED_TEST(TestTablet, TestMetricsInit) {
  // Create a tablet, but do not open it
  this->CreateTestTablet();
  MetricRegistry* registry = this->harness()->metrics_registry();
  std::stringstream out;
  JsonWriter writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(registry->WriteAsJson(&writer, { "*" }, MetricJsonOptions()));
  // Open tablet, should still work
  this->harness()->Open();
  ASSERT_OK(registry->WriteAsJson(&writer, { "*" }, MetricJsonOptions()));
}

// Test that we find the correct log segment size for different indexes.
TEST(TestTablet, TestGetLogRetentionSizeForIndex) {
  std::map<int64_t, int64_t> idx_size_map;
  // We build a map that represents 3 logs. The key is the index where that log ends, and the value
  // is its size.
  idx_size_map[3] = 1;
  idx_size_map[6] = 10;
  idx_size_map[9] = 100;

  // The default value should return a size of 0.
  int64_t min_log_index = -1;
  ASSERT_EQ(Tablet::GetLogRetentionSizeForIndex(min_log_index, idx_size_map), 0);

  // A value at the beginning of the first segment retains all the logs.
  min_log_index = 1;
  ASSERT_EQ(Tablet::GetLogRetentionSizeForIndex(min_log_index, idx_size_map), 111);

  // A value at the end of the first segment also retains everything.
  min_log_index = 3;
  ASSERT_EQ(Tablet::GetLogRetentionSizeForIndex(min_log_index, idx_size_map), 111);

  // Beginning of second segment, only retain that one and the next.
  min_log_index = 4;
  ASSERT_EQ(Tablet::GetLogRetentionSizeForIndex(min_log_index, idx_size_map), 110);

  // Beginning of third segment, only retain that one.
  min_log_index = 7;
  ASSERT_EQ(Tablet::GetLogRetentionSizeForIndex(min_log_index, idx_size_map), 100);

  // A value after all the passed segments, doesn't retain anything.
  min_log_index = 10;
  ASSERT_EQ(Tablet::GetLogRetentionSizeForIndex(min_log_index, idx_size_map), 0);
}

} // namespace tablet
} // namespace kudu
