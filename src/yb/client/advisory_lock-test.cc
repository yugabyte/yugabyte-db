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

namespace yb {

using tserver::AdvisoryLockMode;
using tserver::YsqlAdvisoryLocksTable;
using tserver::YsqlAdvisoryLocksTableLockId;

namespace {

constexpr int kNumAdvisoryLocksTablets = 1;
constexpr uint32_t kDBOid = 10000;
constexpr YsqlAdvisoryLocksTableLockId kDefaultLockId{
    .db_oid = kDBOid, .class_oid = 0, .objid = 0, .objsubid = 1};

void CheckNumIntents(MiniCluster* cluster, size_t expected_num_records, const TableId& id = "") {
  auto peers = ListTableActiveTabletLeadersPeers(cluster, id);
  bool found = false;
  for (const auto& peer : peers) {
    if (!peer->IsLeaderAndReady()) {
      continue;
    }
    found = true;
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto count = ASSERT_RESULT(tablet->TEST_CountDBRecords(docdb::StorageDbType::kIntents));
    LOG(INFO) << peer->LogPrefix() << "records: " << count;
    ASSERT_EQ(count, expected_num_records);
  }
  ASSERT_TRUE(found) << "No active leader found";
}

[[nodiscard]] bool IsStatusSkipLocking(const Status& s) {
  return !s.ok() &&
         s.IsInternalError() &&
         TransactionError(s).value() == TransactionErrorCode::kSkipLocking;
}

[[nodiscard]] bool IsStatusLockNotFound(const Status& s) {
  return !s.ok() &&
         s.IsInternalError() &&
         TransactionError(s).value() == TransactionErrorCode::kLockNotFound;
}
} // namespace

class AdvisoryLockTest: public MiniClusterTestWithClient<MiniCluster> {
 public:
  void SetUp() override {
    MiniClusterTestWithClient::SetUp();

    SetFlags();
    cluster_.reset(new MiniCluster({.num_masters = 1, .num_tablet_servers = 3}));
    ASSERT_OK(cluster_->Start());

    ASSERT_OK(CreateClient());
    ASSERT_OK(WaitForCreateTableToFinishAndLoadTable());
  }

 protected:
  Status WaitForCreateTableToFinishAndLoadTable() {
    client::YBTableName table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName,
        std::string(tserver::kPgAdvisoryLocksTableName));
    RETURN_NOT_OK(client_->WaitForCreateTableToFinish(
        table_name, CoarseMonoClock::Now() + 10s * kTimeMultiplier));
    advisory_locks_table_.emplace(ValueAsFuture(client_.get()));
    table_ = VERIFY_RESULT(advisory_locks_table_->TEST_GetTable());
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

  Result<client::YBTransactionPtr> StartTransaction(
      IsolationLevel level = IsolationLevel::SNAPSHOT_ISOLATION) {
    auto* server = cluster_->mini_tablet_server(0)->server();
    auto& pool = server->TransactionPool();
    auto txn = VERIFY_RESULT(pool.TakeAndInit(SNAPSHOT_ISOLATION, TransactionRpcDeadline()));
    RETURN_NOT_OK(txn->SetPgTxnStart(server->Clock()->Now().GetPhysicalValueMicros(), false));
    return txn;
  }

  static Status Commit(client::YBTransactionPtr txn) {
    return txn->CommitFuture(TransactionRpcDeadline()).get();
  }

  virtual void SetFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_advisory_locks_tablets) = kNumAdvisoryLocksTablets;
  }

  auto MakeLockOp(
      const YsqlAdvisoryLocksTable::LockId& lock_id,
      AdvisoryLockMode mode = AdvisoryLockMode::LOCK_EXCLUSIVE) {
    return advisory_locks_table_->MakeLockOp(lock_id, mode, /* wait = */ true);
  }

  auto MakeLockOpNoWait(
      const YsqlAdvisoryLocksTable::LockId& lock_id,
      AdvisoryLockMode mode = AdvisoryLockMode::LOCK_EXCLUSIVE) {
    return advisory_locks_table_->MakeLockOp(lock_id, mode, /* wait = */ false);
  }

  auto MakeUnlockOp(
      const YsqlAdvisoryLocksTable::LockId& lock_id,
      AdvisoryLockMode mode = AdvisoryLockMode::LOCK_EXCLUSIVE) {
    return advisory_locks_table_->MakeUnlockOp(lock_id, mode);
  }

  client::YBTablePtr table_;
  std::optional<YsqlAdvisoryLocksTable> advisory_locks_table_;
};

TEST_F(AdvisoryLockTest, TestAdvisoryLockTableCreated) {
  ASSERT_OK(CheckNumTablets());
}

TEST_F(AdvisoryLockTest, AcquireXactExclusiveLock_Int8) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  // Acquire the same lock in the same session with non blocking mode.
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  CheckNumIntents(cluster_.get(), 5, table_->id());
  ASSERT_OK(Commit(txn));
}

TEST_F(AdvisoryLockTest, AcquireXactExclusiveLock_Int4) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  constexpr YsqlAdvisoryLocksTableLockId kLockId{kDBOid, 1, 1, 2};
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kLockId))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  // Acquire the same lock in the same session with non blocking mode.
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOpNoWait(kLockId))));
  CheckNumIntents(cluster_.get(), 5, table_->id());
  ASSERT_OK(Commit(txn));
}

TEST_F(AdvisoryLockTest, TryAcquireXactExclusiveLock) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  // Acquire the same lock in a different session with non blocking mode.
  auto s = session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOpNoWait(kDefaultLockId)));
  ASSERT_TRUE(IsStatusSkipLocking(s)) << s;

  ASSERT_OK(Commit(txn));
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOpNoWait(kDefaultLockId))));

  ASSERT_OK(Commit(txn2));
}

TEST_F(AdvisoryLockTest, AcquireAdvisoryLockWithoutTransaction) {
  auto session = NewSession();
  auto s = session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId)));
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
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  auto another_db_lock_id = kDefaultLockId;
  ++another_db_lock_id.db_oid;
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(another_db_lock_id))));
  CheckNumIntents(cluster_.get(), 6, table_->id());
}

TEST_F(AdvisoryLockTest, ShareLocks) {
  // Locks acquired in different DBs shouldn't block each other.
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(
      kDefaultLockId, AdvisoryLockMode::LOCK_SHARE))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(
      kDefaultLockId, AdvisoryLockMode::LOCK_SHARE))));
  CheckNumIntents(cluster_.get(), 6, table_->id());

  auto s = session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOpNoWait(kDefaultLockId)));
  ASSERT_TRUE(IsStatusSkipLocking(s)) << s;
}

TEST_F(AdvisoryLockTest, WaitOnConflict) {
  auto session = NewSession();
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  CheckNumIntents(cluster_.get(), 3, table_->id());

  std::atomic_bool session2_locked{false};
  {
    auto session2 = NewSession();
    session2->SetTransaction(ASSERT_RESULT(StartTransaction()));
    TestThreadHolder thread_holder;
    thread_holder.AddThreadFunctor([session2, this, &session2_locked] {
      ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
      session2_locked.store(true);
    });
    SleepFor(1s);
    ASSERT_FALSE(session2_locked.load());
    ASSERT_OK(Commit(txn));
  }
  ASSERT_TRUE(session2_locked.load());
}

TEST_F(AdvisoryLockTest, LeaderChange) {
  // Acquired locks should be found after leader change.
  auto session = NewSession();
  session->SetTransaction(ASSERT_RESULT(StartTransaction()));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
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
  session2->SetTransaction(ASSERT_RESULT(StartTransaction()));
  // Acquire the same lock in a different session with non blocking mode.
  auto s = session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOpNoWait(kDefaultLockId)));
  ASSERT_TRUE(IsStatusSkipLocking(s)) << s;
  CheckNumIntents(cluster_.get(), 3, table_->id());
}

TEST_F(AdvisoryLockTest, UnlockAllAdvisoryLocks) {
  auto session = NewSession();

  // TODO(advisory-lock #24079): This transaction should be a virtual transaction.
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  // There would be 1 for txn metadata entry.
  // Each lock will have 1 txn reverse index + 1 primary intent.
  CheckNumIntents(cluster_.get(), 7, table_->id());

  // Release all locks.
  ASSERT_OK(session->TEST_ApplyAndFlush(
      ASSERT_RESULT(advisory_locks_table_->MakeUnlockAllOp(kDBOid))));
  // Should be just txn metadata left unremoved.
  CheckNumIntents(cluster_.get(), 1, table_->id());

  // Ensure all locks are actually released so that concurrent lock request won't be blocked.
  auto session2 = NewSession();
  auto txn2 = ASSERT_RESULT(StartTransaction());
  session2->SetTransaction(txn2);
  ASSERT_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  CheckNumIntents(cluster_.get(), 4, table_->id());

  ASSERT_OK(Commit(txn));
  ASSERT_OK(Commit(txn2));
}

TEST_F(AdvisoryLockTest, Unlock) {
  auto session = NewSession();

  // TODO(advisory-lock #24079): This transaction should be a virtual transaction.
  auto txn = ASSERT_RESULT(StartTransaction());
  session->SetTransaction(txn);

  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
  // There would be 1 for txn metadata entry.
  // Each lock will have 1 txn reverse index + 1 primary intent.
  CheckNumIntents(cluster_.get(), 7, table_->id());

  // Releasing a non-existing share lock should fail.
  ASSERT_TRUE(IsStatusLockNotFound(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeUnlockOp(
      kDefaultLockId, AdvisoryLockMode::LOCK_SHARE)))));

  std::atomic_bool session2_locked{false};
  {
    auto session2 = NewSession();
    TestThreadHolder thread_holder;
    thread_holder.AddThreadFunctor([session2, this, &session2_locked] {
      session2->SetTransaction(ASSERT_RESULT(StartTransaction()));
      CHECK_OK(session2->TEST_ApplyAndFlush(ASSERT_RESULT(MakeLockOp(kDefaultLockId))));
      session2_locked.store(true);
    });

    ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeUnlockOp(kDefaultLockId))));
    CheckNumIntents(cluster_.get(), 5, table_->id());
    SleepFor(1s);
    ASSERT_FALSE(session2_locked.load());
    ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeUnlockOp(kDefaultLockId))));
    CheckNumIntents(cluster_.get(), 3, table_->id());
    SleepFor(1s);
    ASSERT_FALSE(session2_locked.load());
    ASSERT_OK(session->TEST_ApplyAndFlush(ASSERT_RESULT(MakeUnlockOp(kDefaultLockId))));
    CheckNumIntents(cluster_.get(), 1, table_->id());
  }
  ASSERT_TRUE(session2_locked.load());

  // All locks have been released. Any unlock requests should fail.
  ASSERT_TRUE(IsStatusLockNotFound(session->TEST_ApplyAndFlush(
      ASSERT_RESULT(MakeUnlockOp(kDefaultLockId)))));
  ASSERT_OK(Commit(txn));
}

} // namespace yb
