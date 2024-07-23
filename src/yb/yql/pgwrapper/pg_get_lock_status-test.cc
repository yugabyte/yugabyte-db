// Copyright (c) YugaByte, Inc.
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

#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_heartbeat.pb.h"

#include "yb/master/mini_master.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/pg_locks_test_base.h"

DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_uint64(refresh_waiter_timeout_ms);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(force_global_transactions);
DECLARE_bool(TEST_mock_tablet_hosts_all_transactions);
DECLARE_bool(TEST_fail_abort_request_with_try_again);
DECLARE_bool(enable_wait_queues);
DECLARE_bool(TEST_skip_returning_old_transactions);
DECLARE_uint64(force_single_shard_waiter_retry_ms);

using namespace std::literals;
using std::string;

namespace yb {
namespace pgwrapper {

struct TestTxnLockInfo {
  TestTxnLockInfo() {}
  explicit TestTxnLockInfo(int num_locks) : num_locks(num_locks) {}
  TestTxnLockInfo(
      int num_locks, bool has_additional_granted_locks, bool has_additional_waiting_locks)
          : num_locks(num_locks), has_additional_granted_locks(has_additional_granted_locks),
            has_additional_waiting_locks(has_additional_waiting_locks) {}
  int num_locks = 0;
  bool has_additional_granted_locks = false;
  bool has_additional_waiting_locks = false;
};

using tserver::TabletServerServiceProxy;
using TransactionIdSet = std::unordered_set<TransactionId, TransactionIdHash>;
using TxnLocksMap = std::unordered_map<TransactionId, TestTxnLockInfo, TransactionIdHash>;
using TabletTxnLocksMap = std::unordered_map<TabletId, TxnLocksMap>;

class PgGetLockStatusTest : public PgLocksTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    PgLocksTestBase::SetUp();
  }

  Result<TransactionIdSet> GetTxnsInLockStatusResponse(
      const tserver::GetLockStatusResponsePB& resp) {
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    TransactionIdSet txn_ids_set;
    for (const auto& tablet_lock_info : resp.tablet_lock_infos()) {
      for (const auto& txn_lock_info : tablet_lock_info.transaction_locks()) {
        auto id = VERIFY_RESULT(FullyDecodeTransactionId(txn_lock_info.id()));
        RSTATUS_DCHECK(!id.IsNil(),
                       IllegalState,
                       "Expected to see non-empty transaction id.");
        txn_ids_set.insert(id);
      }
    }
    return txn_ids_set;
  }

  Result<size_t> GetNumTxnsInLockStatusResponse(const tserver::GetLockStatusResponsePB& resp) {
    return VERIFY_RESULT(GetTxnsInLockStatusResponse(resp)).size();
  }

  Result<size_t> GetNumTabletsInLockStatusResponse(const tserver::GetLockStatusResponsePB& resp) {
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    std::unordered_set<std::string> tablet_ids_set;
    for (const auto& tablet_lock_info : resp.tablet_lock_infos()) {
      tablet_ids_set.insert(tablet_lock_info.tablet_id());
    }
    return tablet_ids_set.size();
  }

  void VerifyResponse(const tserver::PgGetLockStatusResponsePB& resp,
                      TabletTxnLocksMap expected_tablet_txn_locks) {
    for (const auto& node_lock : resp.node_locks()) {
      for (const auto& tablet_locks : node_lock.tablet_lock_infos()) {
        if (tablet_locks.tablet_id().empty()) {
          continue;
        }

        auto tablet_map_it = expected_tablet_txn_locks.find(tablet_locks.tablet_id());
        ASSERT_NE(tablet_map_it, expected_tablet_txn_locks.end());
        ASSERT_EQ(tablet_locks.transaction_locks().size(), tablet_map_it->second.size());

        for (const auto& txn_lock_info : tablet_locks.transaction_locks()) {
          auto id = ASSERT_RESULT(FullyDecodeTransactionId(txn_lock_info.id()));
          auto txn_map_it = tablet_map_it->second.find(id);
          ASSERT_NE(txn_map_it, tablet_map_it->second.end());
          ASSERT_EQ(
              txn_lock_info.granted_locks_size() + txn_lock_info.waiting_locks().locks_size(),
              txn_map_it->second.num_locks);
          ASSERT_EQ(txn_lock_info.has_additional_granted_locks(),
                    txn_map_it->second.has_additional_granted_locks);
          ASSERT_EQ(txn_lock_info.waiting_locks().has_additional_waiting_locks(),
                    txn_map_it->second.has_additional_waiting_locks);
          tablet_map_it->second.erase(txn_map_it);
        }
        ASSERT_TRUE(tablet_map_it->second.empty());
        expected_tablet_txn_locks.erase(tablet_map_it);
      }
    }
    ASSERT_TRUE(expected_tablet_txn_locks.empty());
  }
};


TEST_F(PgGetLockStatusTest, TestGetLockStatusWithCustomTabletTxnsMap) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, key));

  const auto& tablet_id = session.first_involved_tablet;
  auto resp = ASSERT_RESULT(GetLockStatus(tablet_id));
  auto num_txns = ASSERT_RESULT(GetNumTxnsInLockStatusResponse(resp));
  ASSERT_EQ(num_txns, 1);

  // Start another transaction an operate on the same tablet.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=v+10 WHERE k=$1", table, 2));

  // Expect to see 2 transaction in GetLockStatus resp.
  resp = ASSERT_RESULT(GetLockStatus(tablet_id));
  num_txns = ASSERT_RESULT(GetNumTxnsInLockStatusResponse(resp));
  ASSERT_EQ(num_txns, 2);

  // Restrict LockStatus to return locks corresponding to the input transaction ids.
  std::vector<TransactionId> txn_ids(1, session.txn_id);
  resp = ASSERT_RESULT(GetLockStatus(tablet_id, txn_ids));
  num_txns = ASSERT_RESULT(GetNumTxnsInLockStatusResponse(resp));
  ASSERT_EQ(num_txns, 1);
}

TEST_F(PgGetLockStatusTest, TestGetLockStatusWithCustomTransactionsList) {
  const auto table1 = "foo";
  const auto table2 = "bar";
  const auto tablet1 = CreateTableAndGetTabletId(table1);
  const auto tablet2 = CreateTableAndGetTabletId(table2);
  auto session1 = ASSERT_RESULT(Init("foo", "1", false /* create table */));
  auto session2 = ASSERT_RESULT(Init("bar", "1", false /* create table */));

  std::vector<TransactionId> txn_ids;
  txn_ids.push_back(session1.txn_id);
  txn_ids.push_back(session2.txn_id);

  auto resp = ASSERT_RESULT(GetLockStatus(txn_ids));
  auto num_tablets = ASSERT_RESULT(GetNumTabletsInLockStatusResponse(resp));
  ASSERT_EQ(num_tablets, 2);
  auto num_txns = ASSERT_RESULT(GetNumTxnsInLockStatusResponse(resp));
  ASSERT_EQ(num_txns, 2);

  ASSERT_OK(session1.conn->ExecuteFormat("UPDATE $0 SET v=v+10 WHERE k=2", table2));
  txn_ids.pop_back();

  resp = ASSERT_RESULT(GetLockStatus(txn_ids));
  num_tablets = ASSERT_RESULT(GetNumTabletsInLockStatusResponse(resp));
  ASSERT_EQ(num_tablets, 2);
  num_txns = ASSERT_RESULT(GetNumTxnsInLockStatusResponse(resp));
  ASSERT_EQ(num_txns, 1);
}

TEST_F(PgGetLockStatusTest, TestLocksFromWaitQueue) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, key));

  // Create a second transaction and make it wait on the earlier txn. This txn won't acquire
  // any locks and will wait in the wait-queue.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto status_future = ASSERT_RESULT(
      ExpectBlockedAsync(&conn, Format("UPDATE $0 SET v=v+10 WHERE k=$1", table, key)));

  // Assert that locks corresponding to the waiter txn as well are returned in pg_locks;
  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));
  auto num_txns = ASSERT_RESULT(session.conn->FetchRow<int64>(
      "SELECT COUNT(DISTINCT(ybdetails->>'transactionid')) FROM pg_locks"));
  ASSERT_EQ(num_txns, 2);

  ASSERT_OK(session.conn->Execute("COMMIT"));
  ASSERT_OK(status_future.get());
}

TEST_F(PgGetLockStatusTest, YB_DISABLE_TEST_IN_TSAN(TestLocksOfSingleShardWaiters)) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, key));

  auto conn = ASSERT_RESULT(Connect());
  // Fire a single row update that will wait on the earlier launched transaction.
  auto status_future = ASSERT_RESULT(
      ExpectBlockedAsync(&conn, Format("UPDATE $0 SET v=v+10 WHERE k=$1", table, key)));

  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));
  const auto& tablet_id = session.first_involved_tablet;
  auto resp = ASSERT_RESULT(GetLockStatus(tablet_id));
  ASSERT_EQ(resp.tablet_lock_infos(0).single_shard_waiters_size(), 1);
  ASSERT_TRUE(conn.IsBusy());
  ASSERT_OK(session.conn->Execute("COMMIT"));
  ASSERT_OK(status_future.get());
}

TEST_F(PgGetLockStatusTest, TestGetLockStatusSimple) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, key));

  tserver::PgGetLockStatusRequestPB req;
  req.set_transaction_id(session.txn_id.data(), session.txn_id.size());
  req.set_max_num_txns(50);
  auto resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        {session.txn_id, TestTxnLockInfo(2)}
      }
    }
  });
}

TEST_F(PgGetLockStatusTest, TestGetLockStatusOfOldTxns) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, key));

  auto min_txn_age_ms = 2000;
  SleepFor(MonoDelta::FromMilliseconds(kTimeMultiplier * min_txn_age_ms));
  tserver::PgGetLockStatusRequestPB req;
  req.set_min_txn_age_ms(min_txn_age_ms);
  req.set_max_num_txns(50);

  // Start another txn which wouldn't be considered old.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table, 2));

  auto resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        {session.txn_id, TestTxnLockInfo(2)}
      }
    }
  });

  // Fetch lock status after sleep and expect to see the other transaction as well.
  SleepFor(MonoDelta::FromMilliseconds(kTimeMultiplier * min_txn_age_ms));
  // Workaround to get the other transaction id, currently can't get it through a pg command.
  auto tserver_lock_status_resp = ASSERT_RESULT(GetLockStatus(session.first_involved_tablet));
  auto txns_set = ASSERT_RESULT(GetTxnsInLockStatusResponse(tserver_lock_status_resp));
  txns_set.erase(session.txn_id);
  ASSERT_EQ(txns_set.size(), 1);
  auto other_txn = *txns_set.begin();
  ASSERT_NE(other_txn, session.txn_id);

  resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        {session.txn_id, TestTxnLockInfo(2)},
        {other_txn, TestTxnLockInfo(2)}
      }
    }
  });

  // Fetch locks of the specified transaction alone
  req.set_transaction_id(other_txn.data(), other_txn.size());
  resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        {other_txn, TestTxnLockInfo(2)}
      }
    }
  });
  req.clear_transaction_id();

  req.set_min_txn_age_ms(min_txn_age_ms * 100);
  resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {});
}

TEST_F(PgGetLockStatusTest, TestGetLockStatusLimitNumOldTxns) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, key));

  auto min_txn_age_ms = 2000;
  SleepFor(MonoDelta::FromMilliseconds(kTimeMultiplier * min_txn_age_ms));
  tserver::PgGetLockStatusRequestPB req;
  req.set_min_txn_age_ms(min_txn_age_ms);
  // Limit num old txns being returned. Assert that the oldest txns are prioritized over new ones.
  // Sleep to make sure coodinator aborts unresponsive transactions.
  auto abort_unresponsive_txn_usec = FLAGS_transaction_heartbeat_usec *
                                     kTimeMultiplier *
                                     FLAGS_transaction_max_missed_heartbeat_periods * 2;
  SleepFor(MonoDelta::FromMicroseconds(abort_unresponsive_txn_usec));
  // Start another txn which wouldn't be considered old.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table, 2));

  req.set_max_num_txns(1);
  auto resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        {session.txn_id, TestTxnLockInfo(2)}
      }
    }
  });
}

TEST_F(PgGetLockStatusTest, TestWaiterLockContainingColumnId) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, "2"));
  ASSERT_OK(session.conn->ExecuteFormat("UPDATE $0 SET v=1 WHERE k=$1", table, key));

  auto column_id = ASSERT_RESULT(session.conn->FetchRow<std::string>(
      "SELECT ybdetails->'keyrangedetails'->>'column_id' FROM pg_locks WHERE granted AND "
      "ybdetails->'keyrangedetails'->>'column_id' IS NOT NULL"));

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto status_future = ASSERT_RESULT(
      ExpectBlockedAsync(&conn, Format("UPDATE $0 SET v=1 WHERE k=$1", table, key)));

  SleepFor(2s * kTimeMultiplier);
  auto res = ASSERT_RESULT(session.conn->FetchRow<int64_t>(
      "SELECT COUNT(*) FROM pg_locks WHERE NOT granted"));
  // The waiter acquires 3 locks in total,
  // 1 {STRONG_READ,STRONG_WRITE} on the column
  // 1 {WEAK_READ,WEAK_WRITE,STRONG_READ} on the row
  // 1 {WEAK_READ,WEAK_WRITE} on the table
  ASSERT_EQ(res, 3);
  // Assert that the waiter lock too has the same column id as that of the granted lock.
  ASSERT_EQ(column_id, ASSERT_RESULT(session.conn->FetchRow<std::string>(
      "SELECT ybdetails->'keyrangedetails'->>'column_id' FROM pg_locks WHERE NOT granted AND "
      "ybdetails->'keyrangedetails'->>'column_id' IS NOT NULL")));
  ASSERT_OK(session.conn->Execute("COMMIT"));
}

#ifndef NDEBUG
TEST_F(PgGetLockStatusTest, TestGetLockStatusLimitNumTxnLocks) {
  auto session = ASSERT_RESULT(Init("foo", "1"));

  // Create a blocking txn.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn.Fetch("SELECT * FROM foo WHERE k=2 FOR UPDATE"));

  tserver::PgGetLockStatusRequestPB req;
  req.set_transaction_id(session.txn_id.data(), session.txn_id.size());
  req.set_max_txn_locks_per_tablet(1);
  auto resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        // Only one lock should be returned as we limit the num locks per txn per tablet.
        {session.txn_id,
             TestTxnLockInfo(1,
                             true /* has more granted locks */,
                             false /* has more awaiting locks */)}
      }
    }
  });
  resp.Clear();

  yb::SyncPoint::GetInstance()->LoadDependency({
    {"WaitQueue::Impl::SetupWaiterUnlocked:1", "TestGetLockStatusLimitNumTxnLocks"}});
  yb::SyncPoint::GetInstance()->ClearTrace();
  yb::SyncPoint::GetInstance()->EnableProcessing();

  std::thread th([&session] {
    ASSERT_OK(session.conn->Fetch("SELECT * FROM foo WHERE k=2 FOR UPDATE"));
  });

  DEBUG_ONLY_TEST_SYNC_POINT("TestGetLockStatusLimitNumTxnLocks");
  req.set_max_txn_locks_per_tablet(3);
  resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        // All granted locks and partial awaiting locks would be returned.
        {session.txn_id,
             TestTxnLockInfo(3,
                             false /* has more granted locks */,
                             true /* has more awaiting locks */)}
      }
    }
  });
  resp.Clear();

  req.set_max_txn_locks_per_tablet(1);
  resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        // Both granted and awaiting locks returned would be incomplete.
        {session.txn_id,
             TestTxnLockInfo(1,
                             true /* has more granted locks */,
                             true /* has more awaiting locks */)}
      }
    }
  });
  resp.Clear();

  // When the field is set to zero, it could mean wither of the below cases
  // 1. the request is sent from a node running an older version of YB
  // 2. or the client explicitly set the field to zero
  //
  // In either case, we don't limit the locks per transaction and return all the locks.
  req.set_max_txn_locks_per_tablet(0);
  resp = ASSERT_RESULT(GetLockStatus(req));
  VerifyResponse(resp, {
    {
      session.first_involved_tablet,
      {
        // All granted locks and all partial awaiting locks would be returned.
        {session.txn_id,
             TestTxnLockInfo(4,
                             false /* has more granted locks */,
                             false /* has more awaiting locks */)}
      }
    }
  });
  ASSERT_OK(conn.CommitTransaction());
  th.join();
  ASSERT_OK(session.conn->CommitTransaction());
}
#endif // NDEBUG

TEST_F(PgGetLockStatusTest, TestGetWaitStart) {
  const auto table = "foo";
  const auto locked_key = "2";
  auto session = ASSERT_RESULT(Init(table, "1"));

  auto blocker = ASSERT_RESULT(Connect());

  ASSERT_OK(blocker.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(blocker.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table, locked_key));

  std::atomic<bool> txn_finished = false;
  std::thread th([&session, &table, &locked_key, &txn_finished] {
    ASSERT_OK(session.conn->FetchFormat(
        "SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table, locked_key));
    txn_finished.store(true);
  });

  SleepFor(1ms * kTimeMultiplier);

  auto res = ASSERT_RESULT(blocker.FetchRow<int64_t>(
    "SELECT COUNT(*) FROM yb_lock_status(null, null) WHERE waitstart IS NOT NULL"));
  // The statement above acquires two locks --
  // {STRONG_READ,STRONG_WRITE} on the primary key
  // {WEAK_READ,WEAK_WRITE} on the table
  ASSERT_EQ(res, 2);

  ASSERT_OK(blocker.CommitTransaction());
  ASSERT_OK(WaitFor([&] {
    return txn_finished.load();
  }, 5s * kTimeMultiplier, "select for update to unblock and execute"));
  th.join();
  ASSERT_OK(session.conn->CommitTransaction());
}

TEST_F(PgGetLockStatusTest, TestBlockedBy) {
  const auto table = "waiter_table";
  const auto locked_key = "2";

  // Start waiter txn first to ensure it is the oldest
  auto waiter_session = ASSERT_RESULT(Init(table, "1"));

  SleepFor(10ms * kTimeMultiplier);

  auto session1 = ASSERT_RESULT(Init("foo", "1"));
  auto session2 = ASSERT_RESULT(Init("bar", "1"));

  // Have both sessions acquire lock on locked_key so they will both block our waiter
  ASSERT_OK(session1.conn->FetchFormat(
      "SELECT * FROM $0 WHERE k=$1 FOR KEY SHARE", table, locked_key));
  ASSERT_OK(session2.conn->FetchFormat(
      "SELECT * FROM $0 WHERE k=$1 FOR KEY SHARE", table, locked_key));

  // Try acquiring exclusive lock on locked_key async
  std::atomic<bool> lock_acquired = false;
  std::thread th([&waiter_session, &table, &locked_key, &lock_acquired] {
    ASSERT_OK(waiter_session.conn->FetchFormat(
        "SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table, locked_key));
    lock_acquired.store(true);
  });

  SleepFor(2 * FLAGS_heartbeat_interval_ms * 1ms * kTimeMultiplier);

  tserver::PgGetLockStatusRequestPB req;
  req.set_max_num_txns(1);
  auto resp = ASSERT_RESULT(GetLockStatus(req));

  ASSERT_EQ(resp.node_locks_size(), 1);
  ASSERT_EQ(resp.node_locks(0).tablet_lock_infos_size(), 1);
  ASSERT_EQ(resp.node_locks(0).tablet_lock_infos(0).transaction_locks_size(), 1);
  for (const auto& txn_lock_info :
          resp.node_locks(0).tablet_lock_infos(0).transaction_locks()) {
    auto waiter_txn_id = ASSERT_RESULT(FullyDecodeTransactionId(txn_lock_info.id()));
    ASSERT_EQ(waiter_txn_id, waiter_session.txn_id);

    ASSERT_EQ(txn_lock_info.waiting_locks().locks().size(), 2);

    std::set<TransactionId> blockers;
    for (const auto& blocking_txn_id : txn_lock_info.waiting_locks().blocking_txn_ids()) {
      auto decoded = ASSERT_RESULT(FullyDecodeTransactionId(blocking_txn_id));
      blockers.insert(decoded);
      ASSERT_TRUE(decoded == session1.txn_id || decoded == session2.txn_id);
    }
    ASSERT_EQ(blockers.size(), 2);
  }

  ASSERT_OK(session1.conn->CommitTransaction());
  ASSERT_OK(session2.conn->CommitTransaction());
  ASSERT_OK(WaitFor([&] {
    return lock_acquired.load();
  }, 5s * kTimeMultiplier, "select for update to unblock and execute"));
  th.join();

  auto null_blockers_ct = ASSERT_RESULT(session1.conn->FetchRow<int64_t>(
      Format("SELECT COUNT(*) FROM pg_locks WHERE ybdetails->>'blocked_by' IS NULL")));
  auto not_null_blockers_ct = ASSERT_RESULT(session1.conn->FetchRow<int64_t>(
      Format("SELECT COUNT(*) FROM pg_locks WHERE ybdetails->>'blocked_by' IS NOT NULL")));

  EXPECT_GT(null_blockers_ct, 0);
  EXPECT_EQ(not_null_blockers_ct, 0);

  ASSERT_OK(waiter_session.conn->CommitTransaction());
}

TEST_F(PgGetLockStatusTest, TestLocksOfColocatedTables) {
  const auto tablegroup = "tg";
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.ExecuteFormat("CREATE TABLEGROUP $0", tablegroup));

  std::set<std::string> table_names = {"foo", "bar", "baz"};
  for (const auto& table_name : table_names) {
    ASSERT_OK(setup_conn.ExecuteFormat(
        "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) TABLEGROUP $1", table_name, tablegroup));
    ASSERT_OK(setup_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT generate_series(1, 10), 0", table_name));
  }
  const auto key = "1";
  TestThreadHolder thread_holder;
  CountDownLatch fetched_locks{1};
  for (const auto& table_name : table_names) {
    thread_holder.AddThreadFunctor([this, &fetched_locks, table_name, key] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table_name, key));
      ASSERT_TRUE(fetched_locks.WaitFor(15s * kTimeMultiplier));
    });
  }

  SleepFor(5s * kTimeMultiplier);
  // Each transaction above acquires 2 locks, one {STRONG_READ,STRONG_WRITE} on the primary key
  // and the other being a {WEAK_READ,WEAK_WRITE} on the table.
  auto res = ASSERT_RESULT(setup_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM pg_locks"));
  ASSERT_EQ(res, table_names.size() * 2);
  // Assert that the locks held belong to tables "foo", "bar", "baz".
  auto values = ASSERT_RESULT(setup_conn.FetchRows<std::string>(
    "SELECT relname FROM pg_class WHERE oid IN (SELECT DISTINCT relation FROM pg_locks)"));
  ASSERT_EQ(values.size(), 3);
  for (const auto& relname : values) {
    ASSERT_TRUE(table_names.find(relname) != table_names.end());
  }
  fetched_locks.CountDown();
  thread_holder.WaitAndStop(25s * kTimeMultiplier);
}

TEST_F(PgGetLockStatusTest, TestColocatedWaiterWriteLock) {
  const auto tablegroup = "tg";
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.ExecuteFormat("CREATE TABLEGROUP $0", tablegroup));

  const auto table_name = "foo";
  ASSERT_OK(setup_conn.ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) TABLEGROUP $1", table_name, tablegroup));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT generate_series(1, 10), 0", table_name));

  const auto key = "1";
  const auto num_txns = 3;
  TestThreadHolder thread_holder;
  CountDownLatch fetched_locks{1};
  for (auto i = 0 ; i < num_txns ; i++) {
    thread_holder.AddThreadFunctor([this, &fetched_locks, table_name, key] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=1 WHERE k=$1", table_name, key));
      ASSERT_TRUE(fetched_locks.WaitFor(15s * kTimeMultiplier));
      ASSERT_OK(conn.RollbackTransaction());
    });
  }

  SleepFor(5s * kTimeMultiplier);
  // The transaction that has been granted has four locks; one {STRONG_READ,STRONG_WRITE} on the
  // column, one {WEAK_READ, WEAK_WRITE} on the row, a {STRONG_READ} on the row, and a
  // {WEAK_READ,WEAK_WRITE} on the table.
  auto value = ASSERT_RESULT(
      setup_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM pg_locks WHERE granted = true"));
  ASSERT_EQ(value, 4);

  // The waiter displays 3 locks in total,
  // 1 {STRONG_READ,STRONG_WRITE} on the column
  // 1 {WEAK_READ,WEAK_WRITE,STRONG_READ} on the row
  // 1 {WEAK_READ,WEAK_WRITE} on the table
  value = ASSERT_RESULT(
      setup_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM pg_locks WHERE granted = false"));
  ASSERT_EQ(value, (num_txns - 1) * 3);
  fetched_locks.CountDown();
  thread_holder.WaitAndStop(25s * kTimeMultiplier);
}

TEST_F(PgGetLockStatusTest, ReceivesWaiterSubtransactionId) {
  const auto table = "foo";
  const auto locked_key = "1";
  constexpr int kMinTxnAgeSeconds = 1;

  auto blocker_session = ASSERT_RESULT(Init(table, locked_key));

  auto waiter = ASSERT_RESULT(Init("bar", locked_key));
  ASSERT_OK(waiter.conn->Execute("SAVEPOINT s1"));
  std::thread th([&waiter, &table, &locked_key] {
    ASSERT_OK(waiter.conn->FetchFormat(
        "SELECT * FROM $0 WHERE k=$1 FOR SHARE", table, locked_key));
  });

  ASSERT_OK(blocker_session.conn->ExecuteFormat(
      "SET yb_locks_min_txn_age='$0s'", kMinTxnAgeSeconds));
  SleepFor(kMinTxnAgeSeconds * 1s * kTimeMultiplier);

  auto waiting_subtxn_id = ASSERT_RESULT(blocker_session.conn->FetchRow<string>(Format(
    "SELECT DISTINCT(ybdetails->>'subtransaction_id') FROM pg_locks "
    "WHERE ybdetails->>'subtransaction_id' != '1' "
      "AND ybdetails->>'transactionid'='$0' "
      "AND NOT granted",
    waiter.txn_id.ToString())));

  ASSERT_OK(blocker_session.conn->CommitTransaction());

  auto granted_subtxn_id = ASSERT_RESULT(blocker_session.conn->FetchRow<string>(Format(
    "SELECT DISTINCT(ybdetails->>'subtransaction_id') FROM pg_locks "
    "WHERE ybdetails->>'subtransaction_id' != '1' AND ybdetails->>'transactionid'='$0' AND granted",
    waiter.txn_id.ToString())));

  ASSERT_EQ(waiting_subtxn_id, granted_subtxn_id);

  th.join();
}

TEST_F(PgGetLockStatusTest, HidesLocksFromAbortedSubTransactions) {
  const auto table = "foo";
  const auto locked_key = "1";
  constexpr int kMinTxnAgeSeconds = 1;

  auto session = ASSERT_RESULT(Init(table, locked_key));

  ASSERT_OK(session.conn->Execute("SAVEPOINT s1"));
  ASSERT_OK(session.conn->FetchFormat("SELECT * FROM $0 WHERE k=2 FOR UPDATE", table));
  ASSERT_OK(session.conn->Execute("SAVEPOINT s2"));
  ASSERT_OK(session.conn->FetchFormat("SELECT * FROM $0 WHERE k=3 FOR UPDATE", table));

  auto get_distinct_subtxn_count_query = Format(
    "SELECT COUNT(DISTINCT(ybdetails->>'subtransaction_id')) FROM pg_locks "
    "WHERE ybdetails->>'transactionid'='$0'",
    session.txn_id.ToString());

  ASSERT_OK(session.conn->ExecuteFormat("SET yb_locks_min_txn_age='$0s'", kMinTxnAgeSeconds));
  SleepFor(kMinTxnAgeSeconds * 1s * kTimeMultiplier);

  EXPECT_EQ(ASSERT_RESULT(session.conn->FetchRow<int64>(get_distinct_subtxn_count_query)), 3);

  ASSERT_OK(session.conn->Execute("ROLLBACK TO s2"));

  EXPECT_EQ(ASSERT_RESULT(session.conn->FetchRow<int64>(get_distinct_subtxn_count_query)), 2);

  ASSERT_OK(session.conn->Execute("ROLLBACK TO s1"));

  EXPECT_EQ(ASSERT_RESULT(session.conn->FetchRow<int64>(get_distinct_subtxn_count_query)), 1);
}

TEST_F(PgGetLockStatusTest, TestPgLocksWhileDDLInProgress) {
  auto status_future = std::async(std::launch::async, [&]() -> Status {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute(
          "CREATE TABLE foo AS SELECT i AS a, i+1 AS b FROM generate_series(1,10000)i"));
    return Status::OK();
  });

  auto conn = ASSERT_RESULT(Connect());
  while (status_future.wait_for(0ms) != std::future_status::ready) {
    ASSERT_OK(conn.Fetch("SELECT * FROM pg_locks"));
  }
  ASSERT_OK(status_future.get());
}

// While populating the lock status request at a tablet, we make two passes at the intentsdb.
// Since intents could get added/tombstoned amidst passes, ensure that we don't run into errors.
TEST_F(PgGetLockStatusTest, TestPgLocksWhileDMLsInProgress) {
  constexpr int kNumConnections = 10;
  constexpr int kNumItersPerConn = 5;
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO foo SELECT generate_series(1, 10), 0"));

  auto status_future = std::async(std::launch::async, [&]() -> Status {
    TestThreadHolder thread_holder;
    for (int i = 0; i < kNumConnections; i++) {
      thread_holder.AddThreadFunctor([&, i] {
        for (int j = 0; j < kNumItersPerConn; j++) {
          auto conn = ASSERT_RESULT(Connect());
          ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
          ASSERT_OK(conn.ExecuteFormat("UPDATE foo SET v=v+1 WHERE k=$0", i));
          ASSERT_OK(conn.CommitTransaction());
        }
      });
    }
    thread_holder.WaitAndStop(15s * kTimeMultiplier);
    return Status::OK();
  });

  while (status_future.wait_for(0ms) != std::future_status::ready) {
    ASSERT_OK(setup_conn.Fetch("SELECT * FROM pg_locks"));
  }
  ASSERT_OK(status_future.get());
}

TEST_F(PgGetLockStatusTest, TestWaitStartTimeIsConsistentAcrossWaiterReEntries) {
  constexpr int kMinTxnAgeMs = 1;
  constexpr auto waiter_refresh_secs = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_refresh_waiter_timeout_ms) = waiter_refresh_secs * 1000;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));

  auto session_1 = ASSERT_RESULT(Init("foo", "1"));
  auto init_stamp = ASSERT_RESULT(conn.FetchRow<MonoDelta>(
      "SELECT DISTINCT(waitend) FROM pg_locks WHERE granted"));

  auto status_future_write_req = std::async(std::launch::async, [&]() -> Status {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn.Execute("UPDATE foo SET v=v+1 WHERE k=1"));
    RETURN_NOT_OK(conn.CommitTransaction());
    return Status::OK();
  });
  auto status_future_read_req = std::async(std::launch::async, [&]() -> Status {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn.Fetch("SELECT * FROM foo WHERE k=1 FOR UPDATE"));
    RETURN_NOT_OK(conn.CommitTransaction());
    return Status::OK();
  });


  SleepFor(2ms * FLAGS_heartbeat_interval_ms * kTimeMultiplier);
  auto now_stamp = ASSERT_RESULT(conn.FetchRow<MonoDelta>("SELECT NOW()"));

  auto wait_start_time_1_query =
      "SELECT DISTINCT(waitstart) FROM pg_locks WHERE NOT granted ORDER BY waitstart LIMIT 1";
  auto wait_start_time_1 =
      ASSERT_RESULT(conn.FetchRow<MonoDelta>(wait_start_time_1_query));
  ASSERT_LE(init_stamp, wait_start_time_1);
  ASSERT_GE(now_stamp, wait_start_time_1);

  auto wait_start_time_2_query =
      "SELECT DISTINCT(waitstart) FROM pg_locks WHERE NOT granted ORDER BY waitstart DESC LIMIT 1";
  auto wait_start_time_2 =
      ASSERT_RESULT(conn.FetchRow<MonoDelta>(wait_start_time_2_query));
  ASSERT_LE(init_stamp, wait_start_time_2);
  ASSERT_GE(now_stamp, wait_start_time_2);

  SleepFor(2 * waiter_refresh_secs * 1s * kTimeMultiplier);
  ASSERT_EQ(wait_start_time_1,
            ASSERT_RESULT(conn.FetchRow<MonoDelta>(wait_start_time_1_query)));
  ASSERT_EQ(wait_start_time_2,
            ASSERT_RESULT(conn.FetchRow<MonoDelta>(wait_start_time_2_query)));
  ASSERT_OK(session_1.conn->CommitTransaction());
  ASSERT_OK(status_future_write_req.get());
  ASSERT_OK(status_future_read_req.get());
}

TEST_F(PgGetLockStatusTest, TestLockStatusRespHasHostNodeSet) {
  constexpr int kMinTxnAgeMs = 1;
  // All distributed txns returned as part of pg_locks should have the host node uuid set.
  const auto kPgLocksQuery =
      Format("SELECT COUNT(DISTINCT(ybdetails->>'transactionid')) FROM pg_locks "
             "WHERE NOT fastpath AND ybdetails->>'node' IS NULL");
  const auto table = "foo";
  const auto key = "1";

  // Sets up a table, and launches a transaction acquiring for share lock on the key.
  auto session = ASSERT_RESULT(Init(table, key));
  // Launch a fast path transaction that ends up being blocked on the above transaction.
  auto status_future = std::async(std::launch::async, [&]() -> Status {
    auto conn = VERIFY_RESULT(Connect());
    return conn.ExecuteFormat("UPDATE $0 SET v=v+1 WHERE k=$1", table, key);
  });

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64>(kPgLocksQuery)), 0);

  // Try simulating GetLockStatus requests to tablets querying for fast path transactions alone.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_returning_old_transactions) = true;
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64>(kPgLocksQuery)), 0);

  ASSERT_OK(session.conn->CommitTransaction());
  ASSERT_OK(status_future.get());
}
class PgGetLockStatusTestRF3 : public PgGetLockStatusTest {
  size_t NumTabletServers() override {
    return 3;
  }

  void OverrideMiniClusterOptions(MiniClusterOptions* options) override {
    options->transaction_table_num_tablets = 4;
  }
};

TEST_F(PgGetLockStatusTestRF3, TestPrioritizeLocksOfOlderTxns) {
  constexpr auto table = "foo";
  constexpr auto key = "1";
  constexpr int kMinTxnAgeSeconds = 1;
  constexpr int kNumKeys = 10;

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, v INT)", table));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO $0 SELECT generate_series(1,$1), 0", table, kNumKeys));

  auto old_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(old_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(old_conn.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table, key));

  SleepFor(kMinTxnAgeSeconds * 1s * kTimeMultiplier);
  ASSERT_OK(setup_conn.Execute("SET yb_locks_max_transactions=1"));
  auto old_txn_id = ASSERT_RESULT(setup_conn.FetchRow<string>(
      "SELECT DISTINCT(ybdetails->>'transactionid') FROM pg_locks"));

  TestThreadHolder thread_holder;
  CountDownLatch started_txns{kNumKeys - 1};
  CountDownLatch done{1};
  for (auto i = 2 ; i <= kNumKeys ; i++) {
    thread_holder.AddThreadFunctor([this, &done, &started_txns, kMinTxnAgeSeconds, i, table] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table, i));
      SleepFor(kMinTxnAgeSeconds * 1s * kTimeMultiplier);
      started_txns.CountDown();
      ASSERT_TRUE(done.WaitFor(10s * kTimeMultiplier));
    });
  }
  ASSERT_TRUE(started_txns.WaitFor(5s * kTimeMultiplier));
  ASSERT_EQ(old_txn_id, ASSERT_RESULT(setup_conn.FetchRow<string>(
      "SELECT DISTINCT(ybdetails->>'transactionid') FROM pg_locks")));
  done.CountDown();
  thread_holder.WaitAndStop(20s * kTimeMultiplier);
}

#ifndef NDEBUG
TEST_F(PgGetLockStatusTestRF3, TestLocksOfSingleShardWaiters) {
  constexpr auto table = "foo";
  constexpr int kMinTxnAgeMs = 1;
  constexpr int kNumSingleShardWaiters = 2;
  constexpr int kSingleShardWaiterRetryMs = 5000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_single_shard_waiter_retry_ms) = kSingleShardWaiterRetryMs;
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.ExecuteFormat("CREATE TABLE $0(k INT PRIMARY KEY, v INT)", table));
  ASSERT_OK(setup_conn.ExecuteFormat("INSERT INTO $0 SELECT generate_series(1,10), 0", table));

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1 for update", table));

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumSingleShardWaiters; i++) {
    thread_holder.AddThreadFunctor([this, table, i] {
      SleepFor(i * 100 * 1ms);
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v=v+1 WHERE k=1", table));
    });
  }
  SleepFor(2s * kTimeMultiplier);

  ASSERT_OK(setup_conn.ExecuteFormat("SET yb_locks_min_txn_age='$0ms'", kMinTxnAgeMs));
  // When we set max num txns to 1, the distributed transaction should get prioritized over
  // the single shard waiters.
  ASSERT_OK(setup_conn.Execute("SET yb_locks_max_transactions=1"));
  auto num_txns = ASSERT_RESULT(setup_conn.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_locks WHERE fastpath"));
  ASSERT_EQ(num_txns, 0);

  ASSERT_OK(setup_conn.Execute("SET yb_locks_max_transactions=10"));
  num_txns = ASSERT_RESULT(setup_conn.FetchRow<PGUint64>(
      "SELECT COUNT(DISTINCT(waitstart)) FROM pg_locks WHERE fastpath"));
  ASSERT_EQ(num_txns, 2);
  // Assert that the single shard txns are waiting for locks.
  num_txns = ASSERT_RESULT(setup_conn.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_locks WHERE fastpath AND granted"));
  ASSERT_EQ(num_txns, 0);

  auto waiter1_start_time = ASSERT_RESULT(setup_conn.FetchRow<MonoDelta>(
      "SELECT waitstart FROM pg_locks WHERE fastpath order by waitstart limit 1"));
  // Now limit the results to just 1 single shard waiter and see that the older one is prioritized.
  ASSERT_OK(setup_conn.Execute("SET yb_locks_max_transactions=2"));
  num_txns = ASSERT_RESULT(setup_conn.FetchRow<PGUint64>(
      "SELECT COUNT(DISTINCT(waitstart)) FROM pg_locks WHERE fastpath"));
  ASSERT_EQ(num_txns, 1);
  ASSERT_EQ(waiter1_start_time, ASSERT_RESULT(setup_conn.FetchRow<MonoDelta>(
      "SELECT DISTINCT(waitstart) FROM pg_locks WHERE fastpath"
  )));
  // Wait for both the fast path waiters to re-enter the wait-queue post timing out after waiting
  // for kSingleShardWaiterRetryMs. Check that the start time of fast path waiter remains the same.
  std::atomic<int> refreshed_fastpath_waiters{0};
  yb::SyncPoint::GetInstance()->LoadDependency({
    {"ConflictResolver::OnConflictingTransactionsFound", "TestLocksOfSingleShardWaiters"}});
  yb::SyncPoint::GetInstance()->SetCallBack(
      "ConflictResolver::OnConflictingTransactionsFound",
      [&](void* arg) {
        refreshed_fastpath_waiters++;
        while (refreshed_fastpath_waiters.load() < 2) {}
      });
  yb::SyncPoint::GetInstance()->ClearTrace();
  yb::SyncPoint::GetInstance()->EnableProcessing();

  DEBUG_ONLY_TEST_SYNC_POINT("TestLocksOfSingleShardWaiters");
  ASSERT_EQ(waiter1_start_time, ASSERT_RESULT(setup_conn.FetchRow<MonoDelta>(
      "SELECT DISTINCT(waitstart) FROM pg_locks WHERE fastpath"
  )));
  ASSERT_OK(conn.CommitTransaction());
  thread_holder.WaitAndStop(5s * kTimeMultiplier);
}
#endif // NDEBUG

TEST_F(PgGetLockStatusTest, TestPgLocksOutputAfterTableRewrite) {
  const auto colo_db = "colo_db";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH colocation=true", colo_db));
  conn = ASSERT_RESULT(ConnectToDB(colo_db));
  std::set<std::string> table_names = {"foo", "bar"};
  for (const auto& table_name : table_names) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) $1", table_name,
        table_name == "foo" ? "WITH (colocation=false)" : ""));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT generate_series(1, 10), 0", table_name));
    // Perform a table rewrite so that the DocDB table UUID doesn't match the PG table oid.
    ASSERT_OK(conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN new_column SERIAL", table_name));
  }

  const auto key = "1";
  TestThreadHolder thread_holder;
  CountDownLatch fetched_locks{1};
  for (const auto& table_name : table_names) {
    thread_holder.AddThreadFunctor([this, &colo_db, &fetched_locks, table_name, key] {
      auto conn = ASSERT_RESULT(ConnectToDB(colo_db));
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", table_name, key));
      ASSERT_TRUE(fetched_locks.WaitFor(15s * kTimeMultiplier));
    });
  }

  SleepFor(5s * kTimeMultiplier);
  // Assert that the locks held belong to tables "foo", "bar".
  auto values = ASSERT_RESULT(conn.FetchRows<std::string>(
    "SELECT relname FROM pg_class WHERE oid IN (SELECT DISTINCT relation FROM pg_locks)"));
  ASSERT_EQ(values.size(), 2);
  for (const auto& relname : values) {
    ASSERT_TRUE(table_names.find(relname) != table_names.end());
  }
  fetched_locks.CountDown();
  thread_holder.WaitAndStop(25s * kTimeMultiplier);
}

TEST_F(PgGetLockStatusTest, TestPgLocksOutputAfterNodeOperations) {
  const auto kPgLocksQuery = "SELECT count(*) FROM pg_locks";
  const size_t num_tservers = cluster_->num_tablet_servers();

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_EQ(ASSERT_RESULT(setup_conn.FetchRow<int64>(kPgLocksQuery)), 0);
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo(k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO foo SELECT generate_series(1, 10), 0"));

  // Create a connection and acquire a few locks.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn.Fetch("SELECT * FROM foo WHERE k=1 FOR UPDATE"));

  SleepFor(FLAGS_heartbeat_interval_ms * 2ms * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(setup_conn.FetchRow<int64>(kPgLocksQuery)), 2);

  // Add a new tserver to the cluster and wait for it to become "live".
  ASSERT_OK(cluster_->AddTabletServer());
  const auto& mini_ts_1 = cluster_->mini_tablet_server(0);
  ASSERT_OK(WaitFor([&] {
    std::vector<master::TSInformationPB> tservers;
    if (!mini_ts_1->server()->GetLiveTServers(&tservers).ok()) {
      return false;
    }
    return tservers.size() == num_tservers + 1;
  }, 5s * kTimeMultiplier, "Failed to learn about new tserver from master"));

  // Move replicas off to the new tserver.
  ASSERT_OK(cluster_->AddTServerToBlacklist(0));
  WaitForLoadBalanceCompletion();

  // Assert that the pg_locks query returns correct results after the add node operation.
  ASSERT_EQ(ASSERT_RESULT(setup_conn.FetchRow<int64>(kPgLocksQuery)), 2);

  ASSERT_OK(cluster_->ClearBlacklist());
  // Move replicas back to the old tserver
  ASSERT_OK(cluster_->AddTServerToBlacklist(1));
  WaitForLoadBalanceCompletion();

  // Stop the new tserver, and restart the master so that the recently dead TS isn't
  // invoved in the master <-> tserver heartbeats.
  cluster_->mini_tablet_server(1)->server()->Shutdown();
  ASSERT_OK(cluster_->mini_master()->Restart());
  ASSERT_OK(WaitFor([&] {
    std::vector<master::TSInformationPB> tservers;
    if (!mini_ts_1->server()->GetLiveTServers(&tservers).ok()) {
      return false;
    }
    return tservers.size() == num_tservers;
  }, 5s * kTimeMultiplier, "Failed to learn about removed tserver from master"));

  // Assert that the pg_locks query returns correct results after the remove node operation.
  ASSERT_EQ(ASSERT_RESULT(setup_conn.FetchRow<int64>(kPgLocksQuery)), 2);

  ASSERT_OK(conn.CommitTransaction());
  ASSERT_EQ(ASSERT_RESULT(setup_conn.FetchRow<int64>(kPgLocksQuery)), 0);
}

// While fetching locks as part of pg_locks query, at every tablet, we make 2 passes on intentsdb.
// In the first pass, we scan through the reverse index section and store the values of records
// corresponding to the specified transactions in the lock status request. In the second pass, we
// seek to the stored values in the first step (which would be the provisional records), and parse
// the record to add an entry in the lock status response.
// The below test asserts that we don't run into errors if any provisional records get added/
// tombstoned amidst the two passes.
#ifndef NDEBUG
TEST_F(PgGetLockStatusTest, FetchLocksAmidstTransactionCommit) {
  const auto kPgLocksQuery = "SELECT COUNT(DISTINCT(ybdetails->>'transactionid')) FROM pg_locks";

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo(k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO foo SELECT generate_series(1, 10), 0"));

  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn1.Fetch("SELECT * FROM foo WHERE k=1 FOR UPDATE"));

  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn2.Fetch("SELECT * FROM foo WHERE k=2 FOR UPDATE"));

  SleepFor(FLAGS_heartbeat_interval_ms * 2ms * kTimeMultiplier);
  yb::SyncPoint::GetInstance()->LoadDependency({
    {"Tablet::GetLockStatus:1", "FetchLocksAmidstTransactionCommit:1"},
    {"Tablet::RemoveIntentsImpl", "Tablet::GetLockStatus:2"}});
  yb::SyncPoint::GetInstance()->ClearTrace();
  yb::SyncPoint::GetInstance()->EnableProcessing();

  auto result_future = std::async(std::launch::async, [&]() -> Result<int64> {
    auto conn = VERIFY_RESULT(Connect());
    return conn.FetchRow<int64>(kPgLocksQuery);
  });

  // Wait for the lock status request to scan the transaction reverse index section and store
  // provisional records of interest (value part of the txn-reverse index record).
  DEBUG_ONLY_TEST_SYNC_POINT("FetchLocksAmidstTransactionCommit:1");
  ASSERT_OK(conn1.CommitTransaction());
  // Wait for removal of the intents corresponding to the committed txn.
  DEBUG_ONLY_TEST_SYNC_POINT("FetchLocksAmidstTransactionCommit:2");
  // Though the lock status request intially would have found 4 rever index values of interest,
  // the final lock status respose should only have locks corresponding to the active transaction.
  ASSERT_EQ(ASSERT_RESULT(result_future.get()), 1);
  ASSERT_OK(conn2.CommitTransaction());
}
#endif // NDEBUG

} // namespace pgwrapper
} // namespace yb
