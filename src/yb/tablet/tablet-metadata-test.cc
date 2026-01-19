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

// Test that loading & storing the superblock results in an equivalent file.
TEST_F(TestRaftGroupMetadata, TestLoadFromSuperBlock) {
  // Write some data to the tablet and flush.
  QLWriteRequestPB req;
  BuildPartialRow(0, 0, "foo", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync));

  // Create one more row. Write and flush.
  BuildPartialRow(1, 1, "bar", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(harness_->tablet()->Flush(tablet::FlushMode::kSync));

  // Shut down the tablet.
  harness_->tablet()->StartShutdown();
  harness_->tablet()->CompleteShutdown(DisableFlushOnShutdown::kFalse, AbortOps::kFalse);

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
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

  // Create one more row. Write and flush.
  BuildPartialRow(1, 1, "bar", &req);
  ASSERT_OK(writer_->Write(&req));
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));

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
