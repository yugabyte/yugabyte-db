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

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_xcluster_context_if.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enforce_xcluster_guarded_lease);
DECLARE_bool(TEST_tserver_disable_heartbeat);
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
  static constexpr auto kLeaseDuration = 10s;
  static constexpr auto kUnavailableErrorMsg =
      "forbidden because the xCluster role of the database is currently unavailable";

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_guarded_lease_duration_ms) =
        narrow_cast<uint32_t>(MonoDelta(kLeaseDuration).ToMilliseconds());
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
        kLeaseDuration + 10s * kTimeMultiplier,
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
  for (const auto& ddl :
       {"ALTER TABLE tbl ADD COLUMN b int", "CREATE SEQUENCE seq2", "DROP SEQUENCE seq"}) {
    LOG(INFO) << "Executing: " << ddl;
    ASSERT_NOK_STR_CONTAINS(conn.Execute(ddl), kUnavailableErrorMsg);
  }
  ASSERT_OK(conn.Execute("RESET yb_xcluster_ddl_replication.TEST_replication_role_override"));

  ASSERT_OK(RegainLease(namespace_id));
  ASSERT_OK(conn.Execute("ALTER TABLE tbl ADD COLUMN b int"));
}

}  // namespace yb
