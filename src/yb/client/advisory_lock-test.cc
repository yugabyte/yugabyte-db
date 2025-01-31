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

#include <future>
#include <optional>

#include "yb/client/meta_cache.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/transaction_error.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_defaults.h"

#include "yb/rpc/sidecars.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ysql_advisory_lock_table.h"

#include "yb/util/std_util.h"
#include "yb/util/test_thread_holder.h"

DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_uint32(num_advisory_locks_tablets);
DECLARE_bool(ysql_yb_enable_advisory_locks);

namespace yb {

const int kNumAdvisoryLocksTablets = 1;
const uint32_t kDBOid = 10000;

void CheckNumIntents(MiniCluster* cluster, size_t expected_num_records, const TableId& id = "") {
  auto peers = ListTableActiveTabletLeadersPeers(cluster, id);
  bool found = false;
  for (const auto& peer : peers) {
    if (!peer->IsLeaderAndReady()) {
      continue;
    }
    found = true;
    auto count = ASSERT_RESULT(
        peer->tablet()->TEST_CountDBRecords(docdb::StorageDbType::kIntents));
    LOG(INFO) << peer->LogPrefix() << "records: " << count;
    ASSERT_EQ(count, expected_num_records);
  }
  ASSERT_TRUE(found) << "No active leader found";
}

bool IsStatusSkipLocking(const Status& s) {
  if (s.ok() || !s.IsInternalError()) {
    return false;
  }
  const TransactionError txn_err(s);
  return txn_err.value() == TransactionErrorCode::kSkipLocking;
}

class AdvisoryLockTest: public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    MiniClusterTestWithClient::SetUp();

    SetFlags();
    auto opts = MiniClusterOptions();
    opts.num_tablet_servers = 3;
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(CreateClient());
    if (ANNOTATE_UNPROTECTED_READ(FLAGS_ysql_yb_enable_advisory_locks)) {
      ASSERT_OK(WaitForCreateTableToFinishAndLoadTable());
    }
    sidecars_ = std::make_unique<rpc::Sidecars>();
  }

  Status WaitForCreateTableToFinishAndLoadTable() {
    client::YBTableName table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName,
        std::string(tserver::kPgAdvisoryLocksTableName));
    RETURN_NOT_OK(client_->WaitForCreateTableToFinish(
        table_name, CoarseMonoClock::Now() + 10s * kTimeMultiplier));
    advisory_locks_table_.emplace(ValueAsFuture(client_.get()));
    table_ = VERIFY_RESULT(advisory_locks_table_->GetTable());
    return Status::OK();
  }

  Status CheckNumTablets() {
    SCHECK_EQ(VERIFY_RESULT(GetTablets()).size(),
             ANNOTATE_UNPROTECTED_READ(FLAGS_num_advisory_locks_tablets),
             IllegalState, "tablet number mismatch");
    return Status::OK();
  }

  Result<std::vector<client::internal::RemoteTabletPtr>> GetTablets() {
    CHECK_NOTNULL(table_.get());
    return client_->LookupAllTabletsFuture(table_, CoarseMonoClock::Now() + 10s).get();
  }

  Result<client::YBTablePtr> GetTable() {
    return tserver::YsqlAdvisoryLocksTable(ValueAsFuture(client_.get())).GetTable();
  }

  Result<client::YBTransactionPtr> StartTransaction(
      IsolationLevel level = IsolationLevel::SNAPSHOT_ISOLATION) {
    auto* server = cluster_->mini_tablet_server(0)->server();
    auto& pool = server->TransactionPool();
    auto txn = VERIFY_RESULT(pool.TakeAndInit(SNAPSHOT_ISOLATION, TransactionRpcDeadline()));
    RETURN_NOT_OK(txn->SetPgTxnStart(server->Clock()->Now().GetPhysicalValueMicros()));
    return txn;
  }

  static Status Commit(client::YBTransactionPtr txn) {
    return txn->CommitFuture(TransactionRpcDeadline()).get();
  }

 protected:
  virtual void SetFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_advisory_locks_tablets) = kNumAdvisoryLocksTablets;
  }

  std::unique_ptr<rpc::Sidecars> sidecars_;
  client::YBTablePtr table_;
  std::optional<tserver::YsqlAdvisoryLocksTable> advisory_locks_table_;
};

TEST_F(AdvisoryLockTest, TestAdvisoryLockTableCreated) {
  ASSERT_OK(CheckNumTablets());
}

TEST_F(AdvisoryLockTest, AcquireXactExclusiveLock_Int8) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  // Acquire the same lock in the same session with non blocking mode.
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 5, table_->id());
  ASSERT_OK(Commit(txn));
}

TEST_F(AdvisoryLockTest, AcquireXactExclusiveLock_Int4) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 1, 1, 2, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  // Acquire the same lock in the same session with non blocking mode.
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 1, 1, 2, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 5, table_->id());
  ASSERT_OK(Commit(txn));
}

TEST_F(AdvisoryLockTest, TryAcquireXactExclusiveLock) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  // Acquire the same lock in a different session with non blocking mode.
  auto s = session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ false, sidecars_.get())));
  ASSERT_TRUE(IsStatusSkipLocking(s)) << s;

  ASSERT_OK(Commit(txn));
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ false, sidecars_.get()))));

  ASSERT_OK(Commit(txn2));
}

TEST_F(AdvisoryLockTest, AcquireAdvisoryLockWithoutTransaction) {
  auto session = NewSession();
  auto s = session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get())));
  LOG(INFO) << s;
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsInvalidArgument());
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "No transaction found in write batch");
  CheckNumIntents(cluster_.get(), 0, table_->id());
}

TEST_F(AdvisoryLockTest, AcquireLocksInDifferentDBs) {
  // Locks acquired in different DBs shouldn't block each other.
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid + 1, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 6, table_->id());
}

TEST_F(AdvisoryLockTest, ShareLocks) {
  // Locks acquired in different DBs shouldn't block each other.
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_SHARE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_SHARE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 6, table_->id());

  auto s = session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ false, sidecars_.get())));
  ASSERT_TRUE(IsStatusSkipLocking(s)) << s;
}

TEST_F(AdvisoryLockTest, WaitOnConflict) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  TestThreadHolder thread_holder;
  std::atomic_bool session2_locked{false};
  thread_holder.AddThreadFunctor([session2, this, &session2_locked] {
    ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
        /* wait= */ true, sidecars_.get()))));
    session2_locked.store(true);
  });
  SleepFor(1s);
  ASSERT_FALSE(session2_locked.load());
  ASSERT_OK(Commit(txn));
  thread_holder.JoinAll();
}

TEST_F(AdvisoryLockTest, LeaderChange) {
  // Acquired locks should be found after leader change.
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto tablets = ASSERT_RESULT(GetTablets());
  ASSERT_EQ(tablets.size(), 1);
  auto id = tablets[0]->tablet_id();
  auto peer = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), id));

  // Stepdown the leader.
  auto map = ASSERT_RESULT(itest::CreateTabletServerMap(
      ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>()),
      &client_->proxy_cache()));
  ASSERT_OK(itest::LeaderStepDown(
      map[peer->permanent_uuid()].get(), id, /* new_leader= */ nullptr, 10s));

  // Another session shouldn't be able to acquire the lock.
  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  // Acquire the same lock in a different session with non blocking mode.
  auto s = session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ false, sidecars_.get())));
  ASSERT_TRUE(IsStatusSkipLocking(s)) << s;
  CheckNumIntents(cluster_.get(), 3, table_->id());
}

TEST_F(AdvisoryLockTest, UnlockAllAdvisoryLocks) {
  auto session = NewSession();

  // TODO(advisory-lock #24079): This transaction should be a virtual transaction.
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  // There would be 1 for txn metadata entry.
  // Each lock will have 1 txn reverse index + 1 primary intent.
  CheckNumIntents(cluster_.get(), 7, table_->id());

  // Rlease all locks.
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateUnlockAllOp(
      kDBOid, sidecars_.get()))));
  // Should be just txn metadata left unremoved.
  CheckNumIntents(cluster_.get(), 1, table_->id());

  // Ensure all locks are actually released so that concurrent lock request won't be blocked.
  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 4, table_->id());

  ASSERT_OK(Commit(txn));
  ASSERT_OK(Commit(txn2));
}

TEST_F(AdvisoryLockTest, Unlock) {
  auto session = NewSession();

  // TODO(advisory-lock #24079): This transaction should be a virtual transaction.
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);

  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
      /* wait= */ true, sidecars_.get()))));
  // There would be 1 for txn metadata entry.
  // Each lock will have 1 txn reverse index + 1 primary intent.
  CheckNumIntents(cluster_.get(), 7, table_->id());

  // Releasing a non-existing share lock should fail.
  ASSERT_TRUE(IsStatusSkipLocking(
      session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateUnlockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_SHARE, sidecars_.get())))));

  std::atomic_bool session2_locked{false};
  auto session2 = NewSession();
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([session2, this, &session2_locked] {
    auto txn2 = ASSERT_RESULT(StartTransaction());
    session2->SetTransaction(txn2);
    CHECK_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateLockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE,
        /* wait= */ true, sidecars_.get()))));
    session2_locked.store(true);
  });

  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateUnlockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 5, table_->id());
  SleepFor(1s);
  ASSERT_FALSE(session2_locked.load());
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateUnlockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 3, table_->id());
  SleepFor(1s);
  ASSERT_FALSE(session2_locked.load());
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateUnlockOp(
      kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE, sidecars_.get()))));
  CheckNumIntents(cluster_.get(), 1, table_->id());
  thread_holder.JoinAll();
  ASSERT_TRUE(session2_locked.load());

  // All locks have been released. Any unlock requests should fail.
  ASSERT_TRUE(IsStatusSkipLocking(
    session->TEST_ApplyAndFlush(ASSERT_RESULT(advisory_locks_table_->CreateUnlockOp(
        kDBOid, 0, 0, 1, PgsqlLockRequestPB::PG_LOCK_EXCLUSIVE, sidecars_.get())))));
  ASSERT_OK(Commit(txn));
}

class AdvisoryLocksDisabledTest : public AdvisoryLockTest {
 protected:
  void SetFlags() override {
    AdvisoryLockTest::SetFlags();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = false;
  }
};

TEST_F(AdvisoryLocksDisabledTest, ToggleAdvisoryLockFlag) {
  // Wait for the background task to run a few times.
  SleepFor(FLAGS_catalog_manager_bg_task_wait_ms * kTimeMultiplier * 3ms);
  auto res = GetTable();
  ASSERT_NOK(res);
  ASSERT_TRUE(res.status().IsNotSupported());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_advisory_locks) = true;
  ASSERT_OK(WaitForCreateTableToFinishAndLoadTable());
  ASSERT_OK(CheckNumTablets());
}

} // namespace yb
