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
#include "yb/gutil/strings/util.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/tserver_shared_mem.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/string_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/ysql_binary_runner.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

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

  static constexpr auto* kYugabyteDatabase = "yugabyte";
  static constexpr auto* kTestDatabase = "test_db";

  using MasterCatalogVersionMap = std::unordered_map<Oid, CatalogVersion>;
  using ShmCatalogVersionMap = std::unordered_map<Oid, Version>;

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    LibPqTestBase::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        "--allowed_preview_flags_csv=ysql_yb_enable_invalidation_messages");
    options->extra_tserver_flags.push_back(
        "--allowed_preview_flags_csv=ysql_yb_enable_invalidation_messages");
  }

  Result<int64_t> GetCatalogVersion(PGConn* conn) {
    const auto db_oid = VERIFY_RESULT(conn->FetchRow<PGOid>(Format(
        "SELECT oid FROM pg_database WHERE datname = '$0'", PQdb(conn->get()))));
    return conn->FetchRow<PGUint64>(
        Format("SELECT current_version FROM pg_yb_catalog_version where db_oid = $0", db_oid));
  }

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
    RETURN_NOT_OK(SetNonDDLTxnAllowedForSysTableWrite(*conn, true));
    VERIFY_RESULT(conn->FetchFormat(
        "SELECT yb_fix_catalog_version_table($0)", per_database_mode ? "true" : "false"));
    return SetNonDDLTxnAllowedForSysTableWrite(*conn, false);
  }

  void RestartClusterSetDBCatalogVersionMode(
      bool enabled, const std::vector<string>& extra_tserver_flags = {}) {
    LOG(INFO) << "Restart the cluster and turn "
              << (enabled ? "on" : "off") << " --ysql_enable_db_catalog_version_mode";
    cluster_->Shutdown();
    const string db_catalog_version_gflag =
      Format("--ysql_enable_db_catalog_version_mode=$0", enabled ? "true" : "false");
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

  void RestartClusterWithInvalMessageMode(
      bool mode,
      const std::vector<string>& extra_tserver_flags = {}) {
    const auto mode_str = mode ? "true" : "false";
    LOG(INFO) << "Restart the cluster with --ysql_yb_enable_invalidation_messages=" << mode_str;
    cluster_->Shutdown();
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      cluster_->master(i)->mutable_flags()->push_back(
          Format("--ysql_yb_enable_invalidation_messages=$0", mode_str));
    }
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      cluster_->tablet_server(i)->mutable_flags()->push_back(
          Format("--ysql_yb_enable_invalidation_messages=$0", mode_str));
      for (const auto& flag : extra_tserver_flags) {
        cluster_->tablet_server(i)->mutable_flags()->push_back(flag);
      }
    }
    ASSERT_OK(cluster_->Restart());
  }
  void RestartClusterWithInvalMessageEnabled(
      const std::vector<string>& extra_tserver_flags = {}) {
    RestartClusterWithInvalMessageMode(true /* mode */, extra_tserver_flags);
  }
  void RestartClusterWithInvalMessageDisabled(
      const std::vector<string>& extra_tserver_flags = {}) {
    RestartClusterWithInvalMessageMode(false /* mode */, extra_tserver_flags);
  }

  // Return a MasterCatalogVersionMap by making a query of the pg_yb_catalog_version table.
  static Result<MasterCatalogVersionMap> GetMasterCatalogVersionMap(PGConn* conn) {
    const auto rows = VERIFY_RESULT((
        conn->FetchRows<pgwrapper::PGOid, pgwrapper::PGUint64, pgwrapper::PGUint64>(
          "SELECT * FROM pg_yb_catalog_version")));
    SCHECK(!rows.empty(), IllegalState, "empty version map");
    MasterCatalogVersionMap result;
    std::string output;
    for (const auto& [db_oid, current_version, last_breaking_version] : rows) {
      result.emplace(db_oid, CatalogVersion{current_version, last_breaking_version});
      if (!output.empty()) {
        output += ", ";
      }
      output += Format("($0, $1, $2)", db_oid, current_version, last_breaking_version);
    }
    LOG(INFO) << "Catalog version map: " << output;
    return result;
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
      auto uuid = cluster_->tablet_server(0)->instance_id().permanent_uuid();
      tserver::SharedMemoryManager shared_mem_manager;
      RETURN_NOT_OK(shared_mem_manager.InitializePgBackend(uuid));

      auto tserver_shared_data = shared_mem_manager.SharedData();

      size_t initialized_slots_count = 0;
      for (size_t i = 0; i < tserver::TServerSharedData::kMaxNumDbCatalogVersions; ++i) {
        if (tserver_shared_data->ysql_db_catalog_version(i)) {
          ++initialized_slots_count;
        }
      }

      // Get the tserver catalog version info from tserver at 'tablet_index'.
      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      auto proxy = cluster_->GetProxy<tserver::TabletServerServiceProxy>(
          cluster_->tablet_server(tablet_index));

      tserver::GetTserverCatalogVersionInfoRequestPB catalog_version_req;
      tserver::GetTserverCatalogVersionInfoResponsePB catalog_version_resp;
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
        const auto current_version = tserver_shared_data->ysql_db_catalog_version(shm_index);
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
    auto values = VERIFY_RESULT(conn->FetchRows<PGOid>("SELECT db_oid FROM pg_yb_catalog_version"));
    std::unordered_set<PgOid> pg_yb_catalog_version_db_oids;
    for (const auto& oid : values) {
      pg_yb_catalog_version_db_oids.insert(oid);
    }
    if (single_row) {
      return pg_yb_catalog_version_db_oids.size() == 1 &&
             *pg_yb_catalog_version_db_oids.begin() == 1;
    }
    values = VERIFY_RESULT(conn->FetchRows<PGOid>("SELECT oid FROM pg_database"));
    std::unordered_set<PgOid> pg_database_oids;
    for (const auto& oid : values) {
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
    constexpr auto* kTestUser1 = "test_user1";
    constexpr auto* kTestUser2 = "test_user2";
    constexpr auto* kTestGroup = "test_group";
    constexpr auto* kTestTablespace = "test_tsp";
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
    {
      // In PG15, SCHEMA public by default is more restrictive, grant CREATE privilege
      // to all users to allow this test to run successfully in both PG11 and PG15.
      auto conn_yugabyte_on_test = ASSERT_RESULT(ConnectToDB(kTestDatabase));
      ASSERT_OK(conn_yugabyte_on_test.Execute("GRANT CREATE ON SCHEMA public TO public"));
    }
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
  void InvalMessageLocalCatalogVersionHelper() {
    // Create a number of databases.
    auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
    const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
    const int num_databases = 10;
    for (int i = 0; i < num_databases; ++i) {
      ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE DATABASE test_db$0", i));
    }
    // Create a number of connections.
    const int num_connections = num_databases * (num_databases + 1) / 2;
    std::vector<int> indexes;
    for (int i = 1; i <= num_databases; ++i) {
      for (int j = 0; j < i; ++j) {
        indexes.push_back(i - 1);
      }
    }
    LOG(INFO) << "indexes: " << yb::ToString(indexes);
    CHECK_EQ(num_connections, static_cast<int>(indexes.size()));
    std::vector<PGConn> conns;
    // indexes:
    // 0,              connect to test_db0
    // 1, 1,           connect to test_db1
    // 2, 2, 2,        connect to test_db2
    // 3, 3, 3, 3,     connect to test_db3
    // 4, 4, 4, 4, 4   connect to test_db4
    for (size_t i = 0; i < indexes.size(); ++i) {
      std::string dbname = Format("test_db$0", indexes[i]);
      PGConn conn = ASSERT_RESULT(ConnectToDB(dbname));
      conns.emplace_back(std::move(conn));
    }
    // Use the standalone connection conn_yugabyte to cause global impact catalog version bump,
    // so that in the end each connection will have a different local catalog version.
    for (size_t i = 0; i < indexes.size(); ++i) {
      auto result = ASSERT_RESULT(conns[i].FetchAllAsString("SELECT 1"));
      ASSERT_EQ(result, "1");
      // i == 0 needs to be NOSUPERUSER or else it is a noop that does not increment
      // the catalog version.
      ASSERT_OK(BumpCatalogVersion(1, &conn_yugabyte, i % 2 == 0 ? "NOSUPERUSER" : "SUPERUSER"));
      WaitForCatalogVersionToPropagate();
    }
    // Use datid != 1 to exclude template1, which is the database that the tserver
    // background task periodically run a query to find out local catalog versions
    // of all PG backends. Otherwise, it there is a coincident that the background
    // task happens to be running and we will see an extra result of (1, 56).
    const std::string query = "SELECT datid, local_catalog_version FROM "
                              "yb_pg_stat_get_backend_local_catalog_version(NULL) "
                              "WHERE datid != 1 ORDER BY datid ASC, local_catalog_version ASC";
    auto result = ASSERT_RESULT((conn_yugabyte.FetchAllAsString(query)));
    const string expected =
        Format("$0, 56; "
               "16384, 1; 16385, 2; 16385, 3; 16386, 4; 16386, 5; 16386, 6; "
               "16387, 7; 16387, 8; 16387, 9; 16387, 10; "
               "16388, 11; 16388, 12; 16388, 13; 16388, 14; 16388, 15; "
               "16389, 16; 16389, 17; 16389, 18; 16389, 19; 16389, 20; 16389, 21; "
               "16390, 22; 16390, 23; 16390, 24; 16390, 25; 16390, 26; 16390, 27; 16390, 28; "
               "16391, 29; 16391, 30; 16391, 31; 16391, 32; 16391, 33; 16391, 34; 16391, 35; "
               "16391, 36; 16392, 37; 16392, 38; 16392, 39; 16392, 40; 16392, 41; 16392, 42; "
               "16392, 43; 16392, 44; 16392, 45; 16393, 46; 16393, 47; 16393, 48; 16393, 49; "
               "16393, 50; 16393, 51; 16393, 52; 16393, 53; 16393, 54; 16393, 55", yugabyte_db_oid);
    ASSERT_EQ(result, expected);
  }

  static size_t CountRelCacheInitFiles(const string& dirpath) {
    auto CloseDir = [](DIR* d) { closedir(d); };
    std::unique_ptr<DIR, decltype(CloseDir)> d(opendir(dirpath.c_str()),
                                               CloseDir);
    CHECK(d);
    struct dirent* entry;
    unsigned int count = 0;
    while ((entry = readdir(d.get())) != nullptr) {
      if (strstr(entry->d_name, "pg_internal.init")) {
        LOG(INFO) << "found rel cache init file " << dirpath << "/" << entry->d_name;
        count++;
      }
    }
    return count;
  }
  void RemoveRelCacheInitFilesHelper(bool per_database_mode) {
    // Prepare an existing cluster that is the expected mode.
    auto conn_yugabyte = ASSERT_RESULT(Connect());
    ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, per_database_mode));
    RestartClusterSetDBCatalogVersionMode(per_database_mode);
    conn_yugabyte = ASSERT_RESULT(Connect());
    // Under per-database catalog version mode, there is one shared rel
    // cache init file for each database. Test this by making a second
    // connection to the template1 database.
    auto conn_template1 = ASSERT_RESULT(ConnectToDB("template1"));
    auto data_root = cluster_->data_root();
    auto pg_data_root = JoinPathSegments(data_root, "ts-1", "pg_data");
    auto pg_data_global = JoinPathSegments(pg_data_root, "global");
    ASSERT_EQ(CountRelCacheInitFiles(pg_data_global), per_database_mode ? 2 : 1);
    ASSERT_EQ(CountRelCacheInitFiles(pg_data_root), 2);

    // Restart the cluster. The rel cache init files should be removed
    // during postmaster startup.
    cluster_->Shutdown();
    ASSERT_OK(cluster_->Restart());

    ASSERT_EQ(CountRelCacheInitFiles(pg_data_global), 0);
    ASSERT_EQ(CountRelCacheInitFiles(pg_data_root), 0);
  }

  void VerifyCatCacheRefreshMetricsHelper(
      int num_full_refreshes, int num_delta_refreshes) {
    auto json_metrics = GetJsonMetrics();

    int count = 0;
    for (const auto& metric : json_metrics) {
      // Should see one full refresh.
      if (metric.name.find("CatCacheRefresh") != std::string::npos) {
        ++count;
        ASSERT_EQ(metric.value, num_full_refreshes);
      }
      // Should not see any incremental refresh.
      if (metric.name.find("CatCacheDeltaRefresh") != std::string::npos) {
        ++count;
        ASSERT_EQ(metric.value, num_delta_refreshes);
      }
      if (count == 2) {
        break;
      }
    }
    ASSERT_EQ(count, 2);
  }

  // This function is extracted and adapted from ysql_upgrade.cc.
  std::string ReadMigrationFile(const string& migration_file) {
    const char* kStaticDataParentDir = "share";
    const char* kMigrationsDir = "ysql_migrations";
    const std::string search_for_dir = JoinPathSegments(kStaticDataParentDir, kMigrationsDir);
    const std::string root_dir       = env_util::GetRootDir(search_for_dir);
    CHECK(!root_dir.empty());
    const std::string migrations_dir =
      JoinPathSegments(root_dir, kStaticDataParentDir, kMigrationsDir);
    faststring migration_content;
    CHECK_OK(ReadFileToString(Env::Default(),
                              JoinPathSegments(migrations_dir, migration_file),
                              &migration_content));
    return migration_content.ToString();
  }
};

TEST_F(PgCatalogVersionTest, DBCatalogVersion) {
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
  // Under --ysql_enable_db_catalog_version_mode=true, only the row for 'new_db_oid' is updated.
  expected_versions[new_db_oid] = {2, 1};
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte))));
  ASSERT_OK(CheckMatch(expected_versions,
                       ASSERT_RESULT(GetShmCatalogVersionMap())));

  LOG(INFO) << "Execute a DDL statement that causes a breaking catalog change";
  ASSERT_OK(conn_test.Execute("REVOKE ALL ON SCHEMA public FROM public"));

  WaitForCatalogVersionToPropagate();
  // Under --ysql_enable_db_catalog_version_mode=true, only the row for 'new_db_oid' is updated.
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
  ASSERT_TRUE(status.IsNetworkError()) << status;
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
  ASSERT_TRUE(status.IsNetworkError()) << status;
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
TEST_F(PgCatalogVersionTest, DBCatalogVersionDropDB) {
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
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), Format("base $0", new_db_oid));
  ASSERT_STR_CONTAINS(status.ToString(), "base might have been dropped");
}

// Test running a SQL script that makes the table pg_yb_catalog_version
// one row per database when per-database catalog version is prematurely
// turned on. This should not cause any master CHECK failure.
TEST_F(PgCatalogVersionTest, DBCatalogVersionPrematureOn) {
  // Manually switch back to non-per-db catalog version mode.
  RestartClusterWithoutDBCatalogVersionMode();
  auto conn = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(PrepareDBCatalogVersion(&conn));
  ASSERT_OK(PrepareDBCatalogVersion(&conn, false));

  // Manually switch back to per-db catalog version mode, but this step is
  // done prematurely before running the following call to PrepareDBCatalogVersion
  // to prepare the table pg_yb_catalog_version to have one row per database.
  RestartClusterWithDBCatalogVersionMode();

  // Trying to connect to kYugabyteDatabase before it has a row in the table
  // pg_yb_catalog_version should not cause per-db catalog version mode to
  // be enabled because the PG backend will wait for pg_yb_catalog_version
  // to get upgraded to have one row per database.
  conn = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));

  // Now prepare pg_yb_catalog_version to have one row per database.
  ASSERT_OK(PrepareDBCatalogVersion(&conn));
  size_t num_initial_databases = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn)).size();
  LOG(INFO) << "num_initial_databases: " << num_initial_databases;
  ASSERT_GT(num_initial_databases, 1);
  // We should not see master CHECK failure if we try to get duplicate
  // db_oid into the same request.
  ASSERT_OK(SetNonDDLTxnAllowedForSysTableWrite(conn, true));
  auto status = conn.Execute(
      "INSERT INTO pg_catalog.pg_yb_catalog_version VALUES "
      "(16384, 1, 1), (16384, 2, 2)");
  ASSERT_OK(SetNonDDLTxnAllowedForSysTableWrite(conn, false));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(),
                      "duplicate key value violates unique constraint");
}

// Test various global DDL statements in a single-tenant cluster setting.
TEST_F(PgCatalogVersionTest, DBCatalogVersionGlobalDDL) {
  TestDBCatalogVersionGlobalDDLHelper(false /* disable_global_ddl */);
}

// Test disabling global DDL statements in a multi-tenant cluster setting.
TEST_F(PgCatalogVersionTest, DBCatalogVersionDisableGlobalDDL) {
  TestDBCatalogVersionGlobalDDLHelper(true /* disable_global_ddl */);
}

// Test system procedure yb_increment_all_db_catalog_versions works as expected.
TEST_F(PgCatalogVersionTest, IncrementAllDBCatalogVersions) {
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

  constexpr CatalogVersion kSecondCatalogVersion{2, 1};
  ASSERT_OK(IncrementAllDBCatalogVersions(conn_yugabyte, IsBreakingCatalogVersionChange::kFalse));
  WaitForCatalogVersionToPropagate();
  expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kSecondCatalogVersion));
  }
  ASSERT_OK(CheckMatch(expected_versions, ASSERT_RESULT(GetShmCatalogVersionMap())));

  constexpr CatalogVersion kThirdCatalogVersion{3, 3};
  ASSERT_OK(IncrementAllDBCatalogVersions(conn_yugabyte));
  WaitForCatalogVersionToPropagate();
  expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kThirdCatalogVersion));
  }
  ASSERT_OK(CheckMatch(expected_versions, ASSERT_RESULT(GetShmCatalogVersionMap())));

  // Ensure that PUBLICATION will not cause yb_increment_all_db_catalog_versions
  // to fail.
  ASSERT_OK(conn_yugabyte.Execute("SET yb_enable_replication_commands = true"));
  ASSERT_OK(conn_yugabyte.Execute("CREATE PUBLICATION testpub_foralltables FOR ALL TABLES"));
  ASSERT_OK(IncrementAllDBCatalogVersions(conn_yugabyte));

  // Ensure that in global catalog version mode, by turning on
  // yb_non_ddl_txn_for_sys_tables_allowed, we can perform both update and
  // delete on pg_yb_catalog_version table.
  RestartClusterWithoutDBCatalogVersionMode();
  conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));

  // This involves deleting all rows except for template1 from pg_yb_catalog_version.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, false));
  // Update the row for template1 to increment catalog version.
  ASSERT_OK(IncrementAllDBCatalogVersions(conn_yugabyte, IsBreakingCatalogVersionChange::kFalse));
}

// Test yb_fix_catalog_version_table, that will sync up pg_yb_catalog_version
// with pg_database according to 'per_database_mode' argument.
TEST_F(PgCatalogVersionTest, FixCatalogVersionTable) {
  RestartClusterWithDBCatalogVersionMode();
  auto conn_template1 = ASSERT_RESULT(ConnectToDB("template1"));
  // Prepare the table pg_yb_catalog_version for per-db catalog version mode.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_template1, true /* per_database_mode */));
  // Verify pg_database and pg_yb_catalog_version are in sync.
  ASSERT_TRUE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_template1, false /* single_row */)));

  const auto max_oid = ASSERT_RESULT(
      conn_template1.FetchRow<PGOid>("SELECT max(oid) FROM pg_database"));
  // Delete the row with max_oid from pg_catalog.pg_yb_catalog_version.
  ASSERT_OK(SetNonDDLTxnAllowedForSysTableWrite(conn_template1, true));
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

  // Wait for the pg_yb_catalog_version to propagate to tserver so the next
  // connection to "yugabyte" is in per-database catalog version mode.
  WaitForCatalogVersionToPropagate();

  // Connect to database "yugabyte".
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB("yugabyte"));
  // Prepare the table pg_yb_catalog_version for global catalog version mode.
  // Note that this is not a supported scenario where the table pg_yb_catalog_version
  // shrinks while the gflag --ysql_enable_db_catalog_version_mode is still on.
  // The correct order is to turn off the gflag first and then shrink the table.
  // Nevertheless we test that this order violation will not cause unexpected
  // yb-master/yb-tserver crashes and we can go back to per-database mode by
  // re-syncing the table back to one row per database.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, false /* per_database_mode */));
  // Verify there is one row in pg_yb_catalog_version.
  ASSERT_TRUE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_yugabyte, true /* single_row */)));

  // Do not force early serialization for DDLs since the pg_yb_catalog_version table is in global
  // catalog version mode and early serialization requires taking a lock on the per-db catalog
  // version row.
  ASSERT_OK(conn_yugabyte.Execute("SET yb_force_early_ddl_serialization=false"));

  // At this time, an existing connection is still in per-db catalog version mode
  // but the table pg_yb_catalog_version has only one row for template1 and is out
  // of sync with pg_database. Note that once a connection is in per-db catalog
  // version mode, this mode persists till the end of the connection. Even though
  // the row for "yugabyte" is gone, we can still execute queries on this connection.
  // Try some simple queries to verify they still work.
  ASSERT_OK(conn_yugabyte.Execute("CREATE TABLE test_table(id int)"));
  ASSERT_OK(conn_yugabyte.Execute("INSERT INTO test_table VALUES(1), (2), (3)"));
  const auto max_id = ASSERT_RESULT(
      conn_yugabyte.FetchRow<int32_t>("SELECT max(id) FROM test_table"));
  ASSERT_EQ(max_id, 3);
  constexpr CatalogVersion kCurrentCatalogVersion{1, 1};
  auto versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  // There is only one row in the table pg_yb_catalog_version now.
  CHECK_EQ(versions.size(), 1);
  ASSERT_OK(CheckMatch(versions.begin()->second, kCurrentCatalogVersion));
  // A global-impact DDL statement that increments catalog version still works.
  ASSERT_OK(conn_yugabyte.Execute("ALTER ROLE yugabyte NOSUPERUSER"));
  constexpr CatalogVersion kNewCatalogVersion{2, 2};
  versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  CHECK_EQ(versions.size(), 1);
  ASSERT_OK(CheckMatch(versions.begin()->second, kNewCatalogVersion));

  ASSERT_OK(conn_yugabyte.Execute("ALTER TABLE test_table ADD COLUMN c2 INT"));

  // The non-global-impact DDL statement does not have an effect on the
  // table pg_yb_catalog_version when it tries to update the row of yugabyte
  // because that row no longer exists. There is no user visible effect.
  versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  CHECK_EQ(versions.size(), 1);
  ASSERT_OK(CheckMatch(versions.begin()->second, kNewCatalogVersion));

  // Once a tserver enters per-database catalog version mode it remains so.
  // It is an error to change pg_yb_catalog_version back to global catalog
  // version mode when --ysql_enable_db_catalog_version_mode=true.
  // Verify that we can not make a new connection to database "yugabyte"
  // in this error state.
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
  auto status = ResultToStatus(ConnectToDB("yugabyte"));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(),
                      Format("catalog version for database $0 was not found", yugabyte_db_oid));
  ASSERT_STR_CONTAINS(status.ToString(), "Database might have been dropped by another user");

  // We can only make a new connection to database "template1" because now it
  // is the only database that has a row in pg_yb_catalog_version table.
  conn_template1 = ASSERT_RESULT(ConnectToDB("template1"));

  // Sync up pg_yb_catalog_version with pg_database.
  ASSERT_OK(PrepareDBCatalogVersion(&conn_template1, true /* per_database_mode */));
  // Verify pg_database and pg_yb_catalog_version are in sync.
  ASSERT_TRUE(ASSERT_RESULT(
      VerifyCatalogVersionTableDbOids(&conn_template1, false /* single_row */)));
  // Verify that we can connect to "yugabyte" and "template1".
  ASSERT_RESULT(ConnectToDB("yugabyte"));
  ASSERT_RESULT(ConnectToDB("template1"));
}

// This test exercises the wrap around logic in tserver shared memory free
// slot allocation algorithm for a newly created database.
TEST_F(PgCatalogVersionTest, RecycleManyDatabases) {
  RestartClusterWithDBCatalogVersionMode();
  auto conn = ASSERT_RESULT(ConnectToDB("template1"));
  const auto initial_count = ASSERT_RESULT(conn.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  PgOid db_oid = kPgFirstNormalObjectId;
  // Pick a number so that we can trigger wrap around in about 10 passes.
  constexpr int kNumRows = std::max(kYBCMaxNumDbCatalogVersions / 10, 1);
  // Run 11 passes to ensure we can trigger wrap around.
  constexpr int kNumPasses = 11;
  for (int pass = 0; pass < kNumPasses; ++pass) {
    // Each pass we simulate creating a batch of databases by inserting
    // that many rows into pg_yb_catalog_version, then deleting them.
    // The last pass exercises the wrap around logic.
    std::ostringstream ss;
    ss << "INSERT INTO pg_yb_catalog_version VALUES";
    for (int i = 0; i < kNumRows; ++i) {
      ss << Format(i == 0 ? "($0, 1, 1)" : ", ($0, 1, 1)", db_oid++);
    }
    LOG(INFO) << "Inserting " << kNumRows << " rows";
    ASSERT_OK(SetNonDDLTxnAllowedForSysTableWrite(conn, true));
    ASSERT_OK(conn.Execute(ss.str()));
    ASSERT_OK(SetNonDDLTxnAllowedForSysTableWrite(conn, false));
    WaitForCatalogVersionToPropagate();
    auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>(
        "SELECT COUNT(*) FROM pg_yb_catalog_version"));
    CHECK_EQ(count, kNumRows + initial_count);
    LOG(INFO) << "Deleting the newly inserted " << kNumRows << " rows";
    ASSERT_OK(SetNonDDLTxnAllowedForSysTableWrite(conn, true));
    ASSERT_OK(conn.ExecuteFormat(
        "DELETE FROM pg_yb_catalog_version WHERE db_oid >= $0", kPgFirstNormalObjectId));
    ASSERT_OK(SetNonDDLTxnAllowedForSysTableWrite(conn, false));
    WaitForCatalogVersionToPropagate();
    count = ASSERT_RESULT(conn.FetchRow<PGUint64>(
        "SELECT COUNT(*) FROM pg_yb_catalog_version"));
    CHECK_EQ(count, initial_count);
  }
}

class PgCatalogVersionEnableAuthTest
    : public PgCatalogVersionTest,
      public ::testing::WithParamInterface<std::pair<bool, bool>> {

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgCatalogVersionTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--ysql_enable_auth=true");
  }
};

INSTANTIATE_TEST_CASE_P(PgCatalogVersionEnableAuthTest,
                        PgCatalogVersionEnableAuthTest,
                        ::testing::Values(std::make_pair(true, true),
                                          std::make_pair(true, false),
                                          std::make_pair(false, true),
                                          std::make_pair(false, false)));

// This test verifies that changing a user's password does not affect existing
// this user's existing connection. The user is able to continue in the existing
// connection that was authenticated using the old password. Making a new
// connection using the old password will fail.
TEST_P(PgCatalogVersionEnableAuthTest, ChangeUserPassword) {
  const bool per_database_mode = GetParam().first;
  const bool use_tserver_response_cache = GetParam().second;
  LOG(INFO) << "per_database_mode: " << per_database_mode
            << ", use_tserver_response_cache: " << use_tserver_response_cache;
  string conn_str_prefix = Format("host=$0 port=$1 dbname='$2'",
                                  pg_ts->bind_host(),
                                  pg_ts->ysql_port(),
                                  kYugabyteDatabase);
  auto conn_yugabyte = ASSERT_RESULT(PGConnBuilder({
      .host = pg_ts->bind_host(),
      .port = pg_ts->ysql_port(),
      .dbname = kYugabyteDatabase,
      .user = "yugabyte",
      .password = "yugabyte",
    }).Connect());
  constexpr CatalogVersion kInitialCatalogVersion{1, 1};
  auto expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kInitialCatalogVersion));
  }
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, per_database_mode));
  std::vector<string> extra_tserver_flags =
    { Format("--ysql_enable_read_request_caching=$0", use_tserver_response_cache) };
  RestartClusterSetDBCatalogVersionMode(per_database_mode, extra_tserver_flags);
  conn_yugabyte = ASSERT_RESULT(PGConnBuilder({
      .host = pg_ts->bind_host(),
      .port = pg_ts->ysql_port(),
      .dbname = kYugabyteDatabase,
      .user = "yugabyte",
      .password = "yugabyte",
    }).Connect());
  constexpr auto* kTestUser = "test_user";
  constexpr auto* kOldPassword = "123";
  constexpr auto* kNewPassword = "456";
  ASSERT_OK(conn_yugabyte.ExecuteFormat(
      "CREATE USER $0 PASSWORD '$1'", kTestUser, kOldPassword));
  auto conn_test = ASSERT_RESULT(PGConnBuilder({
      .host = pg_ts->bind_host(),
      .port = pg_ts->ysql_port(),
      .dbname = kYugabyteDatabase,
      .user = kTestUser,
      .password = kOldPassword,
    }).Connect());
  auto res = ASSERT_RESULT(conn_test.Fetch("SELECT * FROM pg_yb_catalog_version"));
  ASSERT_OK(conn_yugabyte.ExecuteFormat(
      "ALTER USER $0 PASSWORD '$1'", kTestUser, kNewPassword));
  WaitForCatalogVersionToPropagate();
  // The existing connection that was authenticated with the old password is
  // not affected by the password change.
  res = ASSERT_RESULT(conn_test.Fetch("SELECT * FROM pg_yb_catalog_version"));
  // Making a new connection using the old password should fail.
  auto status = ResultToStatus(PGConnBuilder({
      .host = pg_ts->bind_host(),
      .port = pg_ts->ysql_port(),
      .dbname = kYugabyteDatabase,
      .user = kTestUser,
      .password = kOldPassword,
    }).Connect());
  ASSERT_STR_CONTAINS(status.ToString(), "password authentication failed");
  // Making a new connection using the new password should succeed. As of
  // 2023-06-29, pg_authid is not cached in tserver response cache during
  // the authentication phase when making a new connection. If we ever read
  // cached pg_authid from tserver response cache during authentication
  // time, making a new connection with the new password would fail because
  // tserver cache would have stored the old password.
  auto conn_test_new = ASSERT_RESULT(PGConnBuilder({
      .host = pg_ts->bind_host(),
      .port = pg_ts->ysql_port(),
      .dbname = kYugabyteDatabase,
      .user = kTestUser,
      .password = kNewPassword,
    }).Connect());
  res = ASSERT_RESULT(conn_test_new.Fetch("SELECT * FROM pg_yb_catalog_version"));
  // Verify that catalog version does not change.
  expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kInitialCatalogVersion));
  }
}

class PgCatalogVersionFailOnConflictTest : public PgCatalogVersionTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    UpdateMiniClusterFailOnConflict(options);
    PgCatalogVersionTest::UpdateMiniClusterOptions(options);
  }
};

// This is a sanity test for manual downgrade from per database catalog version mode to
// global catalog version mode. First the gflag --ysql_enable_db_catalog_version_mode is
// turned off and cluster is restarted. After that, the cluster will be running in
// global catalog version mode despite the fact that pg_yb_catalog_version still has
// multiple rows. At this time, we test that concurrently running DML transactions
// behave well and will not be affected before and after the user performs the second
// fix to make pg_yb_catalog_version to have only one row for template1.
TEST_F_EX(PgCatalogVersionTest, SimulateDowngradeToGlobalMode,
          PgCatalogVersionFailOnConflictTest) {
  // Prepare an existing cluster that is in per-database catalog version mode.
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, true /* per_database_mode */));
  RestartClusterWithDBCatalogVersionMode();
  conn_yugabyte = ASSERT_RESULT(Connect());
  auto initial_count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_GT(initial_count, 1);

  // Now simulate downgrading the cluster to global catalog version mode.
  // We first turn off the gflag, after the cluster restarts, the table
  // pg_yb_catalog_version still has one row per database.
  RestartClusterWithoutDBCatalogVersionMode();
  conn_yugabyte = ASSERT_RESULT(ConnectToDB("yugabyte"));
  initial_count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_GT(initial_count, 1);

  bool downgraded = false;
  // This test assumes that the actual downgrade script runs at most this many seconds.
  constexpr int kMaxDowngradeSec = 5;
  constexpr int kMaxSleepSec = 10;
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &downgraded] {
    // Start a thread to simulate the situation where the user manually runs the YSQL
    // downgrade script to make pg_yb_catalog_version one row per database. Wait for
    // some random time so that it runs concurrently with SerializableColoringHelper().
    const int sleep_sec = RandomUniformInt(1, kMaxSleepSec);
    SleepFor(1s * sleep_sec);
    const string ysql_downgrade_sql =
        R"#(
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
SELECT pg_catalog.yb_fix_catalog_version_table(false);
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO false;
        )#";
    auto conn_ysql_downgrade = ASSERT_RESULT(Connect());
    ASSERT_OK(conn_ysql_downgrade.Execute(ysql_downgrade_sql));
    downgraded = true;
  });
  // There's no strict guarantee of this, but it should be fine practically because
  // of the call to SleepFor above.
  ASSERT_FALSE(downgraded);
  // Let the test run longer than the maximum time we assume that the downgrade can take.
  SerializableColoringHelper(kMaxSleepSec + kMaxDowngradeSec);
  // This can fail if downgrade takes longer than kMaxDowngradeSec but in practice
  // this won't happen.
  ASSERT_TRUE(downgraded);
  const auto current_count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_EQ(current_count, 1);
  thread_holder.Stop();
}

TEST_F_EX(PgCatalogVersionTest, SimulateUpgradeToPerdbMode,
          PgCatalogVersionFailOnConflictTest) {
  // Simulate an existing cluster that is in global catalog version mode
  // by ensuring there is only one row in pg_yb_catalog_version.
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, false /* per_database_mode */));
  // During cluster upgrade, we'll first upgrade the new binaries. Restart the
  // cluster to simulate upgrading the binaries and we assume that in the new
  // binaries the per-database catalog version mode gflag is turned on by default.
  RestartClusterWithDBCatalogVersionMode();

  conn_yugabyte = ASSERT_RESULT(Connect());
  // After we upgrade the binaries, we should still only have one row
  // in pg_yb_catalog_version.
  const auto initial_count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_EQ(initial_count, 1);

  bool upgraded = false;
  // This test assumes that the actual upgrade script runs at most this many seconds.
  constexpr int kMaxUpgradeSec = 5;
  constexpr int kMaxSleepSec = 10;
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &upgraded] {
    // Start a thread to simulate the situation where the YSQL upgrade script
    // runs to upgrade pg_yb_catalog_version one row per database. Wait for some
    // random time so that it runs concurrently with SerializableColoringHelper().
    const int sleep_sec = RandomUniformInt(1, kMaxSleepSec);
    SleepFor(1s * sleep_sec);
    const string ysql_upgrade_sql =
        R"#(
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

DO $$
BEGIN
  -- The pg_yb_catalog_version will be upgraded so that it has one row per database.
  if (SELECT count(db_oid) from pg_catalog.pg_yb_catalog_version) = 1 THEN
    PERFORM pg_catalog.yb_fix_catalog_version_table(true);
  END IF;
END $$;
        )#";
    auto conn_ysql_upgrade = ASSERT_RESULT(Connect());
    ASSERT_OK(conn_ysql_upgrade.Execute(ysql_upgrade_sql));
    upgraded = true;
  });
  // There's no strict guarantee of this, but it should be fine practically because
  // of the call to SleepFor above.
  ASSERT_FALSE(upgraded);
  // Let the test run longer than the maximum time we assume that the upgrade can take.
  SerializableColoringHelper(kMaxSleepSec + kMaxUpgradeSec);
  // This can fail if upgrade takes longer than kMaxUpgradeSec but in practice
  // this won't happen.
  ASSERT_TRUE(upgraded);
  const auto current_count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_GT(current_count, 1);
  thread_holder.Stop();
}

TEST_F(PgCatalogVersionTest, ResetIsGlobalDdlState) {
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(conn_yugabyte.Execute("CREATE TABLE foo(a int)"));
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte));
  RestartClusterWithDBCatalogVersionMode();

  conn_yugabyte = ASSERT_RESULT(EnableCacheEventLog(ConnectToDB(kYugabyteDatabase)));
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
  // Get the initial catalog version map.
  constexpr CatalogVersion kInitialCatalogVersion{1, 1};
  auto expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  ASSERT_TRUE(expected_versions.find(yugabyte_db_oid) != expected_versions.end());
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kInitialCatalogVersion));
  }

  ASSERT_OK(conn_yugabyte.Execute("SET yb_test_fail_next_inc_catalog_version=true"));
  // The following ALTER ROLE is a global impact DDL statement. It will
  // fail due to yb_test_fail_next_inc_catalog_version=true.
  auto status = conn_yugabyte.Execute("ALTER ROLE yugabyte NOSUPERUSER");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "Failed increment catalog version as requested");

  // Verify that the above failed global impact DDL statement does not change
  // any of the catalog versions.
  expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    ASSERT_OK(CheckMatch(entry.second, kInitialCatalogVersion));
  }

  // The following ALTER TABLE is a not a global impact DDL statement, if
  // we had not reset is_global_ddl state in YbDdlTransactionState because of
  // the above injected error, this ALTER TABLE would be incorrectly treated
  // as a global impact DDL statement and caused catalog versions of all
  // the databases to increase.
  ASSERT_OK(conn_yugabyte.Execute("ALTER TABLE foo ADD COLUMN b int"));
  expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  for (const auto& entry : expected_versions) {
    if (entry.first != yugabyte_db_oid) {
      ASSERT_OK(CheckMatch(entry.second, kInitialCatalogVersion));
    } else {
      ASSERT_OK(CheckMatch(entry.second, {2, 1}));
    }
  }
}

TEST_F(PgCatalogVersionTest, InvalidateWholeRelCache) {
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte));
  RestartClusterWithDBCatalogVersionMode();
  conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
  // CREATE PUBLICATION is not a global-impact DDL in PG11, but is a global-impact DDL in PG15.
  ASSERT_OK(conn_yugabyte.Execute("SET yb_enable_replication_commands = true"));
  ASSERT_OK(conn_yugabyte.Execute("CREATE PUBLICATION testpub_foralltables FOR ALL TABLES"));

  // This ALTER PUBLICATION causes invalidation of the whole relcache (including
  // shared relations) in PG. YB inherits this behavior but during the execution
  // of this DDL there wasn't any write to a shared relation that has a syscache.
  // In per-database catalog version mode there is a shared rel init file for
  // each database. Ensure we still detect this DDL as global impact so that
  // all shared rel cache init files can be invalidated.
  ASSERT_OK(conn_yugabyte.Execute(
        R"#(
ALTER PUBLICATION testpub_foralltables SET (publish = 'insert, update, delete, truncate')
        )#"));
  auto expected_versions = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn_yugabyte));
  ASSERT_TRUE(expected_versions.find(yugabyte_db_oid) != expected_versions.end());
  auto version_string = ASSERT_RESULT(GetPGVersionString(&conn_yugabyte));
  LOG(INFO) << "PG version string: " << version_string;
  auto is_pg11 = StringStartsWithOrEquals(version_string, "PostgreSQL 11");
  for (const auto& entry : expected_versions) {
    if (entry.first != yugabyte_db_oid && is_pg11) {
      ASSERT_OK(CheckMatch(entry.second, {2, 2}));
    } else {
      ASSERT_OK(CheckMatch(entry.second, {3, 3}));
    }
  }
}

TEST_F(PgCatalogVersionTest, RemoveRelCacheInitFiles) {
  RemoveRelCacheInitFilesHelper(true /* per_database_mode */);
  RemoveRelCacheInitFilesHelper(false /* per_database_mode */);
}

// This test that YSQL can execute DDL statements when the gflag
// --ysql_enable_db_catalog_version_mode is on but the pg_yb_catalog_version
// table isn't updated to have one row per database.
TEST_F(PgCatalogVersionTest, SimulateTryoutPhaseInUpgrade) {
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, false /* per_database_mode */));
  RestartClusterWithDBCatalogVersionMode();
  conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_yugabyte.Execute("CREATE TABLE t(id INT)"));
  ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE INDEX idx ON t(id)"));
  ASSERT_OK(conn_yugabyte.Execute("ALTER ROLE yugabyte SUPERUSER"));
}

TEST_F(PgCatalogVersionTest, SimulateLaggingPGInUpgradeFinalization) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE USER u1"));
  ASSERT_OK(conn.Execute("CREATE USER u2"));
  ASSERT_OK(conn.Execute("CREATE TABLE t(id INT)"));

  // Ensure we start in non-per-db catalog version mode to prepare
  // the simulation of a cluster upgrade to per-db catalog version mode.
  RestartClusterWithoutDBCatalogVersionMode();
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn, false /* per_database_mode */));

  // Simulate cluster upgrade to a new release with per-db catalog version
  // mode on by default. The new binary is installed first and therefore
  // the gflag --ysql_enable_db_catalog_version_mode is true before the
  // table pg_yb_catalog_version is upgraded to per-db mode.
  RestartClusterWithDBCatalogVersionMode();

  // Make two connections, both to DB yugabyte but one via ts-1 and the
  // other via ts-2.
  pg_ts = cluster_->tablet_server(0);
  auto conn1 = ASSERT_RESULT(Connect());
  pg_ts = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(Connect());

  // Let conn1 be a laggard during finalization phase so it will stay in global
  // catalog version mode until yb_test_stay_in_global_catalog_version_mode
  // is reset.
  ASSERT_OK(conn1.Execute(
      "SET yb_test_stay_in_global_catalog_version_mode TO TRUE"));

  // Start a transaction on conn2.
  ASSERT_OK(conn2.Execute("BEGIN"));
  auto current_count = ASSERT_RESULT(conn2.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_EQ(current_count, 1);

  // Simulate finalization phase where we upgrade pg_yb_catalog_version to
  // perdb catalog version mode.
  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn, true /* per_database_mode */));
  // Wait for the new mode to propagate to all tservers.
  WaitForCatalogVersionToPropagate();

  // Issue a breaking DDL statement to the lagging connection conn1.
  ASSERT_OK(conn1.Execute("REVOKE SELECT ON t FROM u1"));
  WaitForCatalogVersionToPropagate();

  // Ensure the effect of the above DDL is seen by conn2.
  auto status = ResultToStatus(conn2.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  const string msg = "catalog snapshot used for this transaction has been invalidated";
  ASSERT_STR_CONTAINS(status.ToString(), msg);
  ASSERT_OK(conn2.Execute("ROLLBACK"));

  // Now repeat the test in the other direction: DDL is executed from conn2
  // which is now operating in perdb catalog version mode.

  // Start a transaction on conn1.
  ASSERT_OK(conn1.Execute("BEGIN"));
  current_count = ASSERT_RESULT(conn1.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_GT(current_count, 1);

  // Issue a non-global-impact breaking DDL statement to the perdb
  // backend of conn2.
  ASSERT_OK(conn2.Execute("REVOKE SELECT ON t FROM u2"));
  WaitForCatalogVersionToPropagate();

  // The effect of the above DDL is not seen by conn1 which stays in global
  // catalog version mode.
  auto new_count = ASSERT_RESULT(conn1.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_EQ(new_count, current_count);

  LOG(INFO) << "Let the lagging connection change to perdb mode";
  ASSERT_OK(conn1.Execute(
      "SET yb_test_stay_in_global_catalog_version_mode TO FALSE"));

  // After turning off yb_test_stay_in_global_catalog_version_mode the
  // first statement on lagging connection conn1 still won't see the effect
  // of the DDL on conn2. This is because conn1 only changes to perdb mode
  // when YBIsDBCatalogVersionMode() is called, which happens after conn1
  // has sent out its first read RPC for the next statement. As a result
  // the first read RPC still uses the old catalog version in global catalog
  // version mode.
  new_count = ASSERT_RESULT(conn1.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_EQ(new_count, current_count);

  // For the second statement, the effect of the DDL on conn2 is seen by conn1.
  // This shows that the effect of the DDL on perdb connection will not get
  // lost forever on a lagging connection.
  status = ResultToStatus(conn1.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_catalog_version"));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), msg);
}

class PgCatalogVersionMasterLeadershipChange : public PgCatalogVersionTest {
 protected:
  int GetNumMasters() const override { return 3; }
};

TEST_F_EX(PgCatalogVersionTest, ChangeMasterLeadership,
          PgCatalogVersionMasterLeadershipChange) {
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte, true /* per_database_mode */));
  RestartClusterWithDBCatalogVersionMode();
  WaitForCatalogVersionToPropagate();
  conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_yugabyte.Execute("CREATE TABLE t(id INT)"));
  ASSERT_OK(conn_yugabyte.Execute("ALTER TABLE t ADD COLUMN c2 TEXT"));
  LOG(INFO) << "Disable next master leader to set catalog version table in perdb mode";
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "TEST_disable_set_catalog_version_table_in_perdb_mode", "true"));
  auto leader_master_index = CHECK_RESULT(cluster_->GetLeaderMasterIndex());
  LOG(INFO) << "Failing over master leader.";
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
  auto new_leader_master_index = CHECK_RESULT(cluster_->GetLeaderMasterIndex());
  LOG(INFO) << "The new master leader is at " << leader_master_index;
  CHECK_NE(leader_master_index, new_leader_master_index);
  ASSERT_OK(conn_yugabyte.Execute("CREATE INDEX idx ON t(id)"));
}

TEST_F(PgCatalogVersionTest, SqlCrossDBLoadWithDDL) {

  const std::vector<std::vector<string>> ddlLists = {
    {
      "CREATE INDEX idx1 ON $0 (k)",
      "DROP INDEX idx1",
    },
    {
      "CREATE TABLE tempTable1 AS SELECT * FROM $0 limit 1000000",
      "ALTER TABLE tempTable1 RENAME TO tempTable1_new",
      "DROP TABLE tempTable1_new",
    },
    {
      "CREATE MATERIALIZED VIEW mv1 as SELECT k from $0 limit 10000",
      "REFRESH MATERIALIZED VIEW mv1",
      "DROP MATERIALIZED VIEW mv1",
    },
    {
      "ALTER TABLE $0 ADD newColumn1 TEXT DEFAULT 'dummyString'",
      "ALTER TABLE $0 DROP newColumn1",
    },
    {
      "ALTER TABLE $0 ADD newColumn2 TEXT NULL",
      "ALTER TABLE $0 DROP newColumn2",
    },
    {
      "CREATE VIEW view1_$0 AS SELECT k from $0",
      "DROP VIEW view1_$0",
    },
    {
      "ALTER TABLE $0 ADD newColumn3 TEXT DEFAULT 'dummyString'",
      "ALTER TABLE $0 ALTER newColumn3 TYPE VARCHAR(1000)",
      "ALTER TABLE $0 DROP newColumn3",
    },
    {
      "CREATE TABLE tempTable2 AS SELECT * FROM $0 limit 1000000",
      "CREATE INDEX idx2 ON tempTable2(k)",
      "ALTER TABLE $0 ADD newColumn4 TEXT DEFAULT 'dummyString'",
      "ALTER TABLE tempTable2 ADD newColumn2 TEXT DEFAULT 'dummyString'",
      "TRUNCATE table $0 cascade",
      "ALTER TABLE $0 DROP newColumn4",
      "ALTER TABLE tempTable2 DROP newColumn2",
      "DROP INDEX idx2",
      "DROP TABLE tempTable2",
    },
    {
      "CREATE VIEW view2_$0 AS SELECT k from $0",
      "CREATE MATERIALIZED VIEW mv2 as SELECT k from $0 limit 10000",
      "REFRESH MATERIALIZED VIEW mv2",
      "DROP MATERIALIZED VIEW mv2",
      "DROP VIEW view2_$0",
    },
  };
  const std::vector<string> tableList = {
    "tb_0",
    "tb_1",
  };

  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn_yugabyte));
  RestartClusterWithDBCatalogVersionMode();

  const int num_databases = 3;
  std::vector<string> db_names;
  for (int i = 0; i < num_databases; ++i) {
    db_names.emplace_back(Format("sqlcrossdb_$0", i));
  }
  conn_yugabyte = ASSERT_RESULT(Connect());
  constexpr auto* kTestUser = "test_user";
  ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE USER $0", kTestUser));
  for (const auto& db_name : db_names) {
    ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE DATABASE $0", db_name));
  }

  for (const auto& db_name : db_names) {
    // On each database, create the tables.
    auto conn = ASSERT_RESULT(ConnectToDB(db_name));
    ASSERT_OK(conn.ExecuteFormat("GRANT ALL ON SCHEMA public TO $0", kTestUser));
    ASSERT_OK(conn.ExecuteFormat("SET SESSION AUTHORIZATION $0", kTestUser));
    for (const auto& table_name : tableList) {
      auto query = Format(
          "CREATE TABLE IF NOT EXISTS $0 "
          "(k varchar PRIMARY KEY, v1 VARCHAR, v2 integer, "
          "v3 money, v4 JSONB, v5 TIMESTAMP, v6 bool, v7 DATE, "
          "v8 TIME, v9 VARCHAR, v10 integer, v11 money, v12 JSONB, "
          "v13 TIMESTAMP, v14 bool, v15 DATE, v16 TIME, v17 VARCHAR, "
          "v18 integer, v19 money, v20 JSONB, "
          "v21 TIMESTAMP, v22 bool, v23 DATE, v24 TIME, v25 VARCHAR, "
          "v26 integer, v27 money, v28 JSONB, v29 TIMESTAMP, v30 bool)",
        table_name);
      LOG(INFO) << db_name << ":" << query;
      ASSERT_OK(conn.Execute(query));
    }
  }
  TestThreadHolder thread_holder;
  const int iterations = 4 / kTimeMultiplier;
  LOG(INFO) << "iterations: " << iterations;
  ASSERT_GE(iterations, 1);
  for (const auto& db_name : db_names) {
    thread_holder.AddThreadFunctor([this, &ddlLists, &tableList, &db_name] {

      for (int i = 0; i < iterations; ++i) {
        auto conn_test = ASSERT_RESULT(ConnectToDBAsUser(db_name, kTestUser));
        for (const auto& table_name : tableList) {
          // Randomly pick 3 lists of DDLs from ddlLists.
          for (int j = 0; j < 3; ++j) {
            const auto max_index = static_cast<int>(ddlLists.size() - 1);
            const size_t random_index = RandomUniformInt(0, max_index);
            // Run the DDLs in the current randomly selected DDL list.
            int k = 0;
            for (const auto& query : ddlLists[random_index]) {
              auto ddlQuery = Format(query, table_name);
              LOG(INFO) << "Executing (" << i << "," << j << "," << k << ") "
                        << db_name << ":" << table_name << " ddl: " << ddlQuery;
              ASSERT_OK(conn_test.Execute(ddlQuery));
              ++k;
            }
          }
        }
      }
    });
  }
  thread_holder.Stop();
}

TEST_F(PgCatalogVersionTest, NonBreakingDDLMode) {
  const string kDatabaseName = "yugabyte";

  auto conn1 = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  auto conn2 = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn1.Execute("CREATE TABLE t1(a int)"));
  ASSERT_OK(conn1.Execute("CREATE TABLE t2(a int)"));
  ASSERT_OK(conn1.Execute("BEGIN"));
  auto values = ASSERT_RESULT(conn1.FetchRows<int32_t>("SELECT * FROM t1"));
  ASSERT_TRUE(values.empty());
  ASSERT_OK(conn2.Execute("REVOKE ALL ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  // REVOKE is a breaking catalog change, the running transaction on conn1 is aborted.
  auto status = ResultToStatus(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  const string msg = "catalog snapshot used for this transaction has been invalidated";
  ASSERT_STR_CONTAINS(status.ToString(), msg);
  ASSERT_OK(conn1.Execute("ABORT"));

  // Let's start over, but this time use yb_make_next_ddl_statement_nonbreaking to suppress the
  // breaking catalog change and the SELECT command on conn1 runs successfully.
  ASSERT_OK(conn1.Execute("BEGIN"));
  values = ASSERT_RESULT(conn1.FetchRows<int32_t>("SELECT * FROM t1"));
  ASSERT_TRUE(values.empty());

  // Do grant first otherwise the next two REVOKE statements will be no-ops.
  ASSERT_OK(conn2.Execute("GRANT ALL ON t2 TO public"));

  ASSERT_OK(conn2.Execute("SET yb_make_next_ddl_statement_nonbreaking TO TRUE"));
  ASSERT_OK(conn2.Execute("REVOKE SELECT ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  values = ASSERT_RESULT(conn1.FetchRows<int32_t>("SELECT * FROM t1"));
  ASSERT_TRUE(values.empty());

  // Verify that the session variable yb_make_next_ddl_statement_nonbreaking auto-resets to false.
  // As a result, the running transaction on conn1 is aborted.
  ASSERT_OK(conn2.Execute("REVOKE INSERT ON t2 FROM public"));
  // Wait for the new catalog version to propagate to TServers.
  std::this_thread::sleep_for(2s);
  status = ResultToStatus(conn1.Fetch("SELECT * FROM t1"));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), msg);
  ASSERT_OK(conn1.Execute("ABORT"));
}

TEST_F(PgCatalogVersionTest, NonIncrementingDDLMode) {
  const string kDatabaseName = "yugabyte";

  auto conn = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  ASSERT_OK(conn.Execute("GRANT CREATE ON SCHEMA public TO yb_db_admin"));
  ASSERT_OK(conn.Execute("SET ROLE yb_db_admin"));
  ASSERT_OK(conn.Execute("CREATE TABLE t1(a int)"));
  auto version = ASSERT_RESULT(GetCatalogVersion(&conn));

  // REVOKE bumps up the catalog version by 1.
  ASSERT_OK(conn.Execute("REVOKE SELECT ON t1 FROM public"));
  auto new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version + 1);
  version = new_version;

  // GRANT bumps up the catalog version by 1.
  ASSERT_OK(conn.Execute("GRANT SELECT ON t1 TO public"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version + 1);
  version = new_version;

  ASSERT_OK(conn.Execute("CREATE INDEX idx1 ON t1(a)"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  // By default CREATE INDEX runs concurrently and its algorithm requires to bump up catalog
  // version 3 times.
  ASSERT_EQ(new_version, version + 3);
  version = new_version;

  // CREATE INDEX CONCURRENTLY bumps up catalog version by 1.
  ASSERT_OK(conn.Execute("CREATE INDEX NONCONCURRENTLY idx2 ON t1(a)"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version + 1);
  version = new_version;

  // Let's start over, but this time use yb_make_next_ddl_statement_nonincrementing to suppress
  // incrementing catalog version.
  ASSERT_OK(conn.Execute("SET yb_make_next_ddl_statement_nonincrementing TO TRUE"));
  ASSERT_OK(conn.Execute("REVOKE SELECT ON t1 FROM public"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version);

  ASSERT_OK(conn.Execute("SET yb_make_next_ddl_statement_nonincrementing TO TRUE"));
  ASSERT_OK(conn.Execute("GRANT SELECT ON t1 TO public"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version);

  ASSERT_OK(conn.Execute("SET yb_make_next_ddl_statement_nonincrementing TO TRUE"));
  ASSERT_OK(conn.Execute("CREATE INDEX idx3 ON t1(a)"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  // By default CREATE INDEX runs concurrently and its algorithm requires to bump up catalog
  // version 3 times, only the first bump is suppressed.
  ASSERT_EQ(new_version, version + 2);
  version = new_version;

  ASSERT_OK(conn.Execute("SET yb_make_next_ddl_statement_nonincrementing TO TRUE"));
  ASSERT_OK(conn.Execute("CREATE INDEX NONCONCURRENTLY idx4 ON t1(a)"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version);

  // Verify that the session variable yb_make_next_ddl_statement_nonbreaking auto-resets to false.
  ASSERT_OK(conn.Execute("REVOKE SELECT ON t1 FROM public"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version + 1);
  version = new_version;

  // Since yb_make_next_ddl_statement_nonbreaking auto-resets to false, we should see catalog
  // version gets bumped up as before.
  ASSERT_OK(conn.Execute("GRANT SELECT ON t1 TO public"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version + 1);
  version = new_version;

  ASSERT_OK(conn.Execute("CREATE INDEX idx5 ON t1(a)"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version + 3);
  version = new_version;

  ASSERT_OK(conn.Execute("CREATE INDEX NONCONCURRENTLY idx6 ON t1(a)"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version + 1);
  version = new_version;

  // Now test the scenario where we create a new table, followed by create index nonconcurrently
  // on the new table. Use yb_make_next_ddl_statement_nonbreaking to suppress catalog version
  // increment on the create index statement.
  // First create a second connection conn2.
  auto conn2 = ASSERT_RESULT(ConnectToDB(kDatabaseName));

  ASSERT_OK(conn.Execute("CREATE TABLE demo (a INT, b INT)"));
  ASSERT_OK(conn.Execute("SET yb_make_next_ddl_statement_nonincrementing TO TRUE"));
  ASSERT_OK(conn.Execute("CREATE INDEX NONCONCURRENTLY a_idx ON demo (a)"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version);

  // Sanity test on conn2 write, count, select and delete on the new table created on conn.
  ASSERT_OK(conn2.Execute("INSERT INTO demo SELECT n, n FROM generate_series(1,100) n"));
  auto row_count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM demo"));
  ASSERT_EQ(row_count, 100);
  std::tuple<int32_t, int32_t> expected_row = {50, 50};
  auto row = ASSERT_RESULT((conn2.FetchRow<int32_t, int32_t>("SELECT * FROM demo WHERE a = 50")));
  ASSERT_EQ(row, expected_row);
  ASSERT_OK(conn2.Execute("DELETE FROM demo WHERE a = 50"));
  row_count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM demo"));
  ASSERT_EQ(row_count, 99);

  // Temp table DDLs should not increment catalog version.
  version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_OK(conn.Execute("CREATE TEMP TABLE temp_demo (a INT, b INT)"));
  ASSERT_OK(conn.Execute("ALTER TABLE temp_demo ADD COLUMN c INT"));
  ASSERT_OK(conn.Execute("CREATE INDEX temp_idx ON temp_demo(c)"));
  ASSERT_OK(conn.Execute("DROP INDEX temp_idx"));
  ASSERT_OK(conn.Execute("DROP TABLE temp_demo"));
  new_version = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(new_version, version);
}

TEST_F(PgCatalogVersionTest, SimulateRollingUpgrade) {
  // Manually switch back to non-per-db catalog version mode.
  RestartClusterWithoutDBCatalogVersionMode();
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(PrepareDBCatalogVersion(&conn, false));

  // Test setup.
  ASSERT_OK(conn.Execute("CREATE USER u1"));
  ASSERT_OK(conn.Execute("CREATE TABLE t(id int)"));
  ASSERT_OK(conn.Execute("GRANT ALL ON t TO public"));

  // Make a connection to the first node.
  pg_ts = cluster_->tablet_server(0);
  auto conn1 = ASSERT_RESULT(Connect());

  // Make a connection to the second node as user u1.
  pg_ts = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(ConnectToDBAsUser("yugabyte", "u1"));

  // On the second connection, u1 should have permission to access table t
  ASSERT_OK(conn2.Fetch("SELECT * FROM t"));

  // Simulate rolling upgrade where masters are upgraded to a new version which has
  // --ysql_enable_db_catalog_version_mode enabled.
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "ysql_enable_db_catalog_version_mode", "true"));
  // Execute a DDL statement on the first connection to bumps up the catalog version
  ASSERT_OK(conn1.Execute("REVOKE ALL ON t FROM public"));
  WaitForCatalogVersionToPropagate();

  // On conn2 we should see permission denied error because of the previous REVOKE.
  auto status = ResultToStatus(conn2.Fetch("SELECT * FROM t"));
  ASSERT_TRUE(status.IsNetworkError()) << status;
  const string msg = "permission denied for table t";
  ASSERT_STR_CONTAINS(status.ToString(), msg);
}

// This test that ALTER ROLE statement will increment catalog version
// if --FLAGS_ysql_yb_enable_nop_alter_role_optimization=false.
TEST_F(PgCatalogVersionTest, DisableNopAlterRoleOptimization) {
  auto conn = ASSERT_RESULT(Connect());
  auto v1 = ASSERT_RESULT(GetCatalogVersion(&conn));
  // This ALTER ROLE should be a nop DDL.
  ASSERT_OK(conn.Execute("ALTER ROLE yugabyte SUPERUSER"));
  auto v2 = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(v2, v1);
  ASSERT_OK(cluster_->SetFlagOnTServers(
      "ysql_yb_enable_nop_alter_role_optimization", "false"));
  // This ALTER ROLE is not a nop DDL because the nop alter role optimization is disabled.
  ASSERT_OK(conn.Execute("ALTER ROLE yugabyte SUPERUSER"));
  auto v3 = ASSERT_RESULT(GetCatalogVersion(&conn));
  ASSERT_EQ(v3, v2 + 1);
}

TEST_F(PgCatalogVersionTest, SimulateDelayedHeartbeatResponse) {
  RestartClusterWithDBCatalogVersionMode({"--TEST_delay_set_catalog_version_table_mode_count=30"});
  auto status = ResultToStatus(Connect());
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(),
                      "catalog_version_table mode not set in shared memory, "
                      "tserver not ready to serve requests");
}

TEST_F(PgCatalogVersionTest, AlterDatabaseCatalogVersionIncrement) {
  PGConn conn = ASSERT_RESULT(Connect());
  // Create a test db and a test user.
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db"));
  ASSERT_OK(conn.Execute("CREATE USER test_user"));
  auto v1_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn));

  // Connect to the test db as the test user.
  PGConn conn_test1 = ASSERT_RESULT(ConnectToDBAsUser("test_db" /* db_name */, "test_user"));
  auto v1_test_db = ASSERT_RESULT(GetCatalogVersion(&conn_test1));

  // Try to perform alter database test_db as the test user, which isn't the owner.
  auto status = conn_test1.Execute("ALTER DATABASE test_db SET statement_timeout = 100");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "must be owner of database");
  status = conn_test1.Execute("ALTER DATABASE test_db SET temp_file_limit = 1024");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "must be owner of database");
  status = conn_test1.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "must be owner of database");

  ASSERT_OK(conn.Execute("ALTER DATABASE test_db OWNER TO test_user"));
  auto v2_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn));
  auto v2_test_db = ASSERT_RESULT(GetCatalogVersion(&conn_test1));
  ASSERT_EQ(v2_yugabyte, v1_yugabyte + 1);
  ASSERT_EQ(v2_test_db, v1_test_db + 1);
  WaitForCatalogVersionToPropagate();
  ASSERT_OK(conn_test1.Execute("ALTER DATABASE test_db SET statement_timeout = 100"));
  auto v3_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn));
  auto v3_test_db = ASSERT_RESULT(GetCatalogVersion(&conn_test1));
  ASSERT_EQ(v3_yugabyte, v2_yugabyte);
  ASSERT_EQ(v3_test_db, v2_test_db + 1);
  // temp_file_limit requires PGC_SUSET, test_user only has PGC_USERSET.
  status = conn_test1.Execute("ALTER DATABASE test_db SET temp_file_limit = 1024");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "permission denied to set parameter");

  // Rename database requires createdb priviledge.
  status = conn_test1.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "permission denied to rename database");

  // Grant createdb priviledge to test user.
  ASSERT_OK(conn.Execute("ALTER USER test_user CREATEDB"));
  auto v4_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn));
  auto v4_test_db = ASSERT_RESULT(GetCatalogVersion(&conn_test1));
  // Alter user is a global-impact DDL.
  ASSERT_EQ(v4_yugabyte, v3_yugabyte + 1);
  ASSERT_EQ(v4_test_db, v3_test_db + 1);
  WaitForCatalogVersionToPropagate();
  status = conn_test1.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "current database cannot be renamed");

  PGConn conn_test2 = ASSERT_RESULT(ConnectToDBAsUser(
      "yugabyte" /* db_name */, "test_user"));
  status = conn_test2.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  // The error is only detected on connection to the same node.
  ASSERT_STR_CONTAINS(status.ToString(), "is being accessed by other users");

  // Make a connection to the second node as test user.
  pg_ts = cluster_->tablet_server(1);
  PGConn conn_test3 = ASSERT_RESULT(ConnectToDBAsUser(
      "yugabyte" /* db_name */, "test_user"));
  // The error is not detected on connection to the a different node, this is
  // unique for YB.
  ASSERT_OK(conn_test3.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed"));
}

// This test ensures that ALTER DATABASE RENAME has global impact. If we only bump up
// the catalog version of the altered database (test_db), or even if we also bump up the
// catalog version of MyDatabaseId (yugabyte in this test), we can have a situation
// where DROP DATABASE executed from a connection to a third DB (postgres) stucks in
// a PG infinite loop: this third-DB connection has a stale cache entry of the database
// with its old name, and performing a scan-based query from the master returns the new
// name. The PG infinite loop can only break until they compare equal but if the third
// DB's catalog version isn't bumped, its connection will never refresh its catalog caches
// and the old name remains in the stale cache entry.
// Note that due to tserver/master heartbeat delay, it is still possible that even if
// ALTER DATABASE RENAME has global impact, the third-DB connection has already entered
// into the infinite loop before it receives the heartbeat and performs a catalog cache
// refresh. So this DROP DATABASE hanging problem is only mitigated not completed avoided.
TEST_F(PgCatalogVersionTest, AlterDatabaseRename) {
  // Test setup: create a test db.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db"));

  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  auto conn_postgres = ASSERT_RESULT(ConnectToDB("postgres"));

  auto v1_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  auto v1_postgres = ASSERT_RESULT(GetCatalogVersion(&conn_postgres));

  // Execute a query on the postgres-connection to get a cache entry with the old DB name.
  ASSERT_OK(conn_postgres.Execute("ALTER DATABASE test_db SET temp_file_limit = 1024"));
  auto v2_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  auto v2_postgres = ASSERT_RESULT(GetCatalogVersion(&conn_postgres));
  ASSERT_EQ(v1_yugabyte, v2_yugabyte);
  ASSERT_EQ(v1_postgres, v2_postgres);

  // Execute a query on the yugabyte-connection to rename the test_db.
  ASSERT_OK(conn_yugabyte.Execute("ALTER DATABASE test_db RENAME TO test_db_renamed"));

  auto v3_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  auto v3_postgres = ASSERT_RESULT(GetCatalogVersion(&conn_postgres));

  // If we did not bump up the catalog version of postgres DB, this DROP DATABASE would
  // stuck and the test timed out.
  ASSERT_OK(conn_postgres.Execute("DROP DATABASE test_db_renamed"));
  auto v4_yugabyte = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  auto v4_postgres = ASSERT_RESULT(GetCatalogVersion(&conn_postgres));

  // ALTER DATABASE RENAME has global-impact.
  ASSERT_EQ(v2_yugabyte + 1, v3_yugabyte);
  ASSERT_EQ(v2_postgres + 1, v3_postgres);

  // DROP DATABASE is a same-version DDL that does not bump up catalog version.
  ASSERT_EQ(v3_yugabyte, v4_yugabyte);
  ASSERT_EQ(v3_postgres, v4_postgres);
}

// This test ensures that ALTER DATABASE OWNER has global impact. If we only bump up
// the catalog version of the altered database (test_db), or even if we also bump up the
// catalog version of MyDatabaseId (yugabyte in this test), we can have a situation
// where a user connected to a third DB (postgres in this test) can end up having a
// stale database entry, which prevents/allows the user to perform an operation
// of the test_db incorrectly.
TEST_F(PgCatalogVersionTest, AlterDatabaseOwner) {
  // Test setup: create a test db and two users.
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db"));
  ASSERT_OK(conn.Execute("CREATE USER test_user1"));
  ASSERT_OK(conn.Execute("CREATE USER test_user2"));

  auto conn_test_user1 = ASSERT_RESULT(ConnectToDBAsUser(
      "postgres" /* db_name */, "test_user1"));
  auto conn_test_user2 = ASSERT_RESULT(ConnectToDBAsUser(
      "postgres" /* db_name */, "test_user2"));

  // Initially neither user can drop test_db.
  ASSERT_NOK_STR_CONTAINS(conn_test_user1.Execute("DROP DATABASE test_db"),
                          "must be owner of database");
  ASSERT_NOK_STR_CONTAINS(conn_test_user2.Execute("DROP DATABASE test_db"),
                          "must be owner of database");

  // Change the owner of test_db to test_user1.
  ASSERT_OK(conn.Execute("ALTER DATABASE test_db OWNER TO test_user1"));

  WaitForCatalogVersionToPropagate();

  // Now test_user1 owns the database, test_user2 should continue not be able to drop test_db.
  ASSERT_NOK_STR_CONTAINS(conn_test_user2.Execute("DROP DATABASE test_db"),
                          "must be owner of database");
  // Now test_user1 owns the database, so test_user1 should be able to drop test_db.
  ASSERT_OK(conn_test_user1.Execute("DROP DATABASE test_db"));

  // Redo the test in a different way.
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db"));

  // Initially neither user can drop test_db.
  ASSERT_NOK_STR_CONTAINS(conn_test_user1.Execute("DROP DATABASE test_db"),
                          "must be owner of database");
  ASSERT_NOK_STR_CONTAINS(conn_test_user2.Execute("DROP DATABASE test_db"),
                          "must be owner of database");

  // Change the owner of test_db to test_user1.
  ASSERT_OK(conn.Execute("ALTER DATABASE test_db OWNER TO test_user1"));

  WaitForCatalogVersionToPropagate();

  // Now test_user1 owns the database, so test_user1 should be able to alter it. This gets
  // the test_db cache entry loaded in conn_test_user1. Note that temp_file_limit requires
  // PGC_SUSET, test_user only has PGC_USERSET so we still get a permission denied error.
  ASSERT_NOK_STR_CONTAINS(conn_test_user1.Execute(
      "ALTER DATABASE test_db SET temp_file_limit = 1024"),
      "permission denied to set parameter");

  // Change the owner of test_db to test_user2.
  ASSERT_OK(conn.Execute("ALTER DATABASE test_db OWNER TO test_user2"));

  WaitForCatalogVersionToPropagate();

  // Now test_user2 owns the database, so test_user1 should not be able to drop test_db.
  ASSERT_NOK_STR_CONTAINS(conn_test_user1.Execute("DROP DATABASE test_db"),
                          "must be owner of database");

  // Now test_user2 owns the database, so test_user2 should be able to drop test_db.
  ASSERT_OK(conn_test_user2.Execute("DROP DATABASE test_db"));
}

// Create or replace view should increment catalog version.
TEST_F(PgCatalogVersionTest, CreateOrReplaceView) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE foo(a INT, b INT)"));
  ASSERT_OK(conn1.Execute("INSERT INTO foo VALUES(1, 2)"));

  auto v1 = ASSERT_RESULT(GetCatalogVersion(&conn1));
  ASSERT_OK(conn1.Execute("CREATE VIEW v AS SELECT a, b FROM foo"));
  auto v2 = ASSERT_RESULT(GetCatalogVersion(&conn1));
  // Create view does not increment catalog version.
  ASSERT_EQ(v2, v1);

  auto query = "SELECT * FROM v"s;
  auto conn2 = ASSERT_RESULT(Connect());
  auto expected_result1 = "1, 2";
  auto expected_result2 = "2, 1";
  auto result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  ASSERT_EQ(result, expected_result1);
  ASSERT_OK(conn1.Execute("CREATE OR REPLACE VIEW v AS SELECT b AS a, a AS b FROM foo"));
  auto v3 = ASSERT_RESULT(GetCatalogVersion(&conn1));
  // Create or replace view increments catalog version.
  ASSERT_EQ(v3, v2 + 1);

  WaitForCatalogVersionToPropagate();

  result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  ASSERT_EQ(result, expected_result2);
}

// This test does sanity check that invalidation messages are portable
// across nodes and they are stable.
TEST_F(PgCatalogVersionTest, InvalMessageSanityTest) {
  RestartClusterWithInvalMessageEnabled();
  auto ts_index = RandomUniformInt(0UL, cluster_->num_tablet_servers() - 1);
  pg_ts = cluster_->tablet_server(ts_index);
  LOG(INFO) << "ts_index: " << ts_index;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET yb_test_inval_message_portability = true"));
  ASSERT_OK(conn.Execute("SET log_min_messages = DEBUG1"));
  auto choice = RandomUniformInt(0, 1);
  LOG(INFO) << "choice: " << choice;
  // We connect to a randomly selected node, and create two types of tables.
  if (choice) {
    ASSERT_OK(conn.Execute("CREATE TABLE foo(a INT, b INT)"));
  } else {
    ASSERT_OK(conn.Execute("CREATE TABLE foo(id TEXT)"));
  }
  ASSERT_OK(conn.Execute("DROP TABLE foo"));
  auto query = "SELECT current_version, encode(messages, 'hex') "
               "FROM pg_yb_invalidation_messages"s;
  auto expected_result0 =
      "2, 5000000000000000cb34000040c1eb0a00000000000000004f00000000000000cb340"
      "0005ac4b85300000000000000005000000000000000cb34000047a2537b0000000000000"
      "0004f00000000000000cb34000021e2d2ca00000000000000000700000000000000cb340"
      "0004a34179b00000000000000000600000000000000cb3400003239589f0000000000000"
      "0000700000000000000cb340000849f9c1300000000000000000600000000000000cb340"
      "00002517d2400000000000000000700000000000000cb340000d519492c0000000000000"
      "0000600000000000000cb340000532bd64f00000000000000000700000000000000cb340"
      "0000ce84cf300000000000000000600000000000000cb340000f1f7a7e80000000000000"
      "0000700000000000000cb340000ecbba96500000000000000000600000000000000cb340"
      "00084a01e3000000000000000000700000000000000cb3400003f53cbc60000000000000"
      "0000600000000000000cb3400001310debc00000000000000000700000000000000cb340"
      "000c76e67a200000000000000000600000000000000cb340000f3cf9e8c0000000000000"
      "0000700000000000000cb34000017e0201d00000000000000000600000000000000cb340"
      "0004ba32d1e00000000000000003700000000000000cb340000465708530000000000000"
      "0003600000000000000cb34000021e2d2ca0000000000000000fb00000000000000cb340"
      "000300a00000000000000000000fb00000000000000cb340000300a00000000000000000"
      "000fe00000000000000cb340000004000000000000000000000fb00000000000000cb340"
      "000300a00000000000000000000";
  auto expected_result1 =
      "2, 5000000000000000cb34000040c1eb0a00000000000000004f00000000000000cb340"
      "0005ac4b85300000000000000005000000000000000cb34000047a2537b0000000000000"
      "0004f00000000000000cb34000021e2d2ca00000000000000000700000000000000cb340"
      "0004a34179b00000000000000000600000000000000cb3400003239589f0000000000000"
      "0000700000000000000cb340000849f9c1300000000000000000600000000000000cb340"
      "00002517d2400000000000000000700000000000000cb340000d519492c0000000000000"
      "0000600000000000000cb340000532bd64f00000000000000000700000000000000cb340"
      "0000ce84cf300000000000000000600000000000000cb340000f1f7a7e80000000000000"
      "0000700000000000000cb340000ecbba96500000000000000000600000000000000cb340"
      "00084a01e3000000000000000000700000000000000cb3400003f53cbc60000000000000"
      "0000600000000000000cb3400001310debc00000000000000000700000000000000cb340"
      "000c76e67a200000000000000000600000000000000cb340000f3cf9e8c0000000000000"
      "0000700000000000000cb34000017e0201d00000000000000000600000000000000cb340"
      "00006e6784000000000000000000700000000000000cb3400007651cba70000000000000"
      "0000600000000000000cb340000bdf7d7b600000000000000003700000000000000cb340"
      "0004657085300000000000000003600000000000000cb34000021e2d2ca0000000000000"
      "000fb00000000000000cb340000300a00000000000000000000fb00000000000000cb340"
      "000300a00000000000000000000fe00000000000000cb340000004000000000000000000"
      "000fb00000000000000cb340000300a00000000000000000000";
  auto result = ASSERT_RESULT(conn.FetchAllAsString(query));
  if (choice) {
    ASSERT_EQ(result, expected_result1);
  } else {
    ASSERT_EQ(result, expected_result0);
  }
}

TEST_F(PgCatalogVersionTest, InvalMessageMultiDDLTest) {
  RestartClusterWithInvalMessageEnabled();
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn.Execute("CREATE TABLE foo(id INT)"));
  auto query = "BEGIN; "s;
  for (int i = 1; i <= 5; i++) {
    query += Format("ALTER TABLE foo ADD COLUMN id$0 INT; ", i);
  }
  query += "END";
  LOG(INFO) << "multi-ddl query: " << query;
  ASSERT_OK(conn.Execute(query));
  auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn, kYugabyteDatabase));
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      Format("SELECT current_version, length(messages) FROM pg_yb_invalidation_messages "
             "WHERE db_oid = $0", yugabyte_db_oid)));
  LOG(INFO) << "result: " << result;
  ASSERT_EQ(result, "2, 120; 3, 144; 4, 144; 5, 144; 6, 144");
}

TEST_F(PgCatalogVersionTest, InvalMessageCatCacheRefreshTest) {
  RestartClusterWithInvalMessageEnabled();
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn2.Execute("SET log_min_messages = DEBUG1"));

  ASSERT_OK(conn1.Execute("CREATE TABLE foo(id INT PRIMARY KEY)"));
  ASSERT_OK(conn1.Execute("INSERT INTO foo VALUES(1)"));

  auto query = "SELECT * FROM foo"s;
  auto result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  auto expected_result1 = "1";
  ASSERT_EQ(result, expected_result1);

  ASSERT_OK(conn1.Execute("ALTER TABLE foo ADD COLUMN value TEXT"));
  ASSERT_OK(conn1.Execute("INSERT INTO foo VALUES(2, '2')"));

  WaitForCatalogVersionToPropagate();
  result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  auto expected_result2 = "1, NULL; 2, 2";
  ASSERT_EQ(result, expected_result2);

  // Verify that the incremental catalog cache refresh happened on conn2.
  VerifyCatCacheRefreshMetricsHelper(0 /* num_full_refreshes */, 1 /* num_delta_refreshes */);
}

TEST_F(PgCatalogVersionTest, InvalMessageQueueOverflowTest) {
  RestartClusterWithInvalMessageEnabled(
      {"--ysql_max_invalidation_message_queue_size=2"});
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn2.Execute("SET log_min_messages = DEBUG1"));

  ASSERT_OK(conn1.Execute("CREATE TABLE foo(id INT PRIMARY KEY)"));

  auto query = "SELECT 1"s;
  auto result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  ASSERT_EQ(result, "1");

  // Execute 4 DDLs that cause catalog version to bump to cause the
  // tserver message queue to overflow.
  for (int i = 0; i < 2; ++i) {
    ASSERT_OK(conn1.Execute("ALTER TABLE foo ADD COLUMN value TEXT"));
    ASSERT_OK(conn1.Execute("ALTER TABLE foo DROP COLUMN value"));
  }

  WaitForCatalogVersionToPropagate();
  result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  ASSERT_EQ(result, "1");

  // Since the message queue overflowed, we will see a full catalog cache refresh on conn2.
  VerifyCatCacheRefreshMetricsHelper(1 /* num_full_refreshes */, 0 /* num_delta_refreshes */);
}

TEST_F(PgCatalogVersionTest, WaitForSharedCatalogVersionToCatchup) {
  RestartClusterWithInvalMessageEnabled(
      { "--TEST_ysql_disable_transparent_cache_refresh_retry=true" });

  std::string ddl_script;
  for (int i = 1; i < 100; ++i) {
    ddl_script += Format("GRANT ALL ON SCHEMA public TO PUBLIC;\n");
    ddl_script += Format("REVOKE USAGE ON SCHEMA public FROM PUBLIC;\n");
  }
  std::unique_ptr<WritableFile> ddl_script_file;
  std::string tmp_file_name;
  ASSERT_OK(Env::Default()->NewTempWritableFile(
      WritableFileOptions(), "ddl_XXXXXX", &tmp_file_name, &ddl_script_file));
  ASSERT_OK(ddl_script_file->Append(ddl_script));
  ASSERT_OK(ddl_script_file->Close());
  LOG(INFO) << "ddl_script:\n" << ddl_script;

  auto hostport = cluster_->ysql_hostport(0);
  std::string main_script = "SET yb_test_delay_after_applying_inval_message_ms = 2000;\n"s;
  main_script += "SET yb_max_query_layer_retries = 0;\n"s;
  main_script += "CREATE TABLE foo(id INT);\n"s;
  main_script += "SELECT * FROM foo;\n"s;
  std::string ysqlsh_path = CHECK_RESULT(path_utils::GetPgToolPath("ysqlsh"));
  main_script += Format("\\! $0 -f $1 -h $2 -p $3 yugabyte > /dev/null\n",
                        ysqlsh_path, tmp_file_name, hostport.host(), hostport.port());
  main_script += "SELECT * FROM foo;\n"s;
  LOG(INFO) << "main_script:\n" << main_script;

  auto scope_exit = ScopeExit([tmp_file_name] {
    if (Env::Default()->FileExists(tmp_file_name)) {
      WARN_NOT_OK(
          Env::Default()->DeleteFile(tmp_file_name),
          Format("Failed to delete temporary sql script file $0.", tmp_file_name));
    }
  });
  YsqlshRunner ysqlsh_runner = CHECK_RESULT(YsqlshRunner::GetYsqlshRunner(hostport));
  auto output = CHECK_RESULT(ysqlsh_runner.ExecuteSqlScript(
      main_script, "WaitForSharedCatalogVersionToCatchup" /* tmp_file_prefix */));
  LOG(INFO) << "output: " << output;
}

TEST_F(PgCatalogVersionTest, AnalyzeSingleTable) {
  RestartClusterWithInvalMessageEnabled();
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
  // Analyze a single table, PG simply uses the transaction that enclosing the YB DDL
  // transaction. We only see one catalog version increment due to the DDL so we should
  // only see one row of version 2 in pg_yb_invalidation_messages.
  ASSERT_OK(conn_yugabyte.Execute("ANALYZE pg_class"));
  auto result = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(
      "SELECT db_oid, current_version, length(messages) FROM pg_yb_invalidation_messages"));
  LOG(INFO) << "result:\n" << result;
  const string expected = Format("$0, 2, 792", yugabyte_db_oid);
  ASSERT_EQ(result, expected);
}

TEST_F(PgCatalogVersionTest, AnalyzeTwoTables) {
  RestartClusterWithInvalMessageEnabled();
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
  // Analyze two tables, PG internally creates an additional transaction that commits separately.
  // We see two catalog version increments so we should see two rows of version 2 and 3 in
  // pg_yb_invalidation_messages.
  ASSERT_OK(conn_yugabyte.Execute("ANALYZE pg_class, pg_attribute"));
  auto result = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(
      "SELECT db_oid, current_version, length(messages) FROM pg_yb_invalidation_messages"));
  LOG(INFO) << "result:\n" << result;
  const string expected = Format("$0, 2, 1416", yugabyte_db_oid);
  ASSERT_EQ(result, expected);
}

TEST_F(PgCatalogVersionTest, AnalyzeAllTables) {
  RestartClusterWithInvalMessageEnabled();
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
  ASSERT_OK(conn_yugabyte.Execute("ANALYZE"));
  auto result = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(
      "SELECT db_oid, current_version, length(messages) FROM pg_yb_invalidation_messages"));
  LOG(INFO) << "result:\n" << result;
  const string expected = Format("$0, 2, 10776", yugabyte_db_oid);
  ASSERT_EQ(result, expected);
}

TEST_F(PgCatalogVersionTest, AnalyzeInsideDdlEventTrigger) {
  RestartClusterWithInvalMessageEnabled();
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  const auto yugabyte_db_oid = ASSERT_RESULT(GetDatabaseOid(&conn_yugabyte, kYugabyteDatabase));
  const string query =
        R"#(
CREATE OR REPLACE FUNCTION log_ddl()
  RETURNS event_trigger AS $$
BEGIN
  ANALYZE pg_class, pg_attribute;
END;
$$ LANGUAGE plpgsql;

CREATE EVENT TRIGGER log_ddl_info ON ddl_command_end EXECUTE PROCEDURE log_ddl();

CREATE TABLE testtable (id INT);
ALTER TABLE testtable ADD COLUMN value INT;
        )#";
  // ANALYZE two tables when nested inside another DDL (CREATE TABLE and ALTER TABLE),
  // PG does not generate a transaction for ANALYZE that commits separately. In this
  // case ANALYZE is executed inside a trigger when the outer DDL execution is active.
  // All of the invalidation messages generated by ANALYZE are simply included into
  // that of the outer DDL. The 4 versions are:
  // 2: CREATE OR REPLACE FUNCTION
  // 3: CREATE EVENT TRIGGER
  // 4: CREATE TABLE -- increments because of the embedded ANALYZE
  // 5: ALTER TABLE
  ASSERT_OK(conn_yugabyte.Execute(query));
  auto result = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(
      "SELECT db_oid, current_version, length(messages) FROM pg_yb_invalidation_messages"));
  const string expected = Format("$0, 2, 72; $0, 3, 96; $0, 4, 2520; $0, 5, 1776",
                                 yugabyte_db_oid);
  LOG(INFO) << "result:\n" << result;
  ASSERT_EQ(result, expected);
}

// This test verifies that for a set of common DDLs generate stable invalidation messages.
// If the test fails due to fingerprint change that indicates one or more DDL has generated
// a different list of invalidation messages and we need to examine that to decide whether
// we should increment yb_version of one or more types of SharedInvalidationMessage.
TEST_F(PgCatalogVersionTest, InvalMessageSampleDDLs) {
  // Disable auto analyze to prevent unexpected invalidation messages.
  RestartClusterWithInvalMessageEnabled(
      { "--ysql_enable_auto_analyze_service=false",
        "--ysql_enable_table_mutation_counter=false",
        "--ysql_yb_invalidation_message_expiration_secs=36000" });
  const string sample_ddl_script =
        R"#(
SET yb_test_inval_message_portability = true;
SET log_min_messages = DEBUG1;
CREATE TABLE my_first_table (
    first_column text,
    second_column integer
);
CREATE TABLE products (
    product_no integer,
    name text,
    price numeric
);
DROP TABLE my_first_table;
DROP TABLE products;
CREATE TABLE products (
    product_no integer PRIMARY KEY,
    name text,
    price numeric
);
CREATE TABLE orders (
    order_id integer PRIMARY KEY,
    product_no integer REFERENCES products (product_no),
    quantity integer
);
CREATE TABLE tenants (
    tenant_id integer PRIMARY KEY
);
CREATE TABLE users (
    tenant_id integer REFERENCES tenants ON DELETE CASCADE,
    user_id integer NOT NULL,
    PRIMARY KEY (tenant_id, user_id)
);
CREATE TABLE posts (
    tenant_id integer REFERENCES tenants ON DELETE CASCADE,
    post_id integer NOT NULL,
    author_id integer,
    PRIMARY KEY (tenant_id, post_id),
    FOREIGN KEY (tenant_id, author_id) REFERENCES users ON DELETE SET NULL (author_id)
);
ALTER TABLE products ADD COLUMN description text CHECK (description <> '');
ALTER TABLE products DROP COLUMN description;
ALTER TABLE products ADD CHECK (name <> '');
ALTER TABLE posts ALTER COLUMN author_id SET NOT NULL;
ALTER TABLE products ADD CONSTRAINT some_name UNIQUE (product_no);
ALTER TABLE products DROP CONSTRAINT some_name;
ALTER TABLE posts ALTER COLUMN author_id DROP NOT NULL;
ALTER TABLE products ALTER COLUMN price SET DEFAULT 7.77;
ALTER TABLE products ALTER COLUMN price DROP DEFAULT;
ALTER TABLE products ALTER COLUMN price TYPE numeric(10,2);
ALTER TABLE products RENAME COLUMN product_no TO product_number;
ALTER TABLE products RENAME TO items;

-- row security example
-- Simple passwd-file based example
CREATE TABLE passwd (
  user_name             text UNIQUE NOT NULL,
  pwhash                text,
  uid                   int  PRIMARY KEY,
  gid                   int  NOT NULL,
  real_name             text NOT NULL,
  home_phone            text,
  extra_info            text,
  home_dir              text NOT NULL,
  shell                 text NOT NULL
);

CREATE ROLE admin;  -- Administrator
CREATE ROLE bob;    -- Normal user
CREATE ROLE alice;  -- Normal user

-- Populate the table
INSERT INTO passwd VALUES
  ('admin','xxx',0,0,'Admin','111-222-3333',null,'/root','/bin/dash');
INSERT INTO passwd VALUES
  ('bob','xxx',1,1,'Bob','123-456-7890',null,'/home/bob','/bin/zsh');
INSERT INTO passwd VALUES
  ('alice','xxx',2,1,'Alice','098-765-4321',null,'/home/alice','/bin/zsh');

-- Be sure to enable row level security on the table
ALTER TABLE passwd ENABLE ROW LEVEL SECURITY;

-- Create policies
-- Administrator can see all rows and add any rows
CREATE POLICY admin_all ON passwd TO admin USING (true) WITH CHECK (true);
-- Normal users can view all rows
CREATE POLICY all_view ON passwd FOR SELECT USING (true);
-- Normal users can update their own records, but
-- limit which shells a normal user is allowed to set
CREATE POLICY user_mod ON passwd FOR UPDATE
  USING (current_user = user_name)
  WITH CHECK (
    current_user = user_name AND
    shell IN ('/bin/bash','/bin/sh','/bin/dash','/bin/zsh','/bin/tcsh')
  );

-- Allow admin all normal rights
GRANT SELECT, INSERT, UPDATE, DELETE ON passwd TO admin;
-- Users only get select access on public columns
GRANT SELECT
  (user_name, uid, gid, real_name, home_phone, extra_info, home_dir, shell)
  ON passwd TO public;
-- Allow users to update certain columns
GRANT UPDATE
  (pwhash, real_name, home_phone, extra_info, shell)
  ON passwd TO public;

CREATE SCHEMA hollywood;
CREATE TABLE hollywood.films (title text, release date, awards text[]);
CREATE VIEW hollywood.winners AS
    SELECT title, release FROM hollywood.films WHERE awards IS NOT NULL;
DROP SCHEMA hollywood CASCADE;

-- Create a partitioned hierarchy of LIST, RANGE and HASH.
CREATE TABLE root_list_parent (list_part_key char, hash_part_key int, range_part_key int)
  PARTITION BY LIST(list_part_key);
CREATE TABLE hash_parent PARTITION OF root_list_parent FOR VALUES in ('a', 'b')
  PARTITION BY HASH (hash_part_key);
CREATE TABLE range_parent PARTITION OF hash_parent FOR VALUES WITH (modulus 1, remainder 0)
  PARTITION BY RANGE (range_part_key);
CREATE TABLE child_partition PARTITION OF range_parent FOR VALUES FROM (1) TO (5);

-- Add a column to the parent table, verify that selecting data still works.
ALTER TABLE root_list_parent ADD COLUMN foo VARCHAR(2);

-- Alter column type at the parent table.
ALTER TABLE root_list_parent ALTER COLUMN foo TYPE VARCHAR(3);

-- Drop a column from the parent table, verify that selecting data still works.
ALTER TABLE root_list_parent DROP COLUMN foo;

-- Retry adding a column after error.
ALTER TABLE root_list_parent ADD COLUMN foo text not null DEFAULT 'abc'; -- passes

-- Rename a column belonging to the parent table.
ALTER TABLE root_list_parent RENAME COLUMN list_part_key TO list_part_key_renamed;
TRUNCATE root_list_parent;

-- Add constraint to the parent table, verify that it reflects on the child partition.
ALTER TABLE root_list_parent ADD CONSTRAINT constraint_test UNIQUE
  (list_part_key_renamed, hash_part_key, range_part_key, foo);

-- Remove constraint from the parent table, verify that it reflects on the child partition.
ALTER TABLE root_list_parent DROP CONSTRAINT constraint_test;

CREATE USER test_user;
CREATE DATABASE sqlcrossdb_0;
\c sqlcrossdb_0
SET yb_test_inval_message_portability = true;
SET log_min_messages = DEBUG1;
GRANT ALL ON SCHEMA public TO test_user;
SET SESSION AUTHORIZATION test_user;
CREATE TABLE IF NOT EXISTS tb_0 (k varchar PRIMARY KEY, v1 VARCHAR, v2 integer, v3 money, v4 JSONB,
v5 TIMESTAMP, v6 bool, v7 DATE, v8 TIME, v9 VARCHAR, v10 integer, v11 money, v12 JSONB, v13
TIMESTAMP, v14 bool, v15 DATE, v16 TIME, v17 VARCHAR, v18 integer, v19 money, v20 JSONB, v21
TIMESTAMP, v22 bool, v23 DATE, v24 TIME, v25 VARCHAR, v26 integer, v27 money, v28 JSONB, v29
TIMESTAMP, v30 bool);
CREATE MATERIALIZED VIEW mv2 as SELECT k from tb_0 limit 10000;
REFRESH MATERIALIZED VIEW mv2;
DROP MATERIALIZED VIEW mv2;
CREATE VIEW view2_tb_0 AS SELECT k from tb_0;
DROP VIEW view2_tb_0;
CREATE TABLE tempTable2 AS SELECT * FROM tb_0 limit 1000000;
CREATE INDEX idx2 ON tempTable2(k);
ALTER TABLE tb_0 ADD newColumn4 TEXT DEFAULT 'dummyString';
ALTER TABLE tempTable2 ADD newColumn2 TEXT DEFAULT 'dummyString';
TRUNCATE table tb_0 cascade;
DROP INDEX idx2;
DROP TABLE tempTable2;
        )#";

  auto ts_index = static_cast<int>(RandomUniformInt(0UL, cluster_->num_tablet_servers() - 1));
  auto hostport = cluster_->ysql_hostport(ts_index);
  YsqlshRunner ysqlsh_runner = CHECK_RESULT(YsqlshRunner::GetYsqlshRunner(hostport));
  auto output = CHECK_RESULT(ysqlsh_runner.ExecuteSqlScript(
      sample_ddl_script, "sample_ddl" /* tmp_file_prefix */));
  LOG(INFO) << "output: " << output;
  auto query = "SELECT current_version, encode(messages, 'hex') "
               "FROM pg_yb_invalidation_messages"s;
  auto conn = ASSERT_RESULT(Connect());
  auto result = ASSERT_RESULT(conn.FetchAllAsString(query));
  auto fingerprint = HashUtil::MurmurHash2_64(result.data(), result.size(), 0 /* seed */);
  LOG(INFO) << "result.size(): " << result.size();
  LOG(INFO) << "fingerprint: " << fingerprint;
  ASSERT_EQ(result.size(), 54564U);
  ASSERT_EQ(fingerprint, 11398401310271930677UL);
}

TEST_F(PgCatalogVersionTest, InvalMessageAlterTableRefreshTest) {
  RestartClusterWithInvalMessageEnabled();
  auto conn1 = ASSERT_RESULT(EnableCacheEventLog(Connect()));
  auto conn2 = ASSERT_RESULT(EnableCacheEventLog(Connect(true /* simple_query_protocol */)));
  ASSERT_OK(conn1.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn2.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn1.Execute("CREATE TABLE foo(id INT PRIMARY KEY)"));
  auto query = "SELECT id FROM foo"s;
  auto result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  for (int i = 0; i < 10; i++) {
    ASSERT_OK(conn1.ExecuteFormat("ALTER TABLE foo ADD COLUMN val$0 TEXT", i));
    // Immediately execute the query on conn2 so that we can have
    // "schema version mismatch". Verify that we still do incremental catalog
    // cache refresh in error handling code path.
    result = ASSERT_RESULT(conn2.FetchAllAsString(query));
  }
  // Verify that the incremental catalog cache refresh happened on conn2.
  VerifyCatCacheRefreshMetricsHelper(0 /* num_full_refreshes */, 10 /* num_delta_refreshes */);
}

TEST_F(PgCatalogVersionTest, InvalMessageLocalCatalogVersion) {
  RestartClusterWithInvalMessageEnabled();
  InvalMessageLocalCatalogVersionHelper();
}

TEST_F(PgCatalogVersionTest, InvalMessageGarbageCollection) {
  RestartClusterWithInvalMessageEnabled(
      { "--check_lagging_catalog_versions_interval_secs=5" });
  InvalMessageLocalCatalogVersionHelper();
}

class PgCatalogVersionHasCatalogWriteTest
    : public PgCatalogVersionTest,
      public ::testing::WithParamInterface<bool> {
};

INSTANTIATE_TEST_CASE_P(PgCatalogVersionHasCatalogWriteTest,
                        PgCatalogVersionHasCatalogWriteTest,
                        ::testing::Values(false, true));

TEST_P(PgCatalogVersionHasCatalogWriteTest, WriteUserTableInsideDdlEventTrigger) {
  const bool enable_inval_messages = GetParam();
  enable_inval_messages ? RestartClusterWithInvalMessageEnabled()
                        : RestartClusterWithInvalMessageDisabled();
  auto conn_yugabyte = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  const string query =
        R"#(
-- Create a table to store DDL history
-- this DDL does not increment catalog version
CREATE TABLE ddl_history (
    id SERIAL PRIMARY KEY,
    ddl_date TIMESTAMP WITH TIME ZONE,
    ddl_tag TEXT,
    object_name TEXT
);

-- Create the log_ddl function (example)
-- this DDL does not increment catalog version
CREATE FUNCTION log_ddl()
RETURNS event_trigger AS $$
DECLARE
    obj record;
BEGIN
    obj := pg_event_trigger_ddl_commands();
    INSERT INTO ddl_history (ddl_date, ddl_tag, object_name)
    VALUES (statement_timestamp(), tg_tag, obj.object_identity);
END;
$$ LANGUAGE plpgsql;

-- Create an event trigger to execute log_ddl on DDL events
-- this DDL does increment catalog version by 1
CREATE EVENT TRIGGER ddl_event_log
ON ddl_command_end
EXECUTE PROCEDURE log_ddl();
        )#";
  ASSERT_OK(conn_yugabyte.Execute(query));
  ASSERT_OK(conn_yugabyte.Execute("SET log_min_messages = DEBUG1"));
  // The first GRANT statement even though could have been a no-op, it writes
  // to pg_attribute table and updated a NULL value to {yugabyte=r/postgres}.
  // So it indeed has written a catalog table. That's why the catalog version
  // is incremented by 1, from 2 to 3.
  ASSERT_OK(conn_yugabyte.Execute(
      "GRANT SELECT (rolname, rolsuper) ON pg_authid TO CURRENT_USER"));
  auto v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 3);
  // The next GRANT statement is a no-op because it is identical to the first GRANT.
  // However we used to increment the catalog version because of the INSERT inside
  // function log_ddl() which is executed as part of the GRANT statement so the GRANT
  // was detected to have made writes. This test ensures that we are now able to more
  // acurately detect that the GRANT statement has not made any catalog table writes.
  // Therefore the sys catalog has not changed and we do not need to increment the
  // catalog version.
  ASSERT_OK(conn_yugabyte.Execute(
      "GRANT SELECT (rolname, rolsuper) ON pg_authid TO CURRENT_USER"));
  v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 3);
}

// We have made a special case to allow expression pushdown for table pg_yb_catalog_version
// in order to ensure continued support of cross-database concurrent DDLs. Without expression
// pushdown PG would read all the rows of pg_yb_catalog_version in order to check for not null
// constraint on column current_version and column last_breaking_version. Reading all the rows
// defeats concurrent cross-database DDLs which would otherwise operate on different rows without
// conflicts. This test verifies our pushdown special case does not incorrectly allow a null
// value gets inserted into the table pg_yb_catalog_version.
TEST_F(PgCatalogVersionTest, NotNullConstraint) {
  const string query =
        R"#(
CREATE OR REPLACE FUNCTION foo(amount INT) RETURNS VOID AS
$$
  UPDATE pg_yb_catalog_version SET current_version = current_version + amount WHERE db_oid = 1;
$$ LANGUAGE SQL;
SET enable_seqscan = off;
SET yb_non_ddl_txn_for_sys_tables_allowed=1;
        )#";
  auto conn = ASSERT_RESULT(ConnectToDB(kYugabyteDatabase));
  ASSERT_OK(conn.Execute(query));
  auto status = conn.Execute("SELECT foo(null)");
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "null value in column \"current_version\" of relation "
                                         "\"pg_yb_catalog_version\" violates not-null constraint");
  auto expected = "1, 1, 1"s;
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM pg_yb_catalog_version WHERE db_oid = 1"));
  ASSERT_EQ(expected, result);
}

// Test YSQL upgrade where we can directly write to catalog tables using DML
// statements under the GUC yb_non_ddl_txn_for_sys_tables_allowed=1. These
// DML statements do generate invalidation messages. We make the COMMIT statement
// in a YSQL migrate script to be a DDL so that we can capture the messages
// generated by these DML statements.
TEST_F(PgCatalogVersionTest, InvalMessageYsqlUpgradeCommit1) {
  RestartClusterWithInvalMessageEnabled();
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_yugabyte.Execute("SET log_min_messages = DEBUG1"));
  // Use snapshot isolation mode during YSQL upgrade. This is needed as a simple work
  // around so that we do not start subtransactions during YSQL upgrade. Otherwise the
  // COMMIT will only capture the invalidation messages generated by the last DML statement
  // preceding the COMMIT statement.
  ASSERT_OK(conn_yugabyte.Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
  auto v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 1);
  string migrate_sql = "SET yb_non_ddl_txn_for_sys_tables_allowed=1;\n";
  // We directly make an update to pg_class that will generate 1 invalidation message.
  // Write for a random number of times, and verify we have captured the same number
  // of messages by the COMMIT statement.
  const auto inval_message_count = RandomUniformInt(1, 100);
  LOG(INFO) << "inval_message_count: " << inval_message_count;
  for (int i = 0; i < inval_message_count; ++i) {
    // The nested BEGIN; does not have any effect other than causing a warning messages
    // WARNING:  there is already a transaction in progress
    // However if we allow YSQL upgrade to run in read committed isolation, then
    // each statement will start a subtransaction which prevents the final COMMIT
    // statement to catpure all the invalidation messages. For now we disallow YSQL
    // upgrade to run in read committed isolation to avoid that.
    migrate_sql += "BEGIN;\nUPDATE pg_class SET relam = 2 WHERE oid = 8010;\n";
  }
  migrate_sql += "COMMIT;\n";
  ASSERT_OK(conn_yugabyte.Execute("SET ysql_upgrade_mode TO true"));
  ASSERT_OK(conn_yugabyte.Execute(migrate_sql));
  // The migrate sql is run under YSQL upgrade mode. Therefore the COMMIT is
  // considered as a DDL and causes catalog version to increment.
  v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 2);
  const auto count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_invalidation_messages"));
  ASSERT_EQ(count, 1);
  auto query = "SELECT encode(messages, 'hex') FROM pg_yb_invalidation_messages "
               "WHERE current_version=$0"s;
  auto result2 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 2)));
  // Each invalidation messages is 24 bytes, in hex is 48 bytes.
  ASSERT_EQ(result2.size(), inval_message_count * 48U);
  // Make sure we only have simple usage of COMMIT in a migration script. PG allows
  // COMMIT inside a an anonymous code block, in YSQL upgrade we do not allow.
  migrate_sql =
        R"#(
DO $$
BEGIN
    UPDATE pg_class SET relam = 2 WHERE oid = 8010;
    COMMIT;
END$$;
        )#";
  auto status = conn_yugabyte.Execute(migrate_sql);
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "invalid transaction termination");
  ASSERT_OK(conn_yugabyte.Execute("ROLLBACK"));
  // PG also allows COMMIT inside a procedure that is invoked via CALL statement.
  // In YSQL upgrade we do not allow.
  migrate_sql =
        R"#(
CREATE OR REPLACE PROCEDURE myproc() AS
$$
BEGIN
    UPDATE pg_class SET relam = 2 WHERE oid = 8010;
    COMMIT;
END $$ LANGUAGE 'plpgsql';
CALL myproc();
        )#";
  status = conn_yugabyte.Execute(migrate_sql);
  ASSERT_TRUE(status.IsNetworkError()) << status;
  ASSERT_STR_CONTAINS(status.ToString(), "invalid transaction termination");
}

TEST_F(PgCatalogVersionTest, InvalMessageYsqlUpgradeCommit2) {
  RestartClusterWithInvalMessageEnabled();
  // Prepare the test setup by reverting
  // V75__26335__pg_set_relation_stats.sql
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_yugabyte.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn_yugabyte.Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
  auto v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 1);
  const string setup_sql =
        R"#(
BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
DELETE FROM pg_catalog.pg_proc WHERE oid in (8091, 8092, 8093, 8094);
DELETE FROM pg_catalog.pg_description WHERE objoid in (8091, 8092, 8093, 8094)
    AND classoid = 1255 AND objsubid = 0;
COMMIT;
        )#";
  ASSERT_OK(conn_yugabyte.Execute(setup_sql));
  // The setup sql is not run under YSQL upgrade mode. Therefore its COMMIT is
  // considered as a DML and does not cause catalog version to increment.
  v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 1);

  // Now run the migrate sql under YSQL upgrade mode:
  // V75__26335__pg_set_relation_stats.sql
  const string migrate_sql =
    ReadMigrationFile("V75__26335__pg_set_relation_stats.sql");
  ASSERT_OK(conn_yugabyte.Execute("SET ysql_upgrade_mode TO true"));
  ASSERT_OK(conn_yugabyte.Execute(migrate_sql));
  // The migrate sql is run under YSQL upgrade mode. Therefore each COMMIT is
  // considered as a DDL and causes catalog version to increment.
  v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 5);
  const auto count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_invalidation_messages"));
  ASSERT_EQ(count, 4);
  auto query = "SELECT encode(messages, 'hex') FROM pg_yb_invalidation_messages "
               "WHERE current_version=$0"s;

  // version 2 messages.
  auto result2 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 2)));
  ASSERT_EQ(result2.size(), 144U);

  // version 3 messages.
  auto result3 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 3)));
  ASSERT_EQ(result3.size(), 144U);

  // version 4 messages.
  auto result4 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 4)));
  ASSERT_EQ(result4.size(), 144U);

  // version 5 messages.
  auto result5 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 5)));
  ASSERT_EQ(result5.size(), 144U);
}

TEST_F(PgCatalogVersionTest, InvalMessageYsqlUpgradeCommit3) {
  RestartClusterWithInvalMessageEnabled();
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_yugabyte.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn_yugabyte.Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
  auto v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 1);

  // Now run the migrate sql under YSQL upgrade mode.
  // V77__26590__query_id_yb_terminated_queries_view.sql
  const string migrate_sql =
    ReadMigrationFile("V77__26590__query_id_yb_terminated_queries_view.sql");
  ASSERT_OK(conn_yugabyte.Execute("SET ysql_upgrade_mode TO true"));
  ASSERT_OK(conn_yugabyte.Execute(migrate_sql));
  // The migrate sql is run under YSQL upgrade mode. Therefore its COMMIT is
  // considered as a DDL. There are two COMMIT statements. The first COMMIT
  // has got invalidation messages so it causes catalog version to increment
  // from 1 to 2. Then the DROP VIEW statement causes catalog version to
  // increment from 2 to 3, the next CREATE OR REPLACE VIEW statement causes
  // catalog version to increment from 3 to 4. The last COMMIT statement got
  // 1 invalidation messages because even though there is no catalog table
  // writes between the CREATE OR REPLACE VIEW and the last COMMIT, the call
  // to increment catalog version does generate one message that is not
  // captured by the call itself. Therefore the last COMMIT still causes
  // catalog version to increment.
  v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 5);
  const auto count = ASSERT_RESULT(conn_yugabyte.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_invalidation_messages"));
  ASSERT_EQ(count, 4);
  auto query = "SELECT encode(messages, 'hex') FROM pg_yb_invalidation_messages "
               "WHERE current_version=$0"s;

  // version 2 messages.
  auto result2 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 2)));
  ASSERT_EQ(result2.size(), 144U);

  // version 3 messages.
  auto result3 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 3)));
  ASSERT_EQ(result3.size(), 1248U);

  // version 4 messages.
  auto result4 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 4)));
  ASSERT_EQ(result4.size(), 1344U);

  // version 5 messages.
  auto result5 = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(Format(query, 5)));
  ASSERT_EQ(result5.size(), 48U);
}

TEST_F(PgCatalogVersionTest, InvalMessageYsqlUpgradeCommit4) {
  RestartClusterWithInvalMessageEnabled();
  // Prepare the test setup by reverting
  // V78__26645__yb_binary_upgrade_set_next_pg_enum_sortorder.sql
  auto conn_yugabyte = ASSERT_RESULT(Connect());
  ASSERT_OK(conn_yugabyte.Execute("SET log_min_messages = DEBUG1"));
  ASSERT_OK(conn_yugabyte.Execute("SET DEFAULT_TRANSACTION_ISOLATION TO \"REPEATABLE READ\""));
  auto v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 1);
  const string setup_sql =
        R"#(
BEGIN;
SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
DELETE FROM pg_catalog.pg_proc WHERE oid = 8095;
DELETE FROM pg_catalog.pg_description WHERE objoid = 8095 AND classoid = 1255 AND objsubid = 0;
COMMIT;
        )#";
  ASSERT_OK(conn_yugabyte.Execute(setup_sql));
  // The setup sql is not run under YSQL upgrade mode. Therefore its COMMIT is
  // considered as a DML and does not cause catalog version to increment.
  v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 1);

  // Now run the migrate sql under YSQL upgrade mode:
  // V78__26645__yb_binary_upgrade_set_next_pg_enum_sortorder.sql
  const string migrate_sql =
    ReadMigrationFile("V78__26645__yb_binary_upgrade_set_next_pg_enum_sortorder.sql");
  ASSERT_OK(conn_yugabyte.Execute("SET ysql_upgrade_mode TO true"));
  ASSERT_OK(conn_yugabyte.Execute(migrate_sql));
  // The migrate sql is run under YSQL upgrade mode. Therefore its COMMIT is
  // considered as a DDL and causes catalog version to increment.
  v = ASSERT_RESULT(GetCatalogVersion(&conn_yugabyte));
  ASSERT_EQ(v, 2);
  auto query = "SELECT encode(messages, 'hex') FROM pg_yb_invalidation_messages"s;
  auto result = ASSERT_RESULT(conn_yugabyte.FetchAllAsString(query));
  // The migrate script has generated 3 messages:
  // 1 SharedInvalCatcacheMsg for PROCNAMEARGSNSP
  // 1 SharedInvalCatcacheMsg for PROCOID
  // 1 SharedInvalSnapshotMsg for pg_description
  // each messages is 24 raw bytes and 48 bytes in 'hex' (48 * 3 = 144).
  ASSERT_EQ(result.size(), 144U);
}

// https://github.com/yugabyte/yugabyte-db/issues/27170
TEST_F(PgCatalogVersionTest, InvalMessageDuplicateVersion) {
  RestartClusterWithInvalMessageEnabled(
      { "--check_lagging_catalog_versions_interval_secs=1" });
  // Make two connections on two different nodes.
  pg_ts = cluster_->tablet_server(0);
  auto conn1 = ASSERT_RESULT(Connect());
  pg_ts = cluster_->tablet_server(1);
  auto conn2 = ASSERT_RESULT(Connect());
  // Let two concurrent DDLs operate on two tables to avoid any concurrent DDL related
  // errors to interfere and prevent the case that we are trying to contrive.
  ASSERT_OK(conn1.Execute("CREATE TABLE foo(id INT)"));
  ASSERT_OK(conn1.Execute("CREATE TABLE bar(id INT)"));
  ASSERT_OK(conn1.Execute("SET yb_test_delay_set_local_tserver_inval_message_ms = 3000"));
  TestThreadHolder thread_holder;
  auto ddl1 = "ALTER TABLE foo ADD COLUMN val TEXT"s;
  auto ddl2 = "ALTER TABLE bar ADD COLUMN val TEXT"s;
  thread_holder.AddThreadFunctor([&conn2, &ddl2] {
    // Delay 1s so that conn1's ddl1 is executed first.
    SleepFor(1s);
    // Statement ddl2 leads to version 3.
    ASSERT_OK(conn2.Execute(ddl2));
  });

  // Execute ddl1 on conn1 that increments the catalog version. The 3-second delay caused by
  // SET yb_test_delay_set_local_tserver_inval_message_ms = 3000 will be long enough for ddl2
  // on conn2 to complete, and heartbeat should happen to propagate the new version of ddl1
  // and the new version of ddl2 to the local tserver.
  // Statement ddl1 leads to version 2.
  ASSERT_OK(conn1.Execute(ddl1));

  // This wait is needed to reproduce the bug 27170 so that we don't jump to the next
  // query right away which will trigger calling TabletServer::GetTserverCatalogMessageLists
  // that also detects the duplication of version 2, causing tserver to FATAL differently
  // from what we expect to see as in GHI 27170.
  SleepFor(5s);

  // In pg_yb_invalidation_messages we should see two rows for DB yugabyte: version 2 and
  // version 3 because version 2 has not expired yet when version 3 was inserted.
  const auto count = ASSERT_RESULT(conn2.FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_invalidation_messages"));
  ASSERT_EQ(count, 2);
  thread_holder.Stop();
}

} // namespace pgwrapper
} // namespace yb
