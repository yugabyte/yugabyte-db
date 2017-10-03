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
#include <unordered_set>

#include "kudu/common/generic_iterators.h"
#include "kudu/common/partial_row.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/server/logical_clock.h"
#include "kudu/tablet/cfile_set.h"
#include "kudu/tablet/delta_compaction.h"
#include "kudu/tablet/local_tablet_writer.h"
#include "kudu/tablet/tablet-test-util.h"
#include "kudu/tablet/diskrowset-test-base.h"
#include "kudu/util/test_util.h"
#include "kudu/gutil/algorithm.h"

using std::shared_ptr;
using std::unordered_set;

namespace kudu {
namespace tablet {

using strings::Substitute;

class TestMajorDeltaCompaction : public KuduRowSetTest {
 public:
  TestMajorDeltaCompaction() :
      KuduRowSetTest(Schema({ ColumnSchema("key", STRING),
                              ColumnSchema("val1", INT32),
                              ColumnSchema("val2", STRING),
                              ColumnSchema("val3", INT32),
                              ColumnSchema("val4", STRING) }, 1)),
      mvcc_(scoped_refptr<server::Clock>(
          server::LogicalClock::CreateStartingAt(Timestamp::kInitialTimestamp))) {
  }

  struct ExpectedRow {
    string key;
    int32_t val1;
    string val2;
    int32_t val3;
    string val4;

    string Formatted() const {
      return strings::Substitute(
        "(string key=$0, int32 val1=$1, string val2=$2, int32 val3=$3, string val4=$4)",
        key, val1, val2, val3, val4);
    }
  };

  virtual void SetUp() OVERRIDE {
    KuduRowSetTest::SetUp();
  }

  // Insert data into tablet_, setting up equivalent state in
  // expected_state_.
  void WriteTestTablet(int nrows) {
    LocalTabletWriter writer(tablet().get(), &client_schema_);
    KuduPartialRow ins_row(&client_schema_);

    for (int i = 0; i < nrows; i++) {
      ExpectedRow row;
      row.key = StringPrintf("hello %08d", i);
      row.val1 = i * 2;
      row.val2 = StringPrintf("a %08d", i * 2);
      row.val3 = i * 10;
      row.val4 = StringPrintf("b %08d", i * 10);

      int col = 0;
      CHECK_OK(ins_row.SetString(col++, row.key));
      CHECK_OK(ins_row.SetInt32(col++, row.val1));
      CHECK_OK(ins_row.SetString(col++, row.val2));
      CHECK_OK(ins_row.SetInt32(col++, row.val3));
      CHECK_OK(ins_row.SetString(col++, row.val4));
      ASSERT_OK_FAST(writer.Insert(ins_row));
      expected_state_.push_back(row);
    }
  }

  // Delete the data that was inserted and clear the expected state, end to front.
  void DeleteRows(int nrows) {
    LocalTabletWriter writer(tablet().get(), &client_schema_);
    KuduPartialRow del_row(&client_schema_);

    for (int i = nrows - 1; i >= 0; i--) {
      CHECK_OK(del_row.SetString(0, expected_state_[i].key));
      ASSERT_OK(writer.Delete(del_row));
      expected_state_.pop_back();
    }
    ASSERT_EQ(expected_state_.size(), 0);
  }

  // Update the data, touching only odd or even rows based on the
  // value of 'even'.
  // Makes corresponding updates in expected_state_.
  void UpdateRows(int nrows, bool even) {
    LocalTabletWriter writer(tablet().get(), &client_schema_);
    KuduPartialRow prow(&client_schema_);
    for (int idx = 0; idx < nrows; idx++) {
      ExpectedRow* row = &expected_state_[idx];
      if ((idx % 2 == 0) == even) {
        // Set key
        CHECK_OK(prow.SetString(0, row->key));

        // Update the data
        row->val1 *= 2;
        row->val3 *= 2;
        row->val4.append("[U]");

        // Apply the updates.
        CHECK_OK(prow.SetInt32(1, row->val1));
        CHECK_OK(prow.SetInt32(3, row->val3));
        CHECK_OK(prow.SetString(4, row->val4));
        ASSERT_OK(writer.Update(prow));
      }
    }
  }

  // Verify that the data seen by scanning the tablet matches the data in
  // expected_state_.
  void VerifyData() {
    MvccSnapshot snap(*tablet()->mvcc_manager());
    VerifyDataWithMvccAndExpectedState(snap, expected_state_);
  }

  void VerifyDataWithMvccAndExpectedState(MvccSnapshot& snap,
                                          const vector<ExpectedRow>& passed_expected_state) {
      gscoped_ptr<RowwiseIterator> row_iter;
      ASSERT_OK(tablet()->NewRowIterator(client_schema_, snap,
                                                Tablet::UNORDERED, &row_iter));
      ASSERT_OK(row_iter->Init(nullptr));

      vector<string> results;
      ASSERT_OK(IterateToStringList(row_iter.get(), &results));
      VLOG(1) << "Results of iterating over the updated materialized rows:";
      ASSERT_EQ(passed_expected_state.size(), results.size());
      for (int i = 0; i < results.size(); i++) {
        SCOPED_TRACE(Substitute("row $0", i));
        const string& str = results[i];
        const ExpectedRow& expected = passed_expected_state[i];
        ASSERT_EQ(expected.Formatted(), str);
      }
    }

  MvccManager mvcc_;
  vector<ExpectedRow> expected_state_;
};

// Tests a major delta compaction run.
// Verifies that the output rowset accurately reflects the mutations, but keeps the
// unchanged columns intact.
TEST_F(TestMajorDeltaCompaction, TestCompact) {
  const int kNumRows = 100;
  ASSERT_NO_FATAL_FAILURE(WriteTestTablet(kNumRows));
  ASSERT_OK(tablet()->Flush());

  vector<shared_ptr<RowSet> > all_rowsets;
  tablet()->GetRowSetsForTests(&all_rowsets);

  shared_ptr<RowSet> rs = all_rowsets.front();

  vector<ColumnId> col_ids_to_compact = { schema_.column_id(1),
                                          schema_.column_id(3),
                                          schema_.column_id(4) };

  // We'll run a few rounds of update/compact to make sure
  // that we don't get into some funny state (regression test for
  // an earlier bug).
  // We first compact all the columns, then for each other round we do one less,
  // so that we test a few combinations.
  for (int i = 0; i < 3; i++) {
    SCOPED_TRACE(Substitute("Update/compact round $0", i));
    // Update the even rows and verify.
    ASSERT_NO_FATAL_FAILURE(UpdateRows(kNumRows, false));
    ASSERT_NO_FATAL_FAILURE(VerifyData());

    // Flush the deltas, make sure data stays the same.
    ASSERT_OK(tablet()->FlushBiggestDMS());
    ASSERT_NO_FATAL_FAILURE(VerifyData());

    // Update the odd rows and flush deltas
    ASSERT_NO_FATAL_FAILURE(UpdateRows(kNumRows, true));
    ASSERT_OK(tablet()->FlushBiggestDMS());
    ASSERT_NO_FATAL_FAILURE(VerifyData());

    // Major compact some columns.
    vector<ColumnId> col_ids;
    for (int col_index = 0; col_index < col_ids_to_compact.size() - i; col_index++) {
      col_ids.push_back(col_ids_to_compact[col_index]);
    }
    ASSERT_OK(tablet()->DoMajorDeltaCompaction(col_ids, rs));

    ASSERT_NO_FATAL_FAILURE(VerifyData());
  }
}

// Verify that we do issue UNDO files and that we can read them.
TEST_F(TestMajorDeltaCompaction, TestUndos) {
  const int kNumRows = 100;
  ASSERT_NO_FATAL_FAILURE(WriteTestTablet(kNumRows));
  ASSERT_OK(tablet()->Flush());

  vector<shared_ptr<RowSet> > all_rowsets;
  tablet()->GetRowSetsForTests(&all_rowsets);

  shared_ptr<RowSet> rs = all_rowsets.front();

  MvccSnapshot snap(*tablet()->mvcc_manager());

  // Verify the old data and grab a copy of the old state.
  ASSERT_NO_FATAL_FAILURE(VerifyDataWithMvccAndExpectedState(snap, expected_state_));
  vector<ExpectedRow> old_state(expected_state_.size());
  std::copy(expected_state_.begin(), expected_state_.end(), old_state.begin());

  // Flush the DMS, make sure we still see the old data.
  ASSERT_NO_FATAL_FAILURE(UpdateRows(kNumRows, false));
  ASSERT_OK(tablet()->FlushBiggestDMS());
  ASSERT_NO_FATAL_FAILURE(VerifyDataWithMvccAndExpectedState(snap, old_state));

  // Major compact, check we still have the old data.
  vector<ColumnId> col_ids_to_compact = { schema_.column_id(1),
                                          schema_.column_id(3),
                                          schema_.column_id(4) };
  ASSERT_OK(tablet()->DoMajorDeltaCompaction(col_ids_to_compact, rs));
  ASSERT_NO_FATAL_FAILURE(VerifyDataWithMvccAndExpectedState(snap, old_state));

  // Test adding three updates per row to three REDO files.
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      ASSERT_NO_FATAL_FAILURE(UpdateRows(kNumRows, false));
    }
    ASSERT_OK(tablet()->FlushBiggestDMS());
  }

  // To complicate things further, only major compact two columns, then verify we can read the old
  // and the new data.
  col_ids_to_compact.pop_back();
  ASSERT_OK(tablet()->DoMajorDeltaCompaction(col_ids_to_compact, rs));
  ASSERT_NO_FATAL_FAILURE(VerifyDataWithMvccAndExpectedState(snap, old_state));
  ASSERT_NO_FATAL_FAILURE(VerifyData());
}

// Test that the delete REDO mutations are written back and not filtered out.
TEST_F(TestMajorDeltaCompaction, TestCarryDeletesOver) {
  const int kNumRows = 100;

  ASSERT_NO_FATAL_FAILURE(WriteTestTablet(kNumRows));
  ASSERT_OK(tablet()->Flush());

  vector<shared_ptr<RowSet> > all_rowsets;
  tablet()->GetRowSetsForTests(&all_rowsets);
  shared_ptr<RowSet> rs = all_rowsets.front();

  ASSERT_NO_FATAL_FAILURE(UpdateRows(kNumRows, false));
  ASSERT_OK(tablet()->FlushBiggestDMS());

  MvccSnapshot updates_snap(*tablet()->mvcc_manager());
  vector<ExpectedRow> old_state(expected_state_.size());
  std::copy(expected_state_.begin(), expected_state_.end(), old_state.begin());

  ASSERT_NO_FATAL_FAILURE(DeleteRows(kNumRows));
  ASSERT_OK(tablet()->FlushBiggestDMS());

  vector<ColumnId> col_ids_to_compact = { schema_.column_id(4) };
  ASSERT_OK(tablet()->DoMajorDeltaCompaction(col_ids_to_compact, rs));

  ASSERT_NO_FATAL_FAILURE(VerifyData());

  ASSERT_NO_FATAL_FAILURE(VerifyDataWithMvccAndExpectedState(updates_snap, old_state));
}

// Verify that reinserts only happen in the MRS and not down into the DRS. This test serves as a
// way to document how things work, and if they change then we'll know that our assumptions have
// changed.
TEST_F(TestMajorDeltaCompaction, TestReinserts) {
  const int kNumRows = 100;

  // Reinsert all the rows directly in the MRS.
  ASSERT_NO_FATAL_FAILURE(WriteTestTablet(kNumRows)); // 1st batch.
  ASSERT_NO_FATAL_FAILURE(DeleteRows(kNumRows)); // Delete 1st batch.
  ASSERT_NO_FATAL_FAILURE(WriteTestTablet(kNumRows)); // 2nd batch.
  ASSERT_OK(tablet()->Flush());

  // Update those rows, we'll try to read them at the end.
  ASSERT_NO_FATAL_FAILURE(UpdateRows(kNumRows, false)); // Update 2nd batch.
  vector<ExpectedRow> old_state(expected_state_.size());
  std::copy(expected_state_.begin(), expected_state_.end(), old_state.begin());
  MvccSnapshot second_batch_inserts(*tablet()->mvcc_manager());

  vector<shared_ptr<RowSet> > all_rowsets;
  tablet()->GetRowSetsForTests(&all_rowsets);
  ASSERT_EQ(1, all_rowsets.size());

  ASSERT_NO_FATAL_FAILURE(VerifyData());

  // Delete the rows (will go into the DMS) then reinsert them (will go in a new MRS), then flush
  // the DMS with the deletes so that we can major compact them.
  ASSERT_NO_FATAL_FAILURE(DeleteRows(kNumRows)); // Delete 2nd batch.
  ASSERT_NO_FATAL_FAILURE(WriteTestTablet(kNumRows)); // 3rd batch.
  ASSERT_OK(tablet()->FlushBiggestDMS());

  // At this point, here's the layout (the 1st batch was discarded during the first flush):
  // MRS: 3rd batch of inserts.
  // RS1: UNDO DF: Deletes for the 2nd batch.
  //      DS: Base data for the 2nd batch.
  //      REDO DF: Updates and deletes for the 2nd.

  // Now we'll push some of the updates down.
  shared_ptr<RowSet> rs = all_rowsets.front();
  vector<ColumnId> col_ids_to_compact = { schema_.column_id(4) };
  ASSERT_OK(tablet()->DoMajorDeltaCompaction(col_ids_to_compact, rs));

  // The data we'll see here is the 3rd batch of inserts, doesn't have updates.
  ASSERT_NO_FATAL_FAILURE(VerifyData());

  // Test that the 3rd batch of inserts goes into a new RS, even though it's the same row keys.
  ASSERT_OK(tablet()->Flush());
  all_rowsets.clear();
  tablet()->GetRowSetsForTests(&all_rowsets);
  ASSERT_EQ(2, all_rowsets.size());

  // Verify the 3rd batch.
  ASSERT_NO_FATAL_FAILURE(VerifyData());

  // Verify the updates in the second batch are still readable, from the first RS.
  ASSERT_NO_FATAL_FAILURE(VerifyDataWithMvccAndExpectedState(second_batch_inserts, old_state));
}

// Verify that we won't schedule a major compaction when files are just composed of deletes.
TEST_F(TestMajorDeltaCompaction, TestJustDeletes) {
  const int kNumRows = 100;

  ASSERT_NO_FATAL_FAILURE(WriteTestTablet(kNumRows));
  ASSERT_OK(tablet()->Flush());
  ASSERT_NO_FATAL_FAILURE(DeleteRows(kNumRows));
  ASSERT_OK(tablet()->FlushBiggestDMS());

  shared_ptr<RowSet> rs;
  ASSERT_EQ(0,
            tablet()->GetPerfImprovementForBestDeltaCompact(RowSet::MAJOR_DELTA_COMPACTION, &rs));
}

} // namespace tablet
} // namespace kudu
