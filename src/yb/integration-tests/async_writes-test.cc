// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver.messages.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/hdr_histogram.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_leader_failure_detection);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_bool(quick_leader_election_on_create);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(use_create_table_leader_hint);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(ysql_enable_write_pipelining);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_int32(ht_lease_duration_ms);
DECLARE_int32(leader_lease_duration_ms);
DECLARE_int32(min_leader_stepdown_retry_interval_ms);
DECLARE_int64(protege_synchronization_timeout_ms);

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_WaitForAsyncWrite);

namespace yb {

constexpr auto kTableName = "tbl1";
const auto kSelectAllStmt = Format("SELECT * FROM $0 ORDER BY key", kTableName);

class YSqlAsyncWriteTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    SetTestFlags();

    TEST_SETUP_SUPER(pgwrapper::PgMiniTestBase);

    // Tablets created while tservers are still registering start under-replicated; let the LB
    // repair them before disabling it.
    ASSERT_OK(cluster_->WaitForAllTabletServers());
    ASSERT_OK(
        WaitAllReplicasReady(cluster_.get(), 120s * kTimeMultiplier, UserTabletsOnly::kFalse));
    // These tests stepdown the leader, so we need to disable load balancing.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
  }

  virtual void SetTestFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_write_pipelining) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_create_table_leader_hint) = false;
  }

  size_t NumMasters() override { return 3; }

  size_t NumTabletServers() override { return 3; }

  Result<TabletId> GetTabletId(const std::string& table_name = kTableName) {
    // Right after CREATE TABLE the tablet's leader may not be elected yet, so the leader-peer list
    // can briefly be empty. Wait for the single leader to settle.
    std::vector<tablet::TabletPeerPtr> tablets;
    RETURN_NOT_OK(LoggedWaitFor(
        [this, &table_name, &tablets]() -> Result<bool> {
          tablets = VERIFY_RESULT(
              ListTabletPeersForTableName(cluster_.get(), table_name, ListPeersFilter::kLeaders));
          return tablets.size() == 1;
        },
        30s, Format("Wait for a single leader for table $0", table_name)));
    return tablets.front()->tablet_id();
  }

  Result<tablet::TabletPeerPtr> GetTabletPeerOnTserver(size_t ts_idx, const TabletId& tablet_id) {
    return cluster_->mini_tablet_server(ts_idx)->server()->tablet_peer_lookup()->GetServingTablet(
        tablet_id);
  }

  size_t GetLeaderIdx(const TabletId& tablet_id) {
    size_t leader_idx;
    GetLeaderForTablet(cluster_.get(), tablet_id, &leader_idx);
    return leader_idx;
  }

  Status StepDown(size_t leader_idx, size_t new_leader_idx, TabletId tablet_id) {
    LOG(INFO) << "StepDown leader_idx: " << leader_idx << " new_leader_idx: " << new_leader_idx
              << " tablet_id: " << tablet_id;
    if (leader_idx == new_leader_idx) {
      return Status::OK();
    }

    auto leader_peer = VERIFY_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
    SCHECK_EQ(
        leader_peer->permanent_uuid(),
        cluster_->mini_tablet_server(leader_idx)->server()->permanent_uuid(), IllegalState,
        "Unexpected leader peer");

    auto leader_consensus = VERIFY_RESULT(leader_peer->GetRaftConsensus());
    auto leader_op_id = VERIFY_RESULT(leader_consensus->GetLastOpId(consensus::COMMITTED_OPID));
    auto new_leader_peer = VERIFY_RESULT(cluster_->mini_tablet_server(new_leader_idx)
                                             ->server()
                                             ->tablet_peer_lookup()
                                             ->GetServingTablet(tablet_id));
    auto new_leader_consensus = VERIFY_RESULT(new_leader_peer->GetRaftConsensus());

    RETURN_NOT_OK(LoggedWaitFor(
        [new_leader_consensus, leader_op_id]() -> Result<bool> {
          return VERIFY_RESULT(new_leader_consensus->GetLastOpId(consensus::RECEIVED_OPID)) >=
                 leader_op_id;
        },
        30s,
        Format(
            "Wait for peer $0 to catch up with leader $1. Op id: $2", new_leader_idx, leader_idx,
            leader_op_id)));

    RETURN_NOT_OK(
        yb::StepDown(
            leader_peer, cluster_->mini_tablet_server(new_leader_idx)->server()->permanent_uuid(),
            ForceStepDown::kTrue));

    return LoggedWaitFor(
        [this, new_leader_idx, tablet_id]() -> Result<bool> {
          return GetLeaderIdx(tablet_id) == new_leader_idx;
        },
        30s, Format("Wait for tablet $0 leader to be $1", tablet_id, new_leader_idx));
  }

  Result<size_t> WaitForNewTabletLeader(const std::string& tablet_id, size_t old_leader_idx) {
    size_t new_leader_idx;
    RETURN_NOT_OK(LoggedWaitFor(
        [this, tablet_id, &old_leader_idx, &new_leader_idx]() -> Result<bool> {
          for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
            if (i == old_leader_idx) {
              // Old leader is stuck so cannot be queried.
              continue;
            }
            if (cluster_->mini_tablet_server(i)->server()->LeaderAndReady(tablet_id)) {
              new_leader_idx = i;
              return true;
            }
          }
          return false;
        },
        30s, "Leader election"));

    return new_leader_idx;
  }

  Status ResolveAndFlushTablet(size_t idx, TabletId tablet_id) {
    auto leader_peer = VERIFY_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
    auto leader_tablet = VERIFY_RESULT(leader_peer->shared_tablet());
    auto now_ht = VERIFY_RESULT(leader_tablet->SafeTime());

    auto tablet_peer = cluster_->GetTabletManager(idx)->LookupTablet(tablet_id);
    auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet());

    RETURN_NOT_OK(
        tablet->transaction_participant()->ResolveIntents(now_ht, CoarseTimePoint::max()));

    auto log = VERIFY_RESULT(tablet_peer->GetRaftConsensus())->log();
    RETURN_NOT_OK(log->WaitUntilAllFlushed());
    RETURN_NOT_OK(tablet->Flush(tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
    return Status::OK();
  }

  Status ValidateData(
      std::string expected_all_as_string, const TabletId& tablet_id = TabletId()) {
    auto table_data = VERIFY_RESULT(conn_->FetchAllAsString(kSelectAllStmt));
    SCHECK_EQ(table_data, expected_all_as_string, IllegalState, "Unexpected data in table");

    if (!tablet_id.empty()) {
      return WaitFor(
          [this, &tablet_id]() -> Result<bool> {
            return ValidateTabletDataAcrossReplicas(tablet_id).ok();
          },
          30s * kTimeMultiplier, "Wait for replicas to converge");
    }
    return Status::OK();
  }

  // Moves every tablet leader off ts_idx except excluded_tablet_id, so that crashing ts_idx
  // only takes down the tablet under test. Loops until a full pass finds no leaders to move,
  // since a tablet that is mid-election during a pass is invisible to the kLeaders filter.
  Status MoveLeadersOffTserver(size_t ts_idx, const TabletId& excluded_tablet_id) {
    const auto& uuid = cluster_->mini_tablet_server(ts_idx)->server()->permanent_uuid();
    const size_t target_idx = ts_idx == 0 ? 1 : 0;
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          bool clean_pass = true;
          for (const auto& peer : ListTabletPeers(
                   cluster_.get(), ListPeersFilter::kLeaders, UserTabletsOnly::kFalse)) {
            if (peer->permanent_uuid() != uuid || peer->tablet_id() == excluded_tablet_id) {
              continue;
            }
            clean_pass = false;
            // Tolerate races: leadership may have moved since the list was taken.
            WARN_NOT_OK(StepDown(ts_idx, target_idx, peer->tablet_id()), "StepDown failed");
          }
          return clean_pass;
        },
        60s * kTimeMultiplier, "Move tablet leaders off Tserver");
  }

  // Returns the leader index.
  Result<size_t> PrepareToBreakConnectivity(TabletId tablet_id) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_max_missed_heartbeat_periods) = 100;

    // Pg is running on tserver 0, so move the leader to tserver 1, so that we can break
    // connectivity to it.
    const size_t old_leader_idx = 1;
    RETURN_NOT_OK(StepDown(GetLeaderIdx(tablet_id), old_leader_idx, tablet_id));
    return old_leader_idx;
  }

  Result<std::vector<tablet::TabletPeerPtr>> DelayFollowers(
      const std::string& table_name, MonoDelta delay) {
    auto peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), table_name, ListPeersFilter::kNonLeaders));
    SCHECK_EQ(peers.size(), 2, IllegalState, "Expected 2 follower peers");
    for (auto& peer : peers) {
      VERIFY_RESULT(peer->GetRaftConsensus())->TEST_DelayUpdate(delay);
    }
    return peers;
  }

  Status SetFollowersPaused(const std::string& table_name, bool paused) {
    auto peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), table_name, ListPeersFilter::kNonLeaders));
    SCHECK_EQ(peers.size(), 2, IllegalState, "Expected 2 follower peers");
    for (auto& peer : peers) {
      VERIFY_RESULT(peer->GetRaftConsensus())->TEST_PauseUpdateConsensus(paused);
    }
    return Status::OK();
  }

  Status PauseFollowers(const std::string& table_name) {
    return SetFollowersPaused(table_name, /* paused */ true);
  }

  Status ResumeFollowers(const std::string& table_name) {
    return SetFollowersPaused(table_name, /* paused */ false);
  }

  void ResumeFollowersAndWait(const std::vector<tablet::TabletPeerPtr>& peers, MonoDelta delay) {
    ASSERT_FALSE(peers.empty());
    const auto& tablet_id = peers[0]->tablet_id();
    auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
    auto leader_consensus = ASSERT_RESULT(leader_peer->GetRaftConsensus());
    const auto leader_op_id =
        ASSERT_RESULT(leader_consensus->GetLastOpId(consensus::RECEIVED_OPID));

    for (auto& peer : peers) {
      ASSERT_RESULT(peer->GetRaftConsensus())->TEST_DelayUpdate(0s);
    }

    // Wait until each follower has caught up to the leader.
    const auto timeout = delay + MonoDelta::FromSeconds(5);
    ASSERT_OK(LoggedWaitFor(
        [leader_consensus, leader_op_id]() -> Result<bool> {
          return VERIFY_RESULT(leader_consensus->GetLastOpId(consensus::COMMITTED_OPID)) >=
                 leader_op_id;
        },
        timeout,
        Format(
            "Wait for leader to commit up to op id $0 after clearing follower delay",
            leader_op_id)));
  }

  // Sums WaitForAsyncWrite RPC arrivals across the tablet servers. The client sends this RPC
  // only after receiving an async_write_op_id ack in a response, so an arrival proves the ack
  // crossed the wire. Summing over the mini tservers only (not masters) excludes the masters'
  // own pipelined sys-catalog writes, whose WaitForAsyncWrite RPCs go to the master's embedded
  // tablet service.
  uint64_t GetWaitForAsyncWriteArrivalCount() {
    uint64_t total = 0;
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      total += cluster_->mini_tablet_server(i)
                   ->metric_entity()
                   .FindOrCreateMetric<Histogram>(
                       &METRIC_handler_latency_yb_tserver_TabletServerService_WaitForAsyncWrite)
                   ->underlying()
                   ->TotalCount();
    }
    return total;
  }

  void LeaderStepDownAfterWriteAckTest(bool perform_read);
  void LeaderStepDownBeforeWriteAckTest(bool use_pk);

  std::unique_ptr<pgwrapper::PGConn> conn_;
};

// Make sure async writes are performed for all transactional writes, and not for non-transactional
// writes.
TEST_F(YSqlAsyncWriteTest, SimpleCRUD) {
  google::SetVLOGLevel("write_query*", 2);
  auto pattern_count = StringWaiterLogSink("Performing Async write");
  constexpr auto create_table = "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT)";
  constexpr auto insert_query = "INSERT INTO $0 VALUES ($1, 'v$1')";
  constexpr auto update_query = "UPDATE $0 SET value = 'v$2' WHERE key = $1";
  constexpr auto delete_query = "DELETE FROM $0 WHERE key = $1";
  constexpr auto select_all_query = "SELECT * FROM $0";

  // Creating a table should result in a few async writes to pg catalog tables.
  ASSERT_OK(conn_->ExecuteFormat(create_table, kTableName));
  auto expected_event_count = pattern_count.GetEventCount();
  ASSERT_GT(expected_event_count, 0);

  int row_count = 0;
  // Non transactional writes should not result in async writes.
  ASSERT_OK(conn_->ExecuteFormat(insert_query, kTableName, row_count++));
  ASSERT_EQ(pattern_count.GetEventCount(), expected_event_count);

  // A SELECT statement should not result in async writes.
  auto tbl1_rows = ASSERT_RESULT(conn_->FetchAllAsString(Format(select_all_query, kTableName)));
  ASSERT_EQ(tbl1_rows, "0, v0");
  ASSERT_EQ(pattern_count.GetEventCount(), expected_event_count);

  // Inserting rows in a transaction should result in async writes.
  ASSERT_OK(conn_->Execute("BEGIN"));
  for (int i = 0; i < 10; ++i) {
    ASSERT_OK(conn_->ExecuteFormat(insert_query, kTableName, row_count++));
    expected_event_count++;
  }
  ASSERT_EQ(pattern_count.GetEventCount(), expected_event_count);
  ASSERT_OK(conn_->CommitTransaction());

  // Updating rows without a transaction should not result in async writes.
  ASSERT_OK(conn_->ExecuteFormat(update_query, kTableName, 0, 0));
  ASSERT_EQ(pattern_count.GetEventCount(), expected_event_count);

  // Updating rows in a transaction should result in async writes.
  ASSERT_OK(conn_->Execute("BEGIN"));
  for (int i = 0; i < row_count; ++i) {
    ASSERT_OK(conn_->ExecuteFormat(update_query, kTableName, i, i + 10));
    expected_event_count++;
  }
  ASSERT_EQ(pattern_count.GetEventCount(), expected_event_count);
  ASSERT_OK(conn_->CommitTransaction());

  // Deleting rows without a transaction should not result in async writes.
  ASSERT_OK(conn_->ExecuteFormat(delete_query, kTableName, 0));
  ASSERT_EQ(pattern_count.GetEventCount(), expected_event_count);

  // Deleting rows in a transaction should result in async writes.
  ASSERT_OK(conn_->Execute("BEGIN"));
  for (int i = 1; i < row_count; ++i) {
    ASSERT_OK(conn_->ExecuteFormat(delete_query, kTableName, i));
    expected_event_count++;
  }
  ASSERT_EQ(pattern_count.GetEventCount(), expected_event_count);
  ASSERT_OK(conn_->CommitTransaction());
}

// Make sure the transaction aborts if the leader steps down after the async write operation is
// acked to client, but before it is replicated to followers.
TEST_F(YSqlAsyncWriteTest, LeaderStepDownAfterWriteAck) {
  ASSERT_NO_FATALS(LeaderStepDownAfterWriteAckTest(/* perform_read */ false));
}

TEST_F(YSqlAsyncWriteTest, LeaderStepDownAfterWriteAckWithRead) {
  ASSERT_NO_FATALS(LeaderStepDownAfterWriteAckTest(/* perform_read */ true));
}

void YSqlAsyncWriteTest::LeaderStepDownAfterWriteAckTest(bool perform_read) {
  constexpr auto create_table =
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS";
  ASSERT_OK(conn_->ExecuteFormat(create_table, kTableName));

  auto tablet_id = ASSERT_RESULT(GetTabletId());

  const size_t old_leader_idx = ASSERT_RESULT(PrepareToBreakConnectivity(tablet_id));

  // Reject non-empty UpdateConsensus on followers so the entry can't replicate via a
  // racing heartbeat between queue_->AppendOperations and BreakConnectivityWithAll.
  std::vector<tablet::TabletPeerPtr> follower_peers;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    if (i == old_leader_idx) {
      continue;
    }
    auto peer = ASSERT_RESULT(GetTabletPeerOnTserver(i, tablet_id));
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNonEmpty);
    follower_peers.push_back(peer);
  }

  // Block the WriteOperation such that the WAL is not replicated.
  auto sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"LeaderStepDownAfterWriteAck::LeaderConnectivityBroken", "WriteQuery::AfterCallbackInvoke"},
  });
  sync_point->EnableProcessing();

  ASSERT_OK(conn_->Execute("BEGIN"));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'A')", kTableName));
  // Client has received the async write ack, but it is not yet replicated to followers.

  ASSERT_OK(BreakConnectivityWithAll(cluster_.get(), old_leader_idx));
  for (auto& peer : follower_peers) {
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNone);
  }
  TEST_SYNC_POINT("LeaderStepDownAfterWriteAck::LeaderConnectivityBroken");

  // Wait for a new leader to be elected.
  size_t new_leader_idx = ASSERT_RESULT(WaitForNewTabletLeader(tablet_id, old_leader_idx));

  // Bring back the old leader.
  ASSERT_OK(SetupConnectivityWithAll(cluster_.get(), old_leader_idx));

  auto get_row_count = [this]() -> Result<int64_t> {
    return conn_->FetchRow<int64_t>(
        Format("SELECT COUNT(*) FROM $0 WHERE key = 1", kTableName));
  };

  // We should not be able to perform further reads, or commit the transaction.
  if (perform_read) {
    // The async write was acked but never replicated. The read carries the pending async-write
    // OpId; the new leader cannot verify that OpId, so the read fails and pg aborts the txn.
    ASSERT_NOK(
        conn_->FetchRow<std::string>(Format("SELECT value FROM $0 WHERE key = 1", kTableName)));
    // COMMIT of a failed transaction internally performs a ROLLBACK in pg.
    ASSERT_OK(conn_->CommitTransaction());
  } else {
    ASSERT_NOK(conn_->CommitTransaction());
  }

  // Reset the connection and make sure the transaction was aborted.
  conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
  ASSERT_EQ(ASSERT_RESULT(get_row_count()), 0);

  // Go back to the old leader and make sure aborted data is not visible.
  ASSERT_OK(StepDown(new_leader_idx, old_leader_idx, tablet_id));

  ASSERT_EQ(ASSERT_RESULT(get_row_count()), 0);
}

// If the leader steps down before the async write operation is acked to client, make sure the
// client can retry on the new leader without errors.
TEST_F(YSqlAsyncWriteTest, LeaderStepDownBeforeWriteAck) {
  ASSERT_NO_FATALS(LeaderStepDownBeforeWriteAckTest(/* use_pk */ false));
}

TEST_F(YSqlAsyncWriteTest, LeaderStepDownBeforeWriteAckWithPK) {
  ASSERT_NO_FATALS(LeaderStepDownBeforeWriteAckTest(/* use_pk */ true));
}

void YSqlAsyncWriteTest::LeaderStepDownBeforeWriteAckTest(bool use_pk) {
  const auto create_table = Format(
      "CREATE TABLE $0 (key INT $1, value TEXT) SPLIT INTO 1 TABLETS", kTableName,
      use_pk ? "PRIMARY KEY" : "");
  ASSERT_OK(conn_->Execute(create_table));

  auto tablet_id = ASSERT_RESULT(GetTabletId());

  const size_t old_leader_idx = ASSERT_RESULT(PrepareToBreakConnectivity(tablet_id));

  auto sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"WriteQuery::BeforeCallbackInvoke",
       "LeaderStepDownBeforeWriteAck::LeaderConnectivityBroken1"},
      {"LeaderStepDownBeforeWriteAck::LeaderConnectivityBroken2",
       "WriteQuery::AfterCallbackInvoke"},
  });
  bool failed_write_once = false;
  sync_point->SetCallBack("WriteQuery::SetCallbackStatus", [&failed_write_once](void* data) {
    if (!failed_write_once) {
      failed_write_once = true;
      *static_cast<Status*>(data) = STATUS(Aborted, "Simulated failure after DoReplicated");
    }
  });

  sync_point->EnableProcessing();

  ASSERT_OK(conn_->Execute("BEGIN"));
  TestThreadHolder thread_holder;
  thread_holder.AddThread(
      [this]() { ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'A')", kTableName)); });

  TEST_SYNC_POINT("LeaderStepDownBeforeWriteAck::LeaderConnectivityBroken1");
  ASSERT_OK(BreakConnectivityWithAll(cluster_.get(), old_leader_idx));
  TEST_SYNC_POINT("LeaderStepDownBeforeWriteAck::LeaderConnectivityBroken2");

  size_t new_leader_idx = ASSERT_RESULT(WaitForNewTabletLeader(tablet_id, old_leader_idx));

  // Client should have retried the write operation on the new leader.
  thread_holder.JoinAll();
  ASSERT_OK(SetupConnectivityWithAll(cluster_.get(), old_leader_idx));

  const auto expected_value = "1, A";
  ASSERT_OK(ValidateData(expected_value, tablet_id));

  ASSERT_OK(conn_->CommitTransaction());
  ASSERT_OK(ValidateData(expected_value, tablet_id));

  // Go back to the old leader and make sure data is still visible.
  ASSERT_OK(StepDown(new_leader_idx, old_leader_idx, tablet_id));

  ASSERT_OK(ValidateData(expected_value, tablet_id));

  // Even after a full flush of the WAL, intents and regular DB the same data should be visible.
  ASSERT_OK(ResolveAndFlushTablet(old_leader_idx, tablet_id));

  ASSERT_OK(ValidateData(expected_value, tablet_id));
}

// Make sure there cannot be any situation where a intent that was written but not replicated is
// visible, even if the transaction is committed.
// To simulate this we have Client A performing a non-idempotent write, such that the intent written
// to the original leader is different from the intent written to the new leader.
// This is done using a statement block with a unique violation error, that is handled with a
// different write, and using another Client B to insert the dup key at the right moment.
// In the diagram below, Peer A gets both a (1, A) and (1, B) intent, with both transactions
// committing.
// This works because pg picks the read time for the writes causing a write conflict error that
// aborts the sub transaction.
//
// Time flows downwards, with important marked with (T1), (T2), etc.
//
// +--------+               +--------+
// |Client A|               |Client B|             Peer A                 Peer B            Peer C
// +----+---+               +----+---+                |                     |                 |
//      v                        |                    |                     |                 |
//    BEGIN                      |                (Leader)             (Follower)        (Follower)
//      |                        |                    |                     |                 |
//      |                        |                    |                     |                 |
//      |                        |                    |                     |                 |
//      v                        |                    |                     |                 |
// INSERT (1, A);                |                    |                     |                 |
//   WHEN unique_violation -(T1)-+--INSERT(1,A)------>|                     |                 |
//   INSERT (2, A);              |                    |                     |                 |
//           | ^ |^ |            |           Insert to intentsDB(1,A)       |                 |
//       |   | | || |            |     X<---Ack-------+                     |                 |
//       |   | | || |            |                    +-WAL(1,A)->X         |                 |
//       |   | | || |            |                    +-WAL(1,A)------------+----->X          |
//       |   | | || |            |                    |                     |                 |
//       |   | | || |            |                    |                     |                 |
//       |   | | || |            |                    |             (T2)(Leader)              |
//       |   | | || |            v                    |                     |                 |
//       |   | | || |          BEGIN                  |                     |                 |
//       |   | | || |            |                    |                     |                 |
//       |   | | || |            |                    |                     |                 |
//       |   | | || |            v                    |                     |                 |
//       |   | | || | INSERT (1, B); --(T3)-----------+----INSERT(1,B)----->|                 |
//       |   | | || |            |                    |                     |                 |
//       |   | | || |            |                    |           Insert to intentsDB(1,B)    |
//       |   | | || |            | <---(T4)-----------+---Ack---------------+                 |
//       |   | | || |            v                    |                     +-----WAL(1,B)--->|
//       |   | | || |         COMMIT                  |                     |                 |
//       |   | | || |          (T5)                   |                     |                 |
//       |   | | || |                                 |                     |                 |
//       |   | | || |                                 |                     |                 |
//       |   | | || |                                 |                     |                 |
//       |   | | || +-----(T6)---------INSERT(1,A)----+-------------------->|                 |
//       |   | | ||                                   |                     |                 |
//       |   | | |+(T7)-conflicts with committed-transaction----------------+                 |
//       |   | | |                                    |                     |                 |
//       |   | | |                                    |                     |                 |
//       |   | | +--------(T8)---------INSERT(1,A)----+-------------------->|                 |
//       |   | |                                      |                     |                 |
//       |   | +-----(T9)-unique_violation------------+---------------------+                 |
//       |   |                                        |                     |                 |
//       |   |                                        |                     |                 |
//       |   +----(T1ad0)-----------INSERT(2,A)---------+----------> Insert to intentsDB(2,A)   |
//       |                                            |                     +-------WAL(2,A)->|
//       |<--(T11)--------Ack-------------------------+---------------------+                 |
//       |                                            |                     |                 |
//       v                                       (Follower)                 |                 |
//    COMMIT                                          |<------WAL(1,B)------+                 |
//    (T12)                                           |                     |                 |
//                                                    |<------WAL(2,A)------+                 |
//                                                    |                     |                 |
//                                                    |                     |                 |
//                                           (T13)(Leader)             (Follower)             |
//                                                    |                     |                 |
//                                                    |                     |                 |
//                                                  (T14)                   |                 |
//                                             Intent (1,A) should
//                                             not be visible
//
TEST_F(YSqlAsyncWriteTest, FailedInsertOnConflict) {
  constexpr auto create_table =
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS";
  ASSERT_OK(conn_->ExecuteFormat(create_table, kTableName));

  auto tablet_id = ASSERT_RESULT(GetTabletId());

  const size_t old_leader_idx = ASSERT_RESULT(PrepareToBreakConnectivity(tablet_id));

  auto sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency(
      {{"WriteQuery::BeforeCallbackInvoke", "FailedInsertOnConflict::LeaderConnectivityBroken1"},
       {"FailedInsertOnConflict::LeaderConnectivityBroken2", "WriteQuery::AfterCallbackInvoke"},
       {"FailedInsertOnConflict::Client2TxnCompleted",
        "FailedInsertOnConflict::SecondWriteAttempt"}});

  bool is_first_write = true;
  sync_point->SetCallBack("WriteQuery::SetCallbackStatus", [&is_first_write](void* data) {
    if (is_first_write) {
      is_first_write = false;
      // Simulate a network failure from the first write so that the client can retry it.
      *static_cast<Status*>(data) = STATUS(Aborted, "Simulated failure after DoReplicated");
    }
  });

  int async_write_attempt_num = 0;
  sync_point->SetCallBack(
      "TabletServiceImpl::PerformWrite", [&async_write_attempt_num](void* data) {
        async_write_attempt_num++;
        auto req = static_cast<tserver::WriteRequestMsg*>(data);
        if (!req->use_async_write()) {
          return;
        }
        if (async_write_attempt_num == 2) {
          // Block the retry attempt so that the other client can insert (1,'B').
          TEST_SYNC_POINT("FailedInsertOnConflict::SecondWriteAttempt");
        }
      });

  sync_point->EnableProcessing();

  TestThreadHolder thread_holder;
  auto se = ScopeExit([&sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn_->Execute("BEGIN TRANSACTION"));
  thread_holder.AddThread([this]() {
    ASSERT_OK(conn_->ExecuteFormat(
        R"(DO $$$$
BEGIN
  INSERT INTO $0 VALUES (1, 'A');
EXCEPTION
  WHEN unique_violation THEN
    INSERT INTO $0 VALUES (2, 'A');
END $$$$;)",
        kTableName));
  });

  TEST_SYNC_POINT("FailedInsertOnConflict::LeaderConnectivityBroken1");
  ASSERT_OK(BreakConnectivityWithAll(cluster_.get(), old_leader_idx));
  TEST_SYNC_POINT("FailedInsertOnConflict::LeaderConnectivityBroken2");

  size_t new_leader_idx;
  ASSERT_OK(LoggedWaitFor(
      [this, tablet_id, &old_leader_idx, &new_leader_idx]() -> Result<bool> {
        for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
          if (i == old_leader_idx) {
            // Old leader is stuck so cannot be queried.
            continue;
          }
          if (cluster_->mini_tablet_server(i)->server()->LeaderAndReady(tablet_id)) {
            new_leader_idx = i;
            return true;
          }
        }
        return false;
      },
      30s, "Leader election"));

  ASSERT_OK(SetupConnectivityWithAll(cluster_.get(), old_leader_idx));

  auto conn2 = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
  ASSERT_OK(conn2->Execute("BEGIN TRANSACTION"));
  ASSERT_OK(conn2->ExecuteFormat("INSERT INTO $0 VALUES (1, 'B')", kTableName));
  ASSERT_OK(conn2->CommitTransaction());

  TEST_SYNC_POINT("FailedInsertOnConflict::Client2TxnCompleted");
  thread_holder.JoinAll();

  ASSERT_OK(conn_->CommitTransaction());

  // Validity checks.
  const auto expected_value = "1, B; 2, A";
  ASSERT_OK(ValidateData(expected_value, tablet_id));

  // Make sure the same data is visible on the old leader.
  ASSERT_OK(StepDown(new_leader_idx, old_leader_idx, tablet_id));

  ASSERT_OK(ValidateData(expected_value, tablet_id));

  // Even after a full flush of the WAL, intents and regular DB the (1, 'A') intent should not
  // become visible.
  ASSERT_OK(ResolveAndFlushTablet(old_leader_idx, tablet_id));

  ASSERT_OK(ValidateData(expected_value, tablet_id));
}

// Make sure async writes and subsequent reads are not blocked by follower network delay.
TEST_F(YSqlAsyncWriteTest, ReadsNotBlockedByAsyncWrites) {
  ASSERT_OK(
      conn_->ExecuteFormat("CREATE TABLE $0 (a INT PRIMARY KEY) SPLIT INTO 1 TABLETS", kTableName));

  const auto delay_duration = MonoDelta(30s);
  auto follower_peers = ASSERT_RESULT(DelayFollowers(kTableName, delay_duration));

  ASSERT_OK(conn_->Execute("BEGIN TRANSACTION"));

  // Async write should not be blocked by the delay.
  const auto insert_start_time = CoarseMonoClock::now();
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName));
  auto now = CoarseMonoClock::now();
  LOG(INFO) << "Insert time: " << MonoDelta(now - insert_start_time);
  ASSERT_LT(now - insert_start_time, 5s);

  // Read should be unblocked by the delay.
  const auto read_start_time = CoarseMonoClock::now();
  auto result = ASSERT_RESULT(conn_->FetchAllAsString(Format("SELECT * FROM $0", kTableName)));
  now = CoarseMonoClock::now();
  LOG(INFO) << "Read time: " << MonoDelta(now - read_start_time);
  ASSERT_LT(now - read_start_time, 5s);
  ASSERT_EQ(result, "1");

  ResumeFollowersAndWait(follower_peers, delay_duration);

  ASSERT_OK(conn_->CommitTransaction());

  result = ASSERT_RESULT(conn_->FetchAllAsString(Format("SELECT * FROM $0", kTableName)));
  ASSERT_EQ(result, "1");
}

TEST_F(YSqlAsyncWriteTest, SelectForUpdateAsyncWrite) {
  google::SetVLOGLevel("write_query*", 2);
  // Internal async writes are triggered by the read path's write query to lock rows.
  auto internal_async_write_count = StringWaiterLogSink("Performing Async write: Internal request");

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'a'), (2, 'b'), (3, 'c')", kTableName));

  auto initial_count = internal_async_write_count.GetEventCount();

  const auto delay_duration = MonoDelta(30s);
  auto follower_peers = ASSERT_RESULT(DelayFollowers(kTableName, delay_duration));

  ASSERT_OK(conn_->Execute("BEGIN"));
  // SELECT FOR UPDATE acquires row-level locks, which go through the read path's write query.
  const auto select_for_update_start = CoarseMonoClock::now();
  auto rows = ASSERT_RESULT(
      conn_->FetchAllAsString(Format("SELECT * FROM $0 ORDER BY key FOR UPDATE", kTableName)));
  // SELECT FOR UPDATE should complete quickly despite follower delay.
  auto elapsed = CoarseMonoClock::now() - select_for_update_start;
  LOG(INFO) << "SELECT FOR UPDATE time: " << MonoDelta(elapsed);
  ASSERT_LT(elapsed, 5s);
  ASSERT_EQ(rows, "1, a; 2, b; 3, c");

  // Verify that the async write was triggered.
  auto count_after = internal_async_write_count.GetEventCount();
  LOG(INFO) << "Counts after SELECT FOR UPDATE: " << count_after << " - " << initial_count;
  ASSERT_EQ(count_after, initial_count + 1);

  const auto read_start = CoarseMonoClock::now();
  rows =
      ASSERT_RESULT(conn_->FetchAllAsString(Format("SELECT * FROM $0 ORDER BY key", kTableName)));
  // Follow-up read should also be fast.
  elapsed = CoarseMonoClock::now() - read_start;
  LOG(INFO) << "Follow-up read time: " << MonoDelta(elapsed);
  ASSERT_LT(elapsed, 5s);
  ASSERT_EQ(rows, "1, a; 2, b; 3, c");

  ResumeFollowersAndWait(follower_peers, delay_duration);

  ASSERT_OK(conn_->ExecuteFormat("UPDATE $0 SET value = 'x' WHERE key = 2", kTableName));
  ASSERT_OK(conn_->CommitTransaction());

  rows =
      ASSERT_RESULT(conn_->FetchAllAsString(Format("SELECT * FROM $0 ORDER BY key", kTableName)));
  ASSERT_EQ(rows, "1, a; 2, x; 3, c");
}

// The ack (async_write_op_id) of the read path's internal lock write (SELECT ... FOR UPDATE,
// FK checks, serializable reads) must reach the client, which gates COMMIT on the lock write's
// quorum fate.
//
// Ensures the ack arrived by checking for a WaitForAsyncWrite RPC arrival at a tserver (the
// RPC's handler-latency metric summed over the mini tservers), since the client sends that RPC
// only after receiving an ack. The control phase proves the counting machinery works via the
// write path; the main phase asserts the same arrival for a locking read.
TEST_F(YSqlAsyncWriteTest, SelectForUpdateAckReachesClient) {
  google::SetVLOGLevel("write_query*", 2);
  // Trigger guard: the server routed the read path's lock write through the pipelined path and
  // produced the OpId into its (scratch) response.
  auto internal_async_write_count = StringWaiterLogSink("Performing Async write: Internal request");

  // Diagnostics only (the assertions key on the metric): log which OpIds get verified, so an
  // unexpected bug-phase arrival is traceable.
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("TabletServiceImpl::WaitForAsyncWrite::Verified", [](void* arg) {
    LOG(INFO) << "WaitForAsyncWrite verified for op id " << *static_cast<OpId*>(arg);
  });
  sync_point->EnableProcessing();
  auto se = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'a'), (2, 'b'), (3, 'c')", kTableName));

  // Control phase: a pipelined WRITE's ack reaches the client, which then sends WaitForAsyncWrite.
  // Ensures that the arrival counting works without SELECT ... FOR UPDATE.
  auto arrivals_before = GetWaitForAsyncWriteArrivalCount();
  const auto control_start = CoarseMonoClock::now();
  ASSERT_OK(conn_->Execute("BEGIN"));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (4, 'd')", kTableName));
  ASSERT_OK(WaitFor(
      [this, arrivals_before] { return GetWaitForAsyncWriteArrivalCount() > arrivals_before; },
      10s * kTimeMultiplier, "control phase: WaitForAsyncWrite arrival for a pipelined INSERT"));
  const auto control_latency = CoarseMonoClock::now() - control_start;
  LOG(INFO) << "Control-phase INSERT-to-arrival latency: " << MonoDelta(control_latency);
  ASSERT_OK(conn_->CommitTransaction());
  // COMMIT returns only after every recorded async write is verified, so no control-phase
  // arrival can leak into the main phase's counted window below.

  // Main phase: the same arrival for the internal lock write behind SELECT ... FOR UPDATE.
  // The transaction contains ONLY the locking read, so any arrival in the counted window must
  // be the lock write's.
  const auto internal_count_before = internal_async_write_count.GetEventCount();
  arrivals_before = GetWaitForAsyncWriteArrivalCount();

  ASSERT_OK(conn_->Execute("BEGIN"));
  auto rows = ASSERT_RESULT(
      conn_->FetchAllAsString(Format("SELECT * FROM $0 ORDER BY key FOR UPDATE", kTableName)));
  ASSERT_EQ(rows, "1, a; 2, b; 3, c; 4, d");

  // Trigger guard: if this fails, the pipelined read path never engaged and the test is vacuous.
  ASSERT_EQ(internal_async_write_count.GetEventCount(), internal_count_before + 1)
      << "Trigger guard failed: the locking read did not engage the pipelined write path.";

  // Give the ack a window at least 10x the control phase's observed latency.
  auto bug_window = MonoDelta(10s * kTimeMultiplier);
  bug_window = std::max(bug_window, MonoDelta(control_latency) * 10);
  const auto arrival_status = WaitFor(
      [this, arrivals_before] { return GetWaitForAsyncWriteArrivalCount() > arrivals_before; },
      bug_window, "main phase: WaitForAsyncWrite arrival for the locking read's lock write");

  // The COMMIT is gated on the lock write, but the machine-checked signal is the RPC arrival
  // above (commit latency would be flaky to assert on).
  ASSERT_OK(conn_->CommitTransaction());

  ASSERT_TRUE(arrival_status.ok())
      << "READ-PATH ACK LOST: the lock write's async_write_op_id never reached the client "
         "(wiped by ReadQuery::Complete()'s resp_->Clear() before the read response was "
         "serialized), so the client never tracked the lock write and COMMIT was not gated on "
         "its quorum fate.";
}

// A committed transaction's FOR UPDATE guarantee must hold across a single tserver crash in an
// RF3 cluster. Two accounts, each in its own single-tablet table, both starting at balance 100;
// a withdrawal of 150 is allowed only if the combined balance stays >= 0, so at most one of two
// concurrent withdrawals may commit:
//
//   conn1: FOR UPDATE-locks account Y (healthy tablet), then account X. X's lock write is kept
//          off quorum (X's followers reject appends), so it exists only on X's leader.
//   crash: X's tablet leader is shut down, destroying the lock op.
//   conn2: reads both accounts (total=200, so withdrawing 150 is justified) and withdraws 150
//          from account X.
//   conn1: withdraws 150 from account Y and COMMITs.
//
// conn1's COMMIT must fail: its client holds X's lock-write OpId, whose verification fails
// after the leader change. The two-table split keeps conn1's txn status record and metadata on
// healthy quorums, so the crash destroys nothing but the lock itself and conn1 cannot be
// aborted for an unrelated reason.
TEST_F(YSqlAsyncWriteTest, SelectForUpdateHoldsAcrossLeaderCrash) {
  google::SetVLOGLevel("write_query*", 2);
  auto internal_async_write_count = StringWaiterLogSink("Performing Async write: Internal request");

  constexpr auto kAcctX = "acct_x";
  constexpr auto kAcctY = "acct_y";
  for (const auto* table : {kAcctX, kAcctY}) {
    ASSERT_OK(conn_->ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, balance INT) SPLIT INTO 1 TABLETS", table));
  }
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 100)", kAcctX));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (2, 100)", kAcctY));

  auto tablet_x = ASSERT_RESULT(GetTabletId(kAcctX));
  // Wait for Y's leadership to settle so MoveLeadersOffTserver sees it.
  ASSERT_RESULT(GetTabletId(kAcctY));
  // Moves X's leader off the tserver hosting postgres so it can be crashed.
  const size_t old_leader_idx = ASSERT_RESULT(PrepareToBreakConnectivity(tablet_x));
  // Keep Y's tablet and the transaction status tablets clear of the crash.
  ASSERT_OK(MoveLeadersOffTserver(old_leader_idx, tablet_x));

  // conn1: lock account Y first, while the whole cluster is healthy. This creates the
  // transaction (status record) and writes conn1's txn metadata on Y's tablet, all
  // quorum-replicated. Give the status record a couple of heartbeat periods to settle so the
  // txn itself demonstrably survives the crash below.
  ASSERT_OK(conn_->Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  auto rows = ASSERT_RESULT(
      conn_->FetchAllAsString(Format("SELECT * FROM $0 FOR UPDATE", kAcctY)));
  ASSERT_EQ(rows, "2, 100");
  SleepFor(2s * kTimeMultiplier);

  // Make X's followers reject non-empty appends so the X lock write cannot land on ANY follower
  // before the crash, not even via a racing heartbeat. (TEST_DelayUpdate is not enough: it only
  // delays the follower's response, after the op was already appended to its local log.)
  std::vector<tablet::TabletPeerPtr> follower_peers;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    if (i == old_leader_idx) {
      continue;
    }
    auto peer = ASSERT_RESULT(GetTabletPeerOnTserver(i, tablet_x));
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNonEmpty);
    follower_peers.push_back(peer);
  }

  const auto internal_count_before = internal_async_write_count.GetEventCount();

  // conn1: lock account X. Total = 200 >= 150: the withdrawal below is justified.
  rows = ASSERT_RESULT(
      conn_->FetchAllAsString(Format("SELECT * FROM $0 FOR UPDATE", kAcctX)));
  ASSERT_EQ(rows, "1, 100");

  // Trigger guard 1: the X lock write went through the pipelined read path (otherwise the FOR
  // UPDATE above would still be blocked waiting for quorum against the rejecting followers).
  ASSERT_EQ(internal_async_write_count.GetEventCount(), internal_count_before + 1)
      << "Trigger guard failed: the locking read did not engage the pipelined write path.";

  // Trigger guard 2: the X lock op exists only on X's leader - not quorum-committed, and not
  // present on any follower - so the crash below genuinely destroys it.
  {
    auto leader_peer = ASSERT_RESULT(GetTabletPeerOnTserver(old_leader_idx, tablet_x));
    auto leader_consensus = ASSERT_RESULT(leader_peer->GetRaftConsensus());
    const auto received = ASSERT_RESULT(leader_consensus->GetLastOpId(consensus::RECEIVED_OPID));
    const auto committed = ASSERT_RESULT(leader_consensus->GetLastOpId(consensus::COMMITTED_OPID));
    ASSERT_GT(received.index, committed.index)
        << "Trigger guard failed: the lock op already reached quorum; the crash below would "
           "not destroy it.";
    for (auto& peer : follower_peers) {
      const auto follower_received =
          ASSERT_RESULT(ASSERT_RESULT(peer->GetRaftConsensus())->GetLastOpId(
              consensus::RECEIVED_OPID));
      ASSERT_LT(follower_received.index, received.index)
          << "Trigger guard failed: follower " << peer->permanent_uuid()
          << " received the lock op despite reject mode; the crash below would not destroy it.";
    }
  }

  // Crash X's leader. The un-replicated lock op (conn1's lock on account X) dies with it.
  cluster_->mini_tablet_server(old_leader_idx)->Shutdown();

  // Let the surviving followers accept appends again so they can elect a new leader.
  for (auto& peer : follower_peers) {
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNone);
  }
  const size_t new_leader_idx = ASSERT_RESULT(WaitForNewTabletLeader(tablet_x, old_leader_idx));
  LOG(INFO) << "New leader of X's tablet after crash: tserver index " << new_leader_idx;

  // conn2 withdraws from account X, writing to the row conn1's destroyed FOR UPDATE lock covered.
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn2.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  const auto x_balance = ASSERT_RESULT(conn2.FetchRow<int32_t>(
      Format("SELECT balance FROM $0 WHERE id = 1", kAcctX)));
  const auto y_balance = ASSERT_RESULT(conn2.FetchRow<int32_t>(
      Format("SELECT balance FROM $0 WHERE id = 2", kAcctY)));
  ASSERT_EQ(x_balance + y_balance, 200);  // conn2's withdrawal is justified too.
  ASSERT_OK(conn2.ExecuteFormat("UPDATE $0 SET balance = balance - 150 WHERE id = 1", kAcctX));
  ASSERT_OK(conn2.CommitTransaction());

  // conn1 completes its withdrawal (on the untouched, healthy Y tablet) and commits. The COMMIT
  // must FAIL: conn1's lock on X is lost and unverified.
  auto update_status =
      conn_->ExecuteFormat("UPDATE $0 SET balance = balance - 150 WHERE id = 2", kAcctY);
  auto commit_status = update_status.ok() ? conn_->CommitTransaction() : update_status;

  const auto final_x = ASSERT_RESULT(conn2.FetchRow<int32_t>(
      Format("SELECT balance FROM $0 WHERE id = 1", kAcctX)));
  const auto final_y = ASSERT_RESULT(conn2.FetchRow<int32_t>(
      Format("SELECT balance FROM $0 WHERE id = 2", kAcctY)));
  LOG(INFO) << "conn1 update: " << update_status << ", commit: " << commit_status
            << ", final balances: X=" << final_x << " Y=" << final_y
            << " total=" << final_x + final_y;

  ASSERT_FALSE(update_status.ok() && commit_status.ok())
      << "LOCK GUARANTEE VOID: both lock-guarded withdrawals committed after a single tserver "
         "crash; final balances X=" << final_x << " Y=" << final_y << " (total "
      << final_x + final_y << ") break the total >= 0 invariant the FOR UPDATE locks existed "
         "to protect. conn1's COMMIT was never gated on its lock write because the read-path "
         "async_write_op_id ack was lost.";
}

// The SERIALIZABLE flavor of SelectForUpdateHoldsAcrossLeaderCrash: plain SELECTs write read
// intents through the same pipelined lock-write path FOR UPDATE uses. Same two accounts and
// withdrawal rule:
//
//   conn1 (SERIALIZABLE): reads both balances with plain SELECTs; the read intents on X's
//                         tablet are kept off quorum.
//   crash: X's tablet leader is shut down, destroying conn1's read locks on X.
//   conn2 (SERIALIZABLE): reads both balances, withdraws 150 from account X, COMMITs.
//   conn1: withdraws 150 from account Y and COMMITs.
//
// conn1's COMMIT must fail: committing both transactions would be a write-skew with no serial
// order (conn1 read X before conn2's withdrawal; conn2 read Y before conn1's).
TEST_F(YSqlAsyncWriteTest, SerializableIsolationHoldsAcrossLeaderCrash) {
  google::SetVLOGLevel("write_query*", 2);
  auto internal_async_write_count = StringWaiterLogSink("Performing Async write: Internal request");

  constexpr auto kAcctX = "acct_x";
  constexpr auto kAcctY = "acct_y";
  for (const auto* table : {kAcctX, kAcctY}) {
    ASSERT_OK(conn_->ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, balance INT) SPLIT INTO 1 TABLETS", table));
  }
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 100)", kAcctX));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (2, 100)", kAcctY));

  auto tablet_x = ASSERT_RESULT(GetTabletId(kAcctX));
  // Wait for Y's leadership to settle so MoveLeadersOffTserver sees it.
  ASSERT_RESULT(GetTabletId(kAcctY));
  const size_t old_leader_idx = ASSERT_RESULT(PrepareToBreakConnectivity(tablet_x));
  // Keep Y's tablet and the transaction status tablets clear of the crash.
  ASSERT_OK(MoveLeadersOffTserver(old_leader_idx, tablet_x));

  // conn1: read account Y first, while the whole cluster is healthy (creates the transaction and
  // its Y-side read intents on quorum-safe ground; see SelectForUpdateHoldsAcrossLeaderCrash for
  // why). The read is a plain SELECT: SERIALIZABLE turns it into a pipelined lock write.
  ASSERT_OK(conn_->Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  auto y_balance = ASSERT_RESULT(
      conn_->FetchRow<int32_t>(Format("SELECT balance FROM $0 WHERE id = 2", kAcctY)));
  ASSERT_EQ(y_balance, 100);
  SleepFor(2s * kTimeMultiplier);

  std::vector<tablet::TabletPeerPtr> follower_peers;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    if (i == old_leader_idx) {
      continue;
    }
    auto peer = ASSERT_RESULT(GetTabletPeerOnTserver(i, tablet_x));
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNonEmpty);
    follower_peers.push_back(peer);
  }

  const auto internal_count_before = internal_async_write_count.GetEventCount();

  // conn1: read account X. Total = 200 >= 150: the withdrawal below is justified.
  auto x_balance = ASSERT_RESULT(
      conn_->FetchRow<int32_t>(Format("SELECT balance FROM $0 WHERE id = 1", kAcctX)));
  ASSERT_EQ(x_balance, 100);

  // Trigger guard 1: the plain serializable SELECT engaged the pipelined lock-write path.
  ASSERT_EQ(internal_async_write_count.GetEventCount(), internal_count_before + 1)
      << "Trigger guard failed: the serializable read did not engage the pipelined write path.";

  // Trigger guard 2: conn1's X read-intent op exists only on X's leader.
  {
    auto leader_peer = ASSERT_RESULT(GetTabletPeerOnTserver(old_leader_idx, tablet_x));
    auto leader_consensus = ASSERT_RESULT(leader_peer->GetRaftConsensus());
    const auto received = ASSERT_RESULT(leader_consensus->GetLastOpId(consensus::RECEIVED_OPID));
    const auto committed = ASSERT_RESULT(leader_consensus->GetLastOpId(consensus::COMMITTED_OPID));
    ASSERT_GT(received.index, committed.index)
        << "Trigger guard failed: the read-intent op already reached quorum; the crash below "
           "would not destroy it.";
    for (auto& peer : follower_peers) {
      const auto follower_received =
          ASSERT_RESULT(ASSERT_RESULT(peer->GetRaftConsensus())->GetLastOpId(
              consensus::RECEIVED_OPID));
      ASSERT_LT(follower_received.index, received.index)
          << "Trigger guard failed: follower " << peer->permanent_uuid()
          << " received the read-intent op despite reject mode.";
    }
  }

  // Crash X's leader. conn1's read locks on account X die with it.
  cluster_->mini_tablet_server(old_leader_idx)->Shutdown();
  for (auto& peer : follower_peers) {
    ASSERT_RESULT(peer->GetRaftConsensus())->TEST_RejectMode(consensus::RejectMode::kNone);
  }
  const size_t new_leader_idx = ASSERT_RESULT(WaitForNewTabletLeader(tablet_x, old_leader_idx));
  LOG(INFO) << "New leader of X's tablet after crash: tserver index " << new_leader_idx;

  // conn2: the same guarded withdrawal, from account X. Its read of Y takes a shared read lock
  // (compatible with conn1's read lock on Y); its write to X is what conn1's dead read lock on X
  // should have serialized.
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn2.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  const auto conn2_x = ASSERT_RESULT(
      conn2.FetchRow<int32_t>(Format("SELECT balance FROM $0 WHERE id = 1", kAcctX)));
  const auto conn2_y = ASSERT_RESULT(
      conn2.FetchRow<int32_t>(Format("SELECT balance FROM $0 WHERE id = 2", kAcctY)));
  ASSERT_EQ(conn2_x + conn2_y, 200);  // conn2's withdrawal is justified too.
  ASSERT_OK(conn2.ExecuteFormat("UPDATE $0 SET balance = balance - 150 WHERE id = 1", kAcctX));
  ASSERT_OK(conn2.CommitTransaction());

  // Wait out the cleanup of conn2's committed read intents on Y. Residual committed intents can
  // transiently conflict with conn1's write below; that residue is incidental (it disappears on
  // apply), not a serializability guard - the designed guard is conn1's LIVE read lock on X,
  // which the crash destroyed.
  SleepFor(3s * kTimeMultiplier);

  // conn1 completes its withdrawal and commits. The COMMIT must FAIL: conn1's read locks on X
  // are lost and unverified.
  auto update_status =
      conn_->ExecuteFormat("UPDATE $0 SET balance = balance - 150 WHERE id = 2", kAcctY);
  auto commit_status = update_status.ok() ? conn_->CommitTransaction() : update_status;

  const auto final_x = ASSERT_RESULT(conn2.FetchRow<int32_t>(
      Format("SELECT balance FROM $0 WHERE id = 1", kAcctX)));
  const auto final_y = ASSERT_RESULT(conn2.FetchRow<int32_t>(
      Format("SELECT balance FROM $0 WHERE id = 2", kAcctY)));
  LOG(INFO) << "conn1 update: " << update_status << ", commit: " << commit_status
            << ", final balances: X=" << final_x << " Y=" << final_y
            << " total=" << final_x + final_y;

  ASSERT_FALSE(update_status.ok() && commit_status.ok())
      << "SERIALIZABILITY VIOLATED: two SERIALIZABLE transactions committed a write-skew after "
         "a single tserver crash; final balances X=" << final_x << " Y=" << final_y << " (total "
      << final_x + final_y << ") admit no serial order (conn1 read X=100 before conn2's "
         "withdrawal; conn2 read Y=100 before conn1's). conn1's COMMIT was never gated on its "
         "X read-lock write because the read-path async_write_op_id ack was lost.";
}

TEST_F(YSqlAsyncWriteTest, ForeignKeyAsyncWrite) {
  google::SetVLOGLevel("write_query*", 2);
  // Internal async writes are triggered by the read path's write query to lock rows.
  auto async_write_count = StringWaiterLogSink("Performing Async write: Internal request");

  ASSERT_OK(
      conn_->Execute("CREATE TABLE parent(id INT PRIMARY KEY, name TEXT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn_->Execute(
      "CREATE TABLE child(id INT PRIMARY KEY, parent_id INT REFERENCES parent(id)) "
      "SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn_->Execute("INSERT INTO parent VALUES (1, 'p1'), (2, 'p2')"));

  auto initial_count = async_write_count.GetEventCount();

  ASSERT_OK(conn_->Execute("BEGIN"));
  // Foreign key insert triggers a lock on the parent row to prevent concurrent deletion.
  ASSERT_OK(conn_->Execute("INSERT INTO child VALUES (10, 1)"));
  ASSERT_OK(conn_->Execute("INSERT INTO child VALUES (20, 2)"));

  auto count_after = async_write_count.GetEventCount();
  LOG(INFO) << "Counts after FOREIGN KEY INSERT: " << count_after << " - " << initial_count;
  ASSERT_EQ(count_after, initial_count + 2);  // Two async writes for the two INSERTs.

  ASSERT_OK(conn_->CommitTransaction());

  auto parent_rows = ASSERT_RESULT(conn_->FetchAllAsString("SELECT * FROM parent ORDER BY id"));
  ASSERT_EQ(parent_rows, "1, p1; 2, p2");
  auto child_rows = ASSERT_RESULT(conn_->FetchAllAsString("SELECT * FROM child ORDER BY id"));
  ASSERT_EQ(child_rows, "10, 1; 20, 2");
}

// When async write intents are flushed before Raft commit, the SST frontier has a stale op_id
// (skip_opid_update=true). After commit, UpdateOpIdForOperation sets the correct op_id on the
// new (empty) memtable, but that update may not get flushed. If the tserver restarts at this point,
// then it will replay from the stale op_id.
// This test validates this scenario and ensures that write_ids continue advancing on the replay.
TEST_F(YSqlAsyncWriteTest, RestartAfterFlushInMiddleOfTransaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_flush_rocksdb_on_shutdown) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_failure_max_missed_heartbeat_periods) = 100;
  google::SetVLOGLevel("running_transaction", 4);

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));
  auto tablet_id = ASSERT_RESULT(GetTabletId());
  const size_t leader_idx = ASSERT_RESULT(PrepareToBreakConnectivity(tablet_id));

  // Delay followers so that we delay the Raft commit of the async writes.
  const auto delay_duration = MonoDelta(30s);
  auto follower_peers = ASSERT_RESULT(DelayFollowers(kTableName, delay_duration));

  // With packed rows each row uses 1 write_id, without packed rows each uses 2.
  const int writes_per_row = FLAGS_ysql_enable_packed_row ? 1 : 2;
  const int initial_write_id = 5 * writes_per_row;

  RegexWaiterLogSink initial_batch_replicated_log_sink(
      Format(".*BatchReplicated.*next_write_id: $0.*", initial_write_id));

  ASSERT_OK(conn_->Execute("BEGIN TRANSACTION"));
  // Use generate_series to increment write_id counter.
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT g, 'v' || g FROM generate_series(0, 4) g", kTableName));

  // Flush the intents DB. Intent data moves to SST; a new empty memtable is created.
  // The SST frontier has a stale op_id because the async writes used skip_opid_update=true.
  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
  auto leader_tablet = ASSERT_RESULT(leader_peer->shared_tablet());
  ASSERT_OK(leader_tablet->Flush(
      tablet::FlushMode::kSync, tablet::FlushFlags::kIntents, rocksdb::FlushReason::kTestOnly));

  // Resume followers so Raft entries commit. DoReplicated calls UpdateOpIdForOperation, which
  // sets the correct op_id on the new empty memtable.
  ResumeFollowersAndWait(follower_peers, delay_duration);

  ASSERT_TRUE(initial_batch_replicated_log_sink.IsEventOccurred());

  // Expect the replayed batch to continue advancing the write_ids.
  const int replayed_write_id = initial_write_id * 2;
  RegexWaiterLogSink restarted_batch_replicated_log_sink(
      Format(".*BatchReplicated.*next_write_id: $0.*", replayed_write_id));

  // Restart the leader tserver and trigger a bootstrap replay.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_failure_max_missed_heartbeat_periods) = 6;
  cluster_->mini_tablet_server(leader_idx)->Shutdown();
  ASSERT_OK(cluster_->mini_tablet_server(leader_idx)->Start());
  ASSERT_OK(cluster_->mini_tablet_server(leader_idx)->WaitStarted());

  // Validate that the write_ids continued advancing on the replay.
  ASSERT_TRUE(restarted_batch_replicated_log_sink.IsEventOccurred());

  // Validate the transaction commits and the data is correct.
  ASSERT_OK(conn_->CommitTransaction());
  auto table_data = ASSERT_RESULT(conn_->FetchAllAsString(kSelectAllStmt));
  ASSERT_EQ(table_data, "0, v0; 1, v1; 2, v2; 3, v3; 4, v4");
}

class AsyncWritesExternalTest : public pgwrapper::LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--allowed_preview_flags_csv=ysql_enable_write_pipelining");
    options->extra_tserver_flags.push_back("--ysql_enable_write_pipelining=true");
    options->extra_tserver_flags.push_back("--flush_rocksdb_on_shutdown=false");
    options->extra_tserver_flags.push_back("--leader_failure_max_missed_heartbeat_periods=100");
    options->extra_master_flags.push_back("--enable_load_balancing=false");
    options->extra_master_flags.push_back("--use_create_table_leader_hint=false");
  }

  int GetNumTabletServers() const override { return 3; }
};

// Repro of DB-20043 bug: AddedAsPending with `skip_opid_update=false` writes the intents frontier
// at RECEIVED_OPID on the leader.
// Flushing intents on the leader captures the inflated frontier.  After leader shutdown + restart,
// bootstrap's UpdateCommittedFromStored() inflates committed_op_id, raft catchup from the new
// leader skips new WRITE_OPs, leading to data loss.
TEST_F(AsyncWritesExternalTest, IntentsFrontierInflationCausesDataLoss) {
  constexpr auto kTableName = "tbl1";
  const auto kWaitTimeout = 120s * kTimeMultiplier;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));

  auto tablet_id = ASSERT_RESULT(GetSingleTabletId(kTableName));
  LOG(INFO) << "Tablet: " << tablet_id;

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 SELECT g, 'v' || g FROM generate_series(0, 9) g", kTableName));
  auto leader_idx = ASSERT_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));
  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(leader_idx, {tablet_id}));

  auto follower_indexes = ASSERT_RESULT(cluster_->GetTabletFollowerIndexes(tablet_id));
  LOG(INFO) << "Leader tserver is: " << leader_idx;

  // Delay both followers before they can mark the new entries as committed.
  for (auto idx : follower_indexes) {
    ASSERT_OK(cluster_->SetFlag(
        cluster_->tablet_server(idx), "TEST_delay_update_consensus_before_mark_committed_tablet_id",
        tablet_id));
    ASSERT_OK(cluster_->SetFlag(
        cluster_->tablet_server(idx), "TEST_delay_update_consensus_before_mark_committed_ms",
        "30000"));
  }

  ASSERT_OK(conn.Execute("BEGIN"));
  for (int k = 10; k < 20; ++k) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, k));
  }

  // The leader then can't get quorum for the new entries, and ends up with RECEIVED > COMMITTED.
  // With `skip_opid_update=false`, flushing intents on the leader captures this inflated frontier.
  // With `skip_opid_update=true`, the frontier is not updated until commit, so the flushed frontier
  // is correct.
  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(leader_idx, {tablet_id}));
  LOG(INFO) << "Flushed intents on leader tserver: " << leader_idx;

  // Shutdown the leader and kill the transaction.
  cluster_->tablet_server(leader_idx)->Shutdown();

  // Wait for a new leader to be elected.
  // This new leader's index will be close to the same index as the first write from the failed txn,
  // since none of those entries were able to be committed.
  for (auto idx : follower_indexes) {
    ASSERT_OK(cluster_->SetFlag(
        cluster_->tablet_server(idx), "TEST_delay_update_consensus_before_mark_committed_ms", "0"));
    ASSERT_OK(cluster_->SetFlag(
        cluster_->tablet_server(idx), "leader_failure_max_missed_heartbeat_periods", "6"));
  }
  ASSERT_OK(LoggedWaitFor(
      [this, &tablet_id, leader_idx]() -> Result<bool> {
        auto current = cluster_->GetTabletLeaderIndex(tablet_id);
        return current.ok() && *current != leader_idx;
      },
      kWaitTimeout, "Wait for new leader election"));
  auto new_leader_idx = ASSERT_RESULT(cluster_->GetTabletLeaderIndex(tablet_id));

  // Reinsert the same rows on the new leader.
  conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte", follower_indexes[0]));
  ASSERT_OK(conn.Execute("BEGIN"));
  for (int k = 10; k < 20; ++k) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, k));
  }
  ASSERT_OK(conn.Execute("COMMIT"));
  auto result =
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", kTableName)));
  ASSERT_EQ(result, 20);

  // Now restart the old leader.
  // If the frontier was inflated, then it will report an incorrect committed index and will miss
  // all the new entries during the raft catchup. Importantly, this will skip over the first entry
  // with batch_idx=0, so the transaction is not recognized and gets dropped.
  ASSERT_OK(cluster_->tablet_server(leader_idx)->Start());

  // Wait for the restarted node to rejoin as a follower and fully catch up.
  auto healthy_dump = ASSERT_RESULT(DumpTabletData(new_leader_idx, tablet_id));
  tserver::DumpTabletDataResponsePB restarted_dump;
  EXPECT_OK(LoggedWaitFor(
      [this, &tablet_id, leader_idx, &healthy_dump, &restarted_dump]() -> Result<bool> {
        auto result = DumpTabletData(leader_idx, tablet_id);
        if (!result.ok()) {
          return false;
        }
        restarted_dump = *result;
        return restarted_dump.row_count() == healthy_dump.row_count();
      },
      kWaitTimeout, "Wait for restarted node to catch up"));

  LOG(INFO) << "New leader (tserver: " << new_leader_idx
            << ") row_count=" << healthy_dump.row_count()
            << ", restarted old leader (tserver: " << leader_idx
            << ") row_count=" << restarted_dump.row_count();
  EXPECT_EQ(restarted_dump.row_count(), healthy_dump.row_count())
      << "Row count divergence on restarted old leader (tserver: " << leader_idx
      << "). New leader has " << healthy_dump.row_count() << " rows, old leader has "
      << restarted_dump.row_count() << ".";
  EXPECT_EQ(restarted_dump.xor_hash(), healthy_dump.xor_hash())
      << "Data hash mismatch on restarted old leader.";
}

// Verify that WaitForAsyncWrite RPCs retry via TabletInvoker when the leader steps down.
// The write is replicated before the stepdown, so the retry on the new leader should succeed
// by verifying the write was committed from the previous term.
//
// Debug-only because the test uses DEBUG_ONLY_TEST_SYNC_POINT.
TEST_F(YSqlAsyncWriteTest, YB_DEBUG_ONLY_TEST(HandleLeaderStepDown)) {
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));

  auto tablet_id = ASSERT_RESULT(GetTabletId());
  const size_t old_leader_idx = ASSERT_RESULT(PrepareToBreakConnectivity(tablet_id));

  // Block the first WaitForAsyncWrite handler invocation so we can step down the leader while the
  // RPCs are in-flight.
  auto sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency(
      {{"HandleLeaderStepDown::LeaderStepDownComplete",
        "TabletServiceImpl::WaitForAsyncWrite::BeforeRegister"}});
  std::atomic<bool> first_wait_blocked{false};
  sync_point->SetCallBack("TabletServiceImpl::WaitForAsyncWrite::BeforeRegister", [&](void*) {
    first_wait_blocked = true;
  });
  sync_point->EnableProcessing();
  auto se = ScopeExit([&sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn_->Execute("BEGIN"));
  for (int i = 1; i <= 5; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));
  }

  ASSERT_OK(LoggedWaitFor(
      [&first_wait_blocked] { return first_wait_blocked.load(); }, 30s,
      "Wait for first async write to get blocked"));

  ASSERT_OK(BreakConnectivityWithAll(cluster_.get(), old_leader_idx));
  size_t new_leader_idx = ASSERT_RESULT(WaitForNewTabletLeader(tablet_id, old_leader_idx));
  ASSERT_OK(SetupConnectivityWithAll(cluster_.get(), old_leader_idx));

  ASSERT_OK(LoggedWaitFor(
      [this, &tablet_id, old_leader_idx]() -> Result<bool> {
        auto peer = VERIFY_RESULT(GetTabletPeerOnTserver(old_leader_idx, tablet_id));
        auto consensus = VERIFY_RESULT(peer->GetRaftConsensus());
        return !consensus->GetLeaderState().ok();
      },
      30s, "Wait for old leader to step down"));

  // Release the blocked writes.
  DEBUG_ONLY_TEST_SYNC_POINT("HandleLeaderStepDown::LeaderStepDownComplete");

  // Write some more data to the new leader.
  for (int i = 6; i <= 10; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));
  }

  const auto expected_output =
      "1, v1; 2, v2; 3, v3; 4, v4; 5, v5; 6, v6; 7, v7; 8, v8; 9, v9; 10, v10";

  ASSERT_OK(conn_->CommitTransaction());
  ASSERT_OK(ValidateData(expected_output));

  // Verify data on the old leader too.
  ASSERT_OK(StepDown(new_leader_idx, old_leader_idx, tablet_id));
  ASSERT_OK(ValidateData(expected_output));
}

// Test the VerifyAsyncWriteCompletion logic by calling RegisterAsyncWriteCompletion directly.
TEST_F(YSqlAsyncWriteTest, VerifyAsyncWriteCompletion) {
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));

  auto tablet_id = ASSERT_RESULT(GetTabletId());

  // Insert a value and get its op id.
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'A')", kTableName));

  auto leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
  auto leader_consensus = ASSERT_RESULT(leader_peer->GetRaftConsensus());
  auto committed_op_id = ASSERT_RESULT(leader_consensus->GetLastOpId(consensus::COMMITTED_OPID));
  const auto leader_idx = GetLeaderIdx(tablet_id);

  LOG(INFO) << "Committed op_id on leader: " << committed_op_id;

  // Case 1: Same term committed write should succeed.
  {
    Synchronizer sync;
    leader_peer->RegisterAsyncWriteCompletion(committed_op_id, sync.AsStdStatusCallback());
    ASSERT_OK(sync.Wait());
  }

  // Step down the leader to test cross-term scenarios.
  const size_t new_leader_idx = (leader_idx + 1) % NumTabletServers();
  ASSERT_OK(StepDown(leader_idx, new_leader_idx, tablet_id));

  auto new_leader_peer = ASSERT_RESULT(GetTabletPeerOnTserver(new_leader_idx, tablet_id));
  auto new_consensus = ASSERT_RESULT(new_leader_peer->GetRaftConsensus());
  ASSERT_OK(LoggedWaitFor(
      [&new_consensus]() { return new_consensus->GetLeaderState().ok(); }, 30s,
      "new leader to be ready"));

  auto new_committed = ASSERT_RESULT(new_consensus->GetLastOpId(consensus::COMMITTED_OPID));
  LOG(INFO) << "New leader committed op_id: " << new_committed
            << ", first_index_of_current_term: " << new_consensus->GetFirstIndexOfCurrentTerm();

  // Case 2: A write from the previous term that was committed should succeed on the new leader.
  // The committed_op_id from the old term should be verified as committed because its index
  // is less than first_index_of_current_term.
  {
    ASSERT_LT(committed_op_id.index, new_consensus->GetFirstIndexOfCurrentTerm());
    Synchronizer sync;
    new_leader_peer->RegisterAsyncWriteCompletion(committed_op_id, sync.AsStdStatusCallback());
    ASSERT_OK(sync.Wait());
  }

  // Case 3: A write from the previous term at or beyond first_index_of_current_term should fail
  // because it was overwritten by the new leader's NO_OP.
  {
    auto first_index = new_consensus->GetFirstIndexOfCurrentTerm();
    OpId overwritten_op_id(committed_op_id.term, first_index);
    Synchronizer sync;
    new_leader_peer->RegisterAsyncWriteCompletion(overwritten_op_id, sync.AsStdStatusCallback());
    auto status = sync.Wait();
    ASSERT_NOK(status);
    ASSERT_TRUE(status.IsAborted()) << "Expected Aborted, got: " << status;
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), "tablet leader changed");
  }

  // Case 4: A write from two terms ago should fail.
  {
    // Step down again to create a term gap of 2.
    const size_t third_leader_idx = (new_leader_idx + 1) % NumTabletServers();
    ASSERT_OK(StepDown(new_leader_idx, third_leader_idx, tablet_id));

    auto third_leader_peer = ASSERT_RESULT(GetTabletPeerOnTserver(third_leader_idx, tablet_id));
    auto third_consensus = ASSERT_RESULT(third_leader_peer->GetRaftConsensus());
    ASSERT_OK(LoggedWaitFor(
        [&third_consensus]() { return third_consensus->GetLeaderState().ok(); }, 30s,
        "third leader to be ready"));

    Synchronizer sync;
    third_leader_peer->RegisterAsyncWriteCompletion(committed_op_id, sync.AsStdStatusCallback());
    auto status = sync.Wait();
    ASSERT_NOK(status);
    ASSERT_TRUE(status.IsAborted()) << "Expected Aborted, got: " << status;
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), "leader moved more than once");
  }
}

TEST_F(YSqlAsyncWriteTest, RepeatedStepDownsWithAsyncWrites) {
  // Verify that a transaction with async writes can survive multiple leader stepdowns, with each
  // write left pending across a stepdown and verified on the new leader.
  constexpr int kNumIterations = 5;

  // This test does rapid stepdowns, so make sure that we don't block due to lost pre-elections.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_min_leader_stepdown_retry_interval_ms) = 0;
  // Increase the drain timeout for better test stability.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_protege_synchronization_timeout_ms) = 5000;

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));

  auto tablet_id = ASSERT_RESULT(GetTabletId());

  std::mutex verified_mutex;
  std::set<OpId> verified_ops;
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("TabletServiceImpl::WaitForAsyncWrite::Verified", [&](void* arg) {
    std::lock_guard l(verified_mutex);
    verified_ops.insert(*static_cast<OpId*>(arg));
  });
  sync_point->EnableProcessing();
  auto se = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn_->Execute("BEGIN"));

  for (int i = 0; i < kNumIterations; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));

    // Step down to the rotating protege.
    size_t leader_idx = GetLeaderIdx(tablet_id);
    const size_t protege_idx = (leader_idx + 1) % NumTabletServers();
    auto leader_peer = ASSERT_RESULT(GetTabletPeerOnTserver(leader_idx, tablet_id));
    const auto& protege_uuid =
        cluster_->mini_tablet_server(protege_idx)->server()->permanent_uuid();

    // Capture the op id of the async write we just issued on the current leader/term.
    auto leader_consensus = ASSERT_RESULT(leader_peer->GetRaftConsensus());
    const auto write_op_id = ASSERT_RESULT(leader_consensus->GetLastOpId(consensus::RECEIVED_OPID));

    ASSERT_OK(yb::StepDown(leader_peer, protege_uuid, ForceStepDown::kFalse));

    ASSERT_OK(WaitUntilTabletHasLeader(
        cluster_.get(), tablet_id, CoarseMonoClock::Now() + 30s, RequireLeaderIsReady::kTrue));

    // Wait for this write's completion RPC to verify before the next stepdown, so it never spans
    // more than two terms.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          std::lock_guard l(verified_mutex);
          return verified_ops.contains(write_op_id);
        },
        30s, Format("Wait for async write $0 to be verified", write_op_id)));
  }

  // Validate the transaction committed successfully, and all rows are present.
  ASSERT_OK(conn_->CommitTransaction());
  const auto count = ASSERT_RESULT(
      conn_->FetchRow<pgwrapper::PGUint64>(Format("SELECT COUNT(*) FROM $0", kTableName)));
  ASSERT_EQ(count, kNumIterations);
}

class YSqlAsyncWriteLongLeaseTest : public YSqlAsyncWriteTest {
 public:
  void SetTestFlags() override {
    YSqlAsyncWriteTest::SetTestFlags();
    // Bump up leader leases so we can pause followers for an extended period of time.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_lease_duration_ms) = 30000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ht_lease_duration_ms) = 30000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_failure_max_missed_heartbeat_periods) = 100;
    // Speed up test start up.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_quick_leader_election_on_create) = true;
  }
};

// Ensure that the protege synchronization timeout handles draining of async writes for graceful
// stepdowns. Increment the protege timeout to a large value while we pause all followers and
// trigger async writes. Async writes should be blocked on the original leader and get routed to
// the new leader after the stepdown completes.
TEST_F(YSqlAsyncWriteLongLeaseTest, GracefulStepDownWithExtendedProtegeSyncWait) {
  // Increase the timeout for the protege to catch up while we pause the followers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_protege_synchronization_timeout_ms) = 30000;

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));

  auto tablet_id = ASSERT_RESULT(GetTabletId());
  const size_t leader_idx = GetLeaderIdx(tablet_id);
  auto leader_peer = ASSERT_RESULT(GetTabletPeerOnTserver(leader_idx, tablet_id));
  const size_t protege_idx = (leader_idx + 1) % NumTabletServers();
  const auto& protege_uuid = cluster_->mini_tablet_server(protege_idx)->server()->permanent_uuid();

  ASSERT_OK(PauseFollowers(kTableName));

  ASSERT_OK(conn_->Execute("BEGIN"));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 'A')", kTableName));

  // Trigger graceful stepdown to a lagging protege.
  auto delay_step_down_log = StringWaiterLogSink("Delay step down:");
  ASSERT_OK(yb::StepDown(leader_peer, protege_uuid, ForceStepDown::kFalse));

  // We will be blocked in this drain until the followers are resumed.
  ASSERT_OK(LoggedWaitFor(
      [&delay_step_down_log]() -> Result<bool> { return delay_step_down_log.GetEventCount() > 0; },
      10s, "Wait for delayed_step_down_ log"));

  // Trigger another write, this should get rejected by the leader since it is waiting for the
  // protege to catch up. Once that happens, the write should get routed properly to the new leader.
  auto reject_log = StringWaiterLogSink("Rejecting because of planned step down");
  Status extra_write_status;
  std::thread extra_write([this, &extra_write_status] {
    extra_write_status = conn_->ExecuteFormat("INSERT INTO $0 VALUES (2, 'B')", kTableName);
  });

  // While the followers stay paused the leader can't drain, so the retried write keeps hitting it
  // and getting rejected.
  constexpr int kMinRejections = 3;
  ASSERT_OK(LoggedWaitFor(
      [&reject_log]() -> Result<bool> { return reject_log.GetEventCount() >= kMinRejections; }, 30s,
      "Wait for write to be rejected repeatedly while stepdown is stuck"));

  // Unpause the followers. The protege catches up, which completes the stepdown.
  ASSERT_OK(ResumeFollowers(kTableName));

  ASSERT_OK(LoggedWaitFor(
      [this, &tablet_id, leader_idx]() -> Result<bool> {
        return GetLeaderIdx(tablet_id) != leader_idx;
      },
      10s, "Wait for stepdown to complete"));

  // Validate that the second write completed.
  extra_write.join();
  ASSERT_OK(extra_write_status);

  ASSERT_OK(conn_->CommitTransaction());

  const auto rows =
      ASSERT_RESULT(conn_->FetchAllAsString(Format("SELECT * FROM $0 ORDER BY key", kTableName)));
  ASSERT_EQ(rows, "1, A; 2, B");
}

class YSqlAsyncWriteSplitTest : public YSqlAsyncWriteTest {
 public:
  void SetTestFlags() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_write_pipelining) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_create_table_leader_hint) = true;
    // Bump the heartbeat threshold so terms stay stable.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_leader_failure_max_missed_heartbeat_periods) = 100;
  }

  // Force a flush so the parent has an SST file.
  Status PrepareTabletForSplit(const TabletId& tablet_id) {
    RETURN_NOT_OK(cluster_->FlushTablets());
    auto leader_peer = VERIFY_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
    return WaitForAnySstFiles(leader_peer, 60s * kTimeMultiplier);
  }
};

// Pre-split OpIds can be verified on both the surviving parent and the children.
TEST_F(YSqlAsyncWriteSplitTest, VerifyAsyncWriteOnSplitParentAndChildren) {
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));

  auto parent_tablet_id = ASSERT_RESULT(GetTabletId());

  for (int i = 0; i < 50; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));
  }

  auto parent_leader_peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), parent_tablet_id));
  auto parent_consensus = ASSERT_RESULT(parent_leader_peer->GetRaftConsensus());
  const auto pre_split_op_id =
      ASSERT_RESULT(parent_consensus->GetLastOpId(consensus::COMMITTED_OPID));
  LOG(INFO) << "Pre-split committed op_id: " << pre_split_op_id;

  ASSERT_OK(PrepareTabletForSplit(parent_tablet_id));
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), parent_tablet_id));

  const auto split_op_id = parent_leader_peer->tablet_metadata()->split_op_id();
  ASSERT_FALSE(split_op_id.empty());
  ASSERT_LE(pre_split_op_id.index, split_op_id.index);
  // The one-term-ago check only catches pre-split OpIds whose term matches SPLIT_OP's.
  ASSERT_EQ(pre_split_op_id.term, split_op_id.term);
  LOG(INFO) << "Parent SPLIT_OP id: " << split_op_id;

  // Parent in TABLET_DATA_SPLIT_COMPLETED state should still answer from its own log.
  ASSERT_EQ(
      parent_leader_peer->tablet_metadata()->tablet_data_state(),
      tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED);
  {
    Synchronizer sync;
    parent_leader_peer->RegisterAsyncWriteCompletion(pre_split_op_id, sync.AsStdStatusCallback());
    ASSERT_OK(sync.Wait());
  }

  const auto child_tablet_ids = parent_leader_peer->tablet_metadata()->split_child_tablet_ids();
  ASSERT_EQ(child_tablet_ids.size(), 2);

  for (const auto& child_tablet_id : child_tablet_ids) {
    tablet::TabletPeerPtr child_leader_peer;
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          auto result = GetLeaderPeerForTablet(cluster_.get(), child_tablet_id);
          if (!result.ok()) return false;
          child_leader_peer = *result;
          auto consensus = result->get()->GetRaftConsensus();
          return consensus.ok() && (*consensus)->GetLeaderState().ok();
        },
        30s, Format("child $0 leader to be ready", child_tablet_id)));
    SCOPED_TRACE(Format("child $0", child_tablet_id));

    // Pre-split committed OpId: accepted.
    {
      Synchronizer sync;
      child_leader_peer->RegisterAsyncWriteCompletion(pre_split_op_id, sync.AsStdStatusCallback());
      ASSERT_OK(sync.Wait());
    }

    // SPLIT_OP boundary: also accepted.
    {
      Synchronizer sync;
      child_leader_peer->RegisterAsyncWriteCompletion(split_op_id, sync.AsStdStatusCallback());
      ASSERT_OK(sync.Wait());
    }

    // Bogus post-SPLIT_OP OpId: must be rejected.
    {
      OpId fake_post_split{split_op_id.term + 5, split_op_id.index + 1000};
      Synchronizer sync;
      child_leader_peer->RegisterAsyncWriteCompletion(fake_post_split, sync.AsStdStatusCallback());
      ASSERT_NOK(sync.Wait());
    }
  }
}

// End-to-end test with a split in the middle of a transaction.
TEST_F(YSqlAsyncWriteSplitTest, EndToEndSplitDuringTransaction) {
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));

  auto tablet_id = ASSERT_RESULT(GetTabletId());
  for (int i = 1; i <= 50; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));
  }
  ASSERT_OK(PrepareTabletForSplit(tablet_id));

  // Begin a transaction and trigger a split in the middle.
  ASSERT_OK(conn_->Execute("BEGIN"));
  for (int i = 51; i <= 55; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));
  }

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), tablet_id));

  // Write some more rows.
  for (int i = 56; i <= 60; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));
  }
  ASSERT_OK(conn_->CommitTransaction());

  // Validate data.
  std::string expected;
  for (int i = 1; i <= 60; ++i) {
    if (i > 1) expected += "; ";
    expected += Format("$0, v$0", i);
  }
  ASSERT_OK(ValidateData(expected));
}

// Background writer runs transactional inserts while the main thread triggers a split.
// Ensure no writes fail.
TEST_F(YSqlAsyncWriteSplitTest, ConcurrentInsertsDuringSplit) {
  constexpr int kRowsPerTxn = 5;
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS", kTableName));
  auto tablet_id = ASSERT_RESULT(GetTabletId());

  for (int i = 1; i <= 50; ++i) {
    ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, i));
  }
  ASSERT_OK(PrepareTabletForSplit(tablet_id));

  std::atomic<int> next_key{51};
  std::atomic<int> txns_committed{0};
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &next_key, &txns_committed, &thread_holder] {
    auto conn = ASSERT_RESULT(Connect());
    while (!thread_holder.stop_flag().load(std::memory_order_acquire)) {
      const int batch_start = next_key.fetch_add(kRowsPerTxn, std::memory_order_relaxed);
      ASSERT_OK(conn.Execute("BEGIN"));
      for (int i = 0; i < kRowsPerTxn; ++i) {
        ASSERT_OK(
            conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, 'v$1')", kTableName, batch_start + i));
      }
      ASSERT_OK(conn.CommitTransaction());
      txns_committed.fetch_add(1, std::memory_order_relaxed);
    }
  });

  // Wait for some write to complete before triggering the split.
  constexpr int kMinTxnsPerPhase = 10;
  ASSERT_OK(LoggedWaitFor(
      [&] { return txns_committed.load() >= kMinTxnsPerPhase; }, 30s * kTimeMultiplier,
      "writer to make pre-split progress"));
  const int committed_before_split = txns_committed.load();

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), tablet_id));

  ASSERT_OK(LoggedWaitFor(
      [&] { return txns_committed.load() >= committed_before_split + kMinTxnsPerPhase; },
      30s * kTimeMultiplier, "writer to make post-split progress"));

  thread_holder.Stop();
  const int expected_rows = 50 + kRowsPerTxn * txns_committed.load();
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        auto count =
            VERIFY_RESULT(conn_->FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", kTableName)));
        return count == expected_rows;
      },
      30s * kTimeMultiplier, "all rows visible after split"));
}

}  // namespace yb
