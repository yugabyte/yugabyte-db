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

#include <gtest/gtest.h>

#include "yb/common/schema.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/consensus/consensus-test-util.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log.messages.h"
#include "yb/consensus/log_index.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/peer_manager.h"
#include "yb/consensus/quorum_util.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/replica_state.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/messenger.h"

#include "yb/server/logical_clock.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/drive_io_stats.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/std_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/threadpool.h"

DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_bool(enable_leader_failure_detection);
DECLARE_bool(raft_monotonic_last_received_current_leader);
DECLARE_bool(enable_wal_sync_on_consensus_update);
DECLARE_int32(interval_durable_wal_write_ms);
DECLARE_int32(bytes_durable_wal_write_mb);
DECLARE_bool(never_fsync);

METRIC_DECLARE_entity(table);
METRIC_DECLARE_entity(tablet);

#define REPLICATE_SEQUENCE_OF_MESSAGES(...) \
  ASSERT_NO_FATALS(ReplicateSequenceOfMessages(__VA_ARGS__))

using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using std::string;

namespace yb::consensus {

using log::Log;
using log::LogOptions;
using log::LogReader;
using strings::Substitute;
using strings::SubstituteAndAppend;

const char* kTestTable = "TestTable";
const char* kTestTablet = "TestTablet";

void DoNothing(std::shared_ptr<consensus::StateChangeContext> context) {
}

// Test suite for tests that focus on multiple peer interaction, but
// without integrating with other components, such as transactions.
class RaftConsensusQuorumTest : public YBTest {
 public:
  RaftConsensusQuorumTest()
    : clock_(server::LogicalClock::CreateStartingAt(HybridTime(0))),
      table_metric_entity_(
          METRIC_ENTITY_table.Instantiate(&metric_registry_, "raft-test-table")),
      tablet_metric_entity_(
          METRIC_ENTITY_tablet.Instantiate(&metric_registry_, "raft-test-tablet")),
      schema_(GetSimpleTestSchema()),
      raft_notifications_pool_(std::make_unique<rpc::ThreadPool>(rpc::ThreadPoolOptions {
        .name = "raft_notifications",
        .max_workers = rpc::ThreadPoolOptions::kUnlimitedWorkers
      })) {
    options_.tablet_id = kTestTablet;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_leader_failure_detection) = false;
  }

  // Builds an initial configuration of 'num' elements.
  // All of the peers start as followers.
  void BuildInitialRaftConfigPB(int num) {
    config_ = BuildRaftConfigPBForTests(num);
    config_.set_committed_op_index(kInvalidOpIdIndex);
    peers_.reset(new TestPeerMapManager(config_));
  }

  Status BuildFsManagersAndLogs() {
    // Build the fsmanagers and logs
    for (int i = 0; i < config_.peers_size(); i++) {
      shared_ptr<MemTracker> parent_mem_tracker =
          MemTracker::CreateTracker(Substitute("peer-$0", i));
      parent_mem_trackers_.push_back(parent_mem_tracker);
      string test_path = GetTestPath(Substitute("peer-$0-root", i));
      FsManagerOpts opts;
      opts.parent_mem_tracker = parent_mem_tracker;
      opts.wal_paths = { test_path };
      opts.data_paths = { test_path };
      opts.server_type = "tserver_test";
      std::unique_ptr<FsManager> fs_manager(new FsManager(env_.get(), opts));
      RETURN_NOT_OK(fs_manager->CreateInitialFileSystemLayout());
      RETURN_NOT_OK(fs_manager->CheckAndOpenFileSystemRoots());

      scoped_refptr<Log> log;
      RETURN_NOT_OK(Log::Open(LogOptions(),
                              kTestTablet,
                              fs_manager->GetFirstTabletWalDirOrDie(kTestTable, kTestTablet),
                              fs_manager->uuid(),
                              schema_,
                              /*schema_version=*/0,
                              /*table_metric_entity=*/nullptr,
                              /*tablet_metric_entity=*/nullptr,
                              /*read_wal_mem_tracker=*/nullptr,
                              log_thread_pool_.get(),
                              log_thread_pool_.get(),
                              log_thread_pool_.get(),
                              &log));
      logs_.push_back(log.get());
      fs_managers_.push_back(fs_manager.release());
    }
    return Status::OK();
  }

  void BuildPeers() {
    vector<LocalTestPeerProxyFactory*> proxy_factories;
    for (int i = 0; i < config_.peers_size(); i++) {
      auto proxy_factory = std::make_unique<LocalTestPeerProxyFactory>(peers_.get());
      proxy_factories.push_back(proxy_factory.get());

      auto operation_factory = new TestOperationFactory();

      string peer_uuid = Substitute("peer-$0", i);

      fs_managers_[i]->SetTabletPathByDataPath(kTestTablet, fs_managers_[i]->GetDataRootDirs()[0]);
      std::unique_ptr<ConsensusMetadata> cmeta = ASSERT_RESULT(ConsensusMetadata::Create(
          fs_managers_[i], kTestTablet, peer_uuid, config_, kMinimumTerm));

      RaftPeerPB local_peer_pb;
      ASSERT_OK(GetRaftConfigMember(config_, peer_uuid, &local_peer_pb));
      auto queue = std::make_unique<PeerMessageQueue>(
          tablet_metric_entity_,
          logs_[i],
          MemTracker::FindOrCreateTracker(peer_uuid),
          MemTracker::FindOrCreateTracker(peer_uuid),
          local_peer_pb,
          kTestTablet,
          clock_,
          nullptr /* consensus_context */,
          std::make_unique<rpc::Strand>(raft_notifications_pool_.get()));

      unique_ptr<ThreadPoolToken> pool_token(
          raft_pool_->NewToken(ThreadPool::ExecutionMode::CONCURRENT));

      auto peer_manager = std::make_unique<PeerManager>(
          options_.tablet_id,
          config_.peers(i).permanent_uuid(),
          proxy_factory.get(),
          queue.get(),
          pool_token.get(),
          nullptr);

      consensus::RetryableRequests retryable_requests(
          parent_mem_trackers_[i],
          "");
      retryable_requests.SetServerClock(clock_);
      retryable_requests.SetRequestTimeout(FLAGS_retryable_request_timeout_secs);

      shared_ptr<RaftConsensus> peer(new RaftConsensus(
          options_,
          std::move(cmeta),
          std::move(proxy_factory),
          std::move(queue),
          std::move(peer_manager),
          std::move(pool_token),
          table_metric_entity_,
          tablet_metric_entity_,
          config_.peers(i).permanent_uuid(),
          clock_,
          operation_factory,
          logs_[i],
          parent_mem_trackers_[i],
          Bind(&DoNothing),
          DEFAULT_TABLE_TYPE,
          &retryable_requests));

      operation_factory->SetConsensus(peer.get());
      operation_factories_.emplace_back(operation_factory);
      peers_->AddPeer(config_.peers(i).permanent_uuid(), peer);
    }
  }

  Status StartPeers() {
    ConsensusBootstrapInfo boot_info;

    TestPeerMap all_peers = peers_->GetPeerMapCopy();
    for (const TestPeerMap::value_type& entry : all_peers) {
      RETURN_NOT_OK(entry.second->Start(boot_info));
    }
    return Status::OK();
  }

  Status BuildConfig(int num) {
    RETURN_NOT_OK(ThreadPoolBuilder("raft").Build(&raft_pool_));
    RETURN_NOT_OK(ThreadPoolBuilder("log").Build(&log_thread_pool_));
    BuildInitialRaftConfigPB(num);
    RETURN_NOT_OK(BuildFsManagersAndLogs());
    BuildPeers();
    return Status::OK();
  }

  Status BuildAndStartConfig(int num) {
    RETURN_NOT_OK(BuildConfig(num));
    RETURN_NOT_OK(StartPeers());

    // Automatically elect the last node in the list.
    const int kLeaderIdx = num - 1;
    shared_ptr<RaftConsensus> leader;
    RETURN_NOT_OK(peers_->GetPeerByIdx(kLeaderIdx, &leader));
    RETURN_NOT_OK(leader->EmulateElection());
    RETURN_NOT_OK(leader->WaitUntilLeaderForTests(MonoDelta::FromSeconds(10)));
    return Status::OK();
  }

  LocalTestPeerProxy* GetLeaderProxyToPeer(int peer_idx, int leader_idx) {
    shared_ptr<RaftConsensus> follower;
    CHECK_OK(peers_->GetPeerByIdx(peer_idx, &follower));
    shared_ptr<RaftConsensus> leader;
    CHECK_OK(peers_->GetPeerByIdx(leader_idx, &leader));
    for (LocalTestPeerProxy* proxy : down_cast<LocalTestPeerProxyFactory*>(
        leader->peer_proxy_factory_.get())->GetProxies()) {
      if (proxy->GetTarget() == follower->peer_uuid()) {
        return proxy;
      }
    }
    CHECK(false) << "Proxy not found";
    return nullptr;
  }

  Status AppendDummyMessage(int peer_idx,
                            scoped_refptr<ConsensusRound>* round) {
    auto msg = rpc::MakeSharedMessage<LWReplicateMsg>();
    msg->set_op_type(NO_OP);
    msg->mutable_noop_request();
    msg->set_hybrid_time(clock_->Now().ToUint64());

    shared_ptr<RaftConsensus> peer;
    CHECK_OK(peers_->GetPeerByIdx(peer_idx, &peer));

    // Use a latch in place of a Transaction callback.
    auto sync = std::make_unique<Synchronizer>();
    *round = make_scoped_refptr<ConsensusRound>(peer.get(), std::move(msg));
    (**round).SetCallback(MakeNonTrackedRoundCallback(
        round->get(),
        [sync = sync.get()](const Status& status) {
      sync->StatusCB(status);
    }));
    (**round).BindToTerm(peer->LeaderTerm());
    InsertOrDie(&syncs_, round->get(), sync.release());
    RETURN_NOT_OK_PREPEND(peer->TEST_Replicate(round->get()),
                          Substitute("Unable to replicate to peer $0", peer_idx));
    return Status::OK();
  }

  Status WaitForReplicate(ConsensusRound* round) {
    return FindOrDie(syncs_, round)->Wait();
  }

  Status TimedWaitForReplicate(ConsensusRound* round, const MonoDelta& delta) {
    return FindOrDie(syncs_, round)->WaitFor(delta);
  }

  void WaitForReplicateIfNotAlreadyPresent(const OpIdPB& to_wait_for, int peer_idx) {
    shared_ptr<RaftConsensus> peer;
    ASSERT_OK(peers_->GetPeerByIdx(peer_idx, &peer));
    ReplicaState* state = peer->GetReplicaStateForTests();
    while (true) {
      {
        auto lock = state->LockForRead();
        if (state->GetLastReceivedOpIdUnlocked().index >= to_wait_for.index()) {
          return;
        }
      }
      SleepFor(MonoDelta::FromMilliseconds(1));
    }
  }

  // Waits for an operation to be (database) committed in the replica at index
  // 'peer_idx'. If the operation was already committed this returns immediately.
  void WaitForCommitIfNotAlreadyPresent(const OpIdPB& to_wait_for,
                                        int peer_idx,
                                        int leader_idx) {
    MonoDelta timeout(MonoDelta::FromSeconds(10));
    MonoTime start(MonoTime::Now());

    shared_ptr<RaftConsensus> peer;
    ASSERT_OK(peers_->GetPeerByIdx(peer_idx, &peer));
    ReplicaState* state = peer->GetReplicaStateForTests();

    int backoff_exp = 0;
    const int kMaxBackoffExp = 8;
    OpIdPB committed_op_id;
    while (true) {
      {
        auto lock = state->LockForRead();
        state->GetCommittedOpIdUnlocked().ToPB(&committed_op_id);
        if (OpIdCompare(committed_op_id, to_wait_for) >= 0) {
          return;
        }
      }
      MonoDelta elapsed = MonoTime::Now().GetDeltaSince(start);
      if (elapsed.MoreThan(timeout)) {
        break;
      }
      SleepFor(MonoDelta::FromMilliseconds(1 << backoff_exp));
      backoff_exp = std::min(backoff_exp + 1, kMaxBackoffExp);
    }

    LOG(WARNING)
        << "Max timeout reached (" << timeout.ToString() << ") while waiting for commit of "
        << "op " << to_wait_for << " on replica. Last committed op on replica: "
        << committed_op_id << ". Dumping state and quitting.";
    shared_ptr<RaftConsensus> leader;
    ASSERT_OK(peers_->GetPeerByIdx(leader_idx, &leader));

    // Gather the replica and leader operations for printing
    log::LogEntries replica_ops = GatherLogEntries(peer_idx, logs_[peer_idx]);
    log::LogEntries leader_ops = GatherLogEntries(leader_idx, logs_[leader_idx]);
    SCOPED_TRACE(PrintOnError(replica_ops, Substitute("local peer ($0)", peer->peer_uuid())));
    SCOPED_TRACE(PrintOnError(leader_ops, Substitute("leader (peer-$0)", leader_idx)));
    FAIL() << "Replica did not commit.";
  }

  // Used in ReplicateSequenceOfMessages() to specify whether
  // we should wait for all replicas to have replicated the
  // sequence or just a majority.
  enum ReplicateWaitMode {
    WAIT_FOR_ALL_REPLICAS,
    WAIT_FOR_MAJORITY
  };

  // Used in ReplicateSequenceOfMessages() to specify whether
  // we should also commit the messages in the sequence
  enum CommitMode {
    DONT_COMMIT,
    COMMIT_ONE_BY_ONE
  };

  // Replicates a sequence of messages to the peer passed as leader.
  // Optionally waits for the messages to be replicated to followers.
  // 'last_op_id' is set to the id of the last replicated operation.
  // The operations are only committed if 'commit_one_by_one' is true.
  void ReplicateSequenceOfMessages(int seq_size,
                                   int leader_idx,
                                   ReplicateWaitMode wait_mode,
                                   CommitMode commit_mode,
                                   OpIdPB* last_op_id,
                                   vector<scoped_refptr<ConsensusRound> >* rounds) {
    for (int i = 0; i < seq_size; i++) {
      scoped_refptr<ConsensusRound> round;
      ASSERT_OK(AppendDummyMessage(leader_idx, &round));
      ASSERT_OK(WaitForReplicate(round.get()));
      round->id().ToPB(last_op_id);
      rounds->push_back(round);
    }

    if (wait_mode == WAIT_FOR_ALL_REPLICAS) {
      shared_ptr<RaftConsensus> leader;
      ASSERT_OK(peers_->GetPeerByIdx(leader_idx, &leader));

      TestPeerMap all_peers = peers_->GetPeerMapCopy();
      int i = 0;
      for (const TestPeerMap::value_type& entry : all_peers) {
        if (entry.second->peer_uuid() != leader->peer_uuid()) {
          WaitForReplicateIfNotAlreadyPresent(*last_op_id, i);
        }
        i++;
      }
    }
  }

  log::LogEntries GatherLogEntries(int idx, const scoped_refptr<Log>& log) {
    EXPECT_OK(log->WaitUntilAllFlushed());
    EXPECT_OK(log->Close());
    std::unique_ptr<LogReader> log_reader;
    EXPECT_OK(log::LogReader::Open(fs_managers_[idx]->env(),
                                   scoped_refptr<log::LogIndex>(),
                                   "Log reader: ",
                                   fs_managers_[idx]->GetFirstTabletWalDirOrDie(kTestTable,
                                                                                kTestTablet),
                                   table_metric_entity_.get(),
                                   tablet_metric_entity_.get(),
                                   /*read_wal_mem_tracker=*/nullptr,
                                   &log_reader));
    log::LogEntries ret;
    log::SegmentSequence segments;
    EXPECT_OK(log_reader->GetSegmentsSnapshot(&segments));

    for (const log::SegmentSequence::value_type& entry : segments) {
      auto result = entry->ReadEntries();
      EXPECT_OK(result.status);
      for (auto& e : result.entries) {
        ret.push_back(std::move(e));
      }
    }

    return ret;
  }

  // Verifies that the replica's log match the leader's. This deletes the
  // peers (so we're sure that no further writes occur) and closes the logs
  // so it must be the very last thing to run, in a test.
  void VerifyLogs(int leader_idx, int first_replica_idx, int last_replica_idx) {
    // Wait for in-flight transactions to be done. We're destroying the
    // peers next and leader transactions won't be able to commit anymore.
    for (const auto& factory : operation_factories_) {
      factory->WaitDone();
    }

    // Shut down all the peers.
    TestPeerMap all_peers = peers_->GetPeerMapCopy();
    for (const TestPeerMap::value_type& entry : all_peers) {
      entry.second->Shutdown();
    }

    log::LogEntries leader_entries = GatherLogEntries(leader_idx, logs_[leader_idx]);
    shared_ptr<RaftConsensus> leader;
    ASSERT_OK(peers_->GetPeerByIdx(leader_idx, &leader));

    for (int replica_idx = first_replica_idx; replica_idx < last_replica_idx; replica_idx++) {
      log::LogEntries replica_entries = GatherLogEntries(replica_idx, logs_[replica_idx]);

      shared_ptr<RaftConsensus> replica;
      ASSERT_OK(peers_->GetPeerByIdx(replica_idx, &replica));
      VerifyReplica(leader_entries,
                    replica_entries,
                    leader->peer_uuid(),
                    replica->peer_uuid());
    }
  }

  std::vector<OpId> ExtractReplicateIds(const log::LogEntries& entries) {
    std::vector<OpId> result;
    result.reserve(entries.size() / 2);
    for (const auto& entry : entries) {
      if (entry->has_replicate()) {
        result.push_back(OpId::FromPB(entry->replicate().id()));
      }
    }
    return result;
  }

  void VerifyReplicateOrderMatches(const log::LogEntries& leader_entries,
                                   const log::LogEntries& replica_entries) {
    auto leader_ids = ExtractReplicateIds(leader_entries);
    auto replica_ids = ExtractReplicateIds(replica_entries);
    ASSERT_EQ(leader_ids.size(), replica_ids.size());
    for (size_t i = 0; i < leader_ids.size(); i++) {
      ASSERT_EQ(leader_ids[i], replica_ids[i]);
    }
  }

  void VerifyNoCommitsBeforeReplicates(const log::LogEntries& entries) {
    std::unordered_set<OpId, OpIdHash> replication_ops;

    for (const auto& entry : entries) {
      if (entry->has_replicate()) {
        ASSERT_TRUE(InsertIfNotPresent(&replication_ops, OpId::FromPB(entry->replicate().id())))
          << "REPLICATE op id showed up twice: " << entry->ShortDebugString();
      }
    }
  }

  void VerifyReplica(const log::LogEntries& leader_entries,
                     const log::LogEntries& replica_entries,
                     const string& leader_name,
                     const string& replica_name) {
    SCOPED_TRACE(PrintOnError(leader_entries, Substitute("Leader: $0", leader_name)));
    SCOPED_TRACE(PrintOnError(replica_entries, Substitute("Replica: $0", replica_name)));

    // Check that the REPLICATE messages come in the same order on both nodes.
    VerifyReplicateOrderMatches(leader_entries, replica_entries);

    // Check that no COMMIT precedes its related REPLICATE on both the replica
    // and leader.
    VerifyNoCommitsBeforeReplicates(replica_entries);
    VerifyNoCommitsBeforeReplicates(leader_entries);
  }

  string PrintOnError(const log::LogEntries& replica_entries,
                      const string& replica_id) {
    string ret = "";
    SubstituteAndAppend(&ret, "$1 log entries for replica $0:\n",
                        replica_id, replica_entries.size());
    for (const auto& replica_entry : replica_entries) {
      StrAppend(&ret, "Replica log entry: ", replica_entry->ShortDebugString(), "\n");
    }
    return ret;
  }

  // Read the ConsensusMetadata for the given peer from disk.
  std::unique_ptr<ConsensusMetadata> ReadConsensusMetadataFromDisk(int peer_index) {
    string peer_uuid = Substitute("peer-$0", peer_index);
    std::unique_ptr<ConsensusMetadata> cmeta;
    CHECK_OK(ConsensusMetadata::Load(fs_managers_[peer_index], kTestTablet, peer_uuid, &cmeta));
    return cmeta;
  }

  // Assert that the durable term == term and that the peer that got the vote == voted_for.
  void AssertDurableTermAndVote(int peer_index, int64_t term, const std::string& voted_for) {
    auto cmeta = ReadConsensusMetadataFromDisk(peer_index);
    ASSERT_EQ(term, cmeta->current_term());
    ASSERT_EQ(voted_for, cmeta->voted_for());
  }

  // Assert that the durable term == term and that the peer has not yet voted.
  void AssertDurableTermWithoutVote(int peer_index, int64_t term) {
    auto cmeta = ReadConsensusMetadataFromDisk(peer_index);
    ASSERT_EQ(term, cmeta->current_term());
    ASSERT_FALSE(cmeta->has_voted_for());
  }

  void TearDown() override {
    // Use the same order of shutdown operations as is done by TabletPeer in production. In this
    // test we don't use TabletPeer so we have to emulate this order.
    // 1. TabletPeer::StartShutdown shuts down the consensus.
    // 2. TabletPeer::CompleteShutdown closes the log.
    // 3. TabletPeer::CompleteShutdown destroys the consensus object.
    // If we don't do this, it is possible that a log append operation callback task might try to
    // call methods on PeerMessageQueue concurrently with PeerMessageQueue being destroyed.
    // See https://github.com/yugabyte/yugabyte-db/issues/21564 for more details.
    for (auto& [_, consensus_ptr] : peers_->GetPeerMapCopy()) {
      consensus_ptr->Shutdown();
    }
    for (auto& log : logs_) {
      ASSERT_OK(log->Close());
    }
    YBTest::TearDown();
  }

  ~RaftConsensusQuorumTest() override {
    peers_->Clear();
    operation_factories_.clear();
    // We need to clear the logs before deleting the fs_managers_ or we'll
    // get a SIGSEGV when closing the logs.
    logs_.clear();
    STLDeleteElements(&fs_managers_);
    STLDeleteValues(&syncs_);
  }

 protected:
  ConsensusOptions options_;
  RaftConfigPB config_;
  OpIdPB initial_id_;
  vector<shared_ptr<MemTracker> > parent_mem_trackers_;
  vector<FsManager*> fs_managers_;
  vector<scoped_refptr<Log> > logs_;
  unique_ptr<ThreadPool> raft_pool_;
  unique_ptr<ThreadPool> log_thread_pool_;
  std::unique_ptr<TestPeerMapManager> peers_;
  std::vector<std::unique_ptr<TestOperationFactory>> operation_factories_;
  scoped_refptr<server::Clock> clock_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> table_metric_entity_;
  scoped_refptr<MetricEntity> tablet_metric_entity_;
  const Schema schema_;
  std::unordered_map<ConsensusRound*, Synchronizer*> syncs_;
  std::unique_ptr<rpc::ThreadPool> raft_notifications_pool_;
};

TEST_F(RaftConsensusQuorumTest, TestConsensusContinuesIfAMinorityFallsBehind) {
  // Constants with the indexes of peers with certain roles,
  // since peers don't change roles in this test.
  const int kFollower0Idx = 0;
  const int kFollower1Idx = 1;
  const int kLeaderIdx = 2;

  ASSERT_OK(BuildAndStartConfig(3));

  OpIdPB last_replicate;
  vector<scoped_refptr<ConsensusRound> > rounds;
  {
    // lock one of the replicas down by obtaining the state lock
    // and never letting it go.
    shared_ptr<RaftConsensus> follower0;
    ASSERT_OK(peers_->GetPeerByIdx(kFollower0Idx, &follower0));

    ReplicaState* follower0_rs = follower0->GetReplicaStateForTests();
    auto lock = follower0_rs->LockForRead();

    // If the locked replica would stop consensus we would hang here
    // as we wait for operations to be replicated to a majority.
    ASSERT_NO_FATALS(ReplicateSequenceOfMessages(
                              10,
                              kLeaderIdx,
                              WAIT_FOR_MAJORITY,
                              COMMIT_ONE_BY_ONE,
                              &last_replicate,
                              &rounds));

    // Follower 1 should be fine (Were we to wait for follower0's replicate
    // this would hang here). We know he must have replicated but make sure
    // by calling Wait().
    WaitForReplicateIfNotAlreadyPresent(last_replicate, kFollower1Idx);
    WaitForCommitIfNotAlreadyPresent(last_replicate, kFollower1Idx, kLeaderIdx);
  }

  // After we let the lock go the remaining follower should get up-to-date
  WaitForReplicateIfNotAlreadyPresent(last_replicate, kFollower0Idx);
  WaitForCommitIfNotAlreadyPresent(last_replicate, kFollower0Idx, kLeaderIdx);
  VerifyLogs(2, 0, 1);
}

TEST_F(RaftConsensusQuorumTest, TestConsensusStopsIfAMajorityFallsBehind) {
  // Constants with the indexes of peers with certain roles,
  // since peers don't change roles in this test.
  const int kFollower0Idx = 0;
  const int kFollower1Idx = 1;
  const int kLeaderIdx = 2;

  ASSERT_OK(BuildAndStartConfig(3));

  OpIdPB last_op_id;

  scoped_refptr<ConsensusRound> round;
  {
    // lock two of the replicas down by obtaining the state locks
    // and never letting them go.
    shared_ptr<RaftConsensus> follower0;
    ASSERT_OK(peers_->GetPeerByIdx(kFollower0Idx, &follower0));
    ReplicaState* follower0_rs = follower0->GetReplicaStateForTests();
    auto lock0 = follower0_rs->LockForRead();

    shared_ptr<RaftConsensus> follower1;
    ASSERT_OK(peers_->GetPeerByIdx(kFollower1Idx, &follower1));
    ReplicaState* follower1_rs = follower1->GetReplicaStateForTests();
    auto lock1 = follower1_rs->LockForRead();

    // Append a single message to the queue
    ASSERT_OK(AppendDummyMessage(kLeaderIdx, &round));
    round->id().ToPB(&last_op_id);
    // This should timeout.
    Status status = TimedWaitForReplicate(round.get(), MonoDelta::FromMilliseconds(500));
    ASSERT_TRUE(status.IsTimedOut());
  }

  // After we release the locks the operation should replicate to all replicas
  // and we commit.
  ASSERT_OK(WaitForReplicate(round.get()));

  // Assert that everything was ok
  WaitForReplicateIfNotAlreadyPresent(last_op_id, kFollower0Idx);
  WaitForReplicateIfNotAlreadyPresent(last_op_id, kFollower1Idx);
  WaitForCommitIfNotAlreadyPresent(last_op_id, kFollower0Idx, kLeaderIdx);
  WaitForCommitIfNotAlreadyPresent(last_op_id, kFollower1Idx, kLeaderIdx);
  VerifyLogs(2, 0, 1);
}

// If some communication error happens the leader will resend the request to the
// peers. This tests that the peers handle repeated requests.
TEST_F(RaftConsensusQuorumTest, TestReplicasHandleCommunicationErrors) {
  // Constants with the indexes of peers with certain roles,
  // since peers don't change roles in this test.
  const int kFollower0Idx = 0;
  const int kFollower1Idx = 1;
  const int kLeaderIdx = 2;

  ASSERT_OK(BuildAndStartConfig(3));

  OpIdPB last_op_id;

  // Append a dummy message, with faults injected on the first attempt
  // to send the message.
  scoped_refptr<ConsensusRound> round;
  GetLeaderProxyToPeer(kFollower0Idx, kLeaderIdx)->InjectCommFaultLeaderSide();
  GetLeaderProxyToPeer(kFollower1Idx, kLeaderIdx)->InjectCommFaultLeaderSide();
  ASSERT_OK(AppendDummyMessage(kLeaderIdx, &round));

  // We should successfully replicate it due to retries.
  ASSERT_OK(WaitForReplicate(round.get()));

  GetLeaderProxyToPeer(kFollower0Idx, kLeaderIdx)->InjectCommFaultLeaderSide();
  GetLeaderProxyToPeer(kFollower1Idx, kLeaderIdx)->InjectCommFaultLeaderSide();

  // The commit should eventually reach both followers as well.
  round->id().ToPB(&last_op_id);
  WaitForCommitIfNotAlreadyPresent(last_op_id, kFollower0Idx, kLeaderIdx);
  WaitForCommitIfNotAlreadyPresent(last_op_id, kFollower1Idx, kLeaderIdx);

  // Append a sequence of messages, and keep injecting errors into the
  // replica proxies.
  vector<scoped_refptr<ConsensusRound> > rounds;
  for (int i = 0; i < 100; i++) {
    scoped_refptr<ConsensusRound> round;
    ASSERT_OK(AppendDummyMessage(kLeaderIdx, &round));
    ConsensusRound* round_ptr = round.get();
    round->id().ToPB(&last_op_id);
    rounds.push_back(round);

    // inject comm faults
    if (i % 2 == 0) {
      GetLeaderProxyToPeer(kFollower0Idx, kLeaderIdx)->InjectCommFaultLeaderSide();
    } else {
      GetLeaderProxyToPeer(kFollower1Idx, kLeaderIdx)->InjectCommFaultLeaderSide();
    }

    ASSERT_OK(WaitForReplicate(round_ptr));
  }

  // Assert last operation was correctly replicated and committed.
  WaitForReplicateIfNotAlreadyPresent(last_op_id, kFollower0Idx);
  WaitForReplicateIfNotAlreadyPresent(last_op_id, kFollower1Idx);

  // See comment at the end of TestFollowersReplicateAndCommitMessage
  // for an explanation on this waiting sequence.
  WaitForCommitIfNotAlreadyPresent(last_op_id, kFollower0Idx, kLeaderIdx);
  WaitForCommitIfNotAlreadyPresent(last_op_id, kFollower1Idx, kLeaderIdx);
  VerifyLogs(2, 0, 1);
}

// In this test we test the ability of the leader to send heartbeats
// to replicas by simply pushing nothing after the configuration round
// and still expecting for the replicas Update() hooks to be called.
TEST_F(RaftConsensusQuorumTest, TestLeaderHeartbeats) {
  // Constants with the indexes of peers with certain roles,
  // since peers don't change roles in this test.
  const int kFollower0Idx = 0;
  const int kFollower1Idx = 1;
  const int kLeaderIdx = 2;

  ASSERT_OK(BuildConfig(3));

  shared_ptr<RaftConsensus> follower0;
  ASSERT_OK(peers_->GetPeerByIdx(kFollower0Idx, &follower0));
  shared_ptr<RaftConsensus> follower1;
  ASSERT_OK(peers_->GetPeerByIdx(kFollower1Idx, &follower1));

  shared_ptr<CounterHooks> counter_hook_rpl0(
      new CounterHooks(follower0->GetFaultHooks()));
  shared_ptr<CounterHooks> counter_hook_rpl1(
      new CounterHooks(follower1->GetFaultHooks()));

  // Replace the default fault hooks on the replicas with counter hooks
  // before we start the configuration.
  follower0->SetFaultHooks(counter_hook_rpl0);
  follower1->SetFaultHooks(counter_hook_rpl1);

  ASSERT_OK(StartPeers());

  shared_ptr<RaftConsensus> leader;
  ASSERT_OK(peers_->GetPeerByIdx(kLeaderIdx, &leader));
  ASSERT_OK(leader->EmulateElection());

  // Wait for the config round to get committed and count the number
  // of update calls, calls after that will be heartbeats.
  OpIdPB config_round;
  config_round.set_term(1);
  config_round.set_index(1);
  WaitForCommitIfNotAlreadyPresent(config_round, kFollower0Idx, kLeaderIdx);
  WaitForCommitIfNotAlreadyPresent(config_round, kFollower1Idx, kLeaderIdx);

  int repl0_init_count = counter_hook_rpl0->num_pre_update_calls();
  int repl1_init_count = counter_hook_rpl1->num_pre_update_calls();

  // Now wait for about 4 times the heartbeat period the counters
  // should have increased between 3 to 8 times.
  //
  // Why the variance? Heartbeat timing is jittered such that the period
  // between heartbeats can be anywhere from half the interval to the full interval.
  SleepFor(MonoDelta::FromMilliseconds(FLAGS_raft_heartbeat_interval_ms * 4));

  int repl0_final_count = counter_hook_rpl0->num_pre_update_calls();
  int repl1_final_count = counter_hook_rpl1->num_pre_update_calls();

  ASSERT_GE(repl0_final_count - repl0_init_count, 3);
  ASSERT_LE(repl0_final_count - repl0_init_count, 8);
  ASSERT_GE(repl1_final_count - repl1_init_count, 3);
  ASSERT_LE(repl1_final_count - repl1_init_count, 8);

  VerifyLogs(2, 0, 1);
}

// After creating the initial configuration, this test writes a small sequence
// of messages to the initial leader. It then shuts down the current
// leader, makes another peer become leader and writes a sequence of
// messages to it. The new leader and the follower should agree on the
// sequence of messages.
TEST_F(RaftConsensusQuorumTest, TestLeaderElectionWithQuiescedQuorum) {
  const int kInitialNumPeers = 5;
  ASSERT_OK(BuildAndStartConfig(kInitialNumPeers));

  OpIdPB last_op_id;
  vector<scoped_refptr<ConsensusRound> > rounds;

  // Loop twice, successively shutting down the previous leader.
  for (int current_config_size = kInitialNumPeers;
       current_config_size >= kInitialNumPeers - 1;
       current_config_size--) {
    REPLICATE_SEQUENCE_OF_MESSAGES(10,
                                   current_config_size - 1, // The index of the leader.
                                   WAIT_FOR_ALL_REPLICAS,
                                   COMMIT_ONE_BY_ONE,
                                   &last_op_id,
                                   &rounds);

    // Make sure the last operation is committed everywhere
    for (int i = 0; i < current_config_size - 1; i++) {
      WaitForCommitIfNotAlreadyPresent(last_op_id, i, current_config_size - 1);
    }

    // Now shutdown the current leader.
    LOG(INFO) << "Shutting down current leader with index " << (current_config_size - 1);
    shared_ptr<RaftConsensus> current_leader;
    ASSERT_OK(peers_->GetPeerByIdx(current_config_size - 1, &current_leader));
    current_leader->Shutdown();
    peers_->RemovePeer(current_leader->peer_uuid());

    // ... and make the peer before it become leader.
    shared_ptr<RaftConsensus> new_leader;
    ASSERT_OK(peers_->GetPeerByIdx(current_config_size - 2, &new_leader));

    // This will force an election in which we expect to make the last
    // non-shutdown peer in the list become leader.
    LOG(INFO) << "Running election for future leader with index " << (current_config_size - 1);
    ASSERT_OK(new_leader->StartElection(LeaderElectionData{
        .mode = consensus::ElectionMode::ELECT_EVEN_IF_LEADER_IS_ALIVE,
        .pending_commit = false,
        .must_be_committed_opid = OpId()}));
    ASSERT_OK(new_leader->WaitUntilLeaderForTests(MonoDelta::FromSeconds(15)));
    LOG(INFO) << "Election won";

    // ... replicating a set of messages to the new leader should now be possible.
    REPLICATE_SEQUENCE_OF_MESSAGES(10,
                                   current_config_size - 2, // The index of the new leader.
                                   WAIT_FOR_MAJORITY,
                                   COMMIT_ONE_BY_ONE,
                                   &last_op_id,
                                   &rounds);

    // Make sure the last operation is committed everywhere
    for (int i = 0; i < current_config_size - 2; i++) {
      WaitForCommitIfNotAlreadyPresent(last_op_id, i, current_config_size - 2);
    }
  }
  // We can only verify the logs of the peers that were not killed, due to the
  // old leaders being out-of-date now.
  VerifyLogs(2, 0, 1);
}

TEST_F(RaftConsensusQuorumTest, TestReplicasEnforceTheLogMatchingProperty) {
  ASSERT_OK(BuildAndStartConfig(3));

  OpIdPB last_op_id;
  vector<scoped_refptr<ConsensusRound> > rounds;
  REPLICATE_SEQUENCE_OF_MESSAGES(10,
                                 2, // The index of the initial leader.
                                 WAIT_FOR_ALL_REPLICAS,
                                 COMMIT_ONE_BY_ONE,
                                 &last_op_id,
                                 &rounds);

  // Make sure the last operation is committed everywhere
  WaitForCommitIfNotAlreadyPresent(last_op_id, 0, 2);
  WaitForCommitIfNotAlreadyPresent(last_op_id, 1, 2);

  // Now replicas should only accept operations with
  // 'last_id' as the preceding id.
  auto req_ptr = rpc::MakeSharedMessage<LWConsensusRequestPB>();
  auto& req = *req_ptr;
  LWConsensusResponsePB resp(&req.arena());

  shared_ptr<RaftConsensus> leader;
  ASSERT_OK(peers_->GetPeerByIdx(2, &leader));

  shared_ptr<RaftConsensus> follower;
  ASSERT_OK(peers_->GetPeerByIdx(0, &follower));

  req.ref_caller_uuid(leader->peer_uuid());
  req.set_caller_term(last_op_id.term());
  req.mutable_preceding_id()->CopyFrom(last_op_id);
  req.mutable_committed_op_id()->CopyFrom(last_op_id);

  auto* replicate = req.add_ops();
  replicate->set_hybrid_time(clock_->Now().ToUint64());
  auto* id = replicate->mutable_id();
  id->set_term(last_op_id.term());
  id->set_index(last_op_id.index() + 1);
  // Make a copy of the OpId to be TSAN friendly.
  LWConsensusRequestPB req_copy(&req.arena(), req);
  auto* id_copy = req_copy.mutable_ops()->front().mutable_id();
  replicate->set_op_type(NO_OP);

  // Appending this message to peer0 should work and update
  // its 'last_received' to 'id'.
  ASSERT_OK(follower->Update(req_ptr, &resp, CoarseBigDeadline()));
  ASSERT_EQ(OpId::FromPB(resp.status().last_received()), OpId::FromPB(*id));

  // Now skip one message in the same term. The replica should
  // complain with the right error message.
  req_copy.mutable_preceding_id()->set_index(id_copy->index() + 1);
  id_copy->set_index(id_copy->index() + 2);
  // Appending this message to peer0 should return a Status::OK
  // but should contain an error referring to the log matching property.
  ASSERT_OK(follower->Update(SharedField(req_ptr, &req_copy), &resp, CoarseBigDeadline()));
  ASSERT_TRUE(resp.has_status());
  ASSERT_TRUE(resp.status().has_error());
  ASSERT_EQ(resp.status().error().code(), ConsensusErrorPB::PRECEDING_ENTRY_DIDNT_MATCH);
  ASSERT_STR_CONTAINS(resp.status().error().status().message().ToBuffer(),
                      "Log matching property violated");
}

// The wiring, as opposed to the primitive. The log-test cases drive
// Log::MaybeSyncInBackground() directly; the guarantee an operator actually gets is that an
// UpdateConsensus RPC carrying no operations gets an idle follower's WAL fsynced, and that lives
// in RaftConsensus::Update() rather than in Log.
//
// Constructed so that only that path can explain the fsync: one op is replicated and then no more,
// so the appender is parked in its drain wait and will not call Sync() again; the byte arm is far
// out of reach; and the requests sent below carry no ops at all, so they append nothing that could
// provoke a sync of their own.
class RaftConsensusWalSyncTest : public RaftConsensusQuorumTest {
 protected:
  static constexpr int kFollowerIdx = 0;
  static constexpr int kLeaderIdx = 2;
  static constexpr int kIntervalMs = 100;

  // These tests move process-global durability flags, one of them to a value that would silently
  // disable the feature for anything running afterwards in the same binary. Restored here rather
  // than at the end of each test body so that a failing test cannot leak them either.
  google::FlagSaver flag_saver_;

  void SetUpFlagsAndDrives() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_never_fsync) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_interval_durable_wal_write_ms) = kIntervalMs;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_bytes_durable_wal_write_mb) = 1024;
    // Registered before BuildAndStartConfig, because Log resolves its drive once at construction
    // and PosixWritableFile does the same per open. The roots are this test's own temp paths, so
    // nothing else in the process shares the counters.
    for (int i = 0; i < 3; ++i) {
      DriveIoStatsRegistry::Instance().Register(
          GetTestPath(Substitute("peer-$0-root", i)), nullptr);
    }
  }

  // A heartbeat: right term, right preceding id, no ops. This is what a real leader sends an
  // up-to-date follower when there is nothing to replicate.
  std::shared_ptr<LWConsensusRequestPB> MakeHeartbeat(const OpIdPB& last_op_id) {
    shared_ptr<RaftConsensus> leader;
    CHECK_OK(peers_->GetPeerByIdx(kLeaderIdx, &leader));
    auto req = rpc::MakeSharedMessage<LWConsensusRequestPB>();
    req->ref_caller_uuid(leader->peer_uuid());
    req->set_caller_term(last_op_id.term());
    req->mutable_preceding_id()->CopyFrom(last_op_id);
    req->mutable_committed_op_id()->CopyFrom(last_op_id);
    return req;
  }

  DriveIoStats* FollowerDriveStats() {
    return DriveIoStatsRegistry::Instance().Find(
        GetTestPath(Substitute("peer-$0-root", kFollowerIdx)));
  }
};

TEST_F(RaftConsensusWalSyncTest, TestHeartbeatSyncsAnIdleFollowersWal) {
  ASSERT_NO_FATALS(SetUpFlagsAndDrives());
  ASSERT_OK(BuildAndStartConfig(3));

  OpIdPB last_op_id;
  vector<scoped_refptr<ConsensusRound>> rounds;
  REPLICATE_SEQUENCE_OF_MESSAGES(
      1, kLeaderIdx, WAIT_FOR_ALL_REPLICAS, COMMIT_ONE_BY_ONE, &last_op_id, &rounds);

  auto* drive_stats = FollowerDriveStats();
  ASSERT_NE(drive_stats, nullptr);
  const auto syncs_before = drive_stats->sync_count();

  shared_ptr<RaftConsensus> follower;
  ASSERT_OK(peers_->GetPeerByIdx(kFollowerIdx, &follower));

  SleepFor(MonoDelta::FromMilliseconds(kIntervalMs + 10));

  // Heartbeats until the WAL is on disk. Sent explicitly rather than waiting for the leader's own
  // timer so the test does not depend on the harness's heartbeat plumbing, but they are the same
  // requests through the same entry point.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        LWConsensusResponsePB resp(nullptr);
        auto req = MakeHeartbeat(last_op_id);
        RETURN_NOT_OK(follower->Update(req, &resp, CoarseBigDeadline()));
        return drive_stats->sync_count() > syncs_before;
      },
      MonoDelta::FromSeconds(10), "the follower's WAL to be fsynced from the heartbeat path"));
}

// The kill switch, which matters because the consensus-path check is on by default. Same setup,
// flag off: however many heartbeats arrive and however long the entry has been sitting there,
// nothing fsyncs.
TEST_F(RaftConsensusWalSyncTest, TestNoSyncWhenDisabled) {
  ASSERT_NO_FATALS(SetUpFlagsAndDrives());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wal_sync_on_consensus_update) = false;
  ASSERT_OK(BuildAndStartConfig(3));

  OpIdPB last_op_id;
  vector<scoped_refptr<ConsensusRound>> rounds;
  REPLICATE_SEQUENCE_OF_MESSAGES(
      1, kLeaderIdx, WAIT_FOR_ALL_REPLICAS, COMMIT_ONE_BY_ONE, &last_op_id, &rounds);

  auto* drive_stats = FollowerDriveStats();
  ASSERT_NE(drive_stats, nullptr);
  const auto syncs_before = drive_stats->sync_count();

  shared_ptr<RaftConsensus> follower;
  ASSERT_OK(peers_->GetPeerByIdx(kFollowerIdx, &follower));

  SleepFor(MonoDelta::FromMilliseconds(kIntervalMs + 10));
  for (int i = 0; i < 20; ++i) {
    LWConsensusResponsePB resp(nullptr);
    auto req = MakeHeartbeat(last_op_id);
    ASSERT_OK(follower->Update(req, &resp, CoarseBigDeadline()));
  }
  ASSERT_EQ(drive_stats->sync_count(), syncs_before);
}

// Test that RequestVote performs according to "spec".
TEST_F(RaftConsensusQuorumTest, TestRequestVote) {
  ASSERT_OK(BuildAndStartConfig(3));

  OpIdPB last_op_id;
  vector<scoped_refptr<ConsensusRound> > rounds;
  REPLICATE_SEQUENCE_OF_MESSAGES(10,
                                 2, // The index of the initial leader.
                                 WAIT_FOR_ALL_REPLICAS,
                                 COMMIT_ONE_BY_ONE,
                                 &last_op_id,
                                 &rounds);

  // Make sure the last operation is committed everywhere
  WaitForCommitIfNotAlreadyPresent(last_op_id, 0, 2);
  WaitForCommitIfNotAlreadyPresent(last_op_id, 1, 2);

  // Ensure last-logged OpId is > (0,0).
  ASSERT_TRUE(OpIdLessThan(MinimumOpId(), last_op_id));

  const int kPeerIndex = 1;
  shared_ptr<RaftConsensus> peer;
  ASSERT_OK(peers_->GetPeerByIdx(kPeerIndex, &peer));

  VoteRequestPB request;
  request.set_tablet_id(kTestTablet);
  request.mutable_candidate_status()->mutable_last_received()->CopyFrom(last_op_id);

  // Test that the replica won't vote since it has recently heard from
  // a valid leader.
  VoteResponsePB response;
  request.set_candidate_uuid("peer-0");
  request.set_candidate_term(last_op_id.term() + 1);
  ASSERT_OK(peer->RequestVote(&request, &response));
  ASSERT_FALSE(response.vote_granted());
  ASSERT_EQ(ConsensusErrorPB::LEADER_IS_ALIVE, response.consensus_error().code());

  // Test that replicas only vote yes for a single peer per term.

  // Indicate that replicas should vote even if they think another leader is alive.
  // This will allow the rest of the requests in the test to go through.
  request.set_ignore_live_leader(true);
  ASSERT_OK(peer->RequestVote(&request, &response));
  ASSERT_TRUE(response.vote_granted());
  ASSERT_EQ(last_op_id.term() + 1, response.responder_term());
  ASSERT_NO_FATALS(AssertDurableTermAndVote(kPeerIndex, last_op_id.term() + 1, "peer-0"));

  // Ensure we get same response for same term and same UUID.
  response.Clear();
  ASSERT_OK(peer->RequestVote(&request, &response));
  ASSERT_TRUE(response.vote_granted());

  // Ensure we get a "no" for a different candidate UUID for that term.
  response.Clear();
  request.set_candidate_uuid("peer-2");
  ASSERT_OK(peer->RequestVote(&request, &response));
  ASSERT_FALSE(response.vote_granted());
  ASSERT_TRUE(response.has_consensus_error());
  ASSERT_EQ(ConsensusErrorPB::ALREADY_VOTED, response.consensus_error().code());
  ASSERT_EQ(last_op_id.term() + 1, response.responder_term());
  ASSERT_NO_FATALS(AssertDurableTermAndVote(kPeerIndex, last_op_id.term() + 1, "peer-0"));

  //
  // Test that replicas refuse votes for an old term.
  //

  // Increase the term of our candidate, which will cause the voter replica to
  // increase its own term to match.
  request.set_candidate_uuid("peer-0");
  request.set_candidate_term(last_op_id.term() + 2);
  response.Clear();
  ASSERT_OK(peer->RequestVote(&request, &response));
  ASSERT_TRUE(response.vote_granted());
  ASSERT_EQ(last_op_id.term() + 2, response.responder_term());
  ASSERT_NO_FATALS(AssertDurableTermAndVote(kPeerIndex, last_op_id.term() + 2, "peer-0"));

  // Now try the old term.
  // Note: Use the peer who "won" the election on the previous term (peer-0),
  // although in practice the impl does not store historical vote data.
  request.set_candidate_term(last_op_id.term() + 1);
  response.Clear();
  ASSERT_OK(peer->RequestVote(&request, &response));
  ASSERT_FALSE(response.vote_granted());
  ASSERT_TRUE(response.has_consensus_error());
  ASSERT_EQ(ConsensusErrorPB::INVALID_TERM, response.consensus_error().code());
  ASSERT_EQ(last_op_id.term() + 2, response.responder_term());
  ASSERT_NO_FATALS(AssertDurableTermAndVote(kPeerIndex, last_op_id.term() + 2, "peer-0"));

  //
  // Ensure replicas vote no for an old op index.
  //

  request.set_candidate_uuid("peer-0");
  request.set_candidate_term(last_op_id.term() + 3);
  request.mutable_candidate_status()->mutable_last_received()->CopyFrom(MinimumOpId());
  response.Clear();
  ASSERT_OK(peer->RequestVote(&request, &response));
  ASSERT_FALSE(response.vote_granted());
  ASSERT_TRUE(response.has_consensus_error());
  ASSERT_EQ(ConsensusErrorPB::LAST_OPID_TOO_OLD, response.consensus_error().code());
  ASSERT_EQ(last_op_id.term() + 3, response.responder_term());
  ASSERT_NO_FATALS(AssertDurableTermWithoutVote(kPeerIndex, last_op_id.term() + 3));

  // Send a "heartbeat" to the peer. It should be rejected.
  auto req = rpc::MakeSharedMessage<LWConsensusRequestPB>();
  req->set_caller_term(last_op_id.term());
  req->ref_caller_uuid("peer-0");
  req->mutable_committed_op_id()->CopyFrom(last_op_id);
  LWConsensusResponsePB res(&req->arena());
  Status s = peer->Update(req, &res, CoarseBigDeadline());
  ASSERT_EQ(last_op_id.term() + 3, res.responder_term());
  ASSERT_TRUE(res.status().has_error());
  ASSERT_EQ(ConsensusErrorPB::INVALID_TERM, res.status().error().code());
  LOG(INFO) << "Follower rejected old heartbeat, as expected: " << res.ShortDebugString();
}

// A follower that briefly won a term, appended its own leader NO_OP and lost leadership before
// committing it holds a conflicting uncommitted entry the next leader does not have. To catch it
// up the leader must eventually offer that index so the follower truncates, but it decides where
// to send from using two watermarks the follower reports and both are unusable here:
// last_received names the follower's own overwritten op, which IsOpInLog() rejects, and
// last_received_current_leader is set once per term and then frozen. The leader keeps re-sending
// below the conflict while every exchange succeeds.
//
// No real election is needed: a request with a higher caller_term makes the follower advance its
// term, which is enough to impersonate the succession of leaders.

struct CatchupProbeObservation {
  OpId baseline;

  // The conflicting uncommitted tail, in a later term than anything the impersonated leader has.
  OpId conflicting_op;

  // Carried by the two fully-deduplicated probes; the second is deliberately higher, and that
  // advance is what the follower is supposed to reflect back.
  OpId probe1_preceding;
  OpId probe2_preceding;

  OpId lrcl_after_probe1;
  OpId lrcl_after_probe2;

  // Names an op no other replica has, so the leader cannot position from it.
  OpId last_received_after_probe2;

  // The leader's own log end; the follower's conflicting op lies beyond it.
  OpId leader_last_received;
};

class RaftConsensusCatchupProbeTest : public RaftConsensusQuorumTest {
 public:
  // Every request here is accepted; the livelock is made of successful exchanges.
  void SendAndCheck(const std::shared_ptr<RaftConsensus>& follower,
                    const std::shared_ptr<LWConsensusRequestPB>& req,
                    LWConsensusResponsePB* resp,
                    const std::string& what) {
    ASSERT_OK(follower->Update(req, resp, CoarseBigDeadline()));
    ASSERT_TRUE(resp->has_status()) << what << ": response carried no status";
    ASSERT_FALSE(resp->status().has_error())
        << what << " was rejected: " << resp->ShortDebugString();
    ASSERT_TRUE(resp->status().has_last_received() &&
                resp->status().has_last_received_current_leader())
        << what << ": response is missing a watermark: " << resp->ShortDebugString();
  }

  void RunScenario(CatchupProbeObservation* out) {
    const int kFollower0Idx = 0;
    const int kLeaderIdx = 2;

    ASSERT_OK(BuildAndStartConfig(3));

    // Committed baseline in the original term.
    OpIdPB last_op_id;
    vector<scoped_refptr<ConsensusRound>> rounds;
    REPLICATE_SEQUENCE_OF_MESSAGES(
        10, kLeaderIdx, WAIT_FOR_ALL_REPLICAS, COMMIT_ONE_BY_ONE, &last_op_id, &rounds);
    WaitForCommitIfNotAlreadyPresent(last_op_id, kFollower0Idx, kLeaderIdx);

    const auto baseline = OpId::FromPB(last_op_id);
    out->baseline = baseline;
    ASSERT_GT(baseline.index, 1) << "Need at least two committed indexes to probe between.";

    shared_ptr<RaftConsensus> leader;
    ASSERT_OK(peers_->GetPeerByIdx(kLeaderIdx, &leader));
    shared_ptr<RaftConsensus> follower;
    ASSERT_OK(peers_->GetPeerByIdx(kFollower0Idx, &follower));

    // Give the follower a conflicting uncommitted tail. Delivering it as a request from term+1
    // reaches the same state as it winning that term, with no real election.
    const OpId conflicting_op(baseline.term + 1, baseline.index + 1);
    out->conflicting_op = conflicting_op;
    {
      auto req_ptr = rpc::MakeSharedMessage<LWConsensusRequestPB>();
      auto& req = *req_ptr;
      req.ref_caller_uuid(leader->peer_uuid());
      req.set_caller_term(conflicting_op.term);
      baseline.ToPB(req.mutable_preceding_id());
      baseline.ToPB(req.mutable_committed_op_id());
      auto* replicate = req.add_ops();
      replicate->set_hybrid_time(clock_->Now().ToUint64());
      replicate->set_op_type(NO_OP);
      replicate->mutable_noop_request();
      conflicting_op.ToPB(replicate->mutable_id());

      LWConsensusResponsePB resp(&req.arena());
      ASSERT_NO_FATALS(SendAndCheck(follower, req_ptr, &resp, "conflicting tail append"));
    }
    ASSERT_EQ(follower->GetLastReceivedOpId(), conflicting_op)
        << "Follower did not take the conflicting tail.";

    // A new leader without that entry. Its first contact advances the follower's term, clearing
    // last_received_current_leader. Both probes are ops-empty heartbeats, the simplest shape
    // reaching the empty-dedup branch, preceded by committed entries the follower holds.
    const auto probe_term = conflicting_op.term + 1;
    out->probe1_preceding = OpId(baseline.term, baseline.index - 1);
    out->probe2_preceding = baseline;

    auto send_probe = [&](const OpId& preceding, const std::string& what, OpId* lrcl,
                          OpId* last_received) {
      auto req_ptr = rpc::MakeSharedMessage<LWConsensusRequestPB>();
      auto& req = *req_ptr;
      req.ref_caller_uuid(leader->peer_uuid());
      req.set_caller_term(probe_term);
      preceding.ToPB(req.mutable_preceding_id());
      // At or below what the follower already committed, so nothing new applies.
      preceding.ToPB(req.mutable_committed_op_id());

      LWConsensusResponsePB resp(&req.arena());
      ASSERT_NO_FATALS(SendAndCheck(follower, req_ptr, &resp, what));
      *lrcl = OpId::FromPB(resp.status().last_received_current_leader());
      *last_received = OpId::FromPB(resp.status().last_received());
    };

    OpId ignored;
    ASSERT_NO_FATALS(send_probe(
        out->probe1_preceding, "first deduplicated probe", &out->lrcl_after_probe1, &ignored));
    ASSERT_NO_FATALS(send_probe(
        out->probe2_preceding, "second deduplicated probe, advanced preceding",
        &out->lrcl_after_probe2, &out->last_received_after_probe2));

    out->leader_last_received = leader->GetLastReceivedOpId();

    LOG(INFO) << "Catch-up probe: baseline=" << baseline
              << " conflicting_op=" << conflicting_op
              << " probe1_preceding=" << out->probe1_preceding
              << " lrcl_after_probe1=" << out->lrcl_after_probe1
              << " probe2_preceding=" << out->probe2_preceding
              << " lrcl_after_probe2=" << out->lrcl_after_probe2
              << " last_received_after_probe2=" << out->last_received_after_probe2
              << " leader_last_received=" << out->leader_last_received;
  }
};

// A fully deduplicated request must advance last_received_current_leader to the op the follower
// just confirmed it holds.
TEST_F(RaftConsensusCatchupProbeTest, LastReceivedCurrentLeaderAdvancesAcrossDedupedProbes) {
  CatchupProbeObservation obs;
  ASSERT_NO_FATALS(RunScenario(&obs));

  // Trigger guard: the probes have to actually advance, or this holds vacuously.
  ASSERT_LT(obs.probe1_preceding, obs.probe2_preceding)
      << "The two probes did not carry an advancing preceding op id, so there was nothing "
      << "for the follower to reflect back.";

  ASSERT_EQ(obs.lrcl_after_probe2, obs.probe2_preceding)
      << "last_received_current_leader stayed at " << obs.lrcl_after_probe2
      << " while the leader's probe had advanced to " << obs.probe2_preceding
      << "; the leader cannot discover the conflicting index from this.";

  // The premise this fix exists for is unchanged: the other watermark still names
  // an op only this replica ever had, so the leader still cannot position from it
  // and still depends on the fallback above.
  ASSERT_EQ(obs.last_received_after_probe2, obs.conflicting_op);
  ASSERT_GT(obs.conflicting_op.index, obs.leader_last_received.index);
}

// With the monotonic advance disabled the freeze must come back, proving the flag gates this path.
TEST_F(RaftConsensusCatchupProbeTest, MonotonicAdvanceDisabledRestoresFreeze) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_raft_monotonic_last_received_current_leader) = false;
  auto restore = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_raft_monotonic_last_received_current_leader) = true;
  });

  CatchupProbeObservation obs;
  ASSERT_NO_FATALS(RunScenario(&obs));

  ASSERT_LT(obs.probe1_preceding, obs.probe2_preceding);
  ASSERT_EQ(obs.lrcl_after_probe1, obs.probe1_preceding)
      << "The first probe did not seed last_received_current_leader.";

  // The second probe advanced the preceding op id and the follower kept reporting
  // the first probe's value.
  ASSERT_EQ(obs.lrcl_after_probe2, obs.lrcl_after_probe1)
      << "last_received_current_leader moved from " << obs.lrcl_after_probe1 << " to "
      << obs.lrcl_after_probe2 << "; the kill switch did not restore the freeze.";
  ASSERT_LT(obs.lrcl_after_probe2, obs.probe2_preceding);
  ASSERT_EQ(obs.last_received_after_probe2, obs.conflicting_op);
}

} // namespace yb::consensus
