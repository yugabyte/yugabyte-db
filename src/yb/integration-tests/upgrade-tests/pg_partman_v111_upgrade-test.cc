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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/common/ddl_mode-test-util.h"
#include "yb/util/debug.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

namespace {

constexpr auto kExtensionVersion =
    "SELECT extversion FROM pg_extension WHERE extname = 'pg_partman'";
constexpr auto kUndoPartition =
    "SELECT * FROM partman.undo_partition('public.orders', p_batch_count := 100, "
    "p_keep_table := false, p_target_table := 'public.orders_target')";
constexpr auto kUseNative = "SELECT pg_catalog.yb_pg_partman_check_native_txn_ddl_and_locks()";
constexpr auto kNativeFlag = "ysql_yb_pg_partman_use_native_txn_ddl_and_locks";
constexpr auto kRefusal = "which yb-admin upgrade_ysql adds when the upgrade is finalized";
constexpr auto kNewDatabase = "partman_during_upgrade";
constexpr auto kPartmanFunctionDefs =
    "SELECT string_agg(pg_get_functiondef(p.oid), E'\\n' ORDER BY p.oid::regprocedure::text) "
    "FROM pg_depend d JOIN pg_proc p ON p.oid = d.objid "
    "WHERE d.classid = 'pg_proc'::regclass AND d.deptype = 'e' "
    "AND d.refobjid = (SELECT oid FROM pg_extension WHERE extname = 'pg_partman')";

} // namespace

class PgPartmanV111UpgradeTest : public UpgradeTestBase {
 public:
  PgPartmanV111UpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}

 protected:
  // On the old version: pg_partman 4.7.4 and a daily partition set public.orders with 10 rows.
  Status CreatePartitionSet() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    RETURN_NOT_OK(conn.Execute("CREATE SCHEMA partman"));
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION pg_partman WITH SCHEMA partman"));
    SCHECK_EQ(VERIFY_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "4.7.4", IllegalState,
              "Unexpected pg_partman version");
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE public.orders (id int NOT NULL, created timestamptz NOT NULL) "
        "PARTITION BY RANGE (created)"));
    RETURN_NOT_OK(conn.Execute("CREATE TABLE public.orders_target (LIKE public.orders)"));
    SCHECK(VERIFY_RESULT(conn.FetchRow<bool>(
               "SELECT partman.create_parent('public.orders', 'created', 'native', 'daily', "
               "p_premake := 2)")),
           IllegalState, "create_parent failed");
    return conn.Execute("INSERT INTO public.orders SELECT g, now() FROM generate_series(1, 10) g");
  }
};

TEST_F(PgPartmanV111UpgradeTest, NativeTxnDdlAndLocks) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(CreatePartitionSet());
  std::string leftover;
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NOK_STR_CONTAINS(conn.Fetch(kUndoPartition), "undo_partition not supported yet in YB");
    // Leave the newest child created but not attached, as a run that failed partway would.
    leftover = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT partition_tablename FROM partman.show_partitions('public.orders', 'DESC') "
        "LIMIT 1"));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE public.orders DETACH PARTITION public.$0", leftover));
  }

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  ASSERT_EQ(ASSERT_RESULT(cluster_->GetFlag(cluster_->tablet_server(0), kNativeFlag)),
            kIsDebug ? "false" : "true");

  for (auto* daemon : cluster_->daemons()) {
    ToggleDDLMode(*daemon->mutable_flags(), /* use_legacy = */ false);
    daemon->AddExtraFlag("ysql_yb_enable_advisory_locks", "true");
    daemon->AddExtraFlag("ysql_yb_enable_ddl_savepoint_support", "true");
  }
  cluster_->AddExtraFlagOnTServers(kNativeFlag, "true");
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes));

  // Until the upgrade is finalized, the flag has no effect, and pg_partman refuses to be installed
  // or updated to 4.7.4-yb-1.0. open_conn stays open across finalize, to check that a session does
  // not have to reconnect to get the new functions.
  auto open_conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_NOK_STR_CONTAINS(
      open_conn.Fetch(kUndoPartition), "undo_partition not supported yet in YB");
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "4.7.4");
    ASSERT_NOK_STR_CONTAINS(conn.Execute("ALTER EXTENSION pg_partman UPDATE"), kRefusal);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "4.7.4");

    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kNewDatabase));
    auto new_db_conn = ASSERT_RESULT(cluster_->ConnectToDB(kNewDatabase));
    ASSERT_OK(new_db_conn.Execute("CREATE SCHEMA partman"));
    ASSERT_NOK_STR_CONTAINS(
        new_db_conn.Execute("CREATE EXTENSION pg_partman WITH SCHEMA partman"), kRefusal);
  }

  ASSERT_OK(FinalizeUpgrade());
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "4.7.4-yb-1.0");
    auto new_db_conn = ASSERT_RESULT(cluster_->ConnectToDB(kNewDatabase));
    ASSERT_OK(new_db_conn.Execute("CREATE EXTENSION pg_partman WITH SCHEMA partman"));
    ASSERT_TRUE(ASSERT_RESULT(new_db_conn.FetchRow<bool>(kUseNative)));

    // An updated database has the same pg_partman functions as a fresh install. If this fails, a
    // function under sql/functions or sql/procedures changed without a new version: bump
    // default_version in pg_partman.control, add updates/pg_partman--<old>--<new>.sql with the
    // changed definitions, and add a YSQL migration that runs ALTER EXTENSION pg_partman UPDATE.
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kPartmanFunctionDefs)),
              ASSERT_RESULT(new_db_conn.FetchRow<std::string>(kPartmanFunctionDefs)));

    const auto attached = Format(
        "SELECT count(*) FROM pg_inherits WHERE inhrelid = 'public.$0'::regclass", leftover);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(attached)), 0);
    ASSERT_RESULT(conn.Fetch("SELECT partman.run_maintenance()"));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(attached)), 1);

    ASSERT_RESULT(open_conn.Fetch(kUndoPartition));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM public.orders_target")),
              10);

    // A set created on the native path. Its default partition is empty, so backing out only
    // needs to drop it, as the flag description says.
    ASSERT_OK(conn.Execute(
        "CREATE TABLE public.events (id int NOT NULL, created timestamptz NOT NULL) "
        "PARTITION BY RANGE (created)"));
    ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
        "SELECT partman.create_parent('public.events', 'created', 'native', 'daily', "
        "p_premake := 2)")));
    ASSERT_OK(conn.Execute("DROP TABLE public.events_default"));
  }

  // Back out by turning only this flag off.
  cluster_->AddExtraFlagOnTServers(kNativeFlag, "false");
  ASSERT_OK(RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes));
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("SHOW yb_ddl_transaction_block_enabled")),
              "on");
    ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>(kUseNative)));

    ASSERT_OK(conn.Execute(
        "INSERT INTO public.events SELECT g, now() FROM generate_series(1, 10) g"));
    const auto num_children = [&conn]() -> Result<int64_t> {
      return conn.FetchRow<int64_t>(
          "SELECT count(*) FROM partman.show_partitions('public.events')");
    };
    const auto children = ASSERT_RESULT(num_children());
    ASSERT_OK(conn.Execute(
        "UPDATE partman.part_config SET premake = 4 WHERE parent_table = 'public.events'"));
    ASSERT_RESULT(conn.Fetch("SELECT partman.run_maintenance('public.events')"));
    ASSERT_GT(ASSERT_RESULT(num_children()), children);

    ASSERT_OK(conn.Execute("CREATE TABLE public.items (id int NOT NULL) PARTITION BY RANGE (id)"));
    ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
        "SELECT partman.create_parent('public.items', 'id', 'native', '10')")));
    ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
        "SELECT to_regclass('public.items_default') IS NULL")));

    ASSERT_NOK_STR_CONTAINS(
        conn.Fetch("SELECT * FROM partman.undo_partition('public.events')"),
        "undo_partition requires the gflag ysql_yb_pg_partman_use_native_txn_ddl_and_locks");
  }
}

// Transactional DDL stays off through the upgrade, as the CDC docs ask, while the pg_partman
// flag is on. After finalize the flag still reads on, but pg_partman keeps its existing behavior.
TEST_F(PgPartmanV111UpgradeTest, TxnDdlOff) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(CreatePartitionSet());

  for (auto* daemon : cluster_->daemons()) {
    ToggleDDLMode(*daemon->mutable_flags(), /* use_legacy = */ true);
  }
  cluster_->AddExtraFlagOnTServers(kNativeFlag, "true");
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "4.7.4-yb-1.0");
  ASSERT_EQ(ASSERT_RESULT(cluster_->GetFlag(cluster_->tablet_server(0), kNativeFlag)), "true");
  ASSERT_FALSE(ASSERT_RESULT(conn.FetchRow<bool>(kUseNative)));

  const auto num_children = [&conn]() -> Result<int64_t> {
    return conn.FetchRow<int64_t>(
        "SELECT count(*) FROM partman.show_partitions('public.orders')");
  };
  const auto children = ASSERT_RESULT(num_children());
  ASSERT_OK(conn.Execute("UPDATE partman.part_config SET premake = 4"));
  ASSERT_RESULT(conn.Fetch("SELECT partman.run_maintenance()"));
  ASSERT_GT(ASSERT_RESULT(num_children()), children);

  ASSERT_OK(conn.Execute("CREATE TABLE public.items (id int NOT NULL) PARTITION BY RANGE (id)"));
  ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
      "SELECT partman.create_parent('public.items', 'id', 'native', '10')")));
  ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
      "SELECT to_regclass('public.items_default') IS NULL")));

  ASSERT_NOK_STR_CONTAINS(
      conn.Fetch(kUndoPartition),
      "undo_partition requires the gflag ysql_yb_pg_partman_use_native_txn_ddl_and_locks");
}

} // namespace yb
