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

#include "kudu/common/row.h"
#include "kudu/common/scan_spec.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/server/logical_clock.h"
#include "kudu/tablet/memrowset.h"
#include "kudu/tablet/tablet-test-util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_macros.h"

DECLARE_bool(enable_data_block_fsync);
DEFINE_int32(roundtrip_num_rows, 10000,
             "Number of rows to use for the round-trip test");
DEFINE_int32(num_scan_passes, 1,
             "Number of passes to run the scan portion of the round-trip test");

namespace kudu {
namespace tablet {

using consensus::OpId;
using log::LogAnchorRegistry;
using std::shared_ptr;

class TestMemRowSet : public ::testing::Test {
 public:
  TestMemRowSet()
    : op_id_(consensus::MaximumOpId()),
      log_anchor_registry_(new LogAnchorRegistry()),
      schema_(CreateSchema()),
      key_schema_(schema_.CreateKeyProjection()),
      mvcc_(scoped_refptr<server::Clock>(
              server::LogicalClock::CreateStartingAt(Timestamp::kInitialTimestamp))) {
    FLAGS_enable_data_block_fsync = false; // Keep unit tests fast.
  }

  static Schema CreateSchema() {
    SchemaBuilder builder;
    CHECK_OK(builder.AddKeyColumn("key", STRING));
    CHECK_OK(builder.AddColumn("val", UINT32));
    return builder.Build();
  }

 protected:
  // Check that the given row in the memrowset contains the given data.
  void CheckValue(const shared_ptr<MemRowSet> &mrs, string key,
                  const string &expected_row) {
    gscoped_ptr<MemRowSet::Iterator> iter(mrs->NewIterator());
    ASSERT_OK(iter->Init(nullptr));

    Slice keystr_slice(key);
    Slice key_slice(reinterpret_cast<const char *>(&keystr_slice), sizeof(Slice));

    bool exact;
    ASSERT_OK(iter->SeekAtOrAfter(key_slice, &exact));
    ASSERT_TRUE(exact) << "unable to seek to key " << key;
    ASSERT_TRUE(iter->HasNext());

    vector<string> out;
    ASSERT_OK(IterateToStringList(iter.get(), &out, 1));
    ASSERT_EQ(1, out.size());
    ASSERT_EQ(expected_row, out[0]) << "bad result for key " << key;
  }

  Status CheckRowPresent(const MemRowSet &mrs,
                         const string &key, bool *present) {
    RowBuilder rb(key_schema_);
    rb.AddString(Slice(key));
    RowSetKeyProbe probe(rb.row());
    ProbeStats stats;

    return mrs.CheckRowPresent(probe, present, &stats);
  }

  Status InsertRows(MemRowSet *mrs, int num_rows) {
    RowBuilder rb(schema_);
    char keybuf[256];
    for (uint32_t i = 0; i < num_rows; i++) {
      rb.Reset();
      snprintf(keybuf, sizeof(keybuf), "hello %d", i);
      rb.AddString(Slice(keybuf));
      rb.AddUint32(i);
      RETURN_NOT_OK(mrs->Insert(Timestamp(i), rb.row(), op_id_));
    }

    return Status::OK();
  }

  Status InsertRow(MemRowSet *mrs, const string &key, uint32_t val) {
    ScopedTransaction tx(&mvcc_);
    RowBuilder rb(schema_);
    rb.AddString(key);
    rb.AddUint32(val);
    tx.StartApplying();
    Status s = mrs->Insert(tx.timestamp(), rb.row(), op_id_);
    tx.Commit();
    return s;
  }

  Status UpdateRow(MemRowSet *mrs,
                   const string &key,
                   uint32_t new_val,
                   OperationResultPB* result) {
    ScopedTransaction tx(&mvcc_);
    tx.StartApplying();

    mutation_buf_.clear();
    RowChangeListEncoder update(&mutation_buf_);
    update.AddColumnUpdate(schema_.column(1), schema_.column_id(1), &new_val);

    RowBuilder rb(key_schema_);
    rb.AddString(Slice(key));
    RowSetKeyProbe probe(rb.row());
    ProbeStats stats;
    Status s = mrs->MutateRow(tx.timestamp(),
                              probe,
                              RowChangeList(mutation_buf_),
                              op_id_,
                              &stats,
                              result);
    tx.Commit();
    return s;
  }

  Status DeleteRow(MemRowSet *mrs, const string &key, OperationResultPB* result) {
    ScopedTransaction tx(&mvcc_);
    tx.StartApplying();

    mutation_buf_.clear();
    RowChangeListEncoder update(&mutation_buf_);
    update.SetToDelete();

    RowBuilder rb(key_schema_);
    rb.AddString(Slice(key));
    RowSetKeyProbe probe(rb.row());
    ProbeStats stats;
    Status s = mrs->MutateRow(tx.timestamp(),
                              probe,
                              RowChangeList(mutation_buf_),
                              op_id_,
                              &stats,
                              result);
    tx.Commit();
    return s;
  }

  int ScanAndCount(MemRowSet* mrs, const MvccSnapshot& snap) {
    gscoped_ptr<MemRowSet::Iterator> iter(mrs->NewIterator(&schema_, snap));
    CHECK_OK(iter->Init(NULL));

    Arena arena(1024, 256*1024);
    RowBlock block(schema_, 100, &arena);
    int fetched = 0;
    while (iter->HasNext()) {
      CHECK_OK(iter->NextBlock(&block));
      fetched += block.selection_vector()->CountSelected();
    }
    return fetched;
  }

  OpId op_id_;
  scoped_refptr<LogAnchorRegistry> log_anchor_registry_;

  faststring mutation_buf_;
  const Schema schema_;
  const Schema key_schema_;
  MvccManager mvcc_;
};


TEST_F(TestMemRowSet, TestInsertAndIterate) {
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));

  ASSERT_OK(InsertRow(mrs.get(), "hello world", 12345));
  ASSERT_OK(InsertRow(mrs.get(), "goodbye world", 54321));

  ASSERT_EQ(2, mrs->entry_count());

  gscoped_ptr<MemRowSet::Iterator> iter(mrs->NewIterator());
  ASSERT_OK(iter->Init(nullptr));

  // The first row returned from the iterator should
  // be "goodbye" because 'g' sorts before 'h'
  ASSERT_TRUE(iter->HasNext());
  MRSRow row = iter->GetCurrentRow();
  EXPECT_EQ("(string key=goodbye world, uint32 val=54321)", schema_.DebugRow(row));

  // Next row should be 'hello world'
  ASSERT_TRUE(iter->Next());
  ASSERT_TRUE(iter->HasNext());
  row = iter->GetCurrentRow();
  EXPECT_EQ("(string key=hello world, uint32 val=12345)", schema_.DebugRow(row));

  ASSERT_FALSE(iter->Next());
  ASSERT_FALSE(iter->HasNext());
}

TEST_F(TestMemRowSet, TestInsertAndIterateCompoundKey) {

  SchemaBuilder builder;
  ASSERT_OK(builder.AddKeyColumn("key1", STRING));
  ASSERT_OK(builder.AddKeyColumn("key2", INT32));
  ASSERT_OK(builder.AddColumn("val", UINT32));
  Schema compound_key_schema = builder.Build();

  shared_ptr<MemRowSet> mrs(new MemRowSet(0, compound_key_schema, log_anchor_registry_.get()));

  RowBuilder rb(compound_key_schema);
  {
    ScopedTransaction tx(&mvcc_);
    tx.StartApplying();
    rb.AddString(string("hello world"));
    rb.AddInt32(1);
    rb.AddUint32(12345);
    Status row1 = mrs->Insert(tx.timestamp(), rb.row(), op_id_);
    ASSERT_OK(row1);
    tx.Commit();
  }

  {
    ScopedTransaction tx2(&mvcc_);
    tx2.StartApplying();
    rb.Reset();
    rb.AddString(string("goodbye world"));
    rb.AddInt32(2);
    rb.AddUint32(54321);
    Status row2 = mrs->Insert(tx2.timestamp(), rb.row(), op_id_);
    ASSERT_OK(row2);
    tx2.Commit();
  }

  {
    ScopedTransaction tx3(&mvcc_);
    tx3.StartApplying();
    rb.Reset();
    rb.AddString(string("goodbye world"));
    rb.AddInt32(1);
    rb.AddUint32(12345);
    Status row3 = mrs->Insert(tx3.timestamp(), rb.row(), op_id_);
    ASSERT_OK(row3);
    tx3.Commit();
  }

  ASSERT_EQ(3, mrs->entry_count());

  gscoped_ptr<MemRowSet::Iterator> iter(mrs->NewIterator());
  ASSERT_OK(iter->Init(nullptr));

  // The first row returned from the iterator should
  // be "goodbye" (row3) sorted on the second key
  ASSERT_TRUE(iter->HasNext());
  MRSRow row = iter->GetCurrentRow();
  EXPECT_EQ("(string key1=goodbye world, int32 key2=1, uint32 val=12345)",
            compound_key_schema.DebugRow(row));

  // Next row should be "goodbye" (row2)
  ASSERT_TRUE(iter->Next());
  ASSERT_TRUE(iter->HasNext());
  row = iter->GetCurrentRow();
  EXPECT_EQ("(string key1=goodbye world, int32 key2=2, uint32 val=54321)",
            compound_key_schema.DebugRow(row));

  // Next row should be 'hello world' (row1)
  ASSERT_TRUE(iter->Next());
  ASSERT_TRUE(iter->HasNext());
  row = iter->GetCurrentRow();
  EXPECT_EQ("(string key1=hello world, int32 key2=1, uint32 val=12345)",
            compound_key_schema.DebugRow(row));

  ASSERT_FALSE(iter->Next());
  ASSERT_FALSE(iter->HasNext());
}

// Test that inserting duplicate key data fails with Status::AlreadyPresent
TEST_F(TestMemRowSet, TestInsertDuplicate) {
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));

  ASSERT_OK(InsertRow(mrs.get(), "hello world", 12345));
  Status s = InsertRow(mrs.get(), "hello world", 12345);
  ASSERT_TRUE(s.IsAlreadyPresent()) << "bad status: " << s.ToString();
}

// Test for updating rows in memrowset
TEST_F(TestMemRowSet, TestUpdate) {
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));

  ASSERT_OK(InsertRow(mrs.get(), "hello world", 1));

  // Validate insertion
  CheckValue(mrs, "hello world", "(string key=hello world, uint32 val=1)");

  // Update a key which exists.
  OperationResultPB result;
  ASSERT_OK(UpdateRow(mrs.get(), "hello world", 2, &result));
  ASSERT_EQ(1, result.mutated_stores_size());
  ASSERT_EQ(0L, result.mutated_stores(0).mrs_id());

  // Validate the updated value
  CheckValue(mrs, "hello world", "(string key=hello world, uint32 val=2)");

  // Try to update a key which doesn't exist - should return NotFound
  result.Clear();
  Status s = UpdateRow(mrs.get(), "does not exist", 3, &result);
  ASSERT_TRUE(s.IsNotFound()) << "bad status: " << s.ToString();
  ASSERT_EQ(0, result.mutated_stores_size());
}

// Test which inserts many rows into memrowset and checks for their
// existence
TEST_F(TestMemRowSet, TestInsertCopiesToArena) {
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));

  ASSERT_OK(InsertRows(mrs.get(), 100));
  // Validate insertion
  char keybuf[256];
  for (uint32_t i = 0; i < 100; i++) {
    snprintf(keybuf, sizeof(keybuf), "hello %d", i);
    CheckValue(mrs, keybuf,
               StringPrintf("(string key=%s, uint32 val=%d)", keybuf, i));
  }
}

TEST_F(TestMemRowSet, TestDelete) {
  const char kRowKey[] = "hello world";
  bool present;

  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));

  // Insert row.
  ASSERT_OK(InsertRow(mrs.get(), kRowKey, 1));
  MvccSnapshot snapshot_before_delete(mvcc_);

  // CheckRowPresent should return true
  ASSERT_OK(CheckRowPresent(*mrs, kRowKey, &present));
  EXPECT_TRUE(present);

  // Delete it.
  OperationResultPB result;
  ASSERT_OK(DeleteRow(mrs.get(), kRowKey, &result));
  ASSERT_EQ(1, result.mutated_stores_size());
  ASSERT_EQ(0L, result.mutated_stores(0).mrs_id());

  MvccSnapshot snapshot_after_delete(mvcc_);

  // CheckRowPresent should return false
  ASSERT_OK(CheckRowPresent(*mrs, kRowKey, &present));
  EXPECT_FALSE(present);

  // Trying to Delete again or Update should get an error.
  result.Clear();
  Status s = DeleteRow(mrs.get(), kRowKey, &result);
  ASSERT_TRUE(s.IsNotFound()) << "Unexpected status: " << s.ToString();
  ASSERT_EQ(0, result.mutated_stores_size());

  result.Clear();
  s = UpdateRow(mrs.get(), kRowKey, 12345, &result);
  ASSERT_TRUE(s.IsNotFound()) << "Unexpected status: " << s.ToString();
  ASSERT_EQ(0, result.mutated_stores_size());

  // Re-insert a new row with the same key.
  ASSERT_OK(InsertRow(mrs.get(), kRowKey, 2));
  MvccSnapshot snapshot_after_reinsert(mvcc_);

  // CheckRowPresent should now return true
  ASSERT_OK(CheckRowPresent(*mrs, kRowKey, &present));
  EXPECT_TRUE(present);

  // Verify the MVCC contents of the memrowset.
  // NOTE: the REINSERT has timestamp 4 because of the two failed attempts
  // at mutating the deleted row above -- each of them grabs a timestamp even
  // though it doesn't actually make any successful mutations.
  vector<string> rows;
  ASSERT_OK(mrs->DebugDump(&rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ("@1: row (string key=hello world, uint32 val=1) mutations="
            "[@2(DELETE), "
            "@5(REINSERT (string key=hello world, uint32 val=2))]",
            rows[0]);

  // Verify that iterating the rowset at the first snapshot shows the row.
  ASSERT_OK(DumpRowSet(*mrs, schema_, snapshot_before_delete, &rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ("(string key=hello world, uint32 val=1)", rows[0]);

  // Verify that iterating the rowset at the snapshot where it's deleted
  // doesn't show the row.
  ASSERT_OK(DumpRowSet(*mrs, schema_, snapshot_after_delete, &rows));
  ASSERT_EQ(0, rows.size());

  // Verify that iterating the rowset after it's re-inserted shows the row.
  ASSERT_OK(DumpRowSet(*mrs, schema_, snapshot_after_reinsert, &rows));
  ASSERT_EQ(1, rows.size());
  EXPECT_EQ("(string key=hello world, uint32 val=2)", rows[0]);
}

// Test for basic operations.
// Can operate as a benchmark by setting --roundtrip_num_rows to a high value like 10M
TEST_F(TestMemRowSet, TestMemRowSetInsertCountAndScan) {
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));

  LOG_TIMING(INFO, "Inserting rows") {
    ASSERT_OK(InsertRows(mrs.get(), FLAGS_roundtrip_num_rows));
  }

  LOG_TIMING(INFO, "Counting rows") {
    int count = mrs->entry_count();
    ASSERT_EQ(FLAGS_roundtrip_num_rows, count);
  }

  for (int i = 0; i < FLAGS_num_scan_passes; i++) {
    LOG_TIMING(INFO, "Scanning rows where none are committed") {
      ASSERT_EQ(0, ScanAndCount(mrs.get(), MvccSnapshot(Timestamp(0))));
    }

    LOG_TIMING(INFO, "Scanning rows where all are committed") {
      ASSERT_EQ(FLAGS_roundtrip_num_rows,
                ScanAndCount(mrs.get(),
                             MvccSnapshot(Timestamp(FLAGS_roundtrip_num_rows + 1))));
    }
  }
}
// Test that scanning at past MVCC snapshots will hide rows which are
// not committed in that snapshot.
TEST_F(TestMemRowSet, TestInsertionMVCC) {
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));
  vector<MvccSnapshot> snapshots;

  // Insert 5 rows in tx 0 through 4
  for (uint32_t i = 0; i < 5; i++) {
    {
      ScopedTransaction tx(&mvcc_);
      tx.StartApplying();
      RowBuilder rb(schema_);
      char keybuf[256];
      rb.Reset();
      snprintf(keybuf, sizeof(keybuf), "tx%d", i);
      rb.AddString(Slice(keybuf));
      rb.AddUint32(i);
      ASSERT_OK_FAST(mrs->Insert(tx.timestamp(), rb.row(), op_id_));
      tx.Commit();
    }

    // Transaction is committed. Save the snapshot after this commit.
    snapshots.push_back(MvccSnapshot(mvcc_));
  }
  LOG(INFO) << "MemRowSet after inserts:";
  ASSERT_OK(mrs->DebugDump());

  ASSERT_EQ(5, snapshots.size());
  for (int i = 0; i < 5; i++) {
    SCOPED_TRACE(i);
    // Each snapshot 'i' is taken after row 'i' was committed.
    vector<string> rows;
    ASSERT_OK(kudu::tablet::DumpRowSet(*mrs, schema_, snapshots[i], &rows));
    ASSERT_EQ(1 + i, rows.size());
    string expected = StringPrintf("(string key=tx%d, uint32 val=%d)", i, i);
    ASSERT_EQ(expected, rows[i]);
  }
}

// Test that updates respect MVCC -- i.e. that scanning with a past MVCC snapshot
// will yield old versions of a row which has been updated.
TEST_F(TestMemRowSet, TestUpdateMVCC) {
  shared_ptr<MemRowSet> mrs(new MemRowSet(0, schema_, log_anchor_registry_.get()));

  // Insert a row ("myrow", 0)
  ASSERT_OK(InsertRow(mrs.get(), "my row", 0));

  vector<MvccSnapshot> snapshots;
  // First snapshot is after insertion
  snapshots.push_back(MvccSnapshot(mvcc_));

  // Update the row 5 times (setting its int column to increasing ints 1-5)
  for (uint32_t i = 1; i <= 5; i++) {
    OperationResultPB result;
    ASSERT_OK(UpdateRow(mrs.get(), "my row", i, &result));
    ASSERT_EQ(1, result.mutated_stores_size());
    ASSERT_EQ(0L, result.mutated_stores(0).mrs_id());

    // Transaction is committed. Save the snapshot after this commit.
    snapshots.push_back(MvccSnapshot(mvcc_));
  }

  LOG(INFO) << "MemRowSet after updates:";
  ASSERT_OK(mrs->DebugDump());

  // Validate that each snapshot returns the expected value
  ASSERT_EQ(6, snapshots.size());
  for (int i = 0; i <= 5; i++) {
    SCOPED_TRACE(i);
    vector<string> rows;
    ASSERT_OK(kudu::tablet::DumpRowSet(*mrs, schema_, snapshots[i], &rows));
    ASSERT_EQ(1, rows.size());

    string expected = StringPrintf("(string key=my row, uint32 val=%d)", i);
    LOG(INFO) << "Reading with snapshot " << snapshots[i].ToString() << ": "
              << rows[0];
    EXPECT_EQ(expected, rows[0]);
  }
}

} // namespace tablet
} // namespace kudu
