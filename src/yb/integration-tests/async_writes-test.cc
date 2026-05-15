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

#include "yb/consensus/log.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver.messages.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_load_balancing);
DECLARE_bool(flush_rocksdb_on_shutdown);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(TEST_do_not_replicate_async_writes);
DECLARE_bool(use_create_table_leader_hint);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(ysql_enable_write_pipelining);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_int32(raft_heartbeat_interval_ms);
DECLARE_uint64(max_clock_skew_usec);

namespace yb {

constexpr auto kTableName = "tbl1";
const auto kSelectAllStmt = Format("SELECT * FROM $0 ORDER BY key", kTableName);

class YSqlAsyncWriteTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_write_pipelining) = true;

    // These tests stepdown the leader, so we need to disable load balancing.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_create_table_leader_hint) = false;

    TEST_SETUP_SUPER(pgwrapper::PgMiniTestBase);
    conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
  }

  size_t NumMasters() override { return 3; }

  size_t NumTabletServers() override { return 3; }

  Result<TabletId> GetTabletId(const std::string& table_name = kTableName) {
    auto tablets = VERIFY_RESULT(
        ListTabletsForTableName(cluster_.get(), table_name, ListPeersFilter::kLeaders));
    SCHECK_EQ(tablets.size(), 1, IllegalState, "Expected 1 tablet");
    return tablets.front()->tablet_id();
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
  TEST_SYNC_POINT("LeaderStepDownAfterWriteAck::LeaderConnectivityBroken");

  // Wait for a new leader to be elected.
  size_t new_leader_idx = ASSERT_RESULT(WaitForNewTabletLeader(tablet_id, old_leader_idx));

  // Bring back the old leader.
  ASSERT_OK(SetupConnectivityWithAll(cluster_.get(), old_leader_idx));

  // We should not be able to perform further reads, or commit the transaction.
  if (perform_read) {
    ASSERT_NOK(
        conn_->FetchRow<std::string>(Format("SELECT value FROM $0 WHERE key = 1", kTableName)));
    // COMMIT of a failed transaction internally performs a ROLLBACK in pg.
    ASSERT_OK(conn_->CommitTransaction());
  } else {
    ASSERT_NOK(conn_->CommitTransaction());
  }

  // Reset the connection and make sure the transaction was aborted.
  conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
  auto row_count = ASSERT_RESULT(
      conn_->FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0 WHERE key = 1", kTableName)));
  ASSERT_EQ(row_count, 0);

  // Go back to the old leader and make sure aborted data is not visible.
  ASSERT_OK(StepDown(new_leader_idx, old_leader_idx, tablet_id));

  row_count = ASSERT_RESULT(
      conn_->FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0 WHERE key = 1", kTableName)));
  ASSERT_EQ(row_count, 0);
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

}  // namespace yb
