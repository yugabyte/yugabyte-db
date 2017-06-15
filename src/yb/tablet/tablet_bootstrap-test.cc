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

#include <vector>

#include "yb/common/iterator.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log-test-base.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/consensus-test-util.h"
#include "yb/server/logical_clock.h"
#include "yb/server/metadata.h"
#include "yb/tablet/tablet_bootstrap.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/util/tostring.h"

using std::shared_ptr;
using std::string;
using std::vector;

namespace yb {

namespace log {

extern const char* kTestTable;
extern const char* kTestTablet;

} // namespace log

namespace tablet {

using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::kMinimumTerm;
using consensus::MakeOpId;
using consensus::OpId;
using consensus::ReplicateMsg;
using consensus::ReplicateMsgPtr;
using log::Log;
using log::LogAnchorRegistry;
using log::LogTestBase;
using log::ReadableLogSegment;
using server::Clock;
using server::LogicalClock;
using tserver::WriteRequestPB;

class BootstrapTest : public LogTestBase {
 protected:

  void SetUp() override {
    LogTestBase::SetUp();
  }

  Status LoadTestTabletMetadata(int mrs_id, int delta_id, scoped_refptr<TabletMetadata>* meta) {
    Schema schema = SchemaBuilder(schema_).Build();
    std::pair<PartitionSchema, Partition> partition = CreateDefaultPartition(schema);

    RETURN_NOT_OK(TabletMetadata::LoadOrCreate(
      fs_manager_.get(),
      log::kTestTable,
      log::kTestTablet,
      log::kTestTable,
      TableType::DEFAULT_TABLE_TYPE,
      schema,
      partition.first,
      partition.second,
      TABLET_DATA_READY,
      meta));
    (*meta)->SetLastDurableMrsIdForTests(mrs_id);
    if ((*meta)->GetRowSetForTests(0) != nullptr) {
      (*meta)->GetRowSetForTests(0)->SetLastDurableRedoDmsIdForTests(delta_id);
    }
    return (*meta)->Flush();
  }

  Status PersistTestTabletMetadataState(TabletDataState state) {
    scoped_refptr<TabletMetadata> meta;
    RETURN_NOT_OK(LoadTestTabletMetadata(-1, -1, &meta));
    meta->set_tablet_data_state(state);
    RETURN_NOT_OK(meta->Flush());
    return Status::OK();
  }

  Status RunBootstrapOnTestTablet(const scoped_refptr<TabletMetadata>& meta,
                                  shared_ptr<Tablet>* tablet,
                                  ConsensusBootstrapInfo* boot_info) {
    gscoped_ptr<TabletStatusListener> listener(new TabletStatusListener(meta));
    scoped_refptr<LogAnchorRegistry> log_anchor_registry(new LogAnchorRegistry());
    // Now attempt to recover the log
    RETURN_NOT_OK(BootstrapTablet(
        meta,
        scoped_refptr<Clock>(LogicalClock::CreateStartingAt(HybridTime::kInitialHybridTime)),
        shared_ptr<MemTracker>(),
        NULL,
        listener.get(),
        tablet,
        &log_,
        log_anchor_registry,
        boot_info));

    return Status::OK();
  }

  Status BootstrapTestTablet(int mrs_id,
                             int delta_id,
                             shared_ptr<Tablet>* tablet,
                             ConsensusBootstrapInfo* boot_info) {
    scoped_refptr<TabletMetadata> meta;
    RETURN_NOT_OK_PREPEND(LoadTestTabletMetadata(mrs_id, delta_id, &meta),
                          "Unable to load test tablet metadata");

    consensus::RaftConfigPB config;
    config.set_opid_index(consensus::kInvalidOpIdIndex);
    consensus::RaftPeerPB* peer = config.add_peers();
    peer->set_permanent_uuid(meta->fs_manager()->uuid());
    peer->set_member_type(consensus::RaftPeerPB::VOTER);

    gscoped_ptr<ConsensusMetadata> cmeta;
    RETURN_NOT_OK_PREPEND(ConsensusMetadata::Create(meta->fs_manager(), meta->tablet_id(),
                                                    meta->fs_manager()->uuid(),
                                                    config, kMinimumTerm, &cmeta),
                          "Unable to create consensus metadata");

    RETURN_NOT_OK_PREPEND(RunBootstrapOnTestTablet(meta, tablet, boot_info),
                          "Unable to bootstrap test tablet");
    return Status::OK();
  }

  void IterateTabletRows(const Tablet* tablet,
                         vector<string>* results) {
    gscoped_ptr<RowwiseIterator> iter;
    // TODO: there seems to be something funny with hybrid_times in this test.
    // Unless we explicitly scan at a snapshot including all hybrid_times, we don't
    // see the bootstrapped operation. This is likely due to KUDU-138 -- perhaps
    // we aren't properly setting up the clock after bootstrap.
    MvccSnapshot snap = MvccSnapshot::CreateSnapshotIncludingAllTransactions();
    ASSERT_OK(tablet->NewRowIterator(schema_, snap, Tablet::UNORDERED, &iter));
    ASSERT_OK(iter->Init(nullptr));
    ASSERT_OK(IterateToStringList(iter.get(), results));
    for (const string& result : *results) {
      VLOG(1) << result;
    }
  }
};

// Tests a normal bootstrap scenario
TEST_F(BootstrapTest, TestBootstrap) {
  BuildLog();

  AppendReplicateBatch(MakeOpId(1, current_index_));
  ASSERT_OK(RollLog());

  AppendCommit(MakeOpId(1, current_index_));

  shared_ptr<Tablet> tablet;
  ConsensusBootstrapInfo boot_info;
  ASSERT_OK(BootstrapTestTablet(-1, -1, &tablet, &boot_info));

  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());
}

// Tests attempting a local bootstrap of a tablet that was in the middle of a
// remote bootstrap before "crashing".
TEST_F(BootstrapTest, TestIncompleteRemoteBootstrap) {
  BuildLog();

  ASSERT_OK(PersistTestTabletMetadataState(TABLET_DATA_COPYING));
  shared_ptr<Tablet> tablet;
  ConsensusBootstrapInfo boot_info;
  Status s = BootstrapTestTablet(-1, -1, &tablet, &boot_info);
  ASSERT_TRUE(s.IsCorruption()) << "Expected corruption: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "TabletMetadata bootstrap state is TABLET_DATA_COPYING");
  LOG(INFO) << "State is still TABLET_DATA_COPYING, as expected: " << s.ToString();
}

// Tests the KUDU-141 scenario: bootstrap when there is
// an orphaned commit after a log roll.
// The test simulates the following scenario:
//
// 1) 'Replicate A' is written to Segment_1, which is anchored
// on MemRowSet_1.
// 2) Segment_1 is rolled, 'Commit A' is written to Segment_2.
// 3) MemRowSet_1 is flushed, releasing all anchors.
// 4) Segment_1 is garbage collected.
// 5) We crash, requiring a recovery of Segment_2 which now contains
// the orphan 'Commit A'.
TEST_F(BootstrapTest, TestOrphanCommit) {
  BuildLog();

  OpId opid = MakeOpId(1, current_index_);

  // Step 1) Write a REPLICATE to the log, and roll it.
  AppendReplicateBatch(opid);
  ASSERT_OK(RollLog());

  // Step 2) Write the corresponding COMMIT in the second segment.
  AppendCommit(opid);

  {
    shared_ptr<Tablet> tablet;
    ConsensusBootstrapInfo boot_info;

    // Step 3) Apply the operations in the log to the tablet and flush
    // the tablet to disk.
    ASSERT_OK(BootstrapTestTablet(-1, -1, &tablet, &boot_info));
    ASSERT_OK(tablet->Flush());

    // Create a new log segment.
    ASSERT_OK(RollLog());

    // Step 4) Create an orphanned commit by first adding a commit to
    // the newly rolled logfile, and then by removing the previous
    // commits.
    AppendCommit(opid);
    log::SegmentSequence segments;
    ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
    ASSERT_OK(fs_manager_->env()->DeleteFile(segments[0]->path()));
  }
  {
    shared_ptr<Tablet> tablet;
    ConsensusBootstrapInfo boot_info;

    // Note: when GLOG_v=1, the test logs should include 'Ignoring
    // orphan commit: op_type: WRITE_OP...' line.
    ASSERT_OK(BootstrapTestTablet(2, 1, &tablet, &boot_info));

    // Confirm that the legitimate data (from Step 3) is still there.
    vector<string> results;
    IterateTabletRows(tablet.get(), &results);
    ASSERT_EQ(1, results.size());
    ASSERT_EQ("(int32 key=1, int32 int_val=0, string string_val=this is a test insert)",
              results[0]);
    ASSERT_EQ(2, tablet->metadata()->last_durable_mrs_id());
  }
}

// Tests this scenario:
// Orphan COMMIT with id <= current mrs id, followed by a REPLICATE
// message with mrs_id > current mrs_id, and a COMMIT message for that
// REPLICATE message.
//
// This should result in the orphan COMMIT being ignored, but the last
// REPLICATE/COMMIT messages ending up in the tablet.
TEST_F(BootstrapTest, TestNonOrphansAfterOrphanCommit) {
  BuildLog();

  OpId opid = MakeOpId(1, current_index_);

  AppendReplicateBatch(opid);
  ASSERT_OK(RollLog());

  AppendCommit(opid);

  log::SegmentSequence segments;
  ASSERT_OK(log_->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_OK(fs_manager_->env()->DeleteFile(segments[0]->path()));

  current_index_ += 2;

  opid = MakeOpId(1, current_index_);

  AppendReplicateBatch(opid);
  AppendCommit(opid, 2, 1, 0);

  shared_ptr<Tablet> tablet;
  ConsensusBootstrapInfo boot_info;
  ASSERT_OK(BootstrapTestTablet(1, 0, &tablet, &boot_info));

  // Confirm that the legitimate data is there.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());

  // 'key=3' means the REPLICATE message was inserted when current_id_ was 3, meaning
  // that only the non-orphan commit went in.
  ASSERT_EQ("(int32 key=3, int32 int_val=0, string string_val=this is a test insert)",
            results[0]);
}

// Test for where the server crashes in between REPLICATE and COMMIT.
// Bootstrap should not replay the operation, but should return it in
// the ConsensusBootstrapInfo
TEST_F(BootstrapTest, TestOrphanedReplicate) {
  BuildLog();

  // Append a REPLICATE with no commit
  int replicate_index = current_index_++;

  OpId opid = MakeOpId(1, replicate_index);

  AppendReplicateBatch(opid);

  // Bootstrap the tablet. It shouldn't replay anything.
  ConsensusBootstrapInfo boot_info;
  shared_ptr<Tablet> tablet;
  ASSERT_OK(BootstrapTestTablet(0, 0, &tablet, &boot_info));

  // Table should be empty because we didn't replay the REPLICATE
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(0, results.size());

  // The consensus bootstrap info should include the orphaned REPLICATE.
  ASSERT_EQ(1, boot_info.orphaned_replicates.size())
      << util::ToString(boot_info.orphaned_replicates);
  ASSERT_STR_CONTAINS(boot_info.orphaned_replicates[0]->ShortDebugString(),
                      "this is a test mutate");

  // And it should also include the latest opids.
  EXPECT_EQ("term: 1 index: 1", boot_info.last_id.ShortDebugString());
}

// Bootstrap should fail if no ConsensusMetadata file exists.
TEST_F(BootstrapTest, TestMissingConsensusMetadata) {
  BuildLog();

  scoped_refptr<TabletMetadata> meta;
  ASSERT_OK(LoadTestTabletMetadata(-1, -1, &meta));

  shared_ptr<Tablet> tablet;
  ConsensusBootstrapInfo boot_info;
  Status s = RunBootstrapOnTestTablet(meta, &tablet, &boot_info);

  ASSERT_TRUE(s.IsNotFound());
  ASSERT_STR_CONTAINS(s.ToString(), "Unable to load Consensus metadata");
}

TEST_F(BootstrapTest, TestOperationOverwriting) {
  BuildLog();

  OpId opid = MakeOpId(1, 1);

  // Append a replicate in term 1
  AppendReplicateBatch(opid);

  // Append a commit for op 1.1
  AppendCommit(opid);

  // Now append replicates for 4.2 and 4.3
  AppendReplicateBatch(MakeOpId(4, 2));
  AppendReplicateBatch(MakeOpId(4, 3));

  ASSERT_OK(RollLog());
  // And overwrite with 3.2
  AppendReplicateBatch(MakeOpId(3, 2), true);

  // When bootstrapping we should apply ops 1.1 and get 3.2 as pending.
  ConsensusBootstrapInfo boot_info;
  shared_ptr<Tablet> tablet;
  ASSERT_OK(BootstrapTestTablet(-1, -1, &tablet, &boot_info));

  ASSERT_EQ(boot_info.orphaned_replicates.size(), 1);
  ASSERT_OPID_EQ(boot_info.orphaned_replicates[0]->id(), MakeOpId(3, 2));

  // Confirm that the legitimate data is there.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());

  ASSERT_EQ("(int32 key=1, int32 int_val=0, string string_val=this is a test insert)",
            results[0]);
}

// Tests that when we have out-of-order commits that touch the same rows, operations are
// still applied and in the correct order.
TEST_F(BootstrapTest, TestOutOfOrderCommits) {
  BuildLog();

  auto replicate = std::make_shared<consensus::ReplicateMsg>();
  replicate->set_op_type(consensus::WRITE_OP);
  tserver::WriteRequestPB* batch_request = replicate->mutable_write_request();
  ASSERT_OK(SchemaToPB(schema_, batch_request->mutable_schema()));
  batch_request->set_tablet_id(log::kTestTablet);

  // This appends Insert(1) with op 10.10
  OpId insert_opid = MakeOpId(10, 10);
  replicate->mutable_id()->CopyFrom(insert_opid);
  replicate->set_hybrid_time(clock_->Now().ToUint64());
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, 10, 1,
                 "this is a test insert", batch_request->mutable_row_operations());
  AppendReplicateBatch(replicate, true);

  // This appends Mutate(1) with op 10.11
  OpId mutate_opid = MakeOpId(10, 11);
  batch_request->mutable_row_operations()->Clear();
  replicate->mutable_id()->CopyFrom(mutate_opid);
  replicate->set_hybrid_time(clock_->Now().ToUint64());
  AddTestRowToPB(RowOperationsPB::UPDATE, schema_,
                 10, 2, "this is a test mutate",
                 batch_request->mutable_row_operations());
  AppendReplicateBatch(replicate, true);

  // Now commit the mutate before the insert (in the log).
  gscoped_ptr<consensus::CommitMsg> mutate_commit(new consensus::CommitMsg);
  mutate_commit->set_op_type(consensus::WRITE_OP);
  mutate_commit->mutable_commited_op_id()->CopyFrom(mutate_opid);
  TxResultPB* result = mutate_commit->mutable_result();
  OperationResultPB* mutate = result->add_ops();
  MemStoreTargetPB* target = mutate->add_mutated_stores();
  target->set_mrs_id(1);

  AppendCommit(mutate_commit.Pass());

  gscoped_ptr<consensus::CommitMsg> insert_commit(new consensus::CommitMsg);
  insert_commit->set_op_type(consensus::WRITE_OP);
  insert_commit->mutable_commited_op_id()->CopyFrom(insert_opid);
  result = insert_commit->mutable_result();
  OperationResultPB* insert = result->add_ops();
  target = insert->add_mutated_stores();
  target->set_mrs_id(1);

  AppendCommit(insert_commit.Pass());

  ConsensusBootstrapInfo boot_info;
  shared_ptr<Tablet> tablet;
  ASSERT_OK(BootstrapTestTablet(-1, -1, &tablet, &boot_info));

  // Confirm that both operations were applied.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());

  ASSERT_EQ("(int32 key=10, int32 int_val=2, string string_val=this is a test mutate)",
            results[0]);
}

// Tests that when we have two consecutive replicates but the commit message for the
// first one is missing, both appear as pending in ConsensusInfo.
TEST_F(BootstrapTest, TestMissingCommitMessage) {
  BuildLog();

  auto replicate = std::make_shared<consensus::ReplicateMsg>();
  replicate->set_op_type(consensus::WRITE_OP);
  tserver::WriteRequestPB* batch_request = replicate->mutable_write_request();
  ASSERT_OK(SchemaToPB(schema_, batch_request->mutable_schema()));
  batch_request->set_tablet_id(log::kTestTablet);

  // This appends Insert(1) with op 10.10
  OpId insert_opid = MakeOpId(10, 10);
  replicate->mutable_id()->CopyFrom(insert_opid);
  replicate->set_hybrid_time(clock_->Now().ToUint64());
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, 10, 1,
                 "this is a test insert", batch_request->mutable_row_operations());
  AppendReplicateBatch(replicate, true);

  // This appends Mutate(1) with op 10.11
  OpId mutate_opid = MakeOpId(10, 11);
  batch_request->mutable_row_operations()->Clear();
  replicate->mutable_id()->CopyFrom(mutate_opid);
  replicate->set_hybrid_time(clock_->Now().ToUint64());
  AddTestRowToPB(RowOperationsPB::UPDATE, schema_,
                 10, 2, "this is a test mutate",
                 batch_request->mutable_row_operations());
  AppendReplicateBatch(replicate, true);

  // Now commit the mutate before the insert (in the log).
  gscoped_ptr<consensus::CommitMsg> mutate_commit(new consensus::CommitMsg);
  mutate_commit->set_op_type(consensus::WRITE_OP);
  mutate_commit->mutable_commited_op_id()->CopyFrom(mutate_opid);
  TxResultPB* result = mutate_commit->mutable_result();
  OperationResultPB* mutate = result->add_ops();
  MemStoreTargetPB* target = mutate->add_mutated_stores();
  target->set_mrs_id(1);

  AppendCommit(mutate_commit.Pass());

  ConsensusBootstrapInfo boot_info;
  shared_ptr<Tablet> tablet;
  ASSERT_OK(BootstrapTestTablet(-1, -1, &tablet, &boot_info));
  ASSERT_EQ(boot_info.orphaned_replicates.size(), 2);
  ASSERT_OPID_EQ(boot_info.last_committed_id, mutate_opid);

  // Confirm that no operation was applied.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(0, results.size());
}

// Test that we do not crash when a consensus-only operation has a hybrid_time
// that is higher than a hybrid_time assigned to a write operation that follows
// it in the log.
TEST_F(BootstrapTest, TestConsensusOnlyOperationOutOfOrderHybridTime) {
  BuildLog();

  // Append NO_OP.
  auto noop_replicate = std::make_shared<ReplicateMsg>();
  noop_replicate->set_op_type(consensus::NO_OP);
  *noop_replicate->mutable_id() = MakeOpId(1, 1);
  noop_replicate->set_hybrid_time(2);

  AppendReplicateBatch(noop_replicate, true);

  // Append WRITE_OP with higher OpId and lower hybrid_time.
  auto write_replicate = std::make_shared<ReplicateMsg>();
  write_replicate->set_op_type(consensus::WRITE_OP);
  WriteRequestPB* batch_request = write_replicate->mutable_write_request();
  ASSERT_OK(SchemaToPB(schema_, batch_request->mutable_schema()));
  batch_request->set_tablet_id(log::kTestTablet);
  *write_replicate->mutable_id() = MakeOpId(1, 2);
  write_replicate->set_hybrid_time(1);
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1, 1, "foo",
                 batch_request->mutable_row_operations());

  AppendReplicateBatch(write_replicate, true);

  // Now commit in OpId order.
  // NO_OP...
  gscoped_ptr<consensus::CommitMsg> mutate_commit(new consensus::CommitMsg);
  mutate_commit->set_op_type(consensus::NO_OP);
  *mutate_commit->mutable_commited_op_id() = noop_replicate->id();

  AppendCommit(mutate_commit.Pass());

  // ...and WRITE_OP...
  mutate_commit.reset(new consensus::CommitMsg);
  mutate_commit->set_op_type(consensus::WRITE_OP);
  *mutate_commit->mutable_commited_op_id() = write_replicate->id();
  TxResultPB* result = mutate_commit->mutable_result();
  OperationResultPB* mutate = result->add_ops();
  MemStoreTargetPB* target = mutate->add_mutated_stores();
  target->set_mrs_id(1);

  AppendCommit(mutate_commit.Pass());

  ConsensusBootstrapInfo boot_info;
  shared_ptr<Tablet> tablet;
  ASSERT_OK(BootstrapTestTablet(-1, -1, &tablet, &boot_info));
  ASSERT_EQ(boot_info.orphaned_replicates.size(), 0);
  ASSERT_OPID_EQ(boot_info.last_committed_id, write_replicate->id());

  // Confirm that the insert op was applied.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());
}

} // namespace tablet
} // namespace yb
