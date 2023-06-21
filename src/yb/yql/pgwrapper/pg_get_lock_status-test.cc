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

#include "yb/yql/pgwrapper/pg_locks_test_base.h"

DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(force_global_transactions);
DECLARE_bool(TEST_mock_tablet_hosts_all_transactions);
DECLARE_bool(TEST_fail_abort_request_with_try_again);

using namespace std::literals;
using std::string;

// Returns a status of 'status_type' with message 'msg' when 'condition' evaluates to false.
#define RETURN_ON_FALSE(condition, status_type, msg) do { \
  if (!(condition)) { \
    return STATUS_FORMAT(status_type, msg); \
  } \
} while (false);

namespace yb {
namespace pgwrapper {

using tserver::TabletServerServiceProxy;

class PgGetLockStatusTest : public PgLocksTestBase {
 protected:
  Result<size_t> GetNumTxnsInLockStatusResponse(const tserver::GetLockStatusResponsePB& resp) {
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    std::unordered_set<std::string> txn_ids_set;
    for (const auto& tablet_lock_info : resp.tablet_lock_infos()) {
      for (const auto& txn_lock_pair : tablet_lock_info.transaction_locks()) {
        RETURN_ON_FALSE(!txn_lock_pair.first.empty(),
                        IllegalState,
                        "Expected to see non-empty transaction id.");
        txn_ids_set.insert(txn_lock_pair.first);
      }
    }
    return txn_ids_set.size();
  }
};


TEST_F(PgGetLockStatusTest, TestGetLockStatusWithCustomTransactionsList) {
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

TEST_F(PgGetLockStatusTest, YB_DISABLE_TEST_IN_TSAN(TestLocksFromWaitQueue)) {
  const auto table = "foo";
  const auto key = "1";
  auto session = ASSERT_RESULT(Init(table, key));

  // Create a second transaction and make it wait on the earlier txn. This txn won't acquire
  // any locks and will wait in the wait-queue.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto status_future = ASSERT_RESULT(
      ExpectBlockedAsync(&conn, Format("UPDATE $0 SET v=v+10 WHERE k=$1", table, key)));

  // Assert that locks corresponding to the waiter txn as well are returned in
  // GetLockStatusResponsePB.
  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));
  const auto& tablet_id = session.first_involved_tablet;
  auto resp = ASSERT_RESULT(GetLockStatus(tablet_id));
  auto num_txns = ASSERT_RESULT(GetNumTxnsInLockStatusResponse(resp));
  ASSERT_EQ(num_txns, 2);

  ASSERT_TRUE(conn.IsBusy());
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

} // namespace pgwrapper
} // namespace yb
