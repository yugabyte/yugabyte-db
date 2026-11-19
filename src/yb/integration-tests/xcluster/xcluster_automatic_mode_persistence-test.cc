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

#include <functional>
#include <mutex>

#include <gtest/gtest.h>

#include "yb/client/yb_table_name.h"

#include "yb/common/pgsql_error.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver.messages.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/xcluster_poller.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(abort_active_txns_during_xrepl_bootstrap);
DECLARE_bool(enable_persistence_gate);
DECLARE_bool(enforce_xcluster_guarded_lease);
DECLARE_int32(ysql_client_read_write_timeout_ms);

using namespace std::literals;

namespace yb {

using pgwrapper::PGConn;
using pgwrapper::PGOid;

class XClusterAutomaticModePersistenceTest : public XClusterDDLReplicationSwitchoverTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = true;
    TEST_SETUP_SUPER(XClusterDDLReplicationSwitchoverTest);
  }

 protected:
  static constexpr auto kTableName = "t";

  // Creates table kTableName on the current source and waits for it to be replicated.
  Status CreateTable() {
    RETURN_NOT_OK(producer_conn_->ExecuteFormat(
        "CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 3 TABLETS", kTableName));
    return WaitForSafeTimeToAdvanceToNow();
  }

  Result<int64_t> CountRows(Cluster& cluster) {
    auto conn = VERIFY_RESULT(cluster.ConnectToDB(namespace_name));
    return conn.FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", kTableName));
  }

  // Expects kTableName to be empty on both universes.
  void ExpectNoRowsOnEitherUniverse() {
    // Reads on the target happen at its xCluster safe time; wait for that to reach the current
    // time so that they see everything written so far.
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    ASSERT_EQ(ASSERT_RESULT(CountRows(*cluster_A_)), 0);
    ASSERT_EQ(ASSERT_RESULT(CountRows(*cluster_B_)), 0);
  }

  // Waits until no TServer of cluster has a poller for replication_group_id.
  Status WaitForNoPollers(
      Cluster& cluster, const xcluster::ReplicationGroupId& replication_group_id) {
    return LoggedWaitFor(
        [&cluster, &replication_group_id]() -> Result<bool> {
          for (const auto& mini_tserver : cluster.mini_cluster_->mini_tablet_servers()) {
            auto* tserver = mini_tserver->server();
            tserver::XClusterConsumerIf* xcluster_consumer;
            if (!tserver || !(xcluster_consumer = tserver->GetXClusterConsumer())) {
              continue;
            }
            for (const auto& poller : xcluster_consumer->TEST_ListPollers()) {
              if (poller->GetProducerTabletInfo().replication_group_id == replication_group_id) {
                return false;
              }
            }
          }
          return true;
        },
        propagation_timeout_, Format("Wait for pollers of $0 to stop", replication_group_id));
  }

  // Expects status to be a refusal because conn's database is blocked by the persistence gate.
  static void ExpectBlocked(const Status& status, PGConn& conn) {
    ASSERT_NOK(status);
    ASSERT_EQ(PgsqlError(status).value(), YBPgErrorCode::YB_PG_READ_ONLY_SQL_TRANSACTION) << status;
    const auto database_oid = ASSERT_RESULT(
        conn.FetchRow<PGOid>("SELECT oid FROM pg_database WHERE datname = current_database()"));
    ASSERT_STR_CONTAINS(
        status.ToString(),
        Format("Writes to the database with OID $0 are temporarily disabled", database_oid));
  }

  // Helper class for controlling when a sync point pauses.  The first call of sync_point for which
  // should_pause_func (if given) returns true when passed the sync point's argument waits there
  // until Release is called or the controller is destroyed; all other calls pass through.
  class SyncPointPauseController {
   public:
    SyncPointPauseController(
        const std::string& sync_point, std::function<bool(void*)> should_pause_func = nullptr) {
      SyncPoint::GetInstance()->SetCallBack(
          sync_point, [this, should_pause_func = std::move(should_pause_func)](void* arg) {
            {
              std::lock_guard lock(mutex_);
              if (paused_) {
                return;
              }
              if (should_pause_func && !should_pause_func(arg)) {
                return;
              }
              paused_ = true;
            }
            reached_.CountDown();
            release_.Wait();
          });
      SyncPoint::GetInstance()->EnableProcessing();
    }

    ~SyncPointPauseController() {
      // Release first: ClearAllCallBacks waits for running callbacks, including a paused one.
      Release();
      SyncPoint::GetInstance()->DisableProcessing();
      SyncPoint::GetInstance()->ClearAllCallBacks();
    }

    Status WaitUntilPaused() {
      SCHECK(reached_.WaitFor(30s * kTimeMultiplier), TimedOut, "No call paused at the sync point");
      return Status::OK();
    }

    void Release() { release_.CountDown(); }

   private:
    std::mutex mutex_;
    bool paused_ GUARDED_BY(mutex_) = false;
    CountDownLatch reached_{1};
    CountDownLatch release_{1};
  };
};

// Checks that a distributed transaction that wrote on the old source before switchover cannot
// commit after it, and that none of its writes are visible on either universe.
TEST_F(XClusterAutomaticModePersistenceTest, DistributedTransactionSpanningSwitchover) {
  constexpr int kRowCount = 10;
  ASSERT_OK(SetUpClustersAndReplication());
  ASSERT_OK(CreateTable());

  auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
  ASSERT_OK(conn.Execute("BEGIN"));
  // Each statement flushes its write, so the transaction has intents on A's tablets before the
  // switchover starts.
  for (int i = 0; i < kRowCount; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, i));
  }

  ASSERT_OK(Switchover());
  // B's TServers keep polling A for a little while after the switchover drops A->B, and would copy
  // a commit made then to B.  Wait them out so that a commit that persists here exists only on A.
  ASSERT_OK(WaitForNoPollers(*cluster_B_, kReplicationGroupId));

  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("COMMIT"), conn));
  ASSERT_NO_FATALS(ExpectNoRowsOnEitherUniverse());
}

// Checks that a fast-path write that a tablet leader on the old source received before switchover,
// but had not yet appended to Raft, is refused once released after it, and that it is not visible
// on either universe.
TEST_F(XClusterAutomaticModePersistenceTest, FastPathWriteSpanningSwitchover) {
  // The insert's write RPC is held across the whole switchover, so it must not time out first.
  // This is the default (600 s) made explicit.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_client_read_write_timeout_ms) =
      narrow_cast<int32_t>(ToMilliseconds(600s * kTimeMultiplier));
  ASSERT_OK(SetUpClustersAndReplication());
  ASSERT_OK(CreateTable());
  const auto table_id =
      ASSERT_RESULT(GetYsqlTable(cluster_A_, namespace_name, "public", kTableName)).table_id();

  auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
  TestThreadHolder thread_holder;
  std::atomic<bool> is_transactional{false};
  // Declared after thread_holder so that, on an early return, it is destroyed first and releases
  // the paused write before thread_holder joins the insert's thread.
  SyncPointPauseController pause_controller(
      "TabletServiceImpl::PerformWrite", [&table_id, &is_transactional](void* arg) {
        const auto& req = *static_cast<tserver::WriteRequestMsg*>(arg);
        // Pause our write.
        if (!req.has_origination_info() || req.pgsql_write_batch().empty() ||
            req.pgsql_write_batch().front().table_id().ToBuffer() != table_id) {
          return false;
        }
        // Record whether it was transactional.
        is_transactional = req.write_batch().has_transaction();
        return true;
      });
  Status insert_status;
  thread_holder.AddThreadFunctor([&conn, &insert_status] {
    insert_status = conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName);
  });
  // The write is paused on A's TServer, so it has passed every check made before it reaches the
  // tablet leader, including PgClientSession's refusal of writes to an xCluster target.
  ASSERT_OK(pause_controller.WaitUntilPaused());
  // Make sure we are actually testing the fast-write path.
  ASSERT_FALSE(is_transactional);

  ASSERT_OK(Switchover());
  // B's TServers keep polling A for a little while after the switchover drops A->B, and would copy
  // a write made then to B.  Wait them out so that a write that persists here exists only on A.
  ASSERT_OK(WaitForNoPollers(*cluster_B_, kReplicationGroupId));

  pause_controller.Release();
  thread_holder.JoinAll();
  ASSERT_NO_FATALS(ExpectBlocked(insert_status, conn));
  ASSERT_NO_FATALS(ExpectNoRowsOnEitherUniverse());
}

// Checks that DMLs started after switchover's barrier on the old source, while it is an
// automatic-mode target without an xCluster safe time, are refused.
TEST_F(XClusterAutomaticModePersistenceTest, WriteStartedAfterSwitchoverBarrier) {
  ASSERT_OK(SetUpClustersAndReplication());
  ASSERT_OK(CreateTable());
  const auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  // Switchover changes which clusters cluster_A_ and cluster_B_ refer to as it changes the
  // replication direction, so take what is needed from A before running it in another thread.
  auto* const mini_cluster_A = cluster_A_->mini_cluster_.get();
  auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));

  TestThreadHolder thread_holder;
  // Declared after thread_holder so that, on an early return, it is destroyed first and releases
  // the paused switchover before thread_holder joins its thread.
  SyncPointPauseController pause_controller(
      "XClusterInboundReplicationGroupSetupTask::AfterBlockWritesStartedAsSource");
  Status switchover_status;
  thread_holder.AddThreadFunctor([this, &switchover_status] { switchover_status = Switchover(); });
  ASSERT_OK(pause_controller.WaitUntilPaused());

  // Only the role can make PgClientSession refuse these DMLs: A has no xCluster safe time yet.
  for (const auto& mini_tserver : mini_cluster_A->mini_tablet_servers()) {
    const auto& xcluster_context = mini_tserver->server()->GetXClusterContext();
    ASSERT_EQ(
        xcluster_context.GetXClusterRole(namespace_id),
        XClusterNamespaceInfoPB_XClusterRole_AUTOMATIC_TARGET);
    ASSERT_FALSE(xcluster_context.IsReadOnlyMode(namespace_id));
  }

  // These originate after the barrier, so the persistence gate would let them through.
  constexpr auto kRefusal = "Data modification is forbidden";
  ASSERT_NOK_STR_CONTAINS(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName), kRefusal);
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_NOK_STR_CONTAINS(conn.ExecuteFormat("INSERT INTO $0 VALUES (2)", kTableName), kRefusal);
  ASSERT_OK(conn.Execute("ROLLBACK"));

  pause_controller.Release();
  thread_holder.JoinAll();
  ASSERT_OK(switchover_status);
}

// Checks that a distributed transaction that wrote on the source before setup checkpointed it,
// and that the checkpoint failed to abort, cannot commit after setup's backup, and that none of
// its writes are visible on either universe.
TEST_F(XClusterAutomaticModePersistenceTest, DistributedTransactionSpanningSetup) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }
  constexpr int kRowCount = 10;
  auto params = kDefaultParams;
  params.start_yb_controller_servers = true;
  ASSERT_OK(SetUpClusters(params));
  // B gets the table from the restore.
  ASSERT_OK(producer_conn_->ExecuteFormat(
      "CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 3 TABLETS", kTableName));

  auto conn = ASSERT_RESULT(cluster_A_->ConnectToDB(namespace_name));
  ASSERT_OK(conn.Execute("BEGIN"));
  // Each statement flushes its write, so the transaction's provisional records precede the
  // checkpoint: the stream will not carry them, only the eventual commit.
  for (int i = 0; i < kRowCount; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1)", kTableName, i));
  }

  // Checkpointing a tablet aborts its active transactions, but carries on if an abort fails, e.g.,
  // by timing out (see XClusterCheckpointNamespaceTask::MarkTablesAsCheckpointed for other such
  // cases).  Skipping the aborts simulates that.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_abort_active_txns_during_xrepl_bootstrap) = false;
  ASSERT_OK(CheckpointReplicationGroupOnNamespaces({namespace_name}));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_abort_active_txns_during_xrepl_bootstrap) = true;
  ASSERT_OK(BackupFromProducer());

  // Committing after the backup's read time keeps the rows out of the backup too.
  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("COMMIT"), conn));

  ASSERT_OK(RestoreToConsumer());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_NO_FATALS(ExpectNoRowsOnEitherUniverse());
}

}  // namespace yb
