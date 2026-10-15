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

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/master/xcluster/xcluster_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enforce_xcluster_guarded_lease);
DECLARE_bool(TEST_tserver_disable_heartbeat);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_uint32(xcluster_guarded_lease_duration_ms);

using namespace std::literals;

namespace yb {

using pgwrapper::PGConn;
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
  Status LoseLease(const NamespaceId& namespace_id) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tserver_disable_heartbeat) = true;
    return WaitForRoleOnAllTServers(namespace_id, XClusterNamespaceInfoPB::UNAVAILABLE);
  }

  // Resumes heartbeats and waits for every TServer to hold a lease again.
  Status RegainLease(const NamespaceId& namespace_id) {
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

  ASSERT_OK(LoseLease(namespace_id));
  ASSERT_NOK_STR_CONTAINS(conn.FetchRow<PGUint64>("SELECT nextval('seq')"), kUnavailableErrorMsg);
  ASSERT_NOK_STR_CONTAINS(
      conn.FetchRow<PGUint64>("SELECT setval('seq', 1, true)"), kUnavailableErrorMsg);

  ASSERT_OK(RegainLease(namespace_id));
  ASSERT_OK(conn.FetchRow<PGUint64>("SELECT nextval('seq')"));
}

TEST_F(XClusterGuardedLeaseTest, DdlsBlockedWithoutLease) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE tbl (a int)"));
  ASSERT_OK(conn.Execute("CREATE SEQUENCE seq"));
  ASSERT_OK(conn.Execute("CREATE EXTENSION yb_xcluster_ddl_replication"));
  const auto namespace_id = ASSERT_RESULT(GetCurrentNamespaceId(conn));

  ASSERT_OK(LoseLease(namespace_id));

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

  ASSERT_OK(RegainLease(namespace_id));
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
  ASSERT_OK(LoseLease(namespace_id));
  ASSERT_NOK_STR_CONTAINS(get_new_object_id(), kOidCountUnavailableErrorMsg);

  ASSERT_OK(RegainLease(namespace_id));
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

}  // namespace yb
