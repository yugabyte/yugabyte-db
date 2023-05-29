// Copyright (c) Yugabyte, Inc.
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

#include "yb/common/wire_protocol.h"

#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

using std::string;

using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgCatalogVersionTest : public LibPqTestBase {
 protected:
  using Version = uint64_t;

  struct CatalogVersion {
    Version current_version;
    Version last_breaking_version;
  };

  static constexpr auto kYugabyteDatabase = "yugabyte";
  static constexpr auto kTestDatabase = "test_db";

  using MasterCatalogVersionMap = std::unordered_map<Oid, CatalogVersion>;
  using ShmCatalogVersionMap = std::unordered_map<Oid, Version>;

  // Prepare the table pg_yb_catalog_version according to 'per_database_mode':
  // * if 'per_database_mode' is true, we prepare table pg_yb_catalog_version
  //   for per-database catalog version mode by updating the table to have one
  //   row per database.
  // * if 'per_database_mode' is false, we prepare table pg_yb_catalog_version
  //   for global catalog version mode by deleting all its rows except for
  //   template1.
  Status PrepareDBCatalogVersion(PGConn* conn, bool per_database_mode = true) {
    if (per_database_mode) {
      LOG(INFO) << "Preparing pg_yb_catalog_version to have one row per database";
    } else {
      LOG(INFO) << "Preparing pg_yb_catalog_version to only have one row for template1";
    }
    RETURN_NOT_OK(conn->Execute("SET yb_non_ddl_txn_for_sys_tables_allowed=1"));
    VERIFY_RESULT(conn->FetchFormat(
        "SELECT yb_fix_catalog_version_table($0)", per_database_mode ? "true" : "false"));
    return Status::OK();
  }

  void RestartClusterSetDBCatalogVersionMode(
      bool enabled, const std::vector<string>& extra_tserver_flags) {
    LOG(INFO) << "Restart the cluster and turn "
              << (enabled ? "on" : "off") << " --TEST_enable_db_catalog_version_mode";
    cluster_->Shutdown();
    const string db_catalog_version_gflag =
      Format("--TEST_enable_db_catalog_version_mode=$0", enabled ? "true" : "false");
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      cluster_->master(i)->mutable_flags()->push_back(db_catalog_version_gflag);
    }
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      cluster_->tablet_server(i)->mutable_flags()->push_back(db_catalog_version_gflag);
      for (const auto& flag : extra_tserver_flags) {
        cluster_->tablet_server(i)->mutable_flags()->push_back(flag);
      }
    }
    ASSERT_OK(cluster_->Restart());
  }

  void RestartClusterWithoutDBCatalogVersionMode(
      const std::vector<string>& extra_tserver_flags = {}) {
    RestartClusterSetDBCatalogVersionMode(false, extra_tserver_flags);
  }

  void RestartClusterWithDBCatalogVersionMode(
      const std::vector<string>& extra_tserver_flags = {}) {
    RestartClusterSetDBCatalogVersionMode(true, extra_tserver_flags);
  }

  // Return a MasterCatalogVersionMap by making a query of the pg_yb_catalog_version table.
  static Result<MasterCatalogVersionMap> GetMasterCatalogVersionMap(PGConn* conn) {
    auto res = VERIFY_RESULT(conn->Fetch("SELECT * FROM pg_yb_catalog_version"));
    const auto lines = PQntuples(res.get());
    SCHECK_GT(lines, 0, IllegalState, "empty version map");
    SCHECK_EQ(PQnfields(res.get()), 3, IllegalState, "Unexpected column count");
    MasterCatalogVersionMap result;
    std::string output;
    for (int i = 0; i != lines; ++i) {
      const auto db_oid = VERIFY_RESULT(GetValue<PGOid>(res.get(), i, 0));
      const auto current_version = VERIFY_RESULT(GetValue<PGUint64>(res.get(), i, 1));
      const auto last_breaking_version = VERIFY_RESULT(GetValue<PGUint64>(res.get(), i, 2));
      result.emplace(db_oid, CatalogVersion{current_version, last_breaking_version});
      if (!output.empty()) {
        output += ", ";
      }
      output += Format("($0, $1, $2)", db_oid, current_version, last_breaking_version);
    }
    LOG(INFO) << "Catalog version map: " << output;
    return result;
  }

  static void WaitForCatalogVersionToPropagate() {
    // This is an estimate that should exceed the tserver to master hearbeat interval.
    // However because it is an estimate, this function may return before the catalog version is
    // actually propagated.
    constexpr int kSleepSeconds = 2;
    LOG(INFO) << "Wait " << kSleepSeconds << " seconds for heartbeat to propagate catalog versions";
    std::this_thread::sleep_for(kSleepSeconds * 1s);
  }

  // Verify that all the tservers have identical shared memory db catalog version array by
  // making RPCs to the tservers. Unallocated array slots should have value 0. Return a
  // ShmCatalogVersionMap which represents the contents of allocated slots in the shared
  // memory db catalog version array.
  Result<ShmCatalogVersionMap> GetShmCatalogVersionMap() {
    constexpr auto kRpcTimeout = 30s;
    ShmCatalogVersionMap result;
    for (size_t tablet_index = 0; tablet_index != cluster_->num_tablet_servers(); ++tablet_index) {
      // Get the shared memory object from tserver at 'tablet_index'.
      auto proxy = cluster_->GetProxy<tserver::TabletServerServiceProxy>(
          cluster_->tablet_server(tablet_index));
      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      tserver::GetSharedDataRequestPB shared_data_req;
      tserver::GetSharedDataResponsePB shared_data_resp;
      RETURN_NOT_OK(proxy.GetSharedData(shared_data_req, &shared_data_resp, &controller));
      const auto& data = shared_data_resp.data();
      tserver::TServerSharedData tserver_shared_data;
      SCHECK_EQ(
          data.size(), sizeof(tserver_shared_data),
          IllegalState, "Unexpected response size");
      memcpy(pointer_cast<void*>(&tserver_shared_data), data.c_str(), data.size());
      size_t initialized_slots_count = 0;
      for (size_t i = 0; i < tserver::TServerSharedData::kMaxNumDbCatalogVersions; ++i) {
        if (tserver_shared_data.ysql_db_catalog_version(i)) {
          ++initialized_slots_count;
        }
      }

      // Get the tserver catalog version info from tserver at 'tablet_index'.
      tserver::GetTserverCatalogVersionInfoRequestPB catalog_version_req;
      tserver::GetTserverCatalogVersionInfoResponsePB catalog_version_resp;
      controller.Reset();
      controller.set_timeout(kRpcTimeout);
      RETURN_NOT_OK(proxy.GetTserverCatalogVersionInfo(
          catalog_version_req, &catalog_version_resp, &controller));
      if (catalog_version_resp.has_error()) {
        return StatusFromPB(catalog_version_resp.error().status());
      }
      ShmCatalogVersionMap catalog_versions;
      std::string output;
      for (const auto& entry : catalog_version_resp.entries()) {
        SCHECK(entry.has_db_oid() && entry.has_shm_index(), IllegalState, "missed fields");
        auto db_oid = entry.db_oid();
        auto shm_index = entry.shm_index();
        const auto current_version = tserver_shared_data.ysql_db_catalog_version(shm_index);
        SCHECK_NE(current_version, 0UL, IllegalState, "uninitialized version is not expected");
        catalog_versions.emplace(db_oid, current_version);
        if (!output.empty()) {
          output += ", ";
        }
        output += Format("($0, $1)", db_oid, current_version);
      }
      SCHECK_EQ(
        initialized_slots_count, catalog_versions.size(),
        IllegalState, "unexpected version count");
      LOG(INFO) << "Shm catalog version map at tserver " << tablet_index << ": " << output;
      if (tablet_index == 0) {
        result = std::move(catalog_versions);
      } else {
        // In stable state, all tservers should have the same catalog version map.
        SCHECK(result == catalog_versions, IllegalState, "catalog versions doesn't match");
      }
    }
    return result;
  }

  struct CatalogVersionMatcher {
    Status operator()(const CatalogVersion& lhs, const CatalogVersion& rhs) const {
      SCHECK_EQ(
          lhs.last_breaking_version, rhs.last_breaking_version, InvalidArgument,
          "last_breaking_version doesn't match");
      return (*this)(lhs.current_version, rhs.current_version);
    }

    Status operator()(const CatalogVersion& lhs, const Version& rhs) const {
      return (*this)(lhs.current_version, rhs);
    }

    Status operator()(const Version& lhs, const CatalogVersion& rhs) const {
      return (*this)(lhs, rhs.current_version);
    }

    Status operator()(const Version& lhs, const Version& rhs) const {
      SCHECK_EQ(lhs, rhs, InvalidArgument, "current_version doesn't match");
      return Status::OK();
    }
  };

  static Status CheckMatch(const CatalogVersion& lhs, const CatalogVersion& rhs) {
    return CatalogVersionMatcher()(lhs, rhs);
  }

  template<class K, class V1, class V2, class Matcher>
  static Status CheckMatch(
      const std::unordered_map<K, V1>& lhs,
      const std::unordered_map<K, V2>& rhs,
      Matcher matcher) {
    SCHECK_EQ(
        lhs.size(), rhs.size(), InvalidArgument, "map size doesn't match");
    for (const auto& entry : lhs) {
      auto it = rhs.find(entry.first);
      SCHECK(
          it != rhs.end(), InvalidArgument,
          Format("key '$0' is not found in second map", entry.first));
      RETURN_NOT_OK_PREPEND(matcher(entry.second, it->second),
                            Format("value for key '$0' doesn't match", entry.first));
    }
    return Status::OK();
  }

  template<class K, class V1, class V2>
  static Status CheckMatch(
      const std::unordered_map<K, V1>& lhs, const std::unordered_map<K, V2>& rhs) {
    return CheckMatch(lhs, rhs, CatalogVersionMatcher());
  }

  static Result<PGConn> EnableCacheEventLog(Result<PGConn> connection) {
    return VLOG_IS_ON(1) ? Execute(std::move(connection), "SET yb_debug_log_catcache_events = ON")
                         : std::move(connection);
  }

  // Verify the table pg_yb_catalog_version has the expected set of db_oids:
  // * when single_row is true, return true if pg_yb_catalog_version has one row
  //   for db_oid 1.
  // * when single_row is false, return true if pg_yb_catalog_version has the same set
  //   of db_oids as the set of oids of pg_database.
  Result<bool> VerifyCatalogVersionTableDbOids(PGConn* conn, bool single_row) {
    auto res = VERIFY_RESULT(conn->Fetch("SELECT db_oid FROM pg_yb_catalog_version"));
    auto lines = PQntuples(res.get());
    std::unordered_set<PgOid> pg_yb_catalog_version_db_oids;
    for (int i = 0; i != lines; ++i) {
      const auto oid = VERIFY_RESULT(GetValue<PGOid>(res.get(), i, 0));
      pg_yb_catalog_version_db_oids.insert(oid);
    }
    if (single_row) {
      return pg_yb_catalog_version_db_oids.size() == 1 &&
             *pg_yb_catalog_version_db_oids.begin() == 1;
    }
    res = VERIFY_RESULT(conn->Fetch("SELECT oid FROM pg_database"));
    lines = PQntuples(res.get());
    std::unordered_set<PgOid> pg_database_oids;
    for (int i = 0; i != lines; ++i) {
      const auto oid = VERIFY_RESULT(GetValue<PGOid>(res.get(), i, 0));
      pg_database_oids.insert(oid);
    }
    return pg_database_oids == pg_yb_catalog_version_db_oids;
  }

  // A global DDL statement is a DDL statement that has a cluster-wide impact.
  // If a global DDL statement is executed from a connection that is connected
  // to one database, it should cause catalog cache refreshes on all the active
  // connections that are connected to different databases in order to ensure
  // correctness. A simple implementation is to increment catalog versions of
  // all the databases in pg_yb_catalog_version. Per-database catalog version
  // mode should not be used in single-tenant clusters until we can properly
  // identify and support global DDL statements.
  // Not all shared relations have catalog caches. The following 6 shared
  // relations have been identified that have catalog caches:
  //   pg_authid
  //   pg_auth_members
  //   pg_database
  //   pg_replication_origin
  //   pg_subscription
  //   pg_tablespace
  // Currently, pg_replication_origin and pg_subscription are not used by YSQL.
  // These two tables are used for PostgreSQL replication but YSQL uses raft to
  // achieve that. YSQL also uses a different mechanism to do asynchronous
  // replication.
  // In this test we cover global DDL statements that involve
  //   pg_authid
  //   pg_auth_members
  //   pg_database
  //   pg_tablespace
  void TestDBCatalogVersionGlobalDDLHelper(bool disable_global_ddl) {
    constexpr auto kTestUser1 = "test_user1";
    constexpr auto kTestUser2 = "test_user2";
    constexpr auto kTestGroup = "test_group";
    constexpr auto kTestTablespace = "test_tsp";
    // Test setup.
    auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
    ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte));
    if (disable_global_ddl) {
      RestartClusterWithDBCatalogVersionMode(
        {"--ysql_disable_global_impact_ddl_statements=true"});
    } else {
      RestartClusterWithDBCatalogVersionMode();
    }
    LOG(INFO) << "Connects to database " << kYugabyteDatabase << " on node at index 0.";
    pg_ts = cluster_->tablet_server(0);
    conn_yugabyte = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kYugabyteDatabase)));
    LOG(INFO) << "Create a new database";
    ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE DATABASE $0", kTestDatabase));
    LOG(INFO) << "Create two new test users";
    ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE USER $0", kTestUser1));
    ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE USER $0", kTestUser2));
    LOG(INFO) << "Create a new group that has the second new user";
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "CREATE GROUP $0 WITH USER $1", kTestGroup, kTestUser2));
    LOG(INFO) << "Create a new tablespace";
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "CREATE TABLESPACE $0 LOCATION '/data'", kTestTablespace));
    LOG(INFO) << "Connects to database " << kTestDatabase << " as user "
              << kTestUser1 << " on node at index 1.";
    pg_ts = cluster_->tablet_server(1);
    auto conn_test = ASSERT_RESULT(ConnectToDBAsUser(kTestDatabase, kTestUser1));

    // Test case 1: global ddl writing to pg_database.
    LOG(INFO) << "Create a temporary table t1 on conn_test";
    ASSERT_OK(conn_test.Execute("CREATE TEMP TABLE t1(id INT)"));

    // The following REVOKE is a global DDL that writes to shared relation
    // pg_database and should cause catalog cache refresh of all connections.
    LOG(INFO) << "Revoke temp table creation privilege on the new database";
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "REVOKE TEMP ON DATABASE $0 FROM public", kTestDatabase));
    WaitForCatalogVersionToPropagate();

    auto status = conn_test.Execute("CREATE TEMP TABLE t2(id INT)");
    if (disable_global_ddl) {
      // This temp table t2 creation succeeds because global DDL statements
      // are disabled and the effect of the previous REVOKE is only seen on
      // conn_yugabyte, not on conn_test.
      ASSERT_OK(status);
    } else {
      // This temp table t2 creation should fail because the effect of the
      // previous REVOKE is not only seen on conn_yugabyte but also on
      // conn_test.
      ASSERT_TRUE(status.IsNetworkError()) << status;
      ASSERT_STR_CONTAINS(status.ToString(), "permission denied for schema");
    }

    // Test case 2: global ddl writing to pg_tablespace.
    LOG(INFO) << "Try to create a table t3 in the test tablespace on conn_test";
    ASSERT_NOK(conn_test.ExecuteFormat(
        "CREATE TABLE t3(id INT) TABLESPACE $0", kTestTablespace));

    // The following GRANT is a global DDL that writes to shared relation
    // pg_tablespace and should cause catalog cache refresh of all connections.
    LOG(INFO) << "Grant usage of the new tablespace";
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "GRANT CREATE ON TABLESPACE $0 TO public", kTestTablespace));
    WaitForCatalogVersionToPropagate();

    LOG(INFO) << "Try to create a table t4 in the test tablespace on conn_test";
    status = conn_test.ExecuteFormat(
          "CREATE TABLE t4(id INT) TABLESPACE $0", kTestTablespace);
    if (disable_global_ddl) {
      // This table t4 creation fails because global DDL statements are
      // disabled and the effect of the previous GRANT is only seen on
      // conn_yugabyte, not on conn_test.
      ASSERT_NOK(status);
    } else {
      // This table t4 creation should succeed because the effect of the
      // previous GRANT is not only seen on conn_yugabyte but also on conn_test.
      ASSERT_OK(status);
    }

    // Test case 3: global ddl writing to pg_authid and pg_auth_members.
    LOG(INFO) << "Connects to database " << kTestDatabase << " as user "
              << kTestUser1 << " on node at index 0.";
    pg_ts = cluster_->tablet_server(0);
    auto conn_test1 = ASSERT_RESULT(ConnectToDBAsUser(kTestDatabase, kTestUser1));
    LOG(INFO) << "Create a table t5 on conn_test1 and grant all to test_group";
    ASSERT_OK(conn_test1.Execute("CREATE TABLE t5(id INT)"));
    ASSERT_OK(conn_test1.ExecuteFormat("GRANT ALL ON t5 TO $0", kTestGroup));

    // Connect to database yugabyte as test_user2.
    LOG(INFO) << "Connects to database " << kTestDatabase << " as user "
              << kTestUser2 << " on node at index 1.";
    pg_ts = cluster_->tablet_server(1);
    auto conn_test2 = ASSERT_RESULT(ConnectToDBAsUser(kTestDatabase, kTestUser2));
    // The test_user2 is a member of test_group, which has been granted ALL
    // privileges on table t5. Therefore this query should succeed.
    ASSERT_OK(conn_test2.Fetch("SELECT * FROM t5"));

    LOG(INFO) << "Connects to database template1 on node at index 0.";
    pg_ts = cluster_->tablet_server(0);
    auto conn_template1 = ASSERT_RESULT(ConnectToDB("template1"));

    // The following ALTER is a global DDL that writes to shared relations
    // pg_authid and pg_auth_members so it should cause catalog cache refresh
    // of all connections.
    ASSERT_OK(conn_template1.ExecuteFormat(
        "ALTER GROUP $0 DROP USER $1", kTestGroup, kTestUser2));
    WaitForCatalogVersionToPropagate();

    status = ResultToStatus(conn_test2.Fetch("SELECT * FROM t5"));
    if (disable_global_ddl) {
      // This table t5 selection succeeds because global DDL statements are
      // disabled and the effect of the previous ALTER GROUP is only seen on
      // conn_template1, not on conn_test2.
      ASSERT_OK(status);
    } else {
      // This table t5 selection should fail because test_user2 no longer
      // belongs to test_group and therefore has lost privilege on table t5.
      // Notice that the effect of the previous ALTER GROUP is not only seen
      // on conn_template1 but also on conn_test2.
      ASSERT_TRUE(status.IsNetworkError()) << status;
      ASSERT_STR_CONTAINS(status.ToString(), "permission denied for table t5");
    }
  }
};

TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(DBCatalogVersion)) {
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte));
  // Remember the number of pre-existing databases.
  size_t num_initial_databases = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte)).size();
  LOG(INFO) << "num_initial_databases: " << num_initial_databases;
  // Set --ysql_num_databases_reserved_in_db_catalog_version_mode to a large number
  // that allows room for only one more database to be created.
  RestartClusterWithDBCatalogVersionMode(
      {Format("--ysql_num_databases_reserved_in_db_catalog_version_mode=$0",
              tserver::TServerSharedData::kMaxNumDbCatalogVersions -
              num_initial_databases - 1)});
  LOG(INFO) << "Connects to database '" << kYugabyteDatabase << "' on node at index 0.";
  pg_ts = cluster_->tablet_server(0);
  conn_yugabyte = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kYugabyteDatabase)));

  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));

  // Get the initial catalog version map.
  constexpr CatalogVersion kInitialCatalogVersion{1, 1};
  auto expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  ASSERT_TRUE(expected_versions.find(yugabyte_db_oid) != expected_versions.end());
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kInitialCatalogVersion));
  }

  ASSERT_OK(CheckMatch(expected_versions, ASSERT_RESULT(GetShmCatalogVersionMap())));

  LOG(INFO) << "Create a new database";
  ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE DATABASE $0", kTestDatabase));

  // Wait for heartbeat to happen so that we can see from the test logs that the catalog version
  // change caused by the last DDL is passed from master to tserver via heartbeat. Without the
  // wait, if the next DDL is executed before the next heartbeat then last DDL's catalog version
  // change will be overwritten and we will not see the effect of the last DDL from test logs.
  // So the purpose of this wait is not for correctness but for us to see the catalog version
  // propagation from the test logs. Same is true for all the following calls to do this wait.
  WaitForCatalogVersionToPropagate();
  // There should be a new row in pg_yb_catalog_version for the newly created database.
  const auto new_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kTestDatabase));
  expected_versions[new_db_oid] = kInitialCatalogVersion;
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte))));
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetShmCatalogVersionMap())));

  LOG(INFO) << "Make a new connection to a different node at index 1";
  pg_ts = cluster_->tablet_server(1);
  auto conn_test = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kTestDatabase)));

  LOG(INFO) << "Create a table";
  ASSERT_OK(conn_test.ExecuteFormat("CREATE TABLE t(id int)"));

  WaitForCatalogVersionToPropagate();
  // Should still have the same number of rows in pg_yb_catalog_version.
  // The above create table statement does not cause catalog version to change.
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte))));
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetShmCatalogVersionMap())));

  LOG(INFO) << "Read the table from 'conn_test'";
  ASSERT_OK(conn_test.Fetch("SELECT * FROM t"));

  LOG(INFO) << "Drop the table from 'conn_test'";
  ASSERT_OK(conn_test.ExecuteFormat("DROP TABLE t"));

  WaitForCatalogVersionToPropagate();
  // Under --TEST_enable_db_catalog_version_mode=true, only the row for 'new_db_oid' is updated.
  expected_versions[new_db_oid] = {2, 1};
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte))));
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetShmCatalogVersionMap())));

  LOG(INFO) << "Execute a DDL statement that causes a breaking catalog change";
  ASSERT_OK(conn_test.Execute("REVOKE ALL ON SCHEMA public FROM public"));

  WaitForCatalogVersionToPropagate();
  // Under --TEST_enable_db_catalog_version_mode=true, only the row for 'new_db_oid' is updated.
  // We should have incremented the row for 'new_db_oid', including both the current version
  // and the last breaking version because REVOKE is a DDL statement that causes a breaking
  // catalog change.
  expected_versions[new_db_oid] = {3, 3};
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte))));
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetShmCatalogVersionMap())));

  // Even though 'conn_test' is still accessing 'test_db' through node at index 1, we
  // can still drop it from 'conn_yugabyte'.
  LOG(INFO) << "Drop the new database from 'conn_yugabyte'";
  ASSERT_OK(conn_yugabyte.ExecuteFormat("DROP DATABASE $0", kTestDatabase));

  WaitForCatalogVersionToPropagate();
  // The row for 'new_db_oid' should be deleted.
  // We should not have incremented a row for any database because the drop database
  // statement does not change the catalog version in per-database catalog version mode.
  expected_versions.erase(new_db_oid);
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte))));
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetShmCatalogVersionMap())));

  // After the test database is dropped, 'conn_test' should no longer succeed.
  LOG(INFO) << "Read the table from 'conn_test'";
  auto status = ResultToStatus(conn_test.Fetch("SELECT * FROM t"));
  LOG(INFO) << "status: " << status;
  ASSERT_TRUE(status.IsNetworkError());
  ASSERT_STR_CONTAINS(status.ToString(),
                      Format("catalog version for database $0 was not found", new_db_oid));
  ASSERT_STR_CONTAINS(status.ToString(), "Database might have been dropped by another user");

  // Recreate the same database and table.
  LOG(INFO) << "Re-create the same database";
  ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE DATABASE $0", kTestDatabase));

  // Use a new connection to re-create the table.
  auto new_conn_test = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kTestDatabase)));
  LOG(INFO) << "Re-create the table";
  ASSERT_OK(new_conn_test.ExecuteFormat("CREATE TABLE t(id int)"));

  WaitForCatalogVersionToPropagate();
  // Although we recreate the database using the same name, a new db OID is allocated.
  const auto recreated_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kTestDatabase));
  ASSERT_GT(recreated_db_oid, new_db_oid);
  expected_versions[recreated_db_oid] = kInitialCatalogVersion;
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte))));
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetShmCatalogVersionMap())));

  // The old connection will not become valid simply because we have recreated the
  // same database and table.
  LOG(INFO) << "Read the table from 'conn_test'";
  status = ResultToStatus(conn_test.Fetch("SELECT * FROM t"));
  LOG(INFO) << "status: " << status;
  ASSERT_TRUE(status.IsNetworkError());
  ASSERT_STR_CONTAINS(status.ToString(),
                      Format("catalog version for database $0 was not found", new_db_oid));
  ASSERT_STR_CONTAINS(status.ToString(), "Database might have been dropped by another user");

  // We need to make a new connection to the recreated database in order to have a
  // successful query of the re-created table.
  conn_test = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kTestDatabase)));
  ASSERT_OK(conn_test.Fetch("SELECT * FROM t"));

  // This create database will hit the limit.
  status = conn_yugabyte.ExecuteFormat("CREATE DATABASE $0_2", kTestDatabase);
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "too many databases");
}

/*
 * (1) the test session connects to a database
 * (2) the yugabyte session drops the database from another node
 * (3) the test session runs its first query
 */
TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(DBCatalogVersionDropDB)) {
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte));
  RestartClusterWithDBCatalogVersionMode();
  LOG(INFO) << "Connects to database '" << kYugabyteDatabase << "' on node at index 0.";
  pg_ts = cluster_->tablet_server(0);
  conn_yugabyte = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kYugabyteDatabase)));
  LOG(INFO) << "Create a new database";
  const string new_db_name = kTestDatabase;
  ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE DATABASE $0", new_db_name));
  auto new_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, new_db_name));
  // The test session connects to a database from node at index 1.
  pg_ts = cluster_->tablet_server(1);
  auto conn_test = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(new_db_name)));

  // The yugabyte session drops the database from node at index 0.
  ASSERT_OK(conn_yugabyte.ExecuteFormat("DROP DATABASE $0", new_db_name));
  WaitForCatalogVersionToPropagate();

  // Execute any query in the test session that requires metadata lookup
  // should fail with error indicating that the database has been dropped.
  auto status = ResultToStatus(conn_test.Fetch("SELECT * FROM non_exist_table"));
  LOG(INFO) << "status: " << status;
  ASSERT_TRUE(status.IsNetworkError());
  ASSERT_STR_CONTAINS(status.ToString(), Format("base $0", new_db_oid));
  ASSERT_STR_CONTAINS(status.ToString(), "base might have been dropped");
}

// Test running a SQL script that makes the table pg_yb_catalog_version
// one row per database when per-database catalog version is prematurely
// turned on. This should not cause any master CHECK failure.
TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(DBCatalogVersionPrematureOn)) {
  // Manually switch back to non-per-db catalog version mode.
  RestartClusterWithoutDBCatalogVersionMode();
  auto conn = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(PrepareDBCatalogVersion(&conn));
  LOG(INFO) << "Preparing pg_yb_catalog_version to have a single row for template1";
  ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed=1"));
  ASSERT_RESULT(conn.Fetch("SELECT yb_fix_catalog_version_table(false)"));
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn, kYugabyteDatabase));

  // Manually switch back to per-db catalog version mode, but this step is
  // done prematurely before running the following script to prepare the
  // table pg_yb_catalog_version to have one row per database.
  RestartClusterWithDBCatalogVersionMode();

  // Trying to connect to kYugabyteDatabase before it has a row in the table
  // pg_yb_catalog_version should not cause master CHECK failure.
  auto status = ResultToStatus(ConnectToDB(kYugabyteDatabase));
  LOG(INFO) << "status: " << status;
  ASSERT_TRUE(status.IsNetworkError());
  ASSERT_STR_CONTAINS(status.ToString(),
                      Format("catalog version for database $0 was not found", yugabyte_db_oid));

  // Now prepare pg_yb_catalog_version to have one row per database.
  // The pg_yb_catalog_version has only one row for template1. So connect to
  // template1 to run yb_fix_catalog_version_table(true).
  conn = ASSERT_RESULT(ConnectToDB("template1"));
  // We should not see any master CHECK failure.
  ASSERT_OK(PrepareDBCatalogVersion(&conn));
  size_t num_initial_databases = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn)).size();
  LOG(INFO) << "num_initial_databases: " << num_initial_databases;
  ASSERT_GT(num_initial_databases, 1);
  // We should not see master CHECK failure if we try to get duplicate
  // db_oid into the same request.
  status = conn.Execute(
      "INSERT INTO pg_catalog.pg_yb_catalog_version VALUES "
      "(16384, 1, 1), (16384, 2, 2)");
  LOG(INFO) << "status: " << status;
  ASSERT_TRUE(status.IsNetworkError());
  ASSERT_STR_CONTAINS(status.ToString(),
                      "duplicate key value violates unique constraint");
}

// Test various global DDL statements in a single-tenant cluster setting.
TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(DBCatalogVersionGlobalDDL)) {
  TestDBCatalogVersionGlobalDDLHelper(false /* disable_global_ddl */);
}

// Test disabling global DDL statements in a multi-tenant cluster setting.
TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(DBCatalogVersionDisableGlobalDDL)) {
  TestDBCatalogVersionGlobalDDLHelper(true /* disable_global_ddl */);
}

// Test system procedure yb_increment_all_db_catalog_versions works as expected.
TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(IncrementAllDBCatalogVersions)) {
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte));
  RestartClusterWithDBCatalogVersionMode();
  conn_yugabyte = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kYugabyteDatabase)));

  // Verify the initial catalog version map.
  constexpr CatalogVersion kFirstCatalogVersion{1, 1};
  auto expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kFirstCatalogVersion));
  }
  ASSERT_OK(CheckMatch(expected_versions, ASSERT_RESULT(GetShmCatalogVersionMap())));

  // Get ready to execute yb_increment_all_db_catalog_versions.
  ASSERT_OK(conn_yugabyte.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed=1"));

  constexpr CatalogVersion kSecondCatalogVersion{2, 1};
  ASSERT_RESULT(conn_yugabyte.Fetch("SELECT yb_increment_all_db_catalog_versions(false)"));
  WaitForCatalogVersionToPropagate();
  expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kSecondCatalogVersion));
  }
  ASSERT_OK(CheckMatch(expected_versions, ASSERT_RESULT(GetShmCatalogVersionMap())));

  constexpr CatalogVersion kThirdCatalogVersion{3, 3};
  ASSERT_RESULT(conn_yugabyte.Fetch("SELECT yb_increment_all_db_catalog_versions(true)"));
  WaitForCatalogVersionToPropagate();
  expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kThirdCatalogVersion));
  }
  ASSERT_OK(CheckMatch(expected_versions, ASSERT_RESULT(GetShmCatalogVersionMap())));
}

// Test yb_fix_catalog_version_table, that will sync up pg_yb_catalog_version
// with pg_database according to 'per_database_mode' argument.
TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(FixCatalogVersionTable)) {
  RestartClusterWithDBCatalogVersionMode();
  auto conn_template1 = ASSERT_RESULT(ConnectToDB("template1"));
  // Prepare the table pg_yb_catalog_version for per-db catalog version mode.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_template1, true /* per_database_mode */));
  // Verify pg_database and pg_yb_catalog_version are in sync.
  ASSERT_TRUE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_template1, false /* single_row */)));

  const auto max_oid = ASSERT_RESULT(
      conn_template1.FetchValue<PGOid>("SELECT max(oid) FROM pg_database"));
  // Delete the row with max_oid from pg_catalog.pg_yb_catalog_version.
  ASSERT_OK(conn_template1.ExecuteFormat(
      "DELETE FROM pg_catalog.pg_yb_catalog_version WHERE db_oid = $0", max_oid));
  // Add an extra row to pg_catalog.pg_yb_catalog_version.
  ASSERT_OK(conn_template1.ExecuteFormat(
      "INSERT INTO pg_catalog.pg_yb_catalog_version VALUES ($0, 1, 1)", max_oid + 1));
  // Verify pg_database and pg_yb_catalog_version are not in sync.
  ASSERT_FALSE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_template1, false /* single_row */)));

  // Prepare the table pg_yb_catalog_version for per-db catalog version mode, which
  // automatically sync up pg_yb_catalog_version with pg_database.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_template1, true /* per_database_mode */));
  // Verify pg_database and pg_yb_catalog_version are in sync.
  ASSERT_TRUE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_template1, false /* single_row */)));

  // Connect to database "yugabyte".
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB("yugabyte"));
  // Prepare the table pg_yb_catalog_version for global catalog version mode.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, false /* per_database_mode */));
  // Verify there is one row in pg_yb_catalog_version.
  ASSERT_TRUE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_yugabyte, true /* single_row */)));

  // At this time, the cluster is still in per-db catalog version mode, but the table
  // pg_yb_catalog_version has only one row for template1 and is out of sync with
  // pg_database. Even though the row for "yugabyte" is gone, we can still execute
  // queries on this connection. Try some simple queries to verify they still work.
  ASSERT_OK(conn_yugabyte.Execute("CREATE TABLE test_table(id int)"));
  ASSERT_OK(conn_yugabyte.Execute("INSERT INTO test_table VALUES(1), (2), (3)"));
  const auto max_id = ASSERT_RESULT(
      conn_yugabyte.FetchValue<int32_t>("SELECT max(id) FROM test_table"));
  ASSERT_EQ(max_id, 3);
  // We cannot make a new connection to database "yugabyte".
  auto status = ResultToStatus(ConnectToDB("yugabyte"));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "Database might have been dropped by another user");

  // We can only make a new connection to database "template1" because now it is the only
  // database that has a row in pg_yb_catalog_version table.
  conn_template1 = ASSERT_RESULT(ConnectToDB("template1"));
  // Sync up pg_yb_catalog_version with pg_database.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_template1, true /* per_database_mode */));
  // Verify pg_database and pg_yb_catalog_version are in sync.
  ASSERT_TRUE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_template1, false /* single_row */)));
  // Now we can connect to "yugabyte" again.
  conn_yugabyte = ASSERT_RESULT(ConnectToDB("yugabyte"));
}

TEST_F(PgCatalogVersionTest, YB_DISABLE_TEST_IN_TSAN(NonBreakingDDLMode)) {
  const string kDatabaseName = "yugabyte";

  auto conn1 = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  auto conn2 = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn1.Execute("CREATE TABLE t1(a int)"));
  ASSERT_OK(conn1.Execute("CREATE TABLE t2(a int)"));
  ASSERT_OK(conn1.Execute("BEGIN"));
  auto res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_EQ(0, PQntuples(res.get()));
  ASSERT_OK(conn2.Execute("REVOKE ALL ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  // REVOKE is a breaking catalog change, the running transaction on conn1 is aborted.
  auto result = conn1.Fetch("SELECT * FROM t1");
  auto status = ResultToStatus(result);
  ASSERT_TRUE(status.IsNetworkError()) << status;
  const string msg = "catalog snapshot used for this transaction has been invalidated";
  ASSERT_STR_CONTAINS(status.ToString(), msg);
  ASSERT_OK(conn1.Execute("ABORT"));

  // Let's start over, but this time use yb_make_next_ddl_statement_nonbreaking to suppress the
  // breaking catalog change and the SELECT command on conn1 runs successfully.
  ASSERT_OK(conn1.Execute("BEGIN"));
  res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_EQ(0, PQntuples(res.get()));

  // Do grant first otherwise the next two REVOKE statements will be no-ops.
  ASSERT_OK(conn2.Execute("GRANT ALL ON t2 TO public"));

  ASSERT_OK(conn2.Execute("SET yb_make_next_ddl_statement_nonbreaking TO TRUE"));
  ASSERT_OK(conn2.Execute("REVOKE SELECT ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  res = ASSERT_RESULT(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_EQ(0, PQntuples(res.get()));

  // Verify that the session variable yb_make_next_ddl_statement_nonbreaking auto-resets to false.
  // As a result, the running transaction on conn1 is aborted.
  ASSERT_OK(conn2.Execute("REVOKE INSERT ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  result = conn1.Fetch("SELECT * FROM t1");
  status = ResultToStatus(result);
  LOG(INFO) << "status: " << status;
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), msg);
  ASSERT_OK(conn1.Execute("ABORT"));
}

} // namespace pgwrapper
} // namespace yb
