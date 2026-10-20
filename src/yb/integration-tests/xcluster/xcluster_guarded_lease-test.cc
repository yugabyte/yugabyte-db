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

#include <gtest/gtest.h>

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(TEST_do_not_start_election_test_only);
DECLARE_bool(TEST_enable_ysql_operation_lease_expiry_check);
DECLARE_bool(TEST_no_grant_xcluster_guarded_lease);
DECLARE_bool(TEST_pause_before_remote_bootstrap);
DECLARE_bool(TEST_tserver_disable_heartbeat);
DECLARE_bool(TEST_tserver_enable_ysql_lease_refresh);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(enable_persistence_gate);
DECLARE_bool(enable_ysql_operation_lease);
DECLARE_bool(enforce_xcluster_guarded_lease);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(leader_failure_exp_backoff_max_delta_ms);
DECLARE_uint64(master_ysql_operation_lease_ttl_ms);
DECLARE_uint32(xcluster_guarded_lease_duration_ms);
DECLARE_bool(xcluster_lease_relinquish_leadership);

using namespace std::literals;

namespace yb {

using pgwrapper::LibPqTestBase;
using pgwrapper::PGConn;
using pgwrapper::PGConnBuilder;
using pgwrapper::PgMiniTestBase;
using pgwrapper::PGOid;
using pgwrapper::PGUint64;

// A TServer whose xCluster-guarded information lease has run out reports the xCluster role of
// every database as UNAVAILABLE.  No xCluster replication is involved here: these tests check the
// role-dependent blocking for a plain database in that state.
class XClusterGuardedLeaseTest : public PgMiniTestBase {
 protected:
  virtual MonoDelta GetLeaseDuration() const { return {10s}; }
  static constexpr auto kUnavailableErrorMsg =
      "forbidden because the xCluster role of the database is currently unavailable";
  static constexpr auto kOidCountUnavailableErrorMsg =
      "The OID cache invalidation count is unavailable";

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_guarded_lease_duration_ms) =
        narrow_cast<uint32_t>(GetLeaseDuration().ToMilliseconds());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_xcluster_guarded_lease) = true;
    PgMiniTestBase::SetUp();
  }

  static Result<NamespaceId> GetCurrentNamespaceId(PGConn& conn) {
    const auto db_oid = VERIFY_RESULT(
        conn.FetchRow<PGOid>("SELECT oid FROM pg_database WHERE datname = current_database()"));
    return GetPgsqlNamespaceId(db_oid);
  }

  // Stops heartbeats and waits for every TServer's lease to run out.
  Status LoseXClusterLease(const NamespaceId& namespace_id) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
    return WaitForRoleOnAllTServers(namespace_id, XClusterNamespaceInfoPB::UNAVAILABLE);
  }

  // Resumes heartbeats and waits for every TServer to hold a lease again.
  Status RegainXClusterLease(const NamespaceId& namespace_id) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = false;
    return WaitForRoleOnAllTServers(namespace_id, XClusterNamespaceInfoPB::NOT_AUTOMATIC_MODE);
  }

  Status WaitForRoleOnAllTServers(
      const NamespaceId& namespace_id, XClusterNamespaceInfoPB::XClusterRole role) {
    return WaitFor(
        [&]() -> Result<bool> {
          for (const auto& tserver : cluster_->mini_tablet_servers()) {
            if (tserver->server()->GetXClusterContext().GetXClusterRole(namespace_id) != role) {
              return false;
            }
          }
          return true;
        },
        GetLeaseDuration() + MonoDelta(10s * kTimeMultiplier),
        Format(
            "Wait for all TServers to report role $0",
            XClusterNamespaceInfoPB::XClusterRole_Name(role)));
  }
};

TEST_F(XClusterGuardedLeaseTest, SequenceBumpsBlockedWithoutLease) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SEQUENCE seq"));
  const auto namespace_id = ASSERT_RESULT(GetCurrentNamespaceId(conn));

  ASSERT_OK(LoseXClusterLease(namespace_id));
  ASSERT_NOK_STR_CONTAINS(conn.FetchRow<PGUint64>("SELECT nextval('seq')"), kUnavailableErrorMsg);
  ASSERT_NOK_STR_CONTAINS(
      conn.FetchRow<PGUint64>("SELECT setval('seq', 1, true)"), kUnavailableErrorMsg);

  ASSERT_OK(RegainXClusterLease(namespace_id));
  ASSERT_OK(conn.FetchRow<PGUint64>("SELECT nextval('seq')"));
}

TEST_F(XClusterGuardedLeaseTest, DdlsBlockedWithoutLease) {
  // Every TServer loses its lease below.  With relinquishing on, no tablet would then have a
  // leader, and the DDLs below that reach a tablet would hang instead of failing.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_lease_relinquish_leadership) = false;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE tbl (a int)"));
  ASSERT_OK(conn.Execute("CREATE SEQUENCE seq"));
  ASSERT_OK(conn.Execute("CREATE EXTENSION yb_xcluster_ddl_replication"));
  const auto namespace_id = ASSERT_RESULT(GetCurrentNamespaceId(conn));

  ASSERT_OK(LoseXClusterLease(namespace_id));

  // With the extension installed, its event trigger refuses to run without a role, so a DDL fails
  // there before reaching the PG client service.
  ASSERT_NOK_STR_CONTAINS(
      conn.Execute("ALTER TABLE tbl ADD COLUMN b int"), "unable to fetch replication role");

  // To reach the PG client service's own DDL check, we tell the extension the role is TARGET: its
  // code then clears yb_xcluster_target_ddl_bypass (which the extension's preloaded library
  // otherwise sets for every statement) and lets the DDL through to the TServer, whose real role
  // is still UNAVAILABLE.  Only DDLs that do not need master to create a DocDB table are tested,
  // because DDLs that need to create a table cannot complete without heartbeats.
  ASSERT_OK(
      conn.Execute("SET yb_xcluster_ddl_replication.TEST_replication_role_override TO 'TARGET'"));
  for (const auto& ddl : {"ALTER TABLE tbl ADD COLUMN b int", "DROP SEQUENCE seq"}) {
    LOG(INFO) << "Executing: " << ddl;
    ASSERT_NOK_STR_CONTAINS(conn.Execute(ddl), kUnavailableErrorMsg);
  }
  // A DDL that needs a new OID fails even earlier, when allocating it.
  ASSERT_NOK_STR_CONTAINS(conn.Execute("CREATE SEQUENCE seq2"), kOidCountUnavailableErrorMsg);
  ASSERT_OK(conn.Execute("RESET yb_xcluster_ddl_replication.TEST_replication_role_override"));

  ASSERT_OK(RegainXClusterLease(namespace_id));
  ASSERT_OK(conn.Execute("ALTER TABLE tbl ADD COLUMN b int"));
}

TEST_F(XClusterGuardedLeaseTest, OidAllocationBlockedWithoutLease) {
  auto conn = ASSERT_RESULT(Connect());
  const auto namespace_id = ASSERT_RESULT(GetCurrentNamespaceId(conn));
  const auto db_oid = ASSERT_RESULT(GetPgsqlDatabaseOid(namespace_id));

  // Call the PG client service directly: every SQL statement that allocates an OID is a DDL, which
  // the DDL check would reject first.
  tserver::PgClientServiceProxy proxy(
      &client_->proxy_cache(),
      HostPort::FromBoundEndpoint(cluster_->mini_tablet_server(0)->bound_rpc_addr()));
  auto get_new_object_id = [&]() -> Status {
    tserver::PgGetNewObjectIdRequestPB req;
    req.set_db_oid(db_oid);
    tserver::PgGetNewObjectIdResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.GetNewObjectId(req, &resp, &controller));
    return resp.has_status() ? StatusFromPB(resp.status()) : Status::OK();
  };

  ASSERT_OK(get_new_object_id());

  // Only heartbeats stop, so master stays reachable, and the call above left OIDs in the TServer's
  // cache, so this call needs nothing from master.  It can therefore fail only because the TServer
  // no longer holds a lease.
  ASSERT_OK(LoseXClusterLease(namespace_id));
  ASSERT_NOK_STR_CONTAINS(get_new_object_id(), kOidCountUnavailableErrorMsg);

  ASSERT_OK(RegainXClusterLease(namespace_id));
  ASSERT_OK(get_new_object_id());
}

class XClusterPropagateGuardedInfoTest : public XClusterGuardedLeaseTest {};

// Uses leases long enough that stopping heartbeats does not make roles UNAVAILABLE during a test.
class XClusterPropagateGuardedInfoTestLongLease : public XClusterPropagateGuardedInfoTest {
 protected:
  MonoDelta GetLeaseDuration() const override { return {10min}; }
};

TEST_F_EX(XClusterPropagateGuardedInfoTest, PropagatesWithoutHeartbeats,
          XClusterPropagateGuardedInfoTestLongLease) {
  auto conn = ASSERT_RESULT(Connect());
  const auto namespace_id = ASSERT_RESULT(GetCurrentNamespaceId(conn));
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  auto* xcluster_manager = catalog_manager.GetXClusterManagerImpl();
  const auto& tservers = cluster_->mini_tablet_servers();
  auto get_oid_cache_invalidations_count = [](const auto& tserver) {
    return tserver->server()->GetXClusterContext().GetOidCacheInvalidationsCount();
  };

  // Stop heartbeats, and let any in flight finish, so that the RPC is the only way new
  // information can reach the TServers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
  SleepFor(FLAGS_heartbeat_interval_ms * 2ms * kTimeMultiplier);
  const auto old_oid_invalidations_count =
      ASSERT_RESULT(get_oid_cache_invalidations_count(tservers[0]));

  ASSERT_OK(xcluster_manager->SetXClusterRole(
      catalog_manager.GetLeaderEpochInternal(), namespace_id,
      XClusterNamespaceInfoPB::AUTOMATIC_SOURCE));
  ASSERT_OK(catalog_manager.InvalidateTserverOidCaches());

  for (const auto& tserver : tservers) {
    EXPECT_EQ(
        tserver->server()->GetXClusterContext().GetXClusterRole(namespace_id),
        XClusterNamespaceInfoPB::NOT_AUTOMATIC_MODE);
    EXPECT_EQ(
        ASSERT_RESULT(get_oid_cache_invalidations_count(tserver)), old_oid_invalidations_count);
  }

  ASSERT_OK(xcluster_manager->PropagateXClusterGuardedInfo(
      MonoTime::Now() + MonoDelta(10s * kTimeMultiplier)));

  for (const auto& tserver : tservers) {
    EXPECT_EQ(
        tserver->server()->GetXClusterContext().GetXClusterRole(namespace_id),
        XClusterNamespaceInfoPB::AUTOMATIC_SOURCE);
    EXPECT_EQ(
        ASSERT_RESULT(get_oid_cache_invalidations_count(tserver)),
        old_oid_invalidations_count + 1);
  }
}

TEST_F(XClusterPropagateGuardedInfoTest, PropagateEvenWithDeadTServer) {
  auto* mini_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto* xcluster_manager = mini_master->catalog_manager_impl().GetXClusterManagerImpl();
  auto* dead_tserver = cluster_->mini_tablet_server(0);
  const auto dead_tserver_uuid = dead_tserver->server()->permanent_uuid();
  dead_tserver->Shutdown();

  // Master does not yet know the TServer cannot hold a lease, so it must be waited for, and it is
  // not going to answer.  Keep the deadline well short of the lease duration so the TServer is
  // still MAYBE_HAS_LEASE when the call gives up.
  auto status = xcluster_manager->PropagateXClusterGuardedInfo(MonoTime::Now() + MonoDelta(3s));
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), dead_tserver_uuid);

  // A TServer that loses its lease while we are trying is excused: give the call a deadline past
  // the point master marks the dead TServer DEFINITELY_NO_LEASE.
  auto descriptor =
      ASSERT_RESULT(mini_master->master()->ts_manager()->LookupTSByUUID(dead_tserver_uuid));
  ASSERT_TRUE(descriptor->MaybeHasXClusterGuardedLease());
  ASSERT_OK(xcluster_manager->PropagateXClusterGuardedInfo(
      MonoTime::Now() + GetLeaseDuration() + MonoDelta(10s * kTimeMultiplier)));
  ASSERT_FALSE(descriptor->MaybeHasXClusterGuardedLease());

  // Once master knows the TServer cannot hold a lease, it is skipped from the start, so a deadline
  // that was too short above now suffices.
  ASSERT_OK(xcluster_manager->PropagateXClusterGuardedInfo(MonoTime::Now() + MonoDelta(3s)));
}

// Helpers for tests of when the TServer starts Postgres.
class XClusterGuardedLeasePgTest : public XClusterGuardedLeaseTest {
 protected:
  void SetUp() override {
    // Shortened so that tests can let the YSQL lease expire.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ysql_operation_lease_ttl_ms) = 5000;
    XClusterGuardedLeaseTest::SetUp();
    // The TServer notices that its YSQL lease has expired, and pauses Postgres, only while this
    // check is on.  Keep it off except in LoseYsqlLease, so that the YSQL lease is lost only where
    // a test asks for it.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_ysql_operation_lease_expiry_check) = false;
  }

  tserver::TabletServer& GetPgTServer() {
    return *cluster_->mini_tablet_server(kPgTsIndex)->server();
  }

  Status ExpectPgNotStarted() {
    // Postgres has had ample time to start, so a refused connection means the TServer kept it
    // paused.
    SleepFor(10s * kTimeMultiplier);
    SCHECK_EQ(
        pg_supervisor_->GetState(), YbSubProcessState::kPaused, IllegalState,
        "Postgres supervisor is not paused");
    auto conn = ConnectToDB(/*dbname=*/"", /*timeout=*/2);
    SCHECK(!conn.ok(), IllegalState, "Connected to Postgres that should be paused");
    return Status::OK();
  }

  Status WaitForYsqlLease(bool is_live) {
    return WaitFor(
        [this, is_live]() -> Result<bool> {
          return VERIFY_RESULT(GetPgTServer().GetYSQLLeaseInfo()).is_live == is_live;
        },
        30s * kTimeMultiplier, Format("Wait for YSQL lease to be $0", is_live ? "alive" : "lost"));
  }

  Status LoseYsqlLease() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_enable_ysql_lease_refresh) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_ysql_operation_lease_expiry_check) = true;
    auto status = WaitForYsqlLease(/*is_live=*/false);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_ysql_operation_lease_expiry_check) = false;
    return status;
  }

  Status RegainYsqlLease() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_enable_ysql_lease_refresh) = true;
    return WaitForYsqlLease(/*is_live=*/true);
  }

  Status WaitForXClusterLeaseOnAllTServers(bool has_lease) {
    return WaitFor(
        [this, has_lease] {
          for (const auto& tserver : cluster_->mini_tablet_servers()) {
            if (tserver->server()->GetXClusterContext().HasXClusterGuardedInfoLease() !=
                has_lease) {
              return false;
            }
          }
          return true;
        },
        30s * kTimeMultiplier,
        Format("Wait for xCluster lease to be $0", has_lease ? "held" : "lost"));
  }

  // Makes master withhold the xCluster-guarded information lease from heartbeat responses, then
  // waits for every TServer's lease to run out.  Unlike LoseXClusterLease, heartbeats keep flowing.
  Status WithholdXClusterLease() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_grant_xcluster_guarded_lease) = true;
    return WaitForXClusterLeaseOnAllTServers(/*has_lease=*/false);
  }

  Status GrantXClusterLease() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_grant_xcluster_guarded_lease) = false;
    return WaitForXClusterLeaseOnAllTServers(/*has_lease=*/true);
  }

  // Connects, waiting for Postgres to start, and runs DDL, which fails on a TServer without the
  // xCluster lease because the TServer then reports every database's xCluster role as UNAVAILABLE.
  Status ExpectPgAcceptsDdls() {
    auto conn = VERIFY_RESULT(Connect());
    return conn.Execute("CREATE TABLE tbl (a int)");
  }
};

// Starts the cluster with master withholding the xCluster-guarded information lease.
class XClusterGuardedLeasePgStartTest : public XClusterGuardedLeasePgTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_grant_xcluster_guarded_lease) = true;
    XClusterGuardedLeasePgTest::SetUp();
  }
};

// Checks that a TServer holding a YSQL lease does not start Postgres until it also holds an
// xCluster-guarded information lease.
TEST_F(XClusterGuardedLeasePgStartTest, PgWaitsForXClusterLease) {
  // Wait for the YSQL lease so that only the xCluster lease is missing.
  ASSERT_OK(WaitForYsqlLease(/*is_live=*/true));

  ASSERT_OK(ExpectPgNotStarted());
  ASSERT_OK(GrantXClusterLease());
  ASSERT_OK(ExpectPgAcceptsDdls());
}

class XClusterGuardedLeasePgStartYsqlLeaseDisabledTest : public XClusterGuardedLeasePgStartTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql_operation_lease) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = false;
    XClusterGuardedLeasePgStartTest::SetUp();
  }
};

// Checks that with the YSQL lease disabled a TServer still does not start Postgres until it holds
// an xCluster-guarded information lease.
TEST_F(XClusterGuardedLeasePgStartYsqlLeaseDisabledTest, PgWaitsForXClusterLease) {
  ASSERT_OK(ExpectPgNotStarted());
  ASSERT_OK(GrantXClusterLease());
  ASSERT_OK(ExpectPgAcceptsDdls());
}

class XClusterGuardedLeasePgStartGateDisabledTest : public XClusterGuardedLeasePgStartTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = false;
    XClusterGuardedLeasePgStartTest::SetUp();
  }
};

// Checks that with the persistence gate disabled Postgres starts without an xCluster-guarded
// information lease.
TEST_F(XClusterGuardedLeasePgStartGateDisabledTest, PgStartsWithoutXClusterLease) {
  // Not ExpectPgAcceptsDdls: DDL needs the xCluster lease to allocate OIDs, gate or no gate.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Fetch("SELECT 1"));
}

// Checks that a running TServer that regains its YSQL lease while it has no xCluster-guarded
// information lease keeps Postgres paused until the xCluster lease returns.
TEST_F(XClusterGuardedLeasePgTest, YsqlLeaseRegainedWithoutXClusterLease) {
  ASSERT_OK(WithholdXClusterLease());
  ASSERT_OK(LoseYsqlLease());
  // Regaining the YSQL lease takes a new lease epoch, which restarts Postgres.
  ASSERT_OK(RegainYsqlLease());

  ASSERT_OK(ExpectPgNotStarted());
  ASSERT_OK(GrantXClusterLease());
  ASSERT_OK(ExpectPgAcceptsDdls());
}

// Checks that a TServer keeping Postgres paused for want of an xCluster-guarded information lease
// does not start it when that lease arrives after the YSQL lease has been lost.
TEST_F(XClusterGuardedLeasePgTest, XClusterLeaseRegainedAfterYsqlLeaseLost) {
  // Get Postgres paused with the held flag set, as in YsqlLeaseRegainedWithoutXClusterLease.
  ASSERT_OK(WithholdXClusterLease());
  ASSERT_OK(LoseYsqlLease());
  ASSERT_OK(RegainYsqlLease());

  // Losing the YSQL lease must clear the held flag, so that the xCluster lease's arrival below does
  // not start Postgres.
  ASSERT_OK(LoseYsqlLease());
  ASSERT_OK(GrantXClusterLease());
  ASSERT_OK(ExpectPgNotStarted());

  ASSERT_OK(RegainYsqlLease());
  ASSERT_OK(ExpectPgAcceptsDdls());
}

// Checks that a TServer that loses and regains its xCluster-guarded information lease while
// Postgres is running leaves Postgres running.
TEST_F(XClusterGuardedLeasePgTest, XClusterLeaseRegainedWithPgRunning) {
  auto conn = ASSERT_RESULT(Connect());
  const auto start_time = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT pg_postmaster_start_time()::text"));

  ASSERT_OK(WithholdXClusterLease());
  ASSERT_OK(GrantXClusterLease());
  // Give the heartbeats that follow the xCluster lease's return a chance to restart or pause
  // Postgres.
  SleepFor(FLAGS_heartbeat_interval_ms * 5ms * kTimeMultiplier);

  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<std::string>("SELECT pg_postmaster_start_time()::text")),
      start_time);
}

// Helpers for tests of tablet leadership on a TServer without an xCluster-guarded information
// lease.  Only kLeaselessTServerIndex's master heartbeats fail, so only that TServer loses its
// lease.
class XClusterGuardedLeaseLeadershipTest : public XClusterGuardedLeaseTest {
 protected:
  void SetUp() override {
    // Otherwise master's leader balancing moves leadership too.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    XClusterGuardedLeaseTest::SetUp();
  }

  // Not kPgTsIndex so that Postgres and its connections are unaffected.
  static constexpr size_t kLeaselessTServerIndex = 1;

  tserver::MiniTabletServer& GetTServer(size_t index) {
    return *cluster_->mini_tablet_server(index);
  }

  tserver::MiniTabletServer& GetLeaselessTServer() { return GetTServer(kLeaselessTServerIndex); }

  std::string GetLeaselessTServerUuid() { return GetLeaselessTServer().server()->permanent_uuid(); }

  // Creates table tbl, with one tablet, and returns that tablet's id.
  Result<TabletId> CreateTable(PGConn& conn) {
    RETURN_NOT_OK(conn.Execute("CREATE TABLE tbl (k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS"));
    const auto peers = VERIFY_RESULT(ListTabletPeersForTableName(cluster_.get(), "tbl"));
    SCHECK(!peers.empty(), IllegalState, "Table tbl has no tablets");
    return peers.front()->tablet_id();
  }

  // Counts replicas in the Raft leader role, whether or not they are ready to serve.
  static size_t CountLeaders(tserver::MiniTabletServer& tserver) {
    size_t num_leaders = 0;
    for (const auto& peer : tserver.server()->tablet_manager()->GetTabletPeers()) {
      if (peer->LeaderStatus() != consensus::LeaderStatus::NOT_LEADER) {
        ++num_leaders;
      }
    }
    return num_leaders;
  }

  // Whether tserver's replica of tablet_id is in the Raft leader role, ready to serve or not.
  Result<bool> IsLeaderOf(tserver::MiniTabletServer& tserver, const TabletId& tablet_id) {
    auto peer = VERIFY_RESULT(tserver.server()->tablet_manager()->GetTablet(tablet_id));
    return peer->LeaderStatus() != consensus::LeaderStatus::NOT_LEADER;
  }

  Status WaitForXClusterLease(tserver::MiniTabletServer& tserver, bool has_lease) {
    return WaitFor(
        [&tserver, has_lease] {
          return tserver.server()->GetXClusterContext().HasXClusterGuardedInfoLease() == has_lease;
        },
        GetLeaseDuration() + MonoDelta(10s * kTimeMultiplier),
        Format("Wait for xCluster lease to be $0", has_lease ? "held" : "lost"));
  }

  // Fails the leaseless TServer's master heartbeats and waits for its lease to run out.
  Status CutLeaselessTServerOffFromMaster() {
    GetLeaselessTServer().FailHeartbeats(true);
    return WaitForXClusterLease(GetLeaselessTServer(), /*has_lease=*/false);
  }

  Status ReconnectLeaselessTServerToMaster() {
    GetLeaselessTServer().FailHeartbeats(false);
    return WaitForXClusterLease(GetLeaselessTServer(), /*has_lease=*/true);
  }

  // For tests that turn relinquishing off: checks that the leaseless TServer then keeps leading a
  // tablet as before, and can win it back after losing it.
  Status ExpectLeaselessTServerLeads() {
    auto conn = VERIFY_RESULT(Connect());
    const auto tablet_id = VERIFY_RESULT(CreateTable(conn));
    RETURN_NOT_OK(TransferLeadership(cluster_.get(), tablet_id, GetLeaselessTServerUuid()));

    RETURN_NOT_OK(CutLeaselessTServerOffFromMaster());
    RETURN_NOT_OK(ExpectLeaselessTServerKeepsLeading(tablet_id));

    RETURN_NOT_OK(TransferLeadership(
        cluster_.get(), tablet_id, GetTServer(kPgTsIndex).server()->permanent_uuid()));
    return TransferLeadership(cluster_.get(), tablet_id, GetLeaselessTServerUuid());
  }

  // Fails if the leaseless TServer stops leading tablet_id within a failover time.
  Status ExpectLeaselessTServerKeepsLeading(const TabletId& tablet_id) {
    auto status = WaitFor(
        [this, &tablet_id]() -> Result<bool> {
          return !VERIFY_RESULT(IsLeaderOf(GetLeaselessTServer(), tablet_id));
        },
        10s * kTimeMultiplier, "Wait for the leaseless TServer to stop leading");
    SCHECK(status.IsTimedOut(), IllegalState, Format("Leadership moved: $0", status));
    return Status::OK();
  }
};

// Checks that a TServer without an xCluster-guarded information lease gives up leadership of all
// its tablets while writes through the other TServers continue, and it can lead again once the
// lease returns.
TEST_F(XClusterGuardedLeaseLeadershipTest, LeaselessTServerGivesUpLeadership) {
  auto conn = ASSERT_RESULT(Connect());
  const auto tablet_id = ASSERT_RESULT(CreateTable(conn));
  // Have the leaseless TServer lead tbl's tablet, so that it has a leadership of tbl to give up.
  ASSERT_OK(TransferLeadership(cluster_.get(), tablet_id, GetLeaselessTServerUuid()));

  ASSERT_OK(CutLeaselessTServerOffFromMaster());
  ASSERT_OK(WaitFor(
      [this] { return CountLeaders(GetLeaselessTServer()) == 0; }, 30s * kTimeMultiplier,
      "Wait for the leaseless TServer to lead no tablets"));
  ASSERT_OK(conn.Execute("INSERT INTO tbl SELECT generate_series(1, 100), 0"));

  ASSERT_OK(ReconnectLeaselessTServerToMaster());
  ASSERT_OK(TransferLeadership(cluster_.get(), tablet_id, GetLeaselessTServerUuid()));
}

// Checks that a TServer without an xCluster-guarded information lease gives up leadership of a
// tablet whose followers lag behind it, and that another replica then leads the tablet.
TEST_F(XClusterGuardedLeaseLeadershipTest, LeaselessTServerGivesUpLeadershipToLaggingFollowers) {
  auto conn = ASSERT_RESULT(Connect());
  const auto tablet_id = ASSERT_RESULT(CreateTable(conn));
  ASSERT_OK(TransferLeadership(cluster_.get(), tablet_id, GetLeaselessTServerUuid()));
  std::vector<std::shared_ptr<consensus::RaftConsensus>> followers;
  for (const auto& peer : ASSERT_RESULT(ListTabletPeers(cluster_.get(), tablet_id))) {
    if (peer->permanent_uuid() != GetLeaselessTServerUuid()) {
      followers.push_back(ASSERT_RESULT(peer->GetRaftConsensus()));
    }
  }

  // Make the followers lag: TEST_DelayUpdate below makes each hold every update before
  // acknowledging it.  Each hold outlasts this test's wait for the step-down.  A write then leaves
  // them behind the leader, so a graceful step-down's protege stays behind, and the step-down can
  // complete only by giving up on synchronizing with it.
  //
  // So that only a step-down, not a follower's election, can take leadership from the leader.  Set
  // before the followers start lagging, since their failure detectors may fire once they do.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_do_not_start_election_test_only) = true;
  for (const auto& follower : followers) {
    follower->TEST_DelayUpdate(45s * kTimeMultiplier);
  }
  // The leader appends the write at once, but it cannot commit while the followers lag.  The
  // statement timeout bounds it, so the test cannot hang on it however it ends.
  auto write_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(write_conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(10s * kTimeMultiplier)));
  auto write = std::async(std::launch::async, [&write_conn] {
    return write_conn.Execute("INSERT INTO tbl SELECT generate_series(1, 100), 0");
  });
  SleepFor(1s * kTimeMultiplier);

  ASSERT_OK(CutLeaselessTServerOffFromMaster());
  ASSERT_OK(WaitFor(
      [this, &tablet_id]() -> Result<bool> {
        return !VERIFY_RESULT(IsLeaderOf(GetLeaselessTServer(), tablet_id));
      },
      15s * kTimeMultiplier, "Wait for the leaseless TServer to stop leading"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_do_not_start_election_test_only) = false;
  for (const auto& follower : followers) {
    follower->TEST_DelayUpdate(MonoDelta::kZero);
  }
  // The write could not commit, and was cut short by its statement timeout or the leader change.
  WARN_NOT_OK(write.get(), "Write failed");
  ASSERT_OK(WaitUntilTabletHasLeader(
      cluster_.get(), tablet_id, CoarseMonoClock::Now() + 30s * kTimeMultiplier));
  ASSERT_FALSE(ASSERT_RESULT(IsLeaderOf(GetLeaselessTServer(), tablet_id)));
  ASSERT_OK(conn.Execute("INSERT INTO tbl SELECT generate_series(101, 200), 0"));
}

// Checks that a TServer without an xCluster-guarded information lease gives up leadership of a
// tablet while a new replica of it is in remote bootstrap, and that the remote bootstrap completes
// under the new leader.
TEST_F(XClusterGuardedLeaseLeadershipTest, LeaselessTServerGivesUpLeadershipDuringRemoteBootstrap) {
  const auto timeout = 30s * kTimeMultiplier;
  auto conn = ASSERT_RESULT(Connect());
  const auto tablet_id = ASSERT_RESULT(CreateTable(conn));
  ASSERT_OK(TransferLeadership(cluster_.get(), tablet_id, GetLeaselessTServerUuid()));

  // Hold the new replica in remote bootstrap, so that it stays a live PRE_VOTER, which StepDown
  // otherwise refuses to step down past.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_remote_bootstrap) = true;
  // Flags are restored only after TearDown, whose cluster shutdown would otherwise wait forever on
  // the paused remote bootstrap if this test failed before unpausing it.
  auto unpause_remote_bootstrap = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_remote_bootstrap) = false;
  });
  const auto new_tserver_index = cluster_->num_tablet_servers();
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(new_tserver_index + 1));

  auto tserver_map = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  const auto* leaseless_tserver = tserver_map[GetLeaselessTServerUuid()].get();
  const auto* pg_tserver = tserver_map[GetTServer(kPgTsIndex).server()->permanent_uuid()].get();
  const auto* new_tserver =
      tserver_map[GetTServer(new_tserver_index).server()->permanent_uuid()].get();
  ASSERT_OK(itest::AddServer(
      leaseless_tserver, tablet_id, new_tserver, consensus::PeerMemberType::PRE_VOTER,
      std::nullopt, timeout));
  ASSERT_OK(itest::WaitUntilCommittedConfigMemberTypeIs(
      /*config_size=*/1, leaseless_tserver, tablet_id, timeout,
      consensus::PeerMemberType::PRE_VOTER));

  ASSERT_OK(CutLeaselessTServerOffFromMaster());
  ASSERT_OK(WaitFor(
      [this, &tablet_id]() -> Result<bool> {
        return !VERIFY_RESULT(IsLeaderOf(GetLeaselessTServer(), tablet_id));
      },
      timeout, "Wait for the leaseless TServer to stop leading"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_remote_bootstrap) = false;
  ASSERT_OK(itest::WaitUntilCommittedConfigMemberTypeIs(
      /*config_size=*/cluster_->num_tablet_servers(), pg_tserver, tablet_id,
      MonoDelta(60s * kTimeMultiplier), consensus::PeerMemberType::VOTER));
}

// Checks that a TServer without an xCluster-guarded information lease does not become leader when
// leadership is transferred to it, and that another TServer becomes leader instead.
TEST_F(XClusterGuardedLeaseLeadershipTest, LeaselessTServerDoesNotWinElections) {
  auto conn = ASSERT_RESULT(Connect());
  const auto tablet_id = ASSERT_RESULT(CreateTable(conn));
  // Lead elsewhere so that only the transfer below could make the leaseless TServer leader.
  ASSERT_OK(TransferLeadership(
      cluster_.get(), tablet_id, GetTServer(kPgTsIndex).server()->permanent_uuid()));

  ASSERT_OK(CutLeaselessTServerOffFromMaster());

  // The old leader logs this when the leaseless protege reports that it lost the election.  Without
  // that report, the old leader would wait out its post-stepdown delay before starting an election.
  // Accordingly we are going to check that the leaseless protege reports this.
  StringWaiterLogSink protege_lost_waiter(
      Format("Our protege $0, lost election", GetLeaselessTServerUuid()));

  auto leader = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
  ASSERT_OK(StepDown(leader, GetLeaselessTServerUuid(), ForceStepDown::kTrue));

  // A protege that starts an election wins it well within this time.
  auto became_leader = WaitFor(
      [this, &tablet_id] { return IsLeaderOf(GetLeaselessTServer(), tablet_id); },
      10s * kTimeMultiplier, "Wait for the leaseless TServer to become leader");
  ASSERT_TRUE(became_leader.IsTimedOut()) << became_leader;
  ASSERT_OK(WaitUntilTabletHasLeader(
      cluster_.get(), tablet_id, CoarseMonoClock::Now() + 30s * kTimeMultiplier));
  ASSERT_FALSE(ASSERT_RESULT(IsLeaderOf(GetLeaselessTServer(), tablet_id)));

  ASSERT_OK(protege_lost_waiter.WaitFor(10s * kTimeMultiplier));
}

// Checks that with the persistence gate disabled, a TServer without an xCluster-guarded information
// lease keeps leading its tablets and can win elections.
TEST_F(XClusterGuardedLeaseLeadershipTest, LeaselessTServerLeadsWithGateDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_persistence_gate) = false;
  ASSERT_OK(ExpectLeaselessTServerLeads());
}

// Checks that with xcluster_lease_relinquish_leadership off a TServer without an xCluster-guarded
// information lease keeps leading its tablets and can win elections.
TEST_F(XClusterGuardedLeaseLeadershipTest, LeaselessTServerLeadsWithRelinquishDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_lease_relinquish_leadership) = false;
  ASSERT_OK(ExpectLeaselessTServerLeads());
}

// Checks that after all TServers restart without the xCluster-guarded information lease:
// - while master withholds the lease, a table's tablet has no leader, because its replicas'
//   elections are refused (as their log shows);
// - once master grants the lease, the tablet gets a leader despite those refusals.
TEST_F(XClusterGuardedLeaseLeadershipTest, TabletGetsLeaderWhenLeaseArrivesAfterRestart) {
  auto conn = ASSERT_RESULT(Connect());
  const auto tablet_id = ASSERT_RESULT(CreateTable(conn));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_grant_xcluster_guarded_lease) = true;
  StringWaiterLogSink election_refusal_waiter("this replica must not seek leadership");
  ASSERT_OK(RestartCluster());
  ASSERT_OK(election_refusal_waiter.WaitFor(30s * kTimeMultiplier));
  ASSERT_NOK(GetLeaderPeerForTablet(cluster_.get(), tablet_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_no_grant_xcluster_guarded_lease) = false;
  // A refused election backs the failure detector off by at most
  // leader_failure_exp_backoff_max_delta_ms; the rest is time for the election itself.
  ASSERT_OK(WaitUntilTabletHasLeader(
      cluster_.get(), tablet_id,
      CoarseMonoClock::Now() +
          (FLAGS_leader_failure_exp_backoff_max_delta_ms * 1ms + 10s) * kTimeMultiplier));
}

// Runs the real yb-tserver with the connection manager enabled and the YSQL lease disabled,
// starting with master withholding the xCluster-guarded information lease.
class XClusterGuardedLeaseConnMgrTest : public LibPqTestBase {
 protected:
  // Shortened from 15 s so that a TServer whose connection manager waits for Postgres at startup
  // gives up quickly.
  static constexpr auto kConnMgrPgWaitTimeout = 2s;

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->replication_factor = 1;
    options->enable_ysql_conn_mgr = true;
    for (auto* flags : {&options->extra_master_flags, &options->extra_tserver_flags}) {
      flags->push_back("--enforce_xcluster_guarded_lease=true");
      // The YSQL lease is enabled whenever object locking is (see IsYsqlLeaseEnabled).  So turn
      // object locking off too, along with the flags that require it, as pg_backends-test does.
      flags->insert(flags->end(), {
          "--enable_ysql_operation_lease=false",
          "--enable_object_locking_for_table_locks=false",
          "--ysql_yb_ddl_transaction_block_enabled=false",
          "--ysql_yb_enable_ddl_savepoint_support=false",
          "--ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks=false",
          "--ysql_enable_concurrent_ddl=false"
      });
    }
    options->extra_master_flags.push_back("--TEST_no_grant_xcluster_guarded_lease=true");
    options->extra_tserver_flags.push_back(Format(
        "--ysql_conn_mgr_pg_conf_wait_timeout_ms=$0", ToMilliseconds(kConnMgrPgWaitTimeout)));
  }

  int GetNumTabletServers() const override { return 1; }
};

// Checks that a TServer running the connection manager with the YSQL lease disabled stays up and
// refuses connections without an xCluster-guarded information lease, then accepts them once the
// xCluster lease is granted.
TEST_F(XClusterGuardedLeaseConnMgrTest, ConnMgrWaitsForXClusterLease) {
  // A TServer whose connection manager waited for Postgres at startup would have given up, and
  // exited, by now.
  SleepFor(3 * kConnMgrPgWaitTimeout * kTimeMultiplier);
  ASSERT_TRUE(pg_ts->IsProcessAlive());
  auto refused_conn = PGConnBuilder({
      .host = pg_ts->bind_host(),
      .port = pg_ts->ysql_port(),
      .connect_timeout = 2,
  }).Connect();
  ASSERT_NOK(refused_conn);

  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_no_grant_xcluster_guarded_lease", "false"));
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE tbl (a int)"));
}

}  // namespace yb
