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

#include "yb/client/meta_cache.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/async_util.h"
#include "yb/util/monotime.h"
#include "yb/yql/pgwrapper/geo_transactions_test_base.h"
#include "yb/yql/pgwrapper/pg_locks_test_base.h"

DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(force_global_transactions);
DECLARE_bool(TEST_mock_cancel_unhosted_transactions);
DECLARE_bool(TEST_fail_abort_request_with_try_again);

using namespace std::literals;
using std::string;

namespace yb {
namespace pgwrapper {

using client::internal::RemoteTablet;
using client::internal::RemoteTabletPtr;

using tserver::TabletServerServiceProxy;
using tserver::CancelTransactionRequestPB;
using tserver::CancelTransactionResponsePB;

class PgCancelTransactionTest : public PgLocksTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
    // Skipping PgLocksTestBase::SetUp() and going with a custom setup.
    GeoTransactionsTestBase::SetUp();
    SetupTablespaces();
    PgLocksTestBase::InitTSProxies();
  }

  size_t NumTabletServers() override {
    return 3;
  }

  size_t NumRegions() override {
    return 3;
  }

  Result<std::string> GetLeaderPeerUuid(const TabletId& tablet_id) {
    auto deadline = MonoTime::Now() + MonoDelta::FromSeconds(2 * kTimeMultiplier);
    auto remote_ts_or_status_future = MakeFuture<Result<RemoteTabletPtr>>([&](auto callback) {
      client_->LookupTabletById(
          tablet_id, /* table =*/ nullptr, master::IncludeInactive::kFalse,
          master::IncludeDeleted::kFalse, ToCoarse(deadline),
          [callback] (const auto& lookup_result) {
            callback(lookup_result);
          }, client::UseCache::kFalse);
    });

    scoped_refptr<RemoteTablet> remote_tablet = VERIFY_RESULT(remote_ts_or_status_future.get());
    if (!remote_tablet || !remote_tablet->LeaderTServer()) {
      return STATUS_FORMAT(TryAgain, "");
    }

    return remote_tablet->LeaderTServer()->permanent_uuid();
  }

  // We mimic behaviour similar to that of PgClientServiceImpl::Impl::CancelTransaction. Reuqests
  // to Tablet Servers are made synchronously to avoid additional complexity.
  Result<CancelTransactionResponsePB> DoCancelTransaction(
      const TransactionId& txn_id, const TabletId& status_tablet_id = "") {

    std::string remote_ts_uuid = "";
    CancelTransactionRequestPB req;
    CancelTransactionResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(kTimeoutMs * 1ms * kTimeMultiplier);

    req.set_transaction_id(txn_id.data(), txn_id.size());
    if (!status_tablet_id.empty()) {
      req.set_status_tablet_id(status_tablet_id);
      remote_ts_uuid = VERIFY_RESULT(GetLeaderPeerUuid(status_tablet_id));
    }

    auto txn_found = false;
    CancelTransactionResponsePB success_resp;
    for (auto& proxy : get_ts_proxies(remote_ts_uuid)) {
      resp.Clear();
      controller.Reset();

      RETURN_NOT_OK(proxy->CancelTransaction(req, &resp, &controller));

      // Return the resp on seeing any other error than NOT_FOUND
      if (resp.has_error()) {
        const auto& s = StatusFromPB(resp.error().status());
        if (!s.IsNotFound()) {
          return resp;
        }
      } else {
        txn_found = true;
        success_resp.CopyFrom(resp);
      }
    }

    return txn_found ? success_resp : resp;
  }

  Status CancelTransaction(const TransactionId& txn_id,
                           const TabletId& status_tablet_id = "") {
    auto resp = VERIFY_RESULT(DoCancelTransaction(txn_id, status_tablet_id));
    return resp.has_error() ? StatusFromPB(resp.error().status()) : Status::OK();
  }

  void CreateAndCancelTransaction(bool include_status_tablet) {
    const auto table_name = "foo";
    const auto key = "1";
    auto session = ASSERT_RESULT(Init(table_name, key));
    const auto status_tablet_id =
        include_status_tablet ? ASSERT_RESULT(GetSingularStatusTablet()) : "";

    ASSERT_OK(CancelTransaction(session.txn_id, status_tablet_id));
    ASSERT_NOK(session.conn->CommitTransaction());

    // Trying to cancel a non-existent txn (or an already canceled txn) should result in NotFound.
    auto s = CancelTransaction(session.txn_id, status_tablet_id);
    ASSERT_TRUE(s.IsNotFound()) << s;
  }

  Result<TabletId> CreateLocalTableAndGetTabletId(const TableId& table_name) {
    const auto tablespace = "tablespace1";
    auto setup_conn = VERIFY_RESULT(Connect());

    RETURN_NOT_OK(setup_conn.ExecuteFormat(
        "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) TABLESPACE $1 SPLIT INTO 1 TABLETS",
        table_name, tablespace));

    RETURN_NOT_OK(setup_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT generate_series(1, 10), 0", table_name));

    return VERIFY_RESULT(GetSingularTabletOfTable(table_name));
  }

  Result<TestSession> SetUpTransactionPromotion(const TableId& local_table,
                                                const TableId& global_table,
                                                const std::string& key) {
    // Create a global table, a local table, and initiate a local transaction.
    VERIFY_RESULT(CreateTableAndGetTabletId(global_table));
    VERIFY_RESULT(CreateLocalTableAndGetTabletId(local_table));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
    return Init(local_table, key, false /* create table */);
  }
};

TEST_F(PgCancelTransactionTest, TestCancelingWithoutStatusTablet) {
  CreateAndCancelTransaction(false /* include status tablet in the cancel request */);
}

TEST_F(PgCancelTransactionTest, TestCancelingWithStatusTablet) {
  CreateAndCancelTransaction(true /* include status tablet in the cancel request */);
}

TEST_F(PgCancelTransactionTest, TestCancelWithInvalidStatusTablet) {
  auto session = ASSERT_RESULT(Init("foo" /* table name */, "1" /* key */));
  auto s = CancelTransaction(session.txn_id, session.first_involved_tablet);
  ASSERT_TRUE(s.IsIllegalState()) << s;
}

TEST_F(PgCancelTransactionTest, TestCancelationPostTxnPromotion) {
  const auto global_table = "foo";
  const auto local_table = "bar";
  const auto key = "1";
  auto session = ASSERT_RESULT(SetUpTransactionPromotion(local_table, global_table, key));

  // Operate on a global table to trigger transaction promotion.
  ASSERT_OK(session.conn->ExecuteFormat("UPDATE $0 SET v=v+10 WHERE k=$1", global_table, key));
  ASSERT_OK(CancelTransaction(session.txn_id));
  ASSERT_NOK(session.conn->CommitTransaction());
}

// Assert unsuccessfull cancelation attempt when the new status tablet reports failure, but the old
// status tablet reports success.
TEST_F(PgCancelTransactionTest, TestFailedCancelationPostTxnPromotion) {
  const auto global_table = "foo";
  const auto local_table = "bar";
  const auto key = "1";
  auto session = ASSERT_RESULT(SetUpTransactionPromotion(local_table, global_table, key));

  // Operate on a global table to trigger transaction promotion.
  ASSERT_OK(session.conn->ExecuteFormat("UPDATE $0 SET v=v+10 WHERE k=$1", global_table, key));
  // Mock behavior such that all status tablets except the original one hosting the current txn
  // will report that they aborted the txn, while the original one reports failure.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_mock_cancel_unhosted_transactions) = true;
  // Make the original status tablet report a TryAgain status.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_abort_request_with_try_again) = true;

  const auto& s = CancelTransaction(session.txn_id);
  ASSERT_TRUE(s.IsTryAgain()) << s;
  ASSERT_OK(session.conn->ExecuteFormat("UPDATE $0 SET v=v+100 WHERE k=$1", global_table, key));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_abort_request_with_try_again) = false;
  ASSERT_OK(session.conn->CommitTransaction());
}

// Simulate behavior such that both the old and the new status tablets assume they are responsible
// for the transaction. Assert that cancelation goes through successfully.
TEST_F(PgCancelTransactionTest, TestCancelationAmidstTxnPromotion) {
  const auto global_table = "foo";
  const auto local_table = "bar";
  const auto key = "1";
  auto session = ASSERT_RESULT(SetUpTransactionPromotion(local_table, global_table, key));

  // Make the old status tablet ignore Abort requests that will arise due to txn promotion.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_abort_request_with_try_again) = true;
  // Operate on a global table to trigger transaction promotion.
  ASSERT_OK(session.conn->ExecuteFormat("UPDATE $0 SET v=v+10 WHERE k=$1", global_table, key));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_abort_request_with_try_again) = false;

  // Test successfull cancelation when both status tablets assume they are responsible for the txn.
  ASSERT_OK(CancelTransaction(session.txn_id));
  ASSERT_NOK(session.conn->CommitTransaction());
}

} // namespace pgwrapper
} // namespace yb
