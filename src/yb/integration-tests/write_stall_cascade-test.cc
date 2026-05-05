// Copyright (c) YugabyteDB, Inc.
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

#include <atomic>
#include <thread>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ql_protocol_util.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/rpc/service_pool.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/write_controller.h"

#include "yb/server/rpc_server.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/cast.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_bool(enable_load_balancing);
DECLARE_bool(TEST_allow_stop_writes);
DECLARE_bool(TEST_skip_write_stop_check_in_update_consensus);
DECLARE_bool(TEST_skip_write_stop_check_in_should_apply_write);
DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(rpc_workers_limit);

namespace yb {

// Regression coverage for the write stall cascade described in #30728.
//
// Background: when a follower's RocksDB enters a hard write stop, an RPC thread
// applying writes can block in DelayWrite() while holding RaftConsensus::update_mutex_.
// RequestVote uses try_lock on the same mutex, so a held mutex causes the follower
// to respond "IsBusy" to vote requests, which can starve cluster-wide leader election
// and replication. The fix rejects UpdateConsensus on a stalled tablet before the
// mutex is acquired, keeping update_mutex_ free for vote processing and the RPC
// worker free for other tablets.
//
// These tests validate cross-tablet isolation under write stalls: elections still
// complete when a follower is stalled, writes to healthy tablets succeed despite a
// stalled tablet on the same tserver, and - with the early rejection bypassed -
// cross-tablet writes time out because the stalled follower's RPC thread is consumed
// by DelayWrite. The cross-tablet tests use rpc_workers_limit=1 to make thread
// exhaustion deterministic.
class WriteStallCascadeTest : public integration_tests::YBTableTestBase {
 protected:
  void BeforeStartCluster() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_allow_stop_writes) = true;
  }

  bool use_external_mini_cluster() override { return false; }

  size_t num_tablet_servers() override { return 3; }

  int num_tablets() override { return 1; }

  Result<std::string> GetOnlyTabletId() {
    std::vector<std::string> ranges;
    std::vector<TabletId> tablet_ids;
    RETURN_NOT_OK(
        client_->GetTablets(table_.name(), /* max_tablets = */ 0, &tablet_ids, &ranges));
    SCHECK_EQ(tablet_ids.size(), 1U, IllegalState, "Expected exactly one tablet");
    return tablet_ids[0];
  }

  struct TabletLayout {
    size_t leader_idx;
    size_t stalled_follower_idx;
    size_t other_follower_idx;
    std::shared_ptr<tablet::TabletPeer> stalled_peer;
  };

  // Finds the leader and two followers, returning their tserver indices.
  Result<TabletLayout> GetTabletLayout(const std::string& tablet_id) {
    TabletLayout layout;
    bool found_leader = false;
    std::vector<size_t> follower_indices;

    for (size_t i = 0; i < mini_cluster()->num_tablet_servers(); i++) {
      auto* ts = mini_cluster()->mini_tablet_server(i);
      auto peer = ts->server()->tablet_manager()->LookupTablet(tablet_id);
      if (!peer) continue;
      auto consensus_result = peer->GetConsensus();
      if (!consensus_result.ok()) continue;
      auto role = (*consensus_result)->role();
      if (role == PeerRole::LEADER) {
        layout.leader_idx = i;
        found_leader = true;
      } else if (role == PeerRole::FOLLOWER) {
        follower_indices.push_back(i);
      }
    }

    SCHECK(found_leader, NotFound, "No leader found");
    SCHECK_GE(follower_indices.size(), 2U, NotFound, "Need at least 2 followers");

    layout.stalled_follower_idx = follower_indices[0];
    layout.other_follower_idx = follower_indices[1];

    auto* stalled_ts = mini_cluster()->mini_tablet_server(layout.stalled_follower_idx);
    layout.stalled_peer = stalled_ts->server()->tablet_manager()->LookupTablet(tablet_id);

    return layout;
  }

  // Moves the leader for the given tablet to the tserver at target_ts_idx by stepping down
  // the current leader with a directed new_leader_uuid. Waits for the target to become leader.
  Status MoveLeaderToTserver(const TabletId& tablet_id, size_t target_ts_idx) {
    auto* target_ts = mini_cluster()->mini_tablet_server(target_ts_idx);
    auto target_uuid = target_ts->server()->permanent_uuid();

    auto leader_peer = VERIFY_RESULT(GetLeaderPeerForTablet(mini_cluster(), tablet_id));
    auto leader_consensus = VERIFY_RESULT(leader_peer->GetConsensus());
    if (leader_consensus->LeaderTerm() != OpId::kUnknownTerm) {
      auto leader_uuid = leader_peer->permanent_uuid();
      if (leader_uuid == target_uuid) {
        return Status::OK();
      }
    }

    RETURN_NOT_OK(StepDown(leader_peer, target_uuid, ForceStepDown::kTrue));

    return WaitFor([&]() -> Result<bool> {
      auto peer = target_ts->server()->tablet_manager()->LookupTablet(tablet_id);
      if (!peer) return false;
      auto consensus = VERIFY_RESULT(peer->GetConsensus());
      return consensus->GetLeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
    }, 10s * kTimeMultiplier, Format("Waiting for ts-$0 to become leader for $1",
                                     target_ts_idx, tablet_id));
  }
};

// Verifies that leader election succeeds even when a follower is in a hard write stop.
//
// Scenario: follower F has a write stop, third tserver G is shut down, leader steps down.
// The candidate needs F's vote. VoteRequest uses try_lock(update_mutex_), so if
// UpdateConsensus RPCs acquired update_mutex_ and blocked inside DelayWrite, the vote
// would respond "IsBusy" and the election would time out. Because UpdateConsensus RPCs
// are rejected before update_mutex_ when the tablet is write-stopped (#30728),
// VoteRequests succeed and the election completes.
TEST_F(WriteStallCascadeTest, ElectionSucceedsDespiteFollowerWriteStall) {
  auto tablet_id = ASSERT_RESULT(GetOnlyTabletId());
  auto layout = ASSERT_RESULT(GetTabletLayout(tablet_id));

  LOG(INFO) << "=== LAYOUT ==="
            << " leader=ts-" << layout.leader_idx
            << " stalled_follower=ts-" << layout.stalled_follower_idx
            << " other_follower=ts-" << layout.other_follower_idx;

  auto tablet = ASSERT_RESULT(layout.stalled_peer->shared_tablet());
  auto* db_impl = down_cast<rocksdb::DBImpl*>(tablet->regular_db());
  auto& write_controller = db_impl->TEST_write_controler();

  auto stop_token = write_controller.GetStopToken();
  ASSERT_TRUE(write_controller.IsStopped());

  LOG(INFO) << "=== WRITE STOP INJECTED on ts-" << layout.stalled_follower_idx
            << " tablet=" << tablet_id
            << " IsStopped=" << write_controller.IsStopped()
            << " AreWritesStopped=" << tablet->AreWritesStopped();

  // Pump writes so the leader replicates ops to the stalled follower. The fix rejects
  // these UpdateConsensus RPCs on the follower before update_mutex_ is acquired.
  std::atomic<bool> stop_writing{false};
  std::thread writer([this, &stop_writing] {
    auto session = NewSession();
    int seq = 0;
    while (!stop_writing.load()) {
      auto s = PutKeyValue(session.get(), Format("cascade_key_$0", seq++), "value");
      if (!s.ok()) {
        std::this_thread::sleep_for(10ms);
      }
    }
  });

  // Ensure the writer thread is always stopped and joined, even if a gtest
  // ASSERT_* below triggers an early return from TestBody(). Otherwise the
  // std::thread destructor runs while the thread is still joinable and calls
  // std::terminate(). Any consensus thread parked in DelayWrite is blocked on
  // bg_cv_.Wait(); CancelAllBackgroundWork signals bg_cv_ and breaks the wait.
  auto writer_cleanup = ScopeExit([&] {
    stop_writing.store(true);
    stop_token.reset();
    db_impl->CancelAllBackgroundWork(/* wait */ false);
    if (writer.joinable()) {
      writer.join();
    }
  });

  // Let pumped writes reach the stalled follower before we force an election.
  SleepFor(2s * kTimeMultiplier);

  // Shut down the other follower so the stalled follower's vote is needed for majority.
  LOG(INFO) << "=== Shutting down ts-" << layout.other_follower_idx << " ===";
  mini_cluster()->mini_tablet_server(layout.other_follower_idx)->Shutdown();

  // Step down the leader to trigger an election.
  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(mini_cluster(), tablet_id));
  LOG(INFO) << "=== Stepping down leader (ts-" << layout.leader_idx << ") ===";
  ASSERT_OK(StepDown(leader_peer, std::string(), ForceStepDown::kTrue));

  ASSERT_OK(WaitFor([&]() {
    auto consensus = leader_peer->GetConsensus();
    return consensus.ok() &&
           (*consensus)->GetLeaderStatus() == consensus::LeaderStatus::NOT_LEADER;
  }, 5s * kTimeMultiplier, "Waiting for old leader to step down"));

  LOG(INFO) << "=== Old leader stepped down, waiting for new leader election ===";

  // Wait for a new leader. We use RequireLeaderIsReady::kFalse because the new leader
  // cannot reach LEADER_AND_READY when the only reachable follower rejects ops.
  auto election_result = WaitUntilTabletHasLeader(
      mini_cluster(), tablet_id,
      CoarseMonoClock::Now() + 10s * kTimeMultiplier,
      RequireLeaderIsReady::kFalse);

  LOG(INFO) << "=== ELECTION RESULT: " << election_result << " ===";

  // With the fix in place, UpdateConsensus RPCs on the stalled follower are rejected
  // before update_mutex_ is acquired, so VoteRequests succeed and the election completes.
  ASSERT_OK(election_result);
  LOG(INFO) << "=== TEST COMPLETE ===";
}

// Demonstrates that a write-stalled follower can block elections when UpdateConsensus
// RPCs are not rejected early. With the early rejection bypassed, the follower's RPC
// thread parks in DelayWrite() holding update_mutex_, causing VoteRequest to return
// IsBusy. However, this test is non-deterministic: if the stalled follower itself
// becomes the candidate, it wins via self-vote + the other alive tserver's vote,
// bypassing its own blocked update_mutex_ entirely. This test is intended for repeated
// runs to observe the cascade empirically - it logs the outcome rather than asserting.
TEST_F(WriteStallCascadeTest, WriteStallCanBlockElection) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_write_stop_check_in_update_consensus) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_write_stop_check_in_should_apply_write) = true;

  auto tablet_id = ASSERT_RESULT(GetOnlyTabletId());
  auto layout = ASSERT_RESULT(GetTabletLayout(tablet_id));

  LOG(INFO) << "=== LAYOUT (EARLY REJECTION BYPASSED) ==="
            << " leader=ts-" << layout.leader_idx
            << " stalled_follower=ts-" << layout.stalled_follower_idx
            << " other_follower=ts-" << layout.other_follower_idx;

  auto tablet = ASSERT_RESULT(layout.stalled_peer->shared_tablet());
  auto* db_impl = down_cast<rocksdb::DBImpl*>(tablet->regular_db());
  auto& write_controller = db_impl->TEST_write_controler();

  auto stop_token = write_controller.GetStopToken();
  ASSERT_TRUE(write_controller.IsStopped());

  LOG(INFO) << "=== WRITE STOP INJECTED on ts-" << layout.stalled_follower_idx << " ===";

  std::atomic<bool> stop_writing{false};
  std::thread writer([this, &stop_writing] {
    auto session = NewSession();
    int seq = 0;
    while (!stop_writing.load()) {
      auto s = PutKeyValue(session.get(), Format("cascade_election_$0", seq++), "value");
      if (!s.ok()) {
        std::this_thread::sleep_for(10ms);
      }
    }
  });

  auto writer_cleanup = ScopeExit([&] {
    stop_writing.store(true);
    stop_token.reset();
    db_impl->CancelAllBackgroundWork(/* wait */ false);
    if (writer.joinable()) {
      writer.join();
    }
  });

  SleepFor(2s * kTimeMultiplier);

  LOG(INFO) << "=== Shutting down ts-" << layout.other_follower_idx << " ===";
  mini_cluster()->mini_tablet_server(layout.other_follower_idx)->Shutdown();

  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(mini_cluster(), tablet_id));
  LOG(INFO) << "=== Stepping down leader (ts-" << layout.leader_idx << ") ===";
  ASSERT_OK(StepDown(leader_peer, std::string(), ForceStepDown::kTrue));

  ASSERT_OK(WaitFor([&]() {
    auto consensus = leader_peer->GetConsensus();
    return consensus.ok() &&
           (*consensus)->GetLeaderStatus() == consensus::LeaderStatus::NOT_LEADER;
  }, 5s * kTimeMultiplier, "Waiting for old leader to step down"));

  LOG(INFO) << "=== Old leader stepped down, waiting for election ===";

  auto election_result = WaitUntilTabletHasLeader(
      mini_cluster(), tablet_id,
      CoarseMonoClock::Now() + 10s * kTimeMultiplier,
      RequireLeaderIsReady::kFalse);

  if (election_result.ok()) {
    // The stalled follower likely became the candidate and won via self-vote +
    // the stepped-down leader's vote, bypassing its own blocked update_mutex_.
    LOG(INFO) << "=== ELECTION RESULT: SUCCEEDED (stalled follower likely self-elected) ===";
  } else {
    // The stepped-down leader became the candidate but couldn't get the stalled
    // follower's vote because update_mutex_ was held by a thread in DelayWrite().
    LOG(INFO) << "=== ELECTION RESULT: FAILED (update_mutex_ blocked VoteRequest) ==="
              << " error=" << election_result;
  }

  LOG(INFO) << "=== OBSERVATIONAL TEST COMPLETE ===";
}

// Validates cross-tablet isolation when a follower has a write-stalled tablet.
//
// Uses rpc_workers_limit=1 so that a single thread stuck in DelayWrite for tablet_A
// would leave zero threads for tablet_B's UpdateConsensus RPCs. Both tablet leaders are
// pinned to the same tserver, and the third tserver is shut down, so replication for
// tablet_B must go through the stalled follower. Because UpdateConsensus RPCs for the
// stalled tablet are rejected before update_mutex_, the RPC worker stays free and
// tablet_B's replication succeeds normally.
class WriteStallCrossTabletCascadeTest : public WriteStallCascadeTest {
 protected:
  void BeforeStartCluster() override {
    WriteStallCascadeTest::BeforeStartCluster();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_workers_limit) = 1;
    // Short client timeout so writes fail fast when replication is blocked.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 5000;
  }

  int64_t GetConsensusQueueOverflows(size_t ts_idx) {
    auto* ts = mini_cluster()->mini_tablet_server(ts_idx);
    auto* pool = ts->server()->rpc_server()->TEST_service_pool(
        "yb.consensus.ConsensusService");
    return pool ? pool->RpcsQueueOverflowMetric()->value() : 0;
  }
};

TEST_F(WriteStallCrossTabletCascadeTest, HealthyTabletUnaffectedByWriteStalledTablet) {
  auto tablet_a_id = ASSERT_RESULT(GetOnlyTabletId());
  auto layout = ASSERT_RESULT(GetTabletLayout(tablet_a_id));

  LOG(INFO) << "=== LAYOUT ==="
            << " leader=ts-" << layout.leader_idx
            << " stalled_follower=ts-" << layout.stalled_follower_idx
            << " other_follower=ts-" << layout.other_follower_idx;

  // Create a second table (tablet_B) -- same schema, different name.
  client::YBTableName table_b_name(
      YQL_DATABASE_CQL, table_name().namespace_name(), "table_b");
  client::TableHandle table_b;
  {
    client::YBSchemaBuilder b;
    b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
    b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
    ASSERT_OK(table_b.Create(table_b_name, 1 /* num_tablets */, client_.get(), &b));
  }

  // Wait for tablet_B to have a leader.
  std::vector<TabletId> tablet_b_ids;
  std::vector<std::string> ranges;
  ASSERT_OK(client_->GetTablets(table_b_name, 0, &tablet_b_ids, &ranges));
  ASSERT_EQ(tablet_b_ids.size(), 1);
  ASSERT_OK(WaitUntilTabletHasLeader(
      mini_cluster(), tablet_b_ids[0], CoarseMonoClock::Now() + 15s,
      RequireLeaderIsReady::kTrue));

  // Pin tablet_B's leader to the same tserver as tablet_A's leader so that tablet_B's
  // replication path goes through the stalled follower. Without this, tablet_B's leader
  // could land on the stalled tserver and replicate to the healthy tserver directly,
  // bypassing the stalled follower entirely and not exercising the fix.
  ASSERT_OK(MoveLeaderToTserver(tablet_b_ids[0], layout.leader_idx));
  LOG(INFO) << "=== TABLET_B LEADER PINNED to ts-" << layout.leader_idx << " ===";

  // Inject write stop on the stalled follower for tablet_A.
  auto tablet = ASSERT_RESULT(layout.stalled_peer->shared_tablet());
  auto* db_impl = down_cast<rocksdb::DBImpl*>(tablet->regular_db());
  auto& write_controller = db_impl->TEST_write_controler();

  auto stop_token = write_controller.GetStopToken();
  ASSERT_TRUE(write_controller.IsStopped());

  LOG(INFO) << "=== WRITE STOP INJECTED on tablet_A (ts-" << layout.stalled_follower_idx << ") ===";

  auto overflows_before = GetConsensusQueueOverflows(layout.stalled_follower_idx);

  // Pump writes to tablet_A. With the fix, UpdateConsensus RPCs for the stalled tablet
  // are rejected before update_mutex_, so the single RPC worker never parks in
  // DelayWrite and stays available for tablet_B's RPCs.
  std::atomic<bool> stop_writing{false};
  std::thread writer([this, &stop_writing] {
    auto session = client_->NewSession(MonoDelta::FromSeconds(2));
    int seq = 0;
    while (!stop_writing.load()) {
      auto s = PutKeyValue(session.get(), Format("cascade_a_$0", seq++), "value");
      if (!s.ok()) {
        std::this_thread::sleep_for(10ms);
      }
    }
  });

  // Ensure the writer thread is always stopped and joined, even if a gtest
  // ASSERT_* below triggers an early return from TestBody(). Otherwise the
  // std::thread destructor runs while the thread is still joinable and calls
  // std::terminate().
  auto writer_cleanup = ScopeExit([&] {
    stop_writing.store(true);
    stop_token.reset();
    db_impl->CancelAllBackgroundWork(/* wait */ false);
    if (writer.joinable()) {
      writer.join();
    }
  });

  // Let pumped writes to tablet_A reach the stalled follower before we isolate the
  // cluster and exercise tablet_B.
  SleepFor(2s * kTimeMultiplier);

  // Shut down the other follower so the stalled tserver is needed for majority on BOTH tablets.
  LOG(INFO) << "=== Shutting down ts-" << layout.other_follower_idx << " ===";
  mini_cluster()->mini_tablet_server(layout.other_follower_idx)->Shutdown();

  // Write to tablet_B. The leader needs the stalled tserver for majority.
  LOG(INFO) << "=== Attempting write to tablet_B ===";
  auto session_b = client_->NewSession(MonoDelta::FromSeconds(5));
  auto insert = table_b.NewInsertOp(session_b->arena());
  QLAddStringHashValue(insert->mutable_request(), "cross_tablet_key");
  table_b.AddStringColumnValue(insert->mutable_request(), "v", "cross_tablet_value");
  auto write_status = session_b->TEST_ApplyAndFlush(insert);

  LOG(INFO) << "=== TABLET_B WRITE RESULT: " << write_status << " ===";

  auto overflows_after = GetConsensusQueueOverflows(layout.stalled_follower_idx);
  LOG(INFO) << "=== Queue overflows on stalled tserver: before=" << overflows_before
            << " after=" << overflows_after
            << " new=" << (overflows_after - overflows_before) << " ===";

  // With the fix in place, UpdateConsensus RPCs for the stalled tablet are rejected
  // before acquiring update_mutex_, so the single worker thread stays free for
  // tablet_B's RPCs and the cross-tablet write succeeds.
  ASSERT_OK(write_status);
  ASSERT_EQ(overflows_after - overflows_before, 0)
      << "Consensus queue should not overflow with the fix enabled";
  LOG(INFO) << "=== TEST COMPLETE ===";
}

// Demonstrates the write stall cascade: a stalled follower's RPC worker thread parks in
// DelayWrite() while holding update_mutex_, exhausting the thread pool and starving
// unrelated tablets. With rpc_workers_limit=1, the single thread blocked on tablet_A's
// UpdateConsensus prevents processing of tablet_B's UpdateConsensus. The leader cannot
// reach majority for tablet_B (the third tserver is shut down), so writes time out.
//
// The early rejection in RaftConsensus::Update() is bypassed via test flags to reproduce
// the original cascade behavior described in #30728.
//
// This is deterministic because:
// - WriteController::GetStopToken() injects the stop synchronously.
// - DelayWrite() blocks indefinitely on bg_cv_.Wait() for kStop tokens.
// - With rpc_workers_limit=1, a single blocked thread exhausts the pool.
TEST_F(WriteStallCrossTabletCascadeTest, WriteStalledFollowerStarvesOtherTablets) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_write_stop_check_in_update_consensus) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_write_stop_check_in_should_apply_write) = true;

  auto tablet_a_id = ASSERT_RESULT(GetOnlyTabletId());
  auto layout = ASSERT_RESULT(GetTabletLayout(tablet_a_id));

  LOG(INFO) << "=== LAYOUT (EARLY REJECTION BYPASSED) ==="
            << " leader=ts-" << layout.leader_idx
            << " stalled_follower=ts-" << layout.stalled_follower_idx
            << " other_follower=ts-" << layout.other_follower_idx;

  // Create tablet_B.
  client::YBTableName table_b_name(
      YQL_DATABASE_CQL, table_name().namespace_name(), "table_b_neg");
  client::TableHandle table_b;
  {
    client::YBSchemaBuilder b;
    b.AddColumn("k")->Type(DataType::BINARY)->NotNull()->HashPrimaryKey();
    b.AddColumn("v")->Type(DataType::BINARY)->NotNull();
    ASSERT_OK(table_b.Create(table_b_name, 1 /* num_tablets */, client_.get(), &b));
  }

  std::vector<TabletId> tablet_b_ids;
  std::vector<std::string> ranges;
  ASSERT_OK(client_->GetTablets(table_b_name, 0, &tablet_b_ids, &ranges));
  ASSERT_EQ(tablet_b_ids.size(), 1);
  ASSERT_OK(WaitUntilTabletHasLeader(
      mini_cluster(), tablet_b_ids[0], CoarseMonoClock::Now() + 15s,
      RequireLeaderIsReady::kTrue));

  // Pin tablet_B's leader to the same tserver as tablet_A's leader so the replication
  // path for tablet_B must go through the stalled follower to reach majority.
  ASSERT_OK(MoveLeaderToTserver(tablet_b_ids[0], layout.leader_idx));
  LOG(INFO) << "=== TABLET_B LEADER PINNED to ts-" << layout.leader_idx << " ===";

  // Inject write stop on the stalled follower for tablet_A.
  auto tablet = ASSERT_RESULT(layout.stalled_peer->shared_tablet());
  auto* db_impl = down_cast<rocksdb::DBImpl*>(tablet->regular_db());
  auto& write_controller = db_impl->TEST_write_controler();

  auto stop_token = write_controller.GetStopToken();
  ASSERT_TRUE(write_controller.IsStopped());

  LOG(INFO) << "=== WRITE STOP INJECTED on tablet_A (ts-"
            << layout.stalled_follower_idx << "), early rejection bypassed ===";

  // Pump writes to tablet_A. Without the fix, the first UpdateConsensus that carries
  // committed write ops will park the stalled follower's RPC worker in DelayWrite().
  std::atomic<bool> stop_writing{false};
  std::thread writer([this, &stop_writing] {
    auto session = client_->NewSession(MonoDelta::FromSeconds(2));
    int seq = 0;
    while (!stop_writing.load()) {
      auto s = PutKeyValue(session.get(), Format("cascade_neg_$0", seq++), "value");
      if (!s.ok()) {
        std::this_thread::sleep_for(10ms);
      }
    }
  });

  auto writer_cleanup = ScopeExit([&] {
    stop_writing.store(true);
    stop_token.reset();
    db_impl->CancelAllBackgroundWork(/* wait */ false);
    if (writer.joinable()) {
      writer.join();
    }
  });

  // Wait for the stalled follower to receive committed ops and block in DelayWrite().
  // With rpc_workers_limit=1, this consumes the only thread within ~1 heartbeat cycle.
  SleepFor(2s * kTimeMultiplier);

  // Shut down the other follower so the stalled tserver is needed for majority on tablet_B.
  LOG(INFO) << "=== Shutting down ts-" << layout.other_follower_idx << " ===";
  mini_cluster()->mini_tablet_server(layout.other_follower_idx)->Shutdown();

  // Write to tablet_B. The stalled follower can't process the UpdateConsensus because
  // its only RPC worker is parked in DelayWrite. The leader can't get majority.
  LOG(INFO) << "=== Attempting write to tablet_B (expecting failure) ===";
  auto session_b = client_->NewSession(MonoDelta::FromSeconds(5));
  auto insert = table_b.NewInsertOp(session_b->arena());
  QLAddStringHashValue(insert->mutable_request(), "cross_tablet_neg_key");
  table_b.AddStringColumnValue(insert->mutable_request(), "v", "cross_tablet_neg_value");
  auto write_status = session_b->TEST_ApplyAndFlush(insert);

  LOG(INFO) << "=== TABLET_B WRITE RESULT (EARLY REJECTION BYPASSED): " << write_status << " ===";

  // With the early rejection bypassed, the write should fail because the stalled
  // follower's RPC worker is consumed, preventing tablet_B's replication from reaching
  // majority.
  ASSERT_NOK(write_status) << "Write to tablet_B should fail when early rejection is "
                              "bypassed and stalled follower's RPC thread is consumed";

  LOG(INFO) << "=== NEGATIVE TEST COMPLETE ===";
}

}  // namespace yb
