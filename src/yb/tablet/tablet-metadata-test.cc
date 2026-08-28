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
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <cstddef>

#include "yb/util/logging.h"

#include "yb/common/opid.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/ref_counted.h"

#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/util/status_log.h"

using std::string;

namespace yb {
namespace tablet {

class TestRaftGroupMetadata : public YBTabletTest {
 public:
  TestRaftGroupMetadata()
      : YBTabletTest(GetSimpleTestSchema()) {
  }

  void SetUp() override {
    YBTabletTest::SetUp();
    writer_.reset(new LocalTabletWriter(harness_->tablet()));
  }

  void BuildPartialRow(int key, int intval, const char* strval,
                       QLWriteRequestPB* req);

 protected:
  std::unique_ptr<LocalTabletWriter> writer_;
};

void TestRaftGroupMetadata::BuildPartialRow(int key, int intval, const char* strval,
                                         QLWriteRequestPB* req) {
  req->Clear();
  QLAddInt32HashValue(req, key);
  QLAddInt32ColumnValue(req, kFirstColumnId + 1, intval);
  QLAddStringColumnValue(req, kFirstColumnId + 2, strval);
}

// The two tests below pin down the CURRENT behavior when a tablet's data is not at the absolute
// path its superblock records -- the state a swapped data mount or a manual mv/cp of tablet
// directories produces. Both outcomes are silent, which is what motivates the data-root pin
// check (fs/fs_root_pin.h): nothing on the open path validates the recorded path against where
// the metadata was found.

// Data moved away from the recorded path: the tablet reopens EMPTY, creating fresh directories
// at the recorded path, and serves zero rows with no error anywhere. Acknowledged data becomes
// unreadable without a single complaint, while the bytes sit intact at the moved location.
//
// The harness's logical clock restarts at kInitial on every reopen, which would hide old-HT rows
// from reads and make this test pass vacuously; each reopen therefore restores the clock, and the
// first reopen is a control proving a plain reopen does serve the row.
TEST_F(TestRaftGroupMetadata, MovedRocksDbDirReopensEmptyWithoutComplaint) {
  QLWriteRequestPB req;
  BuildPartialRow(0, 0, "foo", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  const auto recorded_dir = harness_->tablet()->metadata()->rocksdb_dir();
  const auto pre_shutdown_ht = clock()->Now();
  harness_->tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
  harness_->tablet()->CompleteShutdown();
  // The writer holds a ref to the old tablet; drop it so TabletReOpen fully replaces the tablet.
  writer_.reset();

  // Reopens the tablet the way this harness allows and makes prior data readable again: the
  // harness clock restarts at kInitial and the fresh MvccManager only advances with traffic, so
  // restore the clock and write one sentinel row; safe time then covers everything written
  // before the shutdown.
  const auto reopen_and_write_sentinel = [&]() {
    TabletReOpen();
    clock()->Update(pre_shutdown_ht);
    writer_.reset(new LocalTabletWriter(harness_->tablet()));
    QLWriteRequestPB sentinel;
    BuildPartialRow(100, 100, "sentinel", &sentinel);
    ASSERT_OK(writer_->Write(&sentinel));
  };

  // Control: a plain reopen, data untouched, serves the acknowledged row beside the sentinel.
  ASSERT_NO_FATALS(reopen_and_write_sentinel());
  std::vector<std::string> rows;
  ASSERT_OK(DumpTablet(*harness_->tablet(), &rows));
  ASSERT_EQ(2, rows.size());

  harness_->tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
  harness_->tablet()->CompleteShutdown();
  writer_.reset();

  // The operator relocates the data (or the mount swaps): every byte still exists, just not at
  // the recorded absolute path.
  const auto moved_dir = recorded_dir + ".moved-by-operator";
  ASSERT_OK(Env::Default()->RenameFile(recorded_dir, moved_dir));

  // Reopen succeeds and takes new writes; nothing notices.
  ASSERT_NO_FATALS(reopen_and_write_sentinel());

  // The acknowledged rows are gone, a fresh database (holding only the new sentinel) sits at the
  // recorded path, and the real data is stranded at the moved path.
  ASSERT_OK(DumpTablet(*harness_->tablet(), &rows));
  ASSERT_EQ(1, rows.size());
  ASSERT_STR_CONTAINS(rows[0], "sentinel");
  ASSERT_TRUE(Env::Default()->FileExists(recorded_dir));
  ASSERT_TRUE(Env::Default()->FileExists(moved_dir));
}

// An OLDER copy of the same tablet at the recorded path (what one half of a swapped mount pair
// looks like, and what a stale manual cp leaves): the tablet opens the stale copy and serves it,
// silently un-happening every write acknowledged since the copy was taken.
TEST_F(TestRaftGroupMetadata, StaleCopyAtRecordedPathIsServedWithoutComplaint) {
  QLWriteRequestPB req;
  BuildPartialRow(0, 0, "v1", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  const auto recorded_dir = harness_->tablet()->metadata()->rocksdb_dir();
  harness_->tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
  harness_->tablet()->CompleteShutdown();
  writer_.reset();

  // Preserve the v1 incarnation, as a copied-away tablet directory or the other volume of a
  // swapped pair would.
  const auto stale_copy = recorded_dir + ".stale-copy";
  ASSERT_OK(Env::Default()->RenameFile(recorded_dir, stale_copy));

  // Second incarnation: overwrite row 0 and add row 1, i.e. writes the client saw acknowledged.
  ASSERT_NO_FATALS(TabletReOpen());
  writer_.reset(new LocalTabletWriter(harness_->tablet()));
  BuildPartialRow(0, 1, "v2", &req);
  ASSERT_OK(writer_->Write(&req));
  BuildPartialRow(1, 1, "v2-second-row", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
  const auto pre_shutdown_ht = clock()->Now();
  harness_->tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
  harness_->tablet()->CompleteShutdown();
  writer_.reset();

  // The stale incarnation lands back at the recorded path.
  ASSERT_OK(Env::Default()->DeleteRecursively(recorded_dir));
  ASSERT_OK(Env::Default()->RenameFile(stale_copy, recorded_dir));

  // Reopen succeeds and serves the stale data with no error anywhere: the second row never
  // existed and row 0 is back at "v1". (Clock restored and a sentinel written for the same
  // reason as the previous test: the harness clock and MvccManager restart on reopen.)
  ASSERT_NO_FATALS(TabletReOpen());
  clock()->Update(pre_shutdown_ht);
  writer_.reset(new LocalTabletWriter(harness_->tablet()));
  BuildPartialRow(100, 100, "sentinel", &req);
  ASSERT_OK(writer_->Write(&req));

  std::vector<std::string> rows;
  ASSERT_OK(DumpTablet(*harness_->tablet(), &rows));
  ASSERT_EQ(2, rows.size());
  ASSERT_STR_CONTAINS(yb::ToString(rows), "v1");
  ASSERT_STR_NOT_CONTAINS(yb::ToString(rows), "v2-second-row");
}

// Test that loading & storing the superblock results in an equivalent file.
TEST_F(TestRaftGroupMetadata, TestLoadFromSuperBlock) {
  // Write some data to the tablet and flush.
  QLWriteRequestPB req;
  BuildPartialRow(0, 0, "foo", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  // Create one more row. Write and flush.
  BuildPartialRow(1, 1, "bar", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  // Shut down the tablet.
  harness_->tablet()->StartShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);
  harness_->tablet()->CompleteShutdown();

  RaftGroupMetadata* meta = harness_->tablet()->metadata();

  // Dump the superblock to a PB. Save the PB to the side.
  RaftGroupReplicaSuperBlockPB superblock_pb_1;
  meta->ToSuperBlock(&superblock_pb_1);

  // Load the superblock PB back into the RaftGroupMetadata.
  ASSERT_OK(meta->ReplaceSuperBlock(superblock_pb_1));

  // Dump the tablet metadata to a superblock PB again, and save it.
  RaftGroupReplicaSuperBlockPB superblock_pb_2;
  meta->ToSuperBlock(&superblock_pb_2);

  // Compare the 2 dumped superblock PBs.
  ASSERT_EQ(superblock_pb_1.SerializeAsString(),
            superblock_pb_2.SerializeAsString())
    << superblock_pb_1.DebugString()
    << superblock_pb_2.DebugString();

  LOG(INFO) << "Superblocks match:\n"
            << superblock_pb_1.DebugString();
}

TEST_F(TestRaftGroupMetadata, TestDeleteTabletDataClearsDisk) {
  auto tablet = harness_->tablet();

  // Write some data to the tablet and flush.
  QLWriteRequestPB req;
  BuildPartialRow(0, 0, "foo", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  // Create one more row. Write and flush.
  BuildPartialRow(1, 1, "bar", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));

  const string snapshotId = "0123456789ABCDEF0123456789ABCDEF";
  tserver::TabletSnapshotOpRequestPB request;
  request.set_snapshot_id(snapshotId);
  tablet::SnapshotOperation operation(tablet);
  operation.AllocateRequest()->CopyFrom(request);
  operation.set_hybrid_time(tablet->clock()->Now());
  operation.set_op_id(OpId(-1, 2));
  ASSERT_OK(tablet->snapshots().Create(&operation));

  ASSERT_TRUE(env_->DirExists(tablet->metadata()->rocksdb_dir()));
  ASSERT_TRUE(env_->DirExists(tablet->metadata()->intents_rocksdb_dir()));
  ASSERT_TRUE(env_->DirExists(tablet->metadata()->snapshots_dir()));

  CHECK_OK(tablet->metadata()->DeleteTabletData(
    TabletDataState::TABLET_DATA_DELETED, operation.op_id()));

  ASSERT_FALSE(env_->DirExists(tablet->metadata()->rocksdb_dir()));
  ASSERT_FALSE(env_->DirExists(tablet->metadata()->intents_rocksdb_dir()));
  ASSERT_FALSE(env_->DirExists(tablet->metadata()->snapshots_dir()));
}

TEST_F(TestRaftGroupMetadata, NamespaceIdPreservedAcrossSchemaChanges) {
  // Verify that namespace_id is preserved across schema updates and packed schema insertions.
  auto tablet = harness_->tablet();
  auto* meta = tablet->metadata();

  // Simulate namespace backfill.
  const NamespaceId kNamespaceId = "0123456789abcdef0123456789abcdef";
  ASSERT_OK(meta->set_namespace_id(kNamespaceId));
  ASSERT_EQ(meta->primary_table_info()->namespace_id, kNamespaceId);

  auto initial_table_info = meta->primary_table_info();
  const Schema initial_schema = initial_table_info->schema();
  const auto initial_version = initial_table_info->schema_version;
  const qlexpr::IndexMap& index_map = *initial_table_info->index_map;
  const TableId& table_id = initial_table_info->table_id;

  // Perform schema changes and verify that namespace_id is preserved.

  // 1. SetSchema.
  meta->SetSchema(
      initial_schema, index_map, /* deleted_cols */ {}, initial_version + 1, OpId() /* op_id */,
      table_id);
  ASSERT_EQ(meta->primary_table_info()->namespace_id, kNamespaceId);

  // 2. InsertPackedSchemaForXClusterTarget.
  meta->InsertPackedSchemaForXClusterTarget(
      initial_schema, index_map, initial_version + 3, OpId() /* op_id */, table_id);
  ASSERT_EQ(meta->primary_table_info()->namespace_id, kNamespaceId);
}

} // namespace tablet
} // namespace yb
