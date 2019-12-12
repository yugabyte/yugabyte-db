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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <vector>

#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log-test-base.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/consensus-test-util.h"
#include "yb/server/logical_clock.h"
#include "yb/server/metadata.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/util/tostring.h"
#include "yb/tablet/tablet_options.h"

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

  static constexpr TableType kTableType = TableType::YQL_TABLE_TYPE;

  void SetUp() override {
    LogTestBase::SetUp();
  }

  Status LoadTestRaftGroupMetadata(RaftGroupMetadataPtr* meta) {
    Schema schema = SchemaBuilder(schema_).Build();
    std::pair<PartitionSchema, Partition> partition = CreateDefaultPartition(schema);

    RETURN_NOT_OK(RaftGroupMetadata::LoadOrCreate(
        fs_manager_.get(),
        log::kTestTable,
        log::kTestTablet,
        log::kTestTable,
        kTableType,
        schema,
        partition.first,
        partition.second,
        boost::none /* index_info */,
        TABLET_DATA_READY,
        meta));
    return (*meta)->Flush();
  }

  Status PersistTestRaftGroupMetadataState(TabletDataState state) {
    RaftGroupMetadataPtr meta;
    RETURN_NOT_OK(LoadTestRaftGroupMetadata(&meta));
    meta->set_tablet_data_state(state);
    RETURN_NOT_OK(meta->Flush());
    return Status::OK();
  }

  Status RunBootstrapOnTestTablet(const RaftGroupMetadataPtr& meta,
                                  shared_ptr<TabletClass>* tablet,
                                  ConsensusBootstrapInfo* boot_info) {
    gscoped_ptr<TabletStatusListener> listener(new TabletStatusListener(meta));
    scoped_refptr<LogAnchorRegistry> log_anchor_registry(new LogAnchorRegistry());
    // Now attempt to recover the log
    TabletOptions tablet_options;
    BootstrapTabletData data = {
        .meta = meta,
        .client_future = std::shared_future<client::YBClient*>(),
        .clock = scoped_refptr<Clock>(LogicalClock::CreateStartingAt(HybridTime::kInitial)),
        .mem_tracker = shared_ptr<MemTracker>(),
        .block_based_table_mem_tracker = shared_ptr<MemTracker>(),
        .metric_registry = nullptr,
        .listener = listener.get(),
        .log_anchor_registry = log_anchor_registry,
        .tablet_options = tablet_options,
        .log_prefix_suffix = std::string(),
        .transaction_participant_context = nullptr,
        .local_tablet_filter = client::LocalTabletFilter(),
        .transaction_coordinator_context = nullptr,
        .append_pool = append_pool_.get(),
        .retryable_requests = nullptr,
        .txns_enabled = TransactionsEnabled::kTrue,
        .is_sys_catalog = IsSysCatalogTablet::kFalse
    };
    RETURN_NOT_OK(BootstrapTablet(data, tablet, &log_, boot_info));
    return Status::OK();
  }

  Status BootstrapTestTablet(shared_ptr<TabletClass>* tablet,
                             ConsensusBootstrapInfo* boot_info) {
    RaftGroupMetadataPtr meta;
    RETURN_NOT_OK_PREPEND(LoadTestRaftGroupMetadata(&meta),
                          "Unable to load test tablet metadata");

    consensus::RaftConfigPB config;
    config.set_opid_index(consensus::kInvalidOpIdIndex);
    consensus::RaftPeerPB* peer = config.add_peers();
    peer->set_permanent_uuid(meta->fs_manager()->uuid());
    peer->set_member_type(consensus::RaftPeerPB::VOTER);

    std::unique_ptr<ConsensusMetadata> cmeta;
    RETURN_NOT_OK_PREPEND(ConsensusMetadata::Create(meta->fs_manager(), meta->raft_group_id(),
                                                    meta->fs_manager()->uuid(),
                                                    config, kMinimumTerm, &cmeta),
                          "Unable to create consensus metadata");

    RETURN_NOT_OK_PREPEND(RunBootstrapOnTestTablet(meta, tablet, boot_info),
                          "Unable to bootstrap test tablet");
    return Status::OK();
  }

  void IterateTabletRows(const Tablet* tablet,
                         vector<string>* results) {
    auto iter = tablet->NewRowIterator(schema_, boost::none);
    ASSERT_OK(iter);
    ASSERT_OK(IterateToStringList(iter->get(), results));
    for (const string& result : *results) {
      VLOG(1) << result;
    }
  }
};

// Tests a normal bootstrap scenario.
TEST_F(BootstrapTest, TestBootstrap) {
  BuildLog();
  const auto current_op_id = MakeOpId(1, current_index_);
  AppendReplicateBatch(current_op_id, current_op_id);
  shared_ptr<TabletClass> tablet;
  ConsensusBootstrapInfo boot_info;
  ASSERT_OK(BootstrapTestTablet(&tablet, &boot_info));

  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
}

// Tests attempting a local bootstrap of a tablet that was in the middle of a remote bootstrap
// before "crashing".
TEST_F(BootstrapTest, TestIncompleteRemoteBootstrap) {
  BuildLog();

  ASSERT_OK(PersistTestRaftGroupMetadataState(TABLET_DATA_COPYING));
  shared_ptr<TabletClass> tablet;
  ConsensusBootstrapInfo boot_info;
  Status s = BootstrapTestTablet(&tablet, &boot_info);
  ASSERT_TRUE(s.IsCorruption()) << "Expected corruption: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "RaftGroupMetadata bootstrap state is TABLET_DATA_COPYING");
  LOG(INFO) << "State is still TABLET_DATA_COPYING, as expected: " << s.ToString();
}

// Test a crash before a REPLICATE message is marked as committed by a future REPLICATE message.
// Bootstrap should not replay the operation, but should return it in the ConsensusBootstrapInfo.
TEST_F(BootstrapTest, TestOrphanedReplicate) {
  BuildLog();

  // Append a REPLICATE with no commit
  int replicate_index = current_index_++;

  OpId opid = MakeOpId(1, replicate_index);

  AppendReplicateBatch(opid);

  // Bootstrap the tablet. It shouldn't replay anything.
  ConsensusBootstrapInfo boot_info;
  shared_ptr<TabletClass> tablet;
  ASSERT_OK(BootstrapTestTablet(&tablet, &boot_info));

  // Table should be empty because we didn't replay the REPLICATE.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(0, results.size());

  // The consensus bootstrap info should include the orphaned REPLICATE.
  ASSERT_EQ(1, boot_info.orphaned_replicates.size())
      << yb::ToString(boot_info.orphaned_replicates);
  ASSERT_STR_CONTAINS(boot_info.orphaned_replicates[0]->ShortDebugString(),
                      "this is a test mutate");

  // And it should also include the latest opids.
  EXPECT_EQ("term: 1 index: 1", boot_info.last_id.ShortDebugString());
}

// Bootstrap should fail if no ConsensusMetadata file exists.
TEST_F(BootstrapTest, TestMissingConsensusMetadata) {
  BuildLog();

  RaftGroupMetadataPtr meta;
  ASSERT_OK(LoadTestRaftGroupMetadata(&meta));

  shared_ptr<TabletClass> tablet;
  ConsensusBootstrapInfo boot_info;
  Status s = RunBootstrapOnTestTablet(meta, &tablet, &boot_info);

  ASSERT_TRUE(s.IsNotFound());
  ASSERT_STR_CONTAINS(s.ToString(), "Unable to load Consensus metadata");
}

// Tests that when we have two consecutive replicates and the commit index specified in the second
// is that of the first, only the first one is committed.
TEST_F(BootstrapTest, TestCommitFirstMessageBySpecifyingCommittedIndexInSecond) {
  BuildLog();

  // This appends a write with op 1.1
  const OpId insert_opid = MakeOpId(1, 1);
  AppendReplicateBatch(insert_opid, MakeOpId(0, 0),
                       {TupleForAppend(10, 1, "this is a test insert")}, true /* sync */);

  // This appends a write with op 1.2 and commits the previous one.
  const OpId mutate_opid = MakeOpId(1, 2);
  AppendReplicateBatch(mutate_opid, insert_opid,
                       {TupleForAppend(10, 2, "this is a test mutate")}, true /* sync */);
  ConsensusBootstrapInfo boot_info;
  shared_ptr<TabletClass> tablet;
  ASSERT_OK(BootstrapTestTablet(&tablet, &boot_info));
  ASSERT_EQ(boot_info.orphaned_replicates.size(), 1);
  ASSERT_OPID_EQ(boot_info.last_committed_id, insert_opid);

  // Confirm that one operation was applied.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());
}

TEST_F(BootstrapTest, TestOperationOverwriting) {
  BuildLog();

  const OpId opid = MakeOpId(1, 1);

  // Append a replicate in term 1 with only one row.
  AppendReplicateBatch(opid, MakeOpId(0, 0), {TupleForAppend(1, 0, "this is a test insert")});

  // Now append replicates for 4.2 and 4.3
  AppendReplicateBatch(MakeOpId(4, 2));
  AppendReplicateBatch(MakeOpId(4, 3));

  ASSERT_OK(RollLog());
  // And overwrite with 3.2
  AppendReplicateBatch(MakeOpId(3, 2), MakeOpId(1, 1), {}, true /* sync */);

  // When bootstrapping we should apply ops 1.1 and get 3.2 as pending.
  ConsensusBootstrapInfo boot_info;
  shared_ptr<TabletClass> tablet;
  ASSERT_OK(BootstrapTestTablet(&tablet, &boot_info));

  ASSERT_EQ(boot_info.orphaned_replicates.size(), 1);
  ASSERT_OPID_EQ(boot_info.orphaned_replicates[0]->id(), MakeOpId(3, 2));

  // Confirm that the legitimate data is there.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());

  ASSERT_EQ("{ int32_value: 1 int32_value: 0 string_value: \"this is a test insert\" }",
            results[0]);
}

// Test that we do not crash when a consensus-only operation has a hybrid_time that is higher than a
// hybrid_time assigned to a write operation that follows it in the log.
// TODO: this must not happen in YB. Ensure this is not happening and update the test.
TEST_F(BootstrapTest, TestConsensusOnlyOperationOutOfOrderHybridTime) {
  BuildLog();

  // Append NO_OP.
  auto noop_replicate = std::make_shared<ReplicateMsg>();
  noop_replicate->set_op_type(consensus::NO_OP);
  *noop_replicate->mutable_id() = MakeOpId(1, 1);
  noop_replicate->set_hybrid_time(2);

  // All YB REPLICATEs require this:
  *noop_replicate->mutable_committed_op_id() = MakeOpId(0, 0);

  AppendReplicateBatch(noop_replicate, true);

  // Append WRITE_OP with higher OpId and lower hybrid_time, and commit both messages.
  const auto second_opid = MakeOpId(1, 2);
  AppendReplicateBatch(second_opid, second_opid, {TupleForAppend(1, 1, "foo")});

  ConsensusBootstrapInfo boot_info;
  shared_ptr<TabletClass> tablet;
  ASSERT_OK(BootstrapTestTablet(&tablet, &boot_info));
  ASSERT_EQ(boot_info.orphaned_replicates.size(), 0);
  ASSERT_OPID_EQ(boot_info.last_committed_id, second_opid);

  // Confirm that the insert op was applied.
  vector<string> results;
  IterateTabletRows(tablet.get(), &results);
  ASSERT_EQ(1, results.size());
}

// Test that we don't overflow opids. Regression test for KUDU-1933.
TEST_F(BootstrapTest, TestBootstrapHighOpIdIndex) {
  // Start appending with a log index 3 under the int32 max value.
  // Append 6 log entries, which will roll us right through the int32 max.
  const int64_t first_log_index = std::numeric_limits<int32_t>::max() - 3;
  const int kNumEntries = 6;
  BuildLog();
  current_index_ = first_log_index;
  for (int i = 0; i < kNumEntries; i++) {
    AppendReplicateBatchToLog(1);
  }

  // Kick off tablet bootstrap and ensure everything worked.
  shared_ptr<TabletClass> tablet;
  ConsensusBootstrapInfo boot_info;
  ASSERT_OK(BootstrapTestTablet(&tablet, &boot_info));
  OpId last_opid;
  last_opid.set_term(1);
  last_opid.set_index(current_index_ - 1);
  ASSERT_OPID_EQ(last_opid, boot_info.last_id);
  ASSERT_OPID_EQ(last_opid, boot_info.last_committed_id);
}

} // namespace tablet
} // namespace yb
