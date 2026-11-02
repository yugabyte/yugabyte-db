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

#include <gtest/gtest.h>

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/transaction.pb.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/server/skewed_clock.h"

#include "yb/tablet/mvcc.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver.messages.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_disable_persistence_early_gate);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_persistence_gate);
DECLARE_bool(enforce_xcluster_guarded_lease);
DECLARE_bool(xcluster_lease_relinquish_leadership);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_uint32(xcluster_guarded_lease_duration_ms);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_string(time_source);

METRIC_DECLARE_counter(persistence_violation_rejections);

using namespace std::literals;

namespace yb {

using pgwrapper::PGConn;
using pgwrapper::PGOid;
using pgwrapper::PgMiniTestBase;

class XClusterPersistenceGateTest : public PgMiniTestBase {
 protected:
  static constexpr auto kXClusterLeaseDuration = 5s;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_guarded_lease_duration_ms) =
        narrow_cast<uint32_t>(ToMilliseconds(kXClusterLeaseDuration));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = true;
    PgMiniTestBase::SetUp();
  }

  void DoTearDown() override {
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearAllCallBacks();
    PgMiniTestBase::DoTearDown();
  }

  static MonoTime MakeDeadline() { return MonoTime::Now() + MonoDelta(30s * kTimeMultiplier); }

  Result<master::CatalogManager*> GetCatalogManager() {
    return &VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  }

  struct Database {
    std::string name;
    NamespaceId namespace_id;
    uint32_t oid;
  };

  // Creates table t, with a single tablet, and sequence s in existing database name.
  Result<Database> PrepareDatabase(const std::string& name) {
    auto conn = VERIFY_RESULT(Connect());
    const auto oid = VERIFY_RESULT(
        conn.FetchRow<PGOid>(Format("SELECT oid FROM pg_database WHERE datname = '$0'", name)));
    auto db_conn = VERIFY_RESULT(ConnectToDB(name));
    RETURN_NOT_OK(
        db_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS"));
    RETURN_NOT_OK(db_conn.Execute("CREATE SEQUENCE s"));
    return Database{name, GetPgsqlNamespaceId(oid), oid};
  }

  // Creates database name holding a single-tablet table t and a sequence s.
  Result<Database> CreateDatabase(const std::string& name) {
    RETURN_NOT_OK(VERIFY_RESULT(Connect()).ExecuteFormat("CREATE DATABASE $0", name));
    return PrepareDatabase(name);
  }

  Status StopPersisting(const Database& database) {
    auto* catalog_manager = VERIFY_RESULT(GetCatalogManager());
    return catalog_manager->GetXClusterManagerImpl()->StopPersisting(
        {database.namespace_id}, MakeDeadline(), catalog_manager->GetLeaderEpochInternal());
  }

  Status StartPersisting(const Database& database) {
    auto* catalog_manager = VERIFY_RESULT(GetCatalogManager());
    return catalog_manager->GetXClusterManagerImpl()->StartPersisting(
        {database.namespace_id}, MakeDeadline(), catalog_manager->GetLeaderEpochInternal());
  }

  // Expects status to be a refusal because database is blocked.
  static void ExpectBlocked(const Status& status, const Database& database) {
    ASSERT_NOK(status);
    ASSERT_EQ(PgsqlError(status).value(), YBPgErrorCode::YB_PG_READ_ONLY_SQL_TRANSACTION) << status;
    ASSERT_STR_CONTAINS(
        status.ToString(),
        Format("Writes to the database with OID $0 are temporarily disabled", database.oid));
    ASSERT_STR_NOT_CONTAINS(status.ToString(), "Errors from tablet servers");
  }

  void TestAbortsTransactionWaitingForLock();

  int64_t SumTServerRefusals() {
    int64_t refusals = 0;
    for (const auto& tserver : cluster_->mini_tablet_servers()) {
      refusals +=
          METRIC_persistence_violation_rejections.Instantiate(tserver->server()->metric_entity())
              ->value();
    }
    return refusals;
  }

  Result<int64_t> GetMasterRefusals() {
    return METRIC_persistence_violation_rejections
        .Instantiate(VERIFY_RESULT(GetCatalogManager())->sys_catalog()->GetMetricEntity())
        ->value();
  }

  // Cuts every TServer off from master and waits until none of them holds a lease.
  Status CutOffTServersFromMaster() {
    for (const auto& tserver : cluster_->mini_tablet_servers()) {
      tserver->FailHeartbeats();
    }
    return WaitFor(
        [this]() -> Result<bool> {
          for (const auto& tserver : cluster_->mini_tablet_servers()) {
            if (tserver->server()->GetXClusterContext().TEST_GetPersistenceBlockMap().ok()) {
              return false;
            }
          }
          return true;
        },
        kXClusterLeaseDuration * 3 * kTimeMultiplier, "Leases expired");
  }

  // Resumes every TServer's heartbeats to master, so their leases come back.
  void ReconnectTServersToMaster() {
    for (const auto& tserver : cluster_->mini_tablet_servers()) {
      tserver->FailHeartbeats(false);
    }
  }

  Result<tablet::TabletPeerPtr> GetTableLeader(const std::string& table_name) {
    auto peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), table_name, ListPeersFilter::kLeaders));
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected a single tablet leader");
    return peers.front();
  }

  // Makes the TServer with index tserver_index the leader of table_name's single tablet.  Turn off
  // load balancing first, or master may move the leader back.
  Status MoveTableLeader(const std::string& table_name, size_t tserver_index) {
    const auto new_leader_uuid =
        cluster_->mini_tablet_server(tserver_index)->server()->permanent_uuid();
    auto old_leader = VERIFY_RESULT(GetTableLeader(table_name));
    if (old_leader->permanent_uuid() == new_leader_uuid) {
      return Status::OK();
    }
    RETURN_NOT_OK(StepDown(old_leader, new_leader_uuid, ForceStepDown::kTrue));
    return WaitFor(
        [this, &table_name, &new_leader_uuid]() -> Result<bool> {
          auto leader = GetTableLeader(table_name);
          return leader.ok() && (*leader)->permanent_uuid() == new_leader_uuid;
        },
        30s * kTimeMultiplier, Format("$0 leader moved", table_name));
  }

  // Waits until the TServer with index tserver_index holds an xCluster-guarded information lease if
  // has_lease, or lacks one if not.
  Status WaitForLease(size_t tserver_index, bool has_lease) {
    auto* tserver = cluster_->mini_tablet_server(tserver_index)->server();
    return WaitFor(
        [tserver, has_lease]() -> Result<bool> {
          return tserver->GetXClusterContext().TEST_GetPersistenceBlockMap().ok() == has_lease;
        },
        kXClusterLeaseDuration * 3 * kTimeMultiplier,
        Format("TServer $0 $1 a lease", tserver_index, has_lease ? "holds" : "lacks"));
  }


  // The kind of op RunWithStopWhilePaused holds between its receipt and its Raft append.
  enum class OpTypeToPause { kWrite, kCommit };

  // Makes the first stamped write or commit (per op_type_to_pause) to database wait, until Release
  // is called.  Its destructor releases that op.
  class Pauser {
   public:
    Pauser(OpTypeToPause op_type_to_pause, uint32_t database_oid) {
      const bool is_commit = op_type_to_pause == OpTypeToPause::kCommit;
      SyncPoint::GetInstance()->SetCallBack(
          is_commit ? "TabletServiceImpl::UpdateTransaction::BeforeStampedCommitAppend"
                    : "TabletServiceImpl::PerformWrite",
          [this, is_commit, database_oid](void* arg) {
            const auto database_oid_seen = is_commit
                                           ? static_cast<tserver::UpdateTransactionRequestPB*>(arg)
                                                 ->state()
                                                 .origination_info()
                                                 .database_oid()
                                           : static_cast<tserver::WriteRequestMsg*>(arg)
                                                 ->origination_info()
                                                 .database_oid();
            if (database_oid_seen != database_oid || paused_.exchange(true)) {
              return;
            }
            reached_.CountDown();
            release_.Wait();
          });
      SyncPoint::GetInstance()->EnableProcessing();
    }

    ~Pauser() {
      // Release first: ClearAllCallBacks waits for running callbacks, including a paused one.
      Release();
      SyncPoint::GetInstance()->DisableProcessing();
      SyncPoint::GetInstance()->ClearAllCallBacks();
    }

    Status WaitReached() {
      SCHECK(reached_.WaitFor(30s * kTimeMultiplier), TimedOut, "Op not paused");
      return Status::OK();
    }

    void Release() { release_.CountDown(); }

   private:
    std::atomic<bool> paused_{false};
    CountDownLatch reached_{1};
    CountDownLatch release_{1};
  };

  // Runs statement on conn in another thread; once the statement's write or commit (per
  // op_type_to_pause) has been received and paused, runs StopPersisting on database, then releases
  // the paused write or commit.  Sets *statement_status to the statement's status and returns
  // StopPersisting's.
  Status RunWithStopWhilePaused(
      PGConn& conn, const std::string& statement, OpTypeToPause op_type_to_pause,
      const Database& database, Status* statement_status) {
    TestThreadHolder thread_holder;
    // Declared after thread_holder so that, on an early return, it is destroyed first and releases
    // the paused op before thread_holder joins the statement's thread.
    Pauser pauser(op_type_to_pause, database.oid);
    thread_holder.AddThreadFunctor(
        [&conn, &statement, statement_status] { *statement_status = conn.Execute(statement); });
    RETURN_NOT_OK(pauser.WaitReached());
    auto stop_status = StopPersisting(database);
    pauser.Release();
    thread_holder.JoinAll();
    return stop_status;
  }
};

// Checks that after StopPersisting on a database, its fast-path and transactional DML, DDL and
// nextval are refused, while another database is unaffected; and that after StartPersisting all of
// them succeed again.
TEST_F(XClusterPersistenceGateTest, StopBlocksOnlyItsDatabase) {
  const auto blocked = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  const auto other = ASSERT_RESULT(CreateDatabase("other_db"));
  auto blocked_conn = ASSERT_RESULT(ConnectToDB(blocked.name));
  auto other_conn = ASSERT_RESULT(ConnectToDB(other.name));
  ASSERT_OK(StopPersisting(blocked));
  const auto initial_master_refusals = ASSERT_RESULT(GetMasterRefusals());
  const auto initial_tserver_refusals = SumTServerRefusals();

  ASSERT_NO_FATALS(ExpectBlocked(blocked_conn.Execute("INSERT INTO t VALUES (1, 1)"), blocked));
  ASSERT_OK(blocked_conn.Execute("BEGIN"));
  ASSERT_NO_FATALS(ExpectBlocked(blocked_conn.Execute("INSERT INTO t VALUES (2, 2)"), blocked));
  ASSERT_OK(blocked_conn.Execute("ROLLBACK"));
  ASSERT_NO_FATALS(
      ExpectBlocked(ResultToStatus(blocked_conn.Fetch("SELECT nextval('s')")), blocked));
  // At least one counted refusal per statement.
  ASSERT_GE(SumTServerRefusals(), initial_tserver_refusals + 3);

  // This DDL is refused on master, by its first catalog write, rather than only at commit.
  ASSERT_NO_FATALS(
      ExpectBlocked(blocked_conn.Execute("CREATE TABLE t2 (k INT PRIMARY KEY)"), blocked));
  ASSERT_GT(ASSERT_RESULT(GetMasterRefusals()), initial_master_refusals);

  ASSERT_OK(other_conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_OK(other_conn.Execute("BEGIN"));
  ASSERT_OK(other_conn.Execute("INSERT INTO t VALUES (2, 2)"));
  ASSERT_OK(other_conn.Execute("COMMIT"));
  ASSERT_OK(other_conn.Fetch("SELECT nextval('s')"));
  ASSERT_OK(other_conn.Execute("CREATE TABLE t2 (k INT PRIMARY KEY)"));

  ASSERT_OK(StartPersisting(blocked));
  ASSERT_OK(blocked_conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_OK(blocked_conn.Execute("BEGIN"));
  ASSERT_OK(blocked_conn.Execute("INSERT INTO t VALUES (2, 2)"));
  ASSERT_OK(blocked_conn.Execute("COMMIT"));
  ASSERT_OK(blocked_conn.Fetch("SELECT nextval('s')"));
  ASSERT_OK(blocked_conn.Execute("CREATE TABLE t2 (k INT PRIMARY KEY)"));
}

// Checks that a transaction that wrote before StopPersisting cannot commit after
// StartPersisting, and that the refused commit leaves the database usable.
TEST_F(XClusterPersistenceGateTest, StartBlocksEarlierTransaction) {
  // So the transaction reaches its commit rather than being aborted by its heartbeat.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_persistence_early_gate) = true;
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));

  ASSERT_OK(StopPersisting(database));
  ASSERT_OK(StartPersisting(database));
  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("COMMIT"), database));

  // The refused transaction's write never committed, and its key can be written again.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t")), 0);
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 10)"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT v FROM t WHERE k = 1")), 10);
}

// Checks that a write received before StopPersisting, but not yet appended to Raft when
// StopPersisting returns, is refused.  The early gates cannot see this write: only the gate at the
// Raft append is downstream of where it is paused.
TEST_F(XClusterPersistenceGateTest, RefusesWriteReceivedBeforeStop) {
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  Status statement_status;
  ASSERT_OK(RunWithStopWhilePaused(
      conn, "INSERT INTO t VALUES (1, 1)", OpTypeToPause::kWrite, database, &statement_status));
  ASSERT_NO_FATALS(ExpectBlocked(statement_status, database));
}

// Checks that a commit received before StopPersisting, but not yet appended to Raft when
// StopPersisting returns, is refused.
TEST_F(XClusterPersistenceGateTest, RefusesCommitReceivedBeforeStop) {
  // So the transaction reaches its commit rather than being aborted by its heartbeat.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_persistence_early_gate) = true;
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  Status statement_status;
  ASSERT_OK(
      RunWithStopWhilePaused(conn, "COMMIT", OpTypeToPause::kCommit, database, &statement_status));
  ASSERT_NO_FATALS(ExpectBlocked(statement_status, database));
}

// Checks that while the TServer that Postgres uses holds no lease, it refuses YSQL inserts, and
// accepts them again once it regains its lease.
TEST_F(XClusterPersistenceGateTest, SessionRefusesDmlWithoutLease) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  // Only Postgres's TServer (kPgTsIndex) will lose its lease in this test.  Move t's leader to
  // another TServer so that the tablet leader does not refuse the insert too.
  ASSERT_OK(MoveTableLeader("t", kPgTsIndex + 1));

  cluster_->mini_tablet_server(kPgTsIndex)->FailHeartbeats();
  ASSERT_OK(WaitForLease(kPgTsIndex, /*has_lease=*/false));

  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("INSERT INTO t VALUES (1, 1)"),
      "the xCluster role of the database is currently unavailable");
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t")), 0);

  cluster_->mini_tablet_server(kPgTsIndex)->FailHeartbeats(false);
  ASSERT_OK(WaitForLease(kPgTsIndex, /*has_lease=*/true));

  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t")), 1);
}

// Checks that a tablet leader without a lease refuses stamped writes, without counting the
// refusals, and that the client retries them until the lease comes back; unstamped (YCQL) writes to
// tablets led by the same TServer still succeed.
TEST_F(XClusterPersistenceGateTest, LeaderFailsClosedWithoutLease) {
  // The leader loses its lease below.  With relinquishing on, it would hand leadership to a
  // TServer that holds a lease, and the insert would succeed there instead of being refused.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_lease_relinquish_leadership) = false;

  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  auto observer_conn = ASSERT_RESULT(ConnectToDB(database.name));
  client::TableHandle ycql_table;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /*num_tablets=*/1, client_.get(), &ycql_table);
  auto session = client_->NewSession(60s);

  // Postgres's TServer (kPgTsIndex) keeps its lease in this test, so PgClientSession lets the
  // insert through to the leader.
  constexpr size_t kLeaderTsIndex = kPgTsIndex + 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ASSERT_OK(MoveTableLeader("t", kLeaderTsIndex));
  ASSERT_OK(MoveTableLeader(client::kTableName.table_name(), kLeaderTsIndex));
  const auto initial_tserver_refusals = SumTServerRefusals();

  cluster_->mini_tablet_server(kLeaderTsIndex)->FailHeartbeats();
  ASSERT_OK(WaitForLease(kLeaderTsIndex, /*has_lease=*/false));

  // Start an insert in another thread.
  TestThreadHolder thread_holder;
  std::atomic<bool> insert_done{false};
  Status insert_status;
  thread_holder.AddThreadFunctor([&conn, &insert_done, &insert_status] {
    insert_status = conn.Execute("INSERT INTO t VALUES (1, 1)");
    insert_done = true;
  });

  // The insert keeps being refused and retried, so it neither finishes nor takes effect.
  SleepFor(3s * kTimeMultiplier);
  ASSERT_FALSE(insert_done);
  ASSERT_EQ(ASSERT_RESULT(observer_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t")), 0);
  // No-lease refusals are not counted.
  ASSERT_EQ(SumTServerRefusals(), initial_tserver_refusals);

  // YCQL writes still work without the lease.
  ASSERT_OK(client::kv_table_test::WriteRow(&ycql_table, session, /*key=*/1, /*value=*/1));

  // Once heartbeats resume and the lease comes back, the insert succeeds.
  cluster_->mini_tablet_server(kLeaderTsIndex)->FailHeartbeats(false);
  thread_holder.JoinAll();
  ASSERT_OK(insert_status);
  ASSERT_EQ(ASSERT_RESULT(observer_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t")), 1);
}

// Checks that a blocked tablet's new leader enforces the block after a leader change.
TEST_F(XClusterPersistenceGateTest, NewLeaderEnforcesBlock) {
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  ASSERT_OK(StopPersisting(database));
  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("INSERT INTO t VALUES (1, 1)"), database));

  auto old_leader = ASSERT_RESULT(GetTableLeader("t"));
  std::string new_leader_uuid;
  for (const auto& tserver : cluster_->mini_tablet_servers()) {
    if (tserver->server()->permanent_uuid() != old_leader->permanent_uuid()) {
      new_leader_uuid = tserver->server()->permanent_uuid();
      break;
    }
  }
  ASSERT_OK(StepDown(old_leader, new_leader_uuid, ForceStepDown::kTrue));
  ASSERT_OK(WaitFor(
      [this, &new_leader_uuid]() -> Result<bool> {
        auto leader = GetTableLeader("t");
        return leader.ok() && (*leader)->permanent_uuid() == new_leader_uuid;
      },
      30s * kTimeMultiplier, "New leader elected"));

  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("INSERT INTO t VALUES (1, 1)"), database));
}

// Checks that with either flag off nothing is blocked, even with the database's entry present.
TEST_F(XClusterPersistenceGateTest, NothingBlockedWithEitherFlagOff) {
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  ASSERT_OK(StopPersisting(database));
  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("INSERT INTO t VALUES (1, 1)"), database));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = false;
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = true;
  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("INSERT INTO t VALUES (2, 2)"), database));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = false;
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (2, 2)"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = true;
  ASSERT_NO_FATALS(ExpectBlocked(conn.Execute("INSERT INTO t VALUES (3, 3)"), database));
}

// Checks that StopPersisting and StartPersisting refuse to run, changing nothing, with either flag
// off.
TEST_F(XClusterPersistenceGateTest, PrimitivesNotSupportedWithEitherFlagOff) {
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));

  for (auto* flag : {&FLAGS_enable_persistence_gate, &FLAGS_enforce_xcluster_guarded_lease}) {
    ANNOTATE_UNPROTECTED_WRITE(*flag) = false;
    auto status = StopPersisting(database);
    ASSERT_TRUE(status.IsNotSupported()) << status;
    status = StartPersisting(database);
    ASSERT_TRUE(status.IsNotSupported()) << status;
    ANNOTATE_UNPROTECTED_WRITE(*flag) = true;
  }
  auto* xcluster_manager = ASSERT_RESULT(GetCatalogManager())->GetXClusterManagerImpl();
  const auto entry = ASSERT_RESULT(xcluster_manager->GetPersistenceBlockMap());
  ASSERT_EQ(entry.version(), 0);
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
}

// Checks that stamped writes and commits are written to the WALs of their replicas, on the TServers
// and on master, without their stamps.
TEST_F(XClusterPersistenceGateTest, StampsNotInWal) {
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (2, 2)"));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Get every replica, including those of transaction status tablets and the sys catalog tablet,
  // which holds the stamped catalog writes of PrepareDatabase's DDL.
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll, UserTabletsOnly::kFalse);
  peers.push_back(ASSERT_RESULT(GetCatalogManager())->sys_catalog()->tablet_peer());

  int writes = 0;
  int commits = 0;
  for (const auto& peer : peers) {
    auto* log = peer->log();
    ASSERT_OK(log->WaitUntilAllFlushed());
    const auto last_index = log->GetLatestEntryOpId().index;
    if (last_index <= 0) {
      continue;
    }
    consensus::ReplicateMsgs replicates;
    int64_t starting_op_segment_seq_num;
    ASSERT_OK(ASSERT_RESULT(log->GetLogReader())->ReadReplicatesInRange(
        1, last_index, log::LogReader::kNoSizeLimit, log::ObeyMemoryLimit::kFalse, &replicates,
        &starting_op_segment_seq_num));
    for (const auto& replicate : replicates) {
      switch (replicate->op_type()) {
        case consensus::OperationType::WRITE_OP:
          ++writes;
          EXPECT_FALSE(replicate->write().has_origination_info())
              << peer->LogPrefix() << replicate->ShortDebugString();
          break;
        case consensus::OperationType::UPDATE_TRANSACTION_OP:
          if (replicate->transaction_state().status() == TransactionStatus::COMMITTED) {
            ++commits;
          }
          EXPECT_FALSE(replicate->transaction_state().has_origination_info())
              << peer->LogPrefix() << replicate->ShortDebugString();
          break;
        default:
          break;
      }
    }
  }
  ASSERT_GT(writes, 0);
  ASSERT_GT(commits, 0);
}

// Checks that an idle transaction that wrote before StopPersisting is aborted, releasing its row
// lock without waiting for its backend, and that the backend's next statement reports the block.
TEST_F(XClusterPersistenceGateTest, AbortsIdleTransactionHoldingRowLock) {
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto idle_conn = ASSERT_RESULT(ConnectToDB(database.name));
  auto locking_conn = ASSERT_RESULT(ConnectToDB(database.name));
  ASSERT_OK(idle_conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_OK(idle_conn.Execute("BEGIN"));
  ASSERT_OK(idle_conn.Execute("UPDATE t SET v = 2 WHERE k = 1"));
  const auto initial_tserver_refusals = SumTServerRefusals();

  // After StartPersisting the idle transaction is still blocked, but a new one is not, so it can
  // take the row lock once a heartbeat refusal has aborted the idle transaction.
  ASSERT_OK(StopPersisting(database));
  ASSERT_OK(StartPersisting(database));
  // The update succeeds only once the idle transaction is aborted and releases its row lock.  The
  // timeout below only bounds the failing case: without the heartbeat gate the idle transaction
  // keeps heartbeating and holds the lock indefinitely, and statements have no timeout by default.
  ASSERT_OK(locking_conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(10s * kTimeMultiplier)));
  ASSERT_OK(locking_conn.Execute("UPDATE t SET v = 3 WHERE k = 1"));
  ASSERT_GT(SumTServerRefusals(), initial_tserver_refusals);

  // The idle session's next statement reports the refusal that aborted its transaction.
  const auto status = ResultToStatus(idle_conn.Fetch("SELECT * FROM t"));
  LOG(INFO) << "Idle transaction's next statement: " << status;
  ASSERT_NO_FATALS(ExpectBlocked(status, database));
}

// Checks that a blocked transaction queued for a lock fails rather than staying queued until its
// statement times out.  With yb_enable_read_committed_isolation set, it fails as blocked; without
// it (READ COMMITTED then runs as REPEATABLE READ), it may instead fail with a serialization
// failure (40001).
void XClusterPersistenceGateTest::TestAbortsTransactionWaitingForLock() {
  // After its heartbeat is refused the waiter stops heartbeating, so its coordinator would also
  // expire it after missing enough heartbeats, failing it.  Make that take far longer than the
  // statement timeout, so only the coordinator's abort can fail the waiter in time.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_max_missed_heartbeat_periods) = 1000;
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto holding_conn = ASSERT_RESULT(ConnectToDB(database.name));
  auto waiting_conn = ASSERT_RESULT(ConnectToDB(database.name));

  // A session-level advisory lock's transaction is not stamped and so is never aborted, so only the
  // waiter's own abort can free the waiter.
  ASSERT_OK(holding_conn.Fetch("SELECT pg_advisory_lock(1)"));
  ASSERT_OK(waiting_conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(10s * kTimeMultiplier)));
  ASSERT_OK(waiting_conn.Execute("BEGIN"));
  ASSERT_OK(waiting_conn.Execute("INSERT INTO t VALUES (1, 1)"));

  // Now we have the waiting connection wait on the advisory lock, causing it to queue up waiting
  // for that lock to be freed.
  TestThreadHolder thread_holder;
  Status waiting_status;
  thread_holder.AddThreadFunctor([&waiting_conn, &waiting_status] {
    waiting_status = ResultToStatus(waiting_conn.Fetch("SELECT pg_advisory_xact_lock(1)"));
  });

  // Wait for it to actually get queued before we do StopPersisting; otherwise its heartbeat could
  // be refused before it queues, and it would fail without ever waiting.
  auto observer_conn = ASSERT_RESULT(ConnectToDB(database.name));
  // By default pg_locks omits transactions younger than 1 s.
  ASSERT_OK(observer_conn.Execute("SET yb_locks_min_txn_age = 0"));
  ASSERT_OK(WaitFor(
      [&observer_conn]() -> Result<bool> {
        return VERIFY_RESULT(observer_conn.FetchRow<int64_t>(
                   "SELECT COUNT(*) FROM pg_locks WHERE NOT granted")) > 0;
      },
      10s * kTimeMultiplier, "Waiting transaction queued for the advisory lock"));

  ASSERT_OK(StopPersisting(database));
  thread_holder.JoinAll();

  LOG(INFO) << "Waiting transaction's statement status: " << waiting_status;
  // The coordinator's abort fails the queued statement with a serialization failure.  With
  // yb_enable_read_committed_isolation set, the statement runs in a subtransaction, so that failure
  // does not end it: Postgres retries the statement, which then fails with the transaction's
  // recorded heartbeat refusal.  Without it, the serialization failure is what Postgres reports.
  if (!FLAGS_yb_enable_read_committed_isolation &&
      PgsqlError(waiting_status).value() == YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE) {
    return;
  }
  ASSERT_NO_FATALS(ExpectBlocked(waiting_status, database));
}

TEST_F(XClusterPersistenceGateTest, AbortsTransactionWaitingForLock) {
  ASSERT_NO_FATALS(TestAbortsTransactionWaitingForLock());
}

class XClusterPersistenceGateReadCommittedTest : public XClusterPersistenceGateTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    XClusterPersistenceGateTest::SetUp();
  }
};

TEST_F(XClusterPersistenceGateReadCommittedTest, AbortsTransactionWaitingForLock) {
  ASSERT_NO_FATALS(TestAbortsTransactionWaitingForLock());
}

// Checks that a transaction is not aborted while its status tablet's TServer has no lease, even
// though that TServer cannot tell whether the transaction's database is blocked.
TEST_F(XClusterPersistenceGateTest, KeepsTransactionWithoutLease) {
  // Every TServer loses its lease below.  With relinquishing on, the status tablet would then have
  // no leader, so no coordinator would judge the transaction while the leases are gone.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_lease_relinquish_leadership) = false;

  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));

  ASSERT_OK(CutOffTServersFromMaster());
  // Several heartbeat intervals, each judged without a lease.
  SleepFor(3s * kTimeMultiplier);
  ReconnectTServersToMaster();

  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t")), 1);
}

class XClusterPersistenceGateSkewedClockTest : public XClusterPersistenceGateTest {
 protected:
  void SetUp() override {
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    XClusterPersistenceGateTest::SetUp();
  }
};

// Checks that when StopPersisting returns, master's clock is past the hybrid time of a write that
// a TServer whose clock is ahead of master's let through before StopPersisting.
TEST_F(XClusterPersistenceGateSkewedClockTest, StopPersistingWaitsOutClockSkew) {
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  // Warms the catalog caches, so the write below needs no master RPC that would carry the skewed
  // clock to master.
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));

  auto leader = ASSERT_RESULT(GetTableLeader("t"));
  tserver::MiniTabletServer* leader_tserver = nullptr;
  for (const auto& tserver : cluster_->mini_tablet_servers()) {
    if (tserver->server()->permanent_uuid() == leader->permanent_uuid()) {
      leader_tserver = tserver.get();
    }
  }
  ASSERT_NE(leader_tserver, nullptr);

  // Ahead by half the allowed skew, so within what StopPersisting may assume.
  const auto skew = JumpClock(
      leader_tserver, std::chrono::milliseconds(FLAGS_max_clock_skew_usec / 2000));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (2, 2)"));
  const auto write_ht =
      ASSERT_RESULT(leader->shared_tablet())->mvcc_manager()->LastReplicatedHybridTime();

  ASSERT_OK(StopPersisting(database));
  ASSERT_GT(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master()->clock()->Now(), write_ht);
}

class XClusterPersistenceGateMultiMasterTest : public XClusterPersistenceGateTest {
 protected:
  size_t NumMasters() override { return 3; }

  Result<master::MiniMaster*> GetMiniMaster(const std::string& uuid) {
    for (const auto& mini_master : cluster_->mini_masters()) {
      if (mini_master->permanent_uuid() == uuid) {
        return mini_master.get();
      }
    }
    return STATUS_FORMAT(NotFound, "No master $0", uuid);
  }
};

// Checks that a catalog write admitted while a master was ready in one term, but appended after
// that master lost and regained leadership and before it reloaded its catalog, is not judged
// against the block map from the earlier term: there the database is not blocked, but another
// master has blocked it since.  The gate must refuse the write retryably rather than let it
// through, so the client resends it once the catalog is loaded.
TEST_F(XClusterPersistenceGateMultiMasterTest, CatalogWriteNotJudgedByEarlierTermMap) {
  // So the DDL's transaction, held open across StopPersisting, reaches the master gate rather than
  // being aborted by its heartbeat.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_persistence_early_gate) = true;
  const auto database = ASSERT_RESULT(PrepareDatabase("yugabyte"));
  auto conn = ASSERT_RESULT(ConnectToDB(database.name));
  const auto first_leader_uuid = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->permanent_uuid();

  // Hold the DDL's first catalog write after the first leader admitted it; the handler holds that
  // master's leader lock, so its next catalog load cannot start before the write is released.
  // Count resends of that write, identified by its retryable request id.
  CountDownLatch write_reached(1);
  CountDownLatch release_write(1);
  std::atomic<bool> write_paused{false};
  std::optional<std::tuple<uint64_t, uint64_t, int64_t>> paused_write_id;
  std::atomic<int> resends{0};
  SyncPoint::GetInstance()->SetCallBack("TabletServiceImpl::PerformWrite", [&](void* arg) {
    const auto& req = *static_cast<tserver::WriteRequestMsg*>(arg);
    if (req.origination_info().database_oid() != database.oid) {
      return;
    }
    const auto write_id = std::make_tuple(req.client_id1(), req.client_id2(), req.request_id());
    if (!write_paused.exchange(true)) {
      paused_write_id = write_id;
      write_reached.CountDown();
      release_write.Wait();
    } else if (write_id == paused_write_id) {
      ++resends;
    }
  });
  SyncPoint::GetInstance()->EnableProcessing();
  TestThreadHolder thread_holder;
  Status ddl_status;
  thread_holder.AddThreadFunctor(
      [&conn, &ddl_status] { ddl_status = conn.Execute("CREATE TABLE t2 (k INT PRIMARY KEY)"); });
  ASSERT_TRUE(write_reached.WaitFor(30s * kTimeMultiplier));

  // Another master blocks the database, then the first leader becomes leader again.
  ASSERT_OK(cluster_->StepDownMasterLeader());
  // The step down is asynchronous; until it completes the first leader rejects catalog writes.
  ASSERT_OK(WaitFor(
      [this, &first_leader_uuid]() -> Result<bool> {
        return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->permanent_uuid() !=
               first_leader_uuid;
      },
      30s * kTimeMultiplier, "Another master is leader"));
  ASSERT_OK(StopPersisting(database));
  ASSERT_OK(cluster_->StepDownMasterLeader(first_leader_uuid));
  auto* first_leader = ASSERT_RESULT(GetMiniMaster(first_leader_uuid));
  ASSERT_OK(WaitFor(
      [first_leader]() {
        return first_leader->sys_catalog().tablet_peer()->LeaderStatus() ==
               consensus::LeaderStatus::LEADER_AND_READY;
      },
      30s * kTimeMultiplier, "First leader is Raft leader again"));

  // Stall the first leader's catalog load, which cannot start until the write is released (see
  // above), right after it resets the loaded term; then release the write, which is appended in
  // the new term while the catalog is not loaded for it.
  CountDownLatch load_reached(1);
  CountDownLatch release_load(1);
  SyncPoint::GetInstance()->SetCallBack(
      "CatalogManager::VisitSysCatalog::CatalogLoadedTermReset", [&](void*) {
        load_reached.CountDown();
        release_load.Wait();
      });
  release_write.CountDown();
  ASSERT_TRUE(load_reached.WaitFor(30s * kTimeMultiplier));
  // Give the released write time to reach the gate while the load is stalled.
  SleepFor(2s * kTimeMultiplier);
  release_load.CountDown();

  thread_holder.JoinAll();
  ASSERT_NO_FATALS(ExpectBlocked(ddl_status, database));
  ASSERT_GT(resends, 0);
}

}  // namespace yb
