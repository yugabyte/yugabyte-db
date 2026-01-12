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

#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

#include "yb/client/client-test-util.h"
#include "yb/client/table_info.h"
#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

DECLARE_string(ysql_major_upgrade_user);
namespace yb {

class YsqlMajorUpgradeTest : public YsqlMajorUpgradeTestBase {
 public:
  YsqlMajorUpgradeTest() = default;

  constexpr static auto kTemplate0 = "template0";
  constexpr static auto kTemplate1 = "template1";
  constexpr static auto kYugabyte = "yugabyte";
  constexpr static auto kPostgres = "postgres";
  constexpr static auto kSystemPlatform = "system_platform";

  // Stops the tserver running on the yb-master leader node.
  // The tserver must be restarted before the test completes for it to succeed the shutdown in the
  // success case.
  Result<ExternalTabletServer*> StopMasterLeaderTServer() {
    const auto master = cluster_->GetLeaderMaster();
    RETURN_NOT_OK(cluster_->SetFlag(
        master, "tserver_unresponsive_timeout_ms", ToString(2000 * kTimeMultiplier)));

    const auto num_tservers = cluster_->num_tablet_servers();
    size_t master_tserver_idx = num_tservers;
    const auto master_host = master->bound_rpc_addr().host();
    for (size_t i = 0; i < num_tservers; ++i) {
      if (cluster_->tablet_server(i)->bind_host() == master->bound_rpc_addr().host()) {
        master_tserver_idx = i;
        break;
      }
    }
    SCHECK_NE(
        master_tserver_idx, num_tservers, IllegalState,
        Format("Tserver not found on master host $0", master_host));

    auto master_tserver = cluster_->tablet_server(master_tserver_idx);
    master_tserver->Shutdown();
    RETURN_NOT_OK(cluster_->WaitForMasterToMarkTSDead(static_cast<int>(master_tserver_idx)));

    return master_tserver;
  }
};

TEST_F(YsqlMajorUpgradeTest, CheckVersion) {
  const auto kSelectVersion = "SELECT version()";
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto version = ASSERT_RESULT(conn.FetchRowAsString(kSelectVersion));
    ASSERT_STR_CONTAINS(version, old_version_info().version);
  }

  auto ysql_catalog_config = ASSERT_RESULT(DumpYsqlCatalogConfig());
  ASSERT_STR_NOT_CONTAINS(ysql_catalog_config, "catalog_version");

  ASSERT_OK(UpgradeClusterToMixedMode());

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    auto version = ASSERT_RESULT(conn.FetchRowAsString(kSelectVersion));
    ASSERT_STR_CONTAINS(version, current_version_info().version_number());
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    auto version = ASSERT_RESULT(conn.FetchRowAsString(kSelectVersion));
    ASSERT_STR_CONTAINS(version, old_version_info().version);
  }
  ysql_catalog_config = ASSERT_RESULT(DumpYsqlCatalogConfig());
  ASSERT_STR_NOT_CONTAINS(ysql_catalog_config, "catalog_version");

  // We should not be allowed to finalize before upgrading all tservers.
  ASSERT_NOK_STR_CONTAINS(
      FinalizeYsqlMajorCatalogUpgrade(),
      "Cannot finalize YSQL major catalog upgrade before all yb-tservers have been upgraded to the "
      "current version: yb-tserver(s) not on the correct version");
  // We should not be allowed to rollback before rolling back all tservers.
  ASSERT_NOK_STR_CONTAINS(
      RollbackYsqlMajorCatalogVersion(),
      "Cannot rollback YSQL major catalog while yb-tservers are running on a newer YSQL major "
      "version: yb-tserver(s) not on the correct version");

  ASSERT_NOK_STR_CONTAINS(
      PromoteAutoFlags(),
      "Cannot promote non-volatile AutoFlags before YSQL major catalog upgrade is complete");

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto version = ASSERT_RESULT(conn.FetchRowAsString(kSelectVersion));
    ASSERT_STR_CONTAINS(version, current_version_info().version_number());
  }

  ysql_catalog_config = ASSERT_RESULT(DumpYsqlCatalogConfig());
  ASSERT_STR_CONTAINS(ysql_catalog_config, "catalog_version: 15");
}

TEST_F(YsqlMajorUpgradeTest, SimpleTableUpgrade) { ASSERT_OK(TestUpgradeWithSimpleTable()); }

TEST_F(YsqlMajorUpgradeTest, SimpleTableRollback) {
  ASSERT_OK(TestRollbackWithSimpleTable());

// Disabled the re-upgrade step on debug builds because it times out.
#if defined(NDEBUG)
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  ASSERT_OK(InsertRowInSimpleTableAndValidate());
#endif
}

TEST_F(YsqlMajorUpgradeTest, BackslashD) {
  ASSERT_OK(ExecuteStatement("CREATE TABLE t (a INT)"));
  static const auto kBackslashD = "\\d";
  static const auto kExpectedResult =
      "        List of relations\n"
      " Schema | Name | Type  |  Owner   \n"
      "--------+------+-------+----------\n"
      " public | t    | table | postgres\n"
      "(1 row)\n\n";

  auto result = ASSERT_RESULT(ExecuteViaYsqlsh(kBackslashD));
  ASSERT_EQ(result, kExpectedResult);

  ASSERT_OK(UpgradeClusterToMixedMode());

  result = ASSERT_RESULT(ExecuteViaYsqlsh(kBackslashD, kMixedModeTserverPg15));
  ASSERT_EQ(result, kExpectedResult);
  result = ASSERT_RESULT(ExecuteViaYsqlsh(kBackslashD, kMixedModeTserverPg11));
  ASSERT_EQ(result, kExpectedResult);

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // Verify the result from a random tserver.
  result = ASSERT_RESULT(ExecuteViaYsqlsh(kBackslashD));
  ASSERT_EQ(result, kExpectedResult);
}

TEST_F(YsqlMajorUpgradeTest, CreateTableOf) {
  static const auto t1 = "t1_pg11";
  static const auto t2 = "t2_pg11";
  static const auto t3 = "t3_pg15";
  static const auto t4 = "t4_pg15";
  static const auto type_name = "ty";

  ASSERT_OK(ExecuteStatements({
      Format("CREATE TYPE $0 AS (a INT)", type_name),
      Format("CREATE TABLE $0 OF $1", t1, type_name),
      Format("CREATE TABLE $0 (a INT)", t2),
  }));

  std::map<std::string, bool> table_is_typed = {{t1, true}, {t2, false}};

  const auto check_tables = [this, &table_is_typed] (std::optional<size_t> tserver_idx) {
    const auto kTypeTable = Format("Typed table of type: $0\n", type_name);
    const auto kExpectedOutput =
        "              Table \"public.$0\"\n"
        " Column |  Type   | Collation | Nullable | Default \n"
        "--------+---------+-----------+----------+---------\n"
        " a      | integer |           |          | \n"
        "$1\n";

    for (const auto &[table, is_typed] : table_is_typed) {
      auto result = ASSERT_RESULT(ExecuteViaYsqlsh(Format("\\d $0", table), tserver_idx));
      ASSERT_EQ(result, Format(kExpectedOutput, table, is_typed ? kTypeTable : ""));
    }
  };

  ASSERT_NO_FATALS(check_tables(kAnyTserver));

  ASSERT_OK(UpgradeClusterToMixedMode());

  for (const auto tserver_idx : {kMixedModeTserverPg11, kMixedModeTserverPg15})
    ASSERT_NO_FATALS(check_tables(tserver_idx));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  ASSERT_NO_FATALS(check_tables(kAnyTserver));

  ASSERT_OK(ExecuteStatements({
      Format("ALTER TABLE $0 NOT OF", t1),
      Format("ALTER TABLE $0 OF $1", t2, type_name),
      Format("CREATE TABLE $0 OF $1", t3, type_name),
      Format("CREATE TABLE $0 (a INT)", t4),
  }));

  table_is_typed[t1] = false;
  table_is_typed[t2] = true;
  table_is_typed[t3] = true;
  table_is_typed[t4] = false;

  ASSERT_NO_FATALS(check_tables(kAnyTserver));
}

TEST_F(YsqlMajorUpgradeTest, Comments) {
  const auto kPg11DatabaseComment = "PG11: [db] I came first!";
  const auto kPg11TableComment = "PG11: [table] I came first!";
  ASSERT_OK(ExecuteStatements(
      {"CREATE TABLE t (a int)",
       Format("COMMENT ON DATABASE yugabyte IS '$0'", kPg11DatabaseComment),
       Format("COMMENT ON TABLE t IS '$0'", kPg11TableComment)}));

  ASSERT_OK(UpgradeClusterToMixedMode());

  const auto kSelectDatabaseComment =
      "SELECT pg_catalog.shobj_description(d.oid, 'pg_database') FROM pg_catalog.pg_database d "
      "WHERE datname = 'yugabyte'";
  const auto kSelectTableComment =
      "SELECT description from pg_description JOIN pg_class on pg_description.objoid = "
      "pg_class.oid WHERE relname = 't'";

  auto check_pg11_comment = [&](pgwrapper::PGConn& conn) {
    auto comment = ASSERT_RESULT(conn.FetchRow<std::string>(kSelectDatabaseComment));
    ASSERT_EQ(comment, kPg11DatabaseComment);
    comment = ASSERT_RESULT(conn.FetchRow<std::string>(kSelectTableComment));
    ASSERT_EQ(comment, kPg11TableComment);
  };

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(check_pg11_comment(conn));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(check_pg11_comment(conn));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // Check the comment from a random tserver.
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(check_pg11_comment(conn));
  }

  // Update the comment.
  const auto kPg15DatabaseComment = "PG15: [db] I am better than you!";
  const auto kPg15TableComment = "PG15: [table] I am better than you!";
  ASSERT_OK(ExecuteStatements(
      {Format("COMMENT ON DATABASE yugabyte IS '$0'", kPg15DatabaseComment),
       Format("COMMENT ON TABLE t IS '$0'", kPg15TableComment)}));

  // Check the new comment from a random tserver.
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto comment = ASSERT_RESULT(conn.FetchRow<std::string>(kSelectDatabaseComment));
    ASSERT_EQ(comment, kPg15DatabaseComment);
    comment = ASSERT_RESULT(conn.FetchRow<std::string>(kSelectTableComment));
    ASSERT_EQ(comment, kPg15TableComment);
  }
}

TEST_F(YsqlMajorUpgradeTest, Schemas) {
  const auto kSchemaA = "schema_a";
  const auto kSchemaB = "schema_b";
  const auto kPublic = "public";

  const auto kSchemaATable = "s_schema_table_a";
  const auto kSchemaBTable = "s_schema_table_b";
  const auto kDefaultTable = "d_schema_table";
  const auto kDefaultTable2 = "d_schema_table_2";
  const auto kPublicTable = "p_schema_table";
  const auto kPublicTable2 = "p_schema_table_2";

  // This query returns rows in the format "schema.table"
  static const auto kGetTables =
      Format("SELECT nspname || '.' || relname FROM pg_class c "
             "JOIN pg_namespace n ON c.relnamespace = n.oid "
             "WHERE nspname IN ('$0', '$1', '$2') AND relname LIKE '%%schema_table%%' "
             "ORDER BY nspname, relname ASC",
             kSchemaA, kSchemaB, kPublic);

  // YB_TODO: When `CREATE SCHEMA` is the first command in this sequence, it fails with the error:
  // ERROR:  this ddl statement is currently not allowed
  // DETAIL:  The pg_yb_catalog_version table is not in per-database catalog version mode.
  // HINT:  Fix pg_yb_catalog_version table to per-database catalog version mode.
  // (This is before anything upgrade-related occurs)
  ASSERT_OK(ExecuteStatements(
      {Format("CREATE TABLE $0.$1 (a INT)", kPublic, kPublicTable),
       Format("CREATE SCHEMA $0", kSchemaA),
       Format("CREATE TABLE $0.$1 (a INT)", kSchemaA, kSchemaATable),
       Format("CREATE TABLE $0 (a INT)", kDefaultTable)}));

  ASSERT_OK(UpgradeClusterToMixedMode());

  auto check_tables = [&](pgwrapper::PGConn& conn) {
    const auto results = ASSERT_RESULT(conn.FetchRows<std::string>(kGetTables));
    ASSERT_EQ(results.size(), 3);
    ASSERT_STR_CONTAINS(results[0], Format("$0.$1", kPublic, kDefaultTable));
    ASSERT_STR_CONTAINS(results[1], Format("$0.$1", kPublic, kPublicTable));
    ASSERT_STR_CONTAINS(results[2], Format("$0.$1", kSchemaA, kSchemaATable));

    // Check that each table can be selected from (proving it's more than just an entry in pg_class)
    const auto joined_rows = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
      Format("SELECT COUNT(*) FROM $0.$1, $2, $3",
             kSchemaA, kSchemaATable, kDefaultTable, kPublicTable)));
    ASSERT_EQ(joined_rows, 0);
  };

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(check_tables(conn));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(check_tables(conn));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // Check the tables from a random tserver
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(check_tables(conn));
  }

  // Create a new schema and tables
  ASSERT_OK(ExecuteStatements(
      {Format("CREATE SCHEMA $0", kSchemaB),
       Format("CREATE TABLE $0.$1 (a INT)", kPublic, kPublicTable2),
       Format("CREATE TABLE $0.$1 (a INT)", kSchemaB, kSchemaBTable),
       Format("CREATE TABLE $0 (a INT)", kDefaultTable2)}));

  // Check the new tables from a random tserver
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    const auto results = ASSERT_RESULT(conn.FetchRows<std::string>(kGetTables));
    ASSERT_EQ(results.size(), 6);
    int idx = 0;
    ASSERT_STR_CONTAINS(results[idx++], Format("$0.$1", kPublic, kDefaultTable));
    ASSERT_STR_CONTAINS(results[idx++], Format("$0.$1", kPublic, kDefaultTable2));
    ASSERT_STR_CONTAINS(results[idx++], Format("$0.$1", kPublic, kPublicTable));
    ASSERT_STR_CONTAINS(results[idx++], Format("$0.$1", kPublic, kPublicTable2));
    ASSERT_STR_CONTAINS(results[idx++], Format("$0.$1", kSchemaA, kSchemaATable));
    ASSERT_STR_CONTAINS(results[idx++], Format("$0.$1", kSchemaB, kSchemaBTable));

    // Check that each table can be selected from (proving it's more than just an entry in pg_class)
    const auto joined_rows = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
        Format("SELECT COUNT(*) FROM $0.$1, $2.$3, $4, $5, $6, $7",
               kSchemaA, kSchemaATable, kSchemaB, kSchemaBTable,
               kDefaultTable, kDefaultTable2, kPublicTable, kPublicTable2)));
    ASSERT_EQ(joined_rows, 0);
  }
}

TEST_F(YsqlMajorUpgradeTest, YB_RELEASE_ONLY_TEST(MultipleDatabases)) {
  /* Cases:
   * - We support creating / altering databases to disallow connections - but neither YB nor PG
   *   support upgrading those databases. That is tested by DatabaseWithDisallowedConnections below.
   * - We support setting config values for databases
   * - We support setting owner for databases
   * - We support setting connection limits for databases
   * - We do not support tablespaces for databases:
   *   CREATE DATABASE db_ts WITH TABLESPACE ts;
   *   ERROR:  Value other than default for tablespace option is not yet supported
   * - We do not support creating new template databases:
   *   CREATE DATABASE db_template WITH IS_TEMPLATE = true;
   *   ERROR:  Value other than default or false for is_template option is not yet supported
   *
   *   ALTER DATABASE db IS_TEMPLATE true;
   *   ERROR:  Altering is_template option is not yet supported
   */

  static const auto kUser = "new_user";

  static const auto kTempFileLimitFlag = "temp_file_limit";
  static const auto kTempFileLimitValue = "123MB";
  static const auto kDefaultTempFileLimitValue = "1GB";

  static const auto kDatabaseWithConnLimit = "db_with_conn_limit";
  static const auto kDatabaseWithConfig = "db_with_config";
  static const auto kDatabaseWithOwner = "db_with_owner";

  static const auto kDefaultConnLimit = -1;
  static const auto kNewConnLimit = 10;

  static const auto kAnyTserver = std::nullopt;

  struct DbInfo {
    std::string owner;
    int conn_limit;
    std::string temp_file_limit;
    bool disallowed_conns;

    DbInfo() {
      owner = kPostgres;
      conn_limit = kDefaultConnLimit;
      temp_file_limit = kDefaultTempFileLimitValue;
      disallowed_conns = false;
    }
  };

  std::map<std::string, DbInfo> db_map;
  {
    // Populate the map with the databases and their properties.
    std::vector<std::string> db_names = {
      kTemplate0, kTemplate1, kSystemPlatform, kYugabyte, kPostgres,
      kDatabaseWithConnLimit, kDatabaseWithConfig, kDatabaseWithOwner
    };

    std::transform(db_names.begin(), db_names.end(), std::inserter(db_map, db_map.end()),
                  [](const auto db_name) { return std::make_pair(db_name, DbInfo()); });

    db_map[kTemplate0].disallowed_conns = true;
    db_map[kTemplate1].owner = kUser;
    db_map[kTemplate1].temp_file_limit = kTempFileLimitValue;
    db_map[kYugabyte].owner = kUser;
    db_map[kYugabyte].temp_file_limit = kTempFileLimitValue;
    db_map[kDatabaseWithOwner].owner = kUser;
    db_map[kDatabaseWithConnLimit].conn_limit = kNewConnLimit;
    db_map[kDatabaseWithConfig].temp_file_limit = kTempFileLimitValue;
  }

  // Create the databases with their properties. We could programmatically do this using the
  // property map, but we want to check both the CREATE and ALTER DATABASE commands.
  ASSERT_OK(ExecuteStatements({
    Format("CREATE USER $0", kUser),
    Format("CREATE DATABASE $0 WITH OWNER = $1", kDatabaseWithOwner,
           db_map[kDatabaseWithOwner].owner),
    Format("CREATE DATABASE $0 WITH CONNECTION LIMIT = $1", kDatabaseWithConnLimit,
           db_map[kDatabaseWithConnLimit].conn_limit),

    Format("CREATE DATABASE $0", kDatabaseWithConfig),
    Format("ALTER DATABASE $0 SET $1 TO '$2'", kDatabaseWithConfig, kTempFileLimitFlag,
           db_map[kDatabaseWithConfig].temp_file_limit),
  }));

  for (auto db_name : {kYugabyte, kTemplate1}) {
    ASSERT_OK(ExecuteStatements({
      Format("ALTER DATABASE $0 SET $1 = '$2'", db_name, kTempFileLimitFlag,
             db_map[db_name].temp_file_limit),
      Format("ALTER DATABASE $0 OWNER TO $1", db_name, db_map[db_name].owner)
    }));
  }

  // Create tables in some databases.
  {
    int db_number = 0;
    for (const auto& [db_name, db_info] : db_map) {
      if (db_name.starts_with("template"))
        continue;

      auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));
      ASSERT_OK(conn.Execute(Format("CREATE TABLE t (v INT)")));
      ASSERT_OK(conn.Execute(Format("INSERT INTO t VALUES ($0)", std::pow(10, db_number++))));
    }
  }
  int inserted_row_count = 1;

  // Set up assertion lambdas.
  auto add_row_check_rows = [this](const std::map<std::string, DbInfo>& dbs,
                                   const std::optional<size_t> ts_id, const int count) {
    int db_number = 0;
    for (const auto& [db_name, db_info] : dbs) {
      if (db_name.starts_with("template"))
        continue;

      auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name, ts_id));

      ASSERT_OK(conn.Execute(Format("INSERT INTO t VALUES ($0)", count * std::pow(10, db_number))));

      // Generate a vector of numbers [1 * 10^db_number, 2 * 10^db_number, 3 * 10^db_number, ...]
      std::vector<int> expected(count);
      std::generate(expected.begin(), expected.end(), [n = 1, db_number]() mutable {
          return std::pow(10, db_number) * n++;
      });
      auto result = ASSERT_RESULT(conn.FetchRows<int>("SELECT v FROM t ORDER BY v"));
      ASSERT_VECTORS_EQ(expected, result);

      db_number++;
    }
  };

  auto check_dbs = [this, &db_map](std::optional<size_t> ts_id) {
    // Check database owners and connection limits.
    {
      auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
      auto result = ASSERT_RESULT((conn.FetchRows<std::string, std::string, int>(
          "SELECT datname, rolname, datconnlimit FROM pg_database "
          "JOIN pg_roles r on r.oid = datdba "
          "ORDER BY datname")));

      decltype(result) db_owners_and_conn_limits;
      for (const auto& [db_name, db_info] : db_map) {
        db_owners_and_conn_limits.push_back({db_name, db_info.owner, db_info.conn_limit});
      }

      ASSERT_VECTORS_EQ(db_owners_and_conn_limits, result);
    }

    // Check database temp_file_limits.
    {
      for (const auto& [db_name, db_info] : db_map) {
        if (db_info.disallowed_conns)
          continue;

        // It can sometimes take a few seconds for new connections to pick up the new value.
        ASSERT_OK(WaitFor([this, &db_name = db_name, &expected = db_info.temp_file_limit]
                          () -> Result<bool> {
          auto conn = VERIFY_RESULT(cluster_->ConnectToDB(db_name));
          auto result = VERIFY_RESULT(
              conn.FetchRow<std::string>(Format("SHOW $0", kTempFileLimitFlag)));
          return result == expected;
        }, 10s, Format("$0 on $1 was not the expected value", kTempFileLimitFlag, db_name)));
      }
    }
  };

  // Validate the databases and table rows before, during, and after the upgrade.
  ASSERT_NO_FATALS(check_dbs(kAnyTserver));

  ASSERT_NO_FATALS(add_row_check_rows(db_map, kAnyTserver, ++inserted_row_count));

  ASSERT_OK(UpgradeClusterToMixedMode());

  ASSERT_NO_FATALS(check_dbs(kMixedModeTserverPg11));
  ASSERT_NO_FATALS(check_dbs(kMixedModeTserverPg15));

  ASSERT_NO_FATALS(add_row_check_rows(db_map, kMixedModeTserverPg11, ++inserted_row_count));
  ASSERT_NO_FATALS(add_row_check_rows(db_map, kMixedModeTserverPg15, ++inserted_row_count));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  ASSERT_NO_FATALS(check_dbs(kAnyTserver));

  ASSERT_NO_FATALS(add_row_check_rows(db_map, kAnyTserver, ++inserted_row_count));
}

TEST_F(YsqlMajorUpgradeTest, DatabaseWithDisallowedConnections) {
  static const auto kDatabaseDisallowedConnections = "db_with_disallowed_connections";

  ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0 WITH ALLOW_CONNECTIONS = FALSE",
                                    kDatabaseDisallowedConnections)));

  // Validate that we can't connect to the database.
  {
    auto result = cluster_->ConnectToDB(kDatabaseDisallowedConnections);
    ASSERT_NOK_STR_CONTAINS(result,
        Format("database \"$0\" is not currently accepting connections",
               kDatabaseDisallowedConnections));
  }

  // Should fail because we don't support upgrading databases that disallow connections.
  ASSERT_NOK_STR_CONTAINS(UpgradeClusterToMixedMode(),
                          "pg_upgrade' terminated with non-zero exit status");
}

TEST_F(YsqlMajorUpgradeTest, Template1) {
  /*
   * The following statements are extracted from gram.y as CREATE statements that are allowed to run
   * in template1. They are broken into multiple lists:
   *
   * Global objects: these will not be tested because they exist outside of a database:
   * - CreateCastStmt
   * - CreateGroupStmt
   * - CreateRoleStmt
   * - CreateTableSpaceStmt
   * - CreateUserStmt
   * - CreatedbStmt
   *
   * Database objects: these will not be tested because they are attributes of a database:
   * - CreatePublicationStmt
   *
   * Table objects: these will not be tested because they are attributes of a table:
   * - CreatePolicyStmt
   * - CreateTrigStmt
   * - CreateStatsStmt
   *
   * Broken statements: these will not be tested because the behavior is broken on both YB pg11 and
   * YB pg15:
   * - CreateMatViewStmt
   *
   * Complicated statements: these will be skipped for now because they are complicated to create:
   * - CreateAmStmt
   * - CreateOpClassStmt
   * - CreateOpFamilyStmt
   * - CreatePLangStmt
   *
   * The rest of the statements will be tested:
   * - CreateDomainStmt
   * - CreateEventTrigStmt
   * - CreateExtensionStmt
   * - CreateFunctionStmt
   * - CreateSchemaStmt
   */

  const auto kPg11Database = "pg11_database";
  const auto kPg15Database = "pg15_database";

  static const auto kOddIntegerDomain = "odd_integer";
  static const auto kEventTrigger = "template_event_trigger";
  static const auto kAbortCommandFunction = "abort_command";
  static const auto kExtension = "pgcrypto";
  static const auto kExtensionFunction = "SELECT gen_salt('md5')";
  static const auto kFunction = "template_function";
  static const auto kSchema = "template_schema";
  static const auto kFunctionInSchema = "template_function_in_schema";

  const auto kAnyTserver = 0;

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kTemplate1));

    ASSERT_OK(conn.ExecuteFormat("CREATE FUNCTION $0() "
                           "RETURNS INT AS $$$$ SELECT 11 $$$$ LANGUAGE sql",
                           kFunction));
    ASSERT_OK(conn.ExecuteFormat("CREATE EXTENSION $0", kExtension));

    ASSERT_OK(conn.ExecuteFormat(
        "CREATE OR REPLACE FUNCTION $0() "
        "RETURNS event_trigger LANGUAGE plpgsql AS $$$$ "
        "BEGIN RAISE EXCEPTION 'command % is disabled by event trigger', tg_tag; END; "
        "$$$$",
        kAbortCommandFunction));

    ASSERT_OK(conn.ExecuteFormat("CREATE DOMAIN $0 AS INTEGER CHECK (VALUE % 2 <> 0)",
                                 kOddIntegerDomain));
    ASSERT_OK(conn.ExecuteFormat("CREATE EVENT TRIGGER $0 "
                                 "ON ddl_command_start WHEN TAG IN ('DROP EXTENSION') "
                                 "EXECUTE FUNCTION $1()",
                                 kEventTrigger, kAbortCommandFunction));

    ASSERT_OK(conn.ExecuteFormat("CREATE SCHEMA $0", kSchema));
    ASSERT_OK(conn.ExecuteFormat("CREATE FUNCTION $0.$1() "
                                 "RETURNS INT AS $$$$ SELECT 12 $$$$ LANGUAGE sql",
                                 kSchema, kFunctionInSchema));
  }

  ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0", kPg11Database)));

  auto check_objects = [this](const std::vector<std::string>& db_names, const size_t tserver) {
    for (auto &db_name : db_names) {
      auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name, tserver));
      // Select that the function is created and can be used as expected.
      {
        auto result = ASSERT_RESULT(conn.FetchRows<int>("SELECT template_function()"));
        ASSERT_EQ(result, std::vector<int>({11}));
      }
      // Check that the domain is created and can be used as expected.
      {
        auto result = ASSERT_RESULT(conn.FetchRow<int>(Format("SELECT CAST(3 AS $0)",
                                                              kOddIntegerDomain)));
        ASSERT_EQ(result, 3);

        auto bad_cast = conn.Execute(Format("SELECT CAST(4 AS $0)", kOddIntegerDomain));
        ASSERT_NOK_STR_CONTAINS(bad_cast,
            Format("value for domain $0 violates check constraint", kOddIntegerDomain));
      }
      // Check that the extension is created and a function from the extension can be called.
      {
        ASSERT_OK(conn.Fetch(kExtensionFunction));
      }
      // Event triggers run only on DDLs, which are disallowed during upgrade. So we can't directly
      // test them, but we can check that they exist.
      {
        auto result = ASSERT_RESULT(ExecuteViaYsqlsh("\\dy", tserver, db_name));
        ASSERT_STR_CONTAINS(result, kEventTrigger);
      }
      // Check that objects created in the schema are visible only in that schema.
      {
        auto result = ASSERT_RESULT(ExecuteViaYsqlsh("\\df", tserver, db_name));
        ASSERT_STR_NOT_CONTAINS(result, kSchema);
        ASSERT_STR_NOT_CONTAINS(result, kFunctionInSchema);

        auto result_schema = ASSERT_RESULT(ExecuteViaYsqlsh(Format("\\df $0.*", kSchema),
                                                            tserver, db_name));
        ASSERT_STR_CONTAINS(result_schema, kSchema);
        ASSERT_STR_CONTAINS(result_schema, kFunctionInSchema);
      }
    }
  };

  ASSERT_NO_FATALS(check_objects({kTemplate1, kPg11Database}, kAnyTserver));

  ASSERT_OK(UpgradeClusterToMixedMode());

  for (auto tserver : {kMixedModeTserverPg11, kMixedModeTserverPg15})
    ASSERT_NO_FATALS(check_objects({kTemplate1, kPg11Database}, tserver));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0", kPg15Database)));

  ASSERT_NO_FATALS(check_objects({kTemplate1, kPg11Database, kPg15Database}, kAnyTserver));

  /*
   * Now drop the objects in each database. This validates that:
   * 1. The objects can be dropped - this is a basic check that the objects are created / upgraded
   *    correctly.
   * 2. The objects created in template1 are copied to new databases, but are NOT shared.
   */
  for (const auto db_name : {kTemplate1, kPg11Database, kPg15Database}) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));

    ASSERT_OK(conn.ExecuteFormat("DROP DOMAIN $0", kOddIntegerDomain));
    ASSERT_OK(conn.ExecuteFormat("DROP FUNCTION $0", kFunction));

    // Attempt to drop the extension. It fails to drop because the event trigger aborts the command.
    {
      auto blocked_drop = conn.ExecuteFormat("DROP EXTENSION $0", kExtension);
      ASSERT_NOK_STR_CONTAINS(blocked_drop, "command DROP EXTENSION is disabled by event trigger");
    }

    // Drop the event trigger and its function.
    {
      auto no_cascade_drop = conn.ExecuteFormat("DROP FUNCTION $0", kAbortCommandFunction);
      ASSERT_NOK_STR_CONTAINS(no_cascade_drop,
          Format("cannot drop function $0() because other objects depend on it",
                 kAbortCommandFunction));
      ASSERT_STR_CONTAINS(no_cascade_drop.ToString(), kEventTrigger);

      // CASCADE will drop the dependent objects.
      ASSERT_OK(conn.ExecuteFormat("DROP FUNCTION $0 CASCADE", kAbortCommandFunction));
    }

    // Dropping the extension succeeds now, because the event trigger was dropped above.
    ASSERT_OK(conn.ExecuteFormat("DROP EXTENSION $0", kExtension));

    // Drop the schema and function in the schema.
    {
      auto no_cascade_drop = conn.ExecuteFormat("DROP SCHEMA $0", kSchema);
      ASSERT_NOK_STR_CONTAINS(no_cascade_drop,
          Format("cannot drop schema $0 because other objects depend on it", kSchema));
      ASSERT_STR_CONTAINS(no_cascade_drop.ToString(), kFunctionInSchema);

      // CASCADE will drop the dependent objects.
      ASSERT_OK(conn.ExecuteFormat("DROP SCHEMA $0 CASCADE", kSchema));

      // Validate that the function no longer exists.
      auto all_functions = ASSERT_RESULT(ExecuteViaYsqlsh(Format("\\df *.*", kSchema), kAnyTserver,
                                                          db_name));
      ASSERT_STR_NOT_CONTAINS(all_functions, kFunctionInSchema);
    }
  }
}

TEST_F(YsqlMajorUpgradeTest, FunctionWithSemicolons) {
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute(R"(CREATE FUNCTION pg11_function() RETURNS text AS $$
                              BEGIN
                                  RETURN 'Hello from pg11';
                              END;
                              $$ LANGUAGE plpgsql;)"));
  }
  ASSERT_OK(UpgradeClusterToMixedMode());

  auto check_function = [this](const size_t tserver) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver));
    auto result = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT pg11_function()"));
    ASSERT_EQ(result, "Hello from pg11");
  };
  ASSERT_NO_FATALS(check_function(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_function(kMixedModeTserverPg11));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto result = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT pg11_function()"));
    ASSERT_EQ(result, "Hello from pg11");
  }
}

TEST_F(YsqlMajorUpgradeTest, Matviews) {
  ASSERT_OK(ExecuteStatements(
    {"CREATE TABLE t (v INT)",
      "INSERT INTO t VALUES (1),(2),(3)",
      "CREATE MATERIALIZED VIEW mv AS SELECT * FROM t",
      "INSERT INTO t VALUES (4),(5),(6)",
      "REFRESH MATERIALIZED VIEW mv"}));
  ASSERT_OK(UpgradeClusterToMixedMode());

  auto check_matviews = [this](const size_t tserver) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver));
    auto result = ASSERT_RESULT(conn.FetchRows<int32_t>("SELECT * FROM mv ORDER BY v"));
    ASSERT_VECTORS_EQ(result, (decltype(result){1, 2, 3, 4, 5, 6}));
  };
  ASSERT_NO_FATALS(check_matviews(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_matviews(kMixedModeTserverPg11));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  ASSERT_NO_FATALS(check_matviews(0));
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (7)"));
  ASSERT_OK(conn.Execute("REFRESH MATERIALIZED VIEW mv"));
  auto result = ASSERT_RESULT(conn.FetchRows<int>("SELECT * FROM mv ORDER BY v"));
  ASSERT_VECTORS_EQ(result, (decltype(result){1, 2, 3, 4, 5, 6, 7}));
}

TEST_F(YsqlMajorUpgradeTest, PartitionedTables) {
  // Set up partitioned tables
  ASSERT_OK(ExecuteStatements({
    "CREATE TABLE t_r (v INT, z TEXT, PRIMARY KEY(v ASC)) PARTITION BY RANGE (v)",
    "CREATE TABLE t_h (v INT, z TEXT, PRIMARY KEY(v ASC)) PARTITION BY HASH (v)",
    "CREATE TABLE t_l (v INT, z TEXT, PRIMARY KEY(v ASC)) PARTITION BY LIST (v)",
    "CREATE INDEX ON t_r (z)",
    "CREATE INDEX ON t_h (z)",
    "CREATE INDEX ON t_l (z)",
    "CREATE TABLE t_r_1 PARTITION OF t_r FOR VALUES FROM (1) TO (3)",
    "CREATE TABLE t_r_default (z TEXT, v INT NOT NULL)",
    "ALTER TABLE t_r ATTACH PARTITION t_r_default DEFAULT",
    "CREATE TABLE t_h_1 PARTITION OF t_h FOR VALUES WITH (MODULUS 2, REMAINDER 0)",
    "CREATE TABLE t_h_default (z TEXT, v INT NOT NULL)",
    "ALTER TABLE t_h ATTACH PARTITION t_h_default FOR VALUES WITH (MODULUS 2, REMAINDER 1)",
    "CREATE TABLE t_l_1 PARTITION OF t_l FOR VALUES IN (1, 2)",
    "CREATE TABLE t_l_default (z TEXT, v INT NOT NULL)",
    "ALTER TABLE t_l ATTACH PARTITION t_l_default DEFAULT",
    "INSERT INTO t_r VALUES (1, 'one'), (2, 'two'), (3, 'three')",
    "INSERT INTO t_h VALUES (1, 'one'), (2, 'two'), (3, 'three')",
    "INSERT INTO t_l VALUES (1, 'one'), (2, 'two'), (3, 'three')"
  }));

  // Perform some rewrites
  for (const auto& table : {"t_r", "t_h", "t_l"}) {
    ASSERT_OK(ExecuteStatements({
    Format("ALTER TABLE $0 ALTER COLUMN z TYPE text USING (z || v)", table),
    Format("ALTER TABLE $0 DROP CONSTRAINT $0_pkey", table),
    Format("ALTER TABLE $0 ADD PRIMARY KEY (v ASC)", table)
    }));
  }

  enum class CheckType {
    Initial,
    MixedMode,
    AfterUpgrade
  };

  auto check_partitions = [&](pgwrapper::PGConn& conn, CheckType check_type) {
    for (const auto& table : {"t_r", "t_h", "t_l"}) {
      auto result = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
          Format("SELECT * FROM $0 ORDER BY v", table))));

      // Expected rows for each check type:
      // Initial:
      // | v | z     |
      // |---|-------|
      // | 1 | one1  |
      // | 2 | two2  |
      // | 3 | three3|
      //
      // MixedMode:
      // | v | z     |
      // |---|-------|
      // | 1 | one1  |
      // | 2 | two2  |
      // | 3 | three3|
      // | 4 | four4 |
      //
      // AfterUpgrade:
      // | v | z     |
      // |---|-------|
      // | 1 | one1  |
      // | 2 | two2  |
      // | 3 | three3|
      // | 4 | four4 |
      // | 7 | seven7|
      if (check_type == CheckType::Initial) {
        ASSERT_VECTORS_EQ(result, (decltype(result){{1, "one1"}, {2, "two2"}, {3, "three3"}}));
      } else if (check_type == CheckType::MixedMode) {
        ASSERT_VECTORS_EQ(result, (decltype(result){{1, "one1"}, {2, "two2"}, {3, "three3"},
            {4, "four4"}}));
      } else if (check_type == CheckType::AfterUpgrade) {
        ASSERT_VECTORS_EQ(result, (decltype(result){{1, "one1"}, {2, "two2"}, {3, "three3"},
            {4, "four4"}, {7, "seven7"}}));
      }

      // Expected rows for all check types in the first partition:
      // | v | z     |
      // |---|-------|
      // | 1 | one1  |
      // | 2 | two2  |
      auto result_partition_1 = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
          Format("SELECT * FROM $0_1 ORDER BY v", table))));
      ASSERT_VECTORS_EQ(result_partition_1,
          (decltype(result_partition_1){{1, "one1"}, {2, "two2"}}));

      // Expected rows for all check types in the default partition:
      // Initial:
      // | z     | v |
      // |-------|---|
      // | three3| 3 |
      //
      // MixedMode:
      // | z     | v |
      // |-------|---|
      // | three3| 3 |
      // | four4 | 4 |
      //
      // AfterUpgrade:
      // for t_h_default:
      // | z     | v |
      // |-------|---|
      // | three3| 3 |
      // | four4 | 4 |
      // | seven7| 7 |
      // for t_r_default and t_l_default:
      // | z     | v |
      // |-------|---|
      // | three3| 3 |
      // | seven7| 7 |
      // for t_r_2 and t_l_2:
      // | z     | v |
      // |-------|---|
      // | four4 | 4 |
      auto result_partition_default = ASSERT_RESULT((conn.FetchRows<std::string, int32_t>(
        Format("SELECT * FROM $0_default ORDER BY v", table))));
      if (check_type == CheckType::Initial) {
        ASSERT_VECTORS_EQ(result_partition_default,
            (decltype(result_partition_default){{"three3", 3}}));
      } else if (check_type == CheckType::MixedMode) {
        ASSERT_VECTORS_EQ(result_partition_default,
            (decltype(result_partition_default){{"three3", 3}, {"four4", 4}}));
      } else if (check_type == CheckType::AfterUpgrade) {
        if (strcmp(table, "t_h") == 0) {
          ASSERT_VECTORS_EQ(result_partition_default,
              (decltype(result_partition_default){{"three3", 3}, {"four4", 4}, {"seven7", 7}}));
        } else {
          ASSERT_VECTORS_EQ(result_partition_default,
              (decltype(result_partition_default){{"three3", 3}, {"seven7", 7}}));
          auto result_partition_2 = ASSERT_RESULT((conn.FetchRows<int32_t, std::string>(
              Format("SELECT * FROM $0_2 ORDER BY v", table))));
          ASSERT_VECTORS_EQ(result_partition_2, (decltype(result_partition_2){{4, "four4"}}));
        }
      }

      auto result_z = ASSERT_RESULT((conn.FetchRows<std::string>(
          Format("SELECT z FROM $0 WHERE z = 'three3'", table))));
      ASSERT_VECTORS_EQ(result_z, (decltype(result_z){"three3"}));
    }
  };

  ASSERT_OK(UpgradeClusterToMixedMode());
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(check_partitions(conn, CheckType::Initial));
    // Insert new rows
    ASSERT_OK(conn.Execute("INSERT INTO t_r VALUES (4, 'four4')"));
    ASSERT_OK(conn.Execute("INSERT INTO t_h VALUES (4, 'four4')"));
    ASSERT_OK(conn.Execute("INSERT INTO t_l VALUES (4, 'four4')"));

    // Check partitions again with the new rows
    ASSERT_NO_FATALS(check_partitions(conn, CheckType::MixedMode));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(check_partitions(conn, CheckType::MixedMode));
    // Delete new rows
    ASSERT_OK(conn.Execute("DELETE FROM t_r WHERE v = 4"));
    ASSERT_OK(conn.Execute("DELETE FROM t_h WHERE v = 4"));
    ASSERT_OK(conn.Execute("DELETE FROM t_l WHERE v = 4"));
    ASSERT_NO_FATALS(check_partitions(conn, CheckType::Initial));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute("CREATE TABLE t_r_2 PARTITION OF t_r FOR VALUES FROM (4) TO (6)"));
    ASSERT_OK(conn.Execute("CREATE TABLE t_l_2 PARTITION OF t_l FOR VALUES IN (4, 5)"));
    ASSERT_OK(conn.Execute("INSERT INTO t_r VALUES (4, 'four4'), (7, 'seven7')"));
    ASSERT_OK(conn.Execute("INSERT INTO t_h VALUES (4, 'four4'), (7, 'seven7')"));
    ASSERT_OK(conn.Execute("INSERT INTO t_l VALUES (4, 'four4'), (7, 'seven7')"));
    ASSERT_NO_FATALS(check_partitions(conn, CheckType::AfterUpgrade));
  }
}

TEST_F(YsqlMajorUpgradeTest, ColocatedTables) {
  ASSERT_OK(ExecuteStatement("CREATE DATABASE colo WITH COLOCATION = true"));
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB("colo"));
    ASSERT_OK(conn.Execute("CREATE TABLE t1 (k int PRIMARY KEY, v int)"));
    ASSERT_OK(conn.Execute("CREATE INDEX ON t1(v)"));
    ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 1)"));
  }
  ASSERT_OK(UpgradeClusterToMixedMode());
  auto check_index_query = [&](pgwrapper::PGConn& conn, const std::vector<int>& rows) {
    for (auto row : rows) {
      auto index_query = Format("SELECT v FROM t1 WHERE v = $0", row);
      ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(index_query)));
      auto result = ASSERT_RESULT(conn.FetchRows<int>(index_query));
      ASSERT_VECTORS_EQ(result, (decltype(result){row}));
    }
  };
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB("colo", kMixedModeTserverPg15));
    ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (2, 2)"));
    auto result = ASSERT_RESULT(conn.FetchRows<int>("SELECT k FROM t1"));
    ASSERT_VECTORS_EQ(result, (decltype(result){1, 2}));
    check_index_query(conn, {1, 2});
  }
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB("colo", kMixedModeTserverPg11));
    ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (3, 3)"));
    auto result = ASSERT_RESULT(conn.FetchRows<int>("SELECT k FROM t1"));
    ASSERT_VECTORS_EQ(result, (decltype(result){1, 2, 3}));
    check_index_query(conn, {1, 2, 3});
  }
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB("colo"));
    auto result = ASSERT_RESULT(conn.FetchRows<int>("SELECT k FROM t1"));
    ASSERT_VECTORS_EQ(result, (decltype(result){1, 2, 3}));
    check_index_query(conn, {1, 2, 3});

    ASSERT_OK(conn.Execute("CREATE TABLE t2 (k int PRIMARY KEY, v int)"));
    ASSERT_OK(conn.Execute("INSERT INTO t2 VALUES (1, 1), (2, 2), (4, 4)"));
    result = ASSERT_RESULT(conn.FetchRows<int>("SELECT k FROM t2"));
    ASSERT_VECTORS_EQ(result, (decltype(result){1, 2, 4}));
    result = ASSERT_RESULT(conn.FetchRows<int>("SELECT v FROM t2 ORDER BY v"));
    ASSERT_VECTORS_EQ(result, (decltype(result){1, 2, 4}));
  }
}

TEST_F(YsqlMajorUpgradeTest, Tablegroup) {
  ASSERT_OK(ExecuteStatements({
    "CREATE USER user_1",
    "GRANT CREATE ON SCHEMA public TO user_1",
    "CREATE TABLEGROUP test_grant",
    "GRANT ALL ON TABLEGROUP test_grant TO user_1",
    "SET ROLE user_1",
    "CREATE TABLE e(i text) TABLEGROUP test_grant",
    "CREATE INDEX ON e(i)"
  }));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  // After the upgrade, the tablegroup ACL must allow user_1 to create a table in the tablegroup,
  // but not user_2.
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute("CREATE USER user_2"));  // While user is yugabyte
    ASSERT_OK(conn.Execute("SET ROLE user_1"));
    ASSERT_OK(conn.Execute("CREATE TABLE e2(i text) TABLEGROUP test_grant"));
    ASSERT_OK(conn.Execute("SET ROLE user_2"));
    ASSERT_NOK_STR_CONTAINS(
        conn.Execute("CREATE TABLE e3(i text) TABLEGROUP test_grant"),
        "permission denied for tablegroup test_grant");
  }
}

class YsqlMajorUpgradeTestWithAuth : public YsqlMajorUpgradeTest {
 public:
  YsqlMajorUpgradeTestWithAuth() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.enable_ysql_auth = true;
    YsqlMajorUpgradeTest::SetUpOptions(opts);
  }

  Status ValidateUpgradeCompatibility(const std::string& user_name = "yugabyte") override {
    setenv("PGPASSWORD", "yugabyte", /*overwrite=*/true);
    return YsqlMajorUpgradeTest::ValidateUpgradeCompatibility(user_name);
  }
};

// Make sure upgrade succeeds in non auth universes even if there is no tserver on the master node.
TEST_F(YsqlMajorUpgradeTest, NoTserverOnMasterNode) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  auto master_tserver = ASSERT_RESULT(StopMasterLeaderTServer());

  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());
  ASSERT_OK(master_tserver->Restart());
  ASSERT_OK(WaitForClusterToStabilize());

  ASSERT_OK(RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(FinalizeUpgrade());
}

// If there is no tserver on the master node make sure the upgrade fails unless the yugabyte_upgrade
// user is created.
TEST_F(YsqlMajorUpgradeTestWithAuth, NoTserverOnMasterNode) {
// Disabled the rollback step on debug builds because it times out.
#if defined(NDEBUG)
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  {
    auto master_tserver = ASSERT_RESULT(StopMasterLeaderTServer());

    ASSERT_NOK_STR_CONTAINS(PerformYsqlMajorCatalogUpgrade(), "Failed to run pg_upgrade");
    ASSERT_OK(RollbackYsqlMajorCatalogVersion());
    ASSERT_OK(master_tserver->Restart());
  }
#endif

  // Rollback and create the upgrade user.
  ASSERT_OK(RestartAllMastersInOldVersion(kNoDelayBetweenNodes));

  const auto password = "yugabyte";
  // Set the password in the environment variable, which will propagate it to all child processes
  // started after this point.
  setenv("PGPASSWORD", password, /*overwrite=*/true);

  ASSERT_OK(ExecuteStatement(Format(
      "CREATE USER $0 WITH SUPERUSER PASSWORD '$1'", FLAGS_ysql_major_upgrade_user, password)));

  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  auto master_tserver = ASSERT_RESULT(StopMasterLeaderTServer());

  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());
  ASSERT_OK(master_tserver->Restart());
  ASSERT_OK(WaitForClusterToStabilize());

  ASSERT_OK(RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(FinalizeUpgrade());
}

TEST_F(YsqlMajorUpgradeTestWithAuth, UpgradeAuthEnabledUniverse) {
  ASSERT_OK(TestUpgradeWithSimpleTable());
}

TEST_F(YsqlMajorUpgradeTestWithAuth, NoYugabyteUserPassword) {
  ASSERT_OK(ExecuteStatement("ALTER USER yugabyte WITH PASSWORD NULL"));
  ASSERT_NOK_STR_CONTAINS(cluster_->ConnectToDB(), "password authentication failed");

  // We should still be able to upgrade the cluster.
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));
}

// Test scenario 2: MD5->SCRAM version upgrade with no HBA modification
// This tests that during a major version upgrade, existing MD5 passwords continue to work
// while new passwords use the upgraded version's default (SCRAM-SHA-256)
TEST_F(YsqlMajorUpgradeTestWithAuth, MD5ToSCRAMVersionUpgrade) {
  // Set password_encryption to md5 to simulate older version behavior
  ASSERT_OK(ExecuteStatement("SET password_encryption = 'md5'"));

  // Create test users with MD5 passwords (simulating pre-upgrade state)
  ASSERT_OK(ExecuteStatements({
    "CREATE USER md5_test_user WITH PASSWORD 'md5_password'",
    "CREATE USER pre_upgrade_user WITH PASSWORD 'pre_upgrade_pass'"
  }));

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto md5_password_result = conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 3) FROM pg_authid WHERE rolname='md5_test_user'");
    auto md5_password = ASSERT_RESULT(std::move(md5_password_result));
    ASSERT_EQ("md5", md5_password);
  }

  auto test_auth = [this](const std::string& username, const std::string& password) {
    auto conn_settings = pgwrapper::PGConnSettings{
        .host = cluster_->tablet_server(0)->bind_host(),
        .port = cluster_->tablet_server(0)->ysql_port(),
        .dbname = "yugabyte",
        .user = username,
        .password = password};
    return pgwrapper::PGConnBuilder(conn_settings).Connect();
  };

  ASSERT_OK(test_auth("md5_test_user", "md5_password"));
  ASSERT_OK(test_auth("pre_upgrade_user", "pre_upgrade_pass"));

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  // Verify password_encryption default is now SCRAM-SHA-256 after upgrade
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption_result = conn.FetchRow<std::string>(
        "SELECT boot_val FROM pg_settings WHERE name='password_encryption'");
    auto password_encryption = ASSERT_RESULT(std::move(password_encryption_result));
    ASSERT_EQ("scram-sha-256", password_encryption);
  }

  // Verify existing MD5 passwords still work after upgrade
  ASSERT_OK(test_auth("md5_test_user", "md5_password"));
  ASSERT_OK(test_auth("pre_upgrade_user", "pre_upgrade_pass"));

  // Create new user and verify it uses SCRAM-SHA-256 by default
  ASSERT_OK(ExecuteStatement("CREATE USER post_upgrade_user WITH PASSWORD 'post_upgrade_pass'"));
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto scram_password_result = conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname='post_upgrade_user'");
    auto scram_password = ASSERT_RESULT(std::move(scram_password_result));
    ASSERT_EQ("SCRAM-SHA-256", scram_password);
  }

  // Verify new SCRAM user authentication works
  ASSERT_OK(test_auth("post_upgrade_user", "post_upgrade_pass"));

  ASSERT_OK(ExecuteStatements({
      "DROP USER md5_test_user",
    "DROP USER pre_upgrade_user",
    "DROP USER post_upgrade_user"
  }));
}

// Test class for MD5 password encryption configuration scenario
class YsqlMajorUpgradeTestWithMD5 : public YsqlMajorUpgradeTest {
 public:
  YsqlMajorUpgradeTestWithMD5() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.enable_ysql_auth = true;
    YsqlMajorUpgradeTest::SetUpOptions(opts);

    // Configure MD5 authentication in HBA
    // Allow trust for yugabyte and postgres users for setup, require MD5 for others
    std::string hba_conf_value =
        "host all yugabyte 0.0.0.0/0 trust,"
        "host all postgres 0.0.0.0/0 trust,"
        "host all all 0.0.0.0/0 md5";
    opts.extra_tserver_flags.push_back(
        "--ysql_hba_conf_csv=" + hba_conf_value);

    // Set password_encryption to md5 for testing MD5 password storage
    opts.extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=password_encryption=md5");
  }

  Status ValidateUpgradeCompatibility(const std::string& user_name = "yugabyte") override {
    setenv("PGPASSWORD", "yugabyte", /*overwrite=*/true);
    return YsqlMajorUpgradeTest::ValidateUpgradeCompatibility(user_name);
  }

  Result<pgwrapper::PGConn> CreateConnWithAuth(
      std::optional<size_t> tserver,
      const std::string& user_name = "yugabyte",
      const std::string& password = "yugabyte") {
    setenv("PGPASSWORD", password.c_str(), /*overwrite=*/true);
    return CreateConnToTs(tserver, user_name);
  }
};

// Test scenario: MD5 password encryption configuration effects during upgrade
TEST_F(YsqlMajorUpgradeTestWithMD5, PasswordEncryptionWithMD5) {
  // Verify current password_encryption setting
  // password_encryption should be md5 due to ysql_pg_conf_csv configuration
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption_result = conn.FetchRow<std::string>(
        "SELECT setting FROM pg_settings WHERE name='password_encryption'");
    auto password_encryption = ASSERT_RESULT(std::move(password_encryption_result));
    // Should be md5 due to ysql_pg_conf_csv=password_encryption=md5
    ASSERT_EQ("md5", password_encryption);
  }

  // Create test users with default password encryption
  ASSERT_OK(ExecuteStatements({
    "CREATE USER hba_test_user1 WITH PASSWORD 'test_password1'",
    "CREATE USER hba_test_user2 WITH PASSWORD 'test_password2'"
  }));

  // Verify users have MD5 passwords (due to ysql_pg_conf_csv setting)
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format_result = conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 3) FROM pg_authid WHERE rolname='hba_test_user1'");
    auto password_format = ASSERT_RESULT(std::move(password_format_result));
    ASSERT_EQ("md5", password_format);
  }

  auto test_md5_auth = [this](const std::string& username, const std::string& password) {
    auto conn_settings = pgwrapper::PGConnSettings{
        .host = cluster_->tablet_server(0)->bind_host(),
        .port = cluster_->tablet_server(0)->ysql_port(),
        .dbname = "yugabyte",
        .user = username,
        .password = password};
    return pgwrapper::PGConnBuilder(conn_settings).Connect();
  };

  ASSERT_OK(test_md5_auth("hba_test_user1", "test_password1"));
  ASSERT_OK(test_md5_auth("hba_test_user2", "test_password2"));

  // Perform major version upgrade with MD5 HBA configuration
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  // Verify password_encryption setting is preserved after upgrade
  // Should still be md5 due to ysql_pg_conf_csv configuration being preserved
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption_result = conn.FetchRow<std::string>(
        "SELECT setting FROM pg_settings WHERE name='password_encryption'");
    auto password_encryption = ASSERT_RESULT(std::move(password_encryption_result));
    ASSERT_EQ("md5", password_encryption);
  }

  // Verify existing users still work after upgrade
  ASSERT_OK(test_md5_auth("hba_test_user1", "test_password1"));
  ASSERT_OK(test_md5_auth("hba_test_user2", "test_password2"));

  // Create new user after upgrade and verify password format
  // Since we explicitly set password_encryption=md5 via ysql_pg_conf_csv,
  // new users should still use MD5 format even after upgrade
  ASSERT_OK(ExecuteStatement("CREATE USER post_upgrade_hba_user WITH PASSWORD 'post_password'"));
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format_result = conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 3) FROM pg_authid WHERE rolname='post_upgrade_hba_user'");
    auto password_format = ASSERT_RESULT(std::move(password_format_result));
    ASSERT_EQ("md5", password_format);
  }

  ASSERT_OK(test_md5_auth("post_upgrade_hba_user", "post_password"));

  ASSERT_OK(ExecuteStatements({
    "DROP USER hba_test_user1",
    "DROP USER hba_test_user2",
    "DROP USER post_upgrade_hba_user"
  }));
}

class YsqlMajorUpgradeTestMD5ToSCRAM : public YsqlMajorUpgradeTest {
 public:
  YsqlMajorUpgradeTestMD5ToSCRAM() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.enable_ysql_auth = true;
    YsqlMajorUpgradeTest::SetUpOptions(opts);

    // Simulate old cluster with MD5 password encryption
    opts.extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=password_encryption=md5");
  }

  Status ValidateUpgradeCompatibility(const std::string& user_name = "yugabyte") override {
    setenv("PGPASSWORD", "yugabyte", /*overwrite=*/true);
    return YsqlMajorUpgradeTest::ValidateUpgradeCompatibility(user_name);
  }

  Result<pgwrapper::PGConn> TestAuth(const std::string& username, const std::string& password) {
    auto conn_settings = pgwrapper::PGConnSettings{
        .host = cluster_->tablet_server(0)->bind_host(),
        .port = cluster_->tablet_server(0)->ysql_port(),
        .dbname = "yugabyte",
        .user = username,
        .password = password};
    return pgwrapper::PGConnBuilder(conn_settings).Connect();
  }
};

// Test scenario: Complete MD5->SCRAM migration with dynamic configuration updates
TEST_F(YsqlMajorUpgradeTestMD5ToSCRAM, CompleteMigrationScenario) {
  // Verify pre-upgrade state - should be MD5 due to explicit gflag
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT setting FROM pg_settings WHERE name='password_encryption'"));
    ASSERT_EQ("md5", password_encryption);
  }

  // Create users with MD5 passwords (simulating pre-upgrade users)
  ASSERT_OK(ExecuteStatements({
    "CREATE USER old_user1 WITH PASSWORD 'old_password1'",
    "CREATE USER old_user2 WITH PASSWORD 'old_password2'"
  }));

  // Verify users have MD5 passwords
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 3) FROM pg_authid WHERE rolname='old_user1'"));
    ASSERT_EQ("md5", password_format);
  }

  ASSERT_OK(TestAuth("old_user1", "old_password1"));
  ASSERT_OK(TestAuth("old_user2", "old_password2"));

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  // Verify password_encryption setting preserved after upgrade
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT setting FROM pg_settings WHERE name='password_encryption'"));
    ASSERT_EQ("md5", password_encryption); // Should still be MD5 due to gflag preservation
  }

  // Verify existing users still work after upgrade
  ASSERT_OK(TestAuth("old_user1", "old_password1"));
  ASSERT_OK(TestAuth("old_user2", "old_password2"));

  // Create another user - should still be MD5 due to explicit gflag
  ASSERT_OK(ExecuteStatement("CREATE USER mid_upgrade_user WITH PASSWORD 'mid_password'"));
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 3) FROM pg_authid WHERE rolname='mid_upgrade_user'"));
    ASSERT_EQ("md5", password_format);
  }

  // Remove MD5 setting to allow new default
  LOG(INFO) << "Updating tserver flags to remove explicit password_encryption setting";
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_pg_conf_csv", ""));

  // Give time for configuration to be reloaded
  SleepFor(2s);

  // Verify password_encryption now defaults to SCRAM-SHA-256
  {
    // Reconnect to get updated configuration
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT setting FROM pg_settings WHERE name='password_encryption'"));
    ASSERT_EQ("scram-sha-256", password_encryption); // Should now be SCRAM default
  }

  ASSERT_OK(ExecuteStatement("CREATE USER new_user WITH PASSWORD 'new_password'"));
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname='new_user'"));
    ASSERT_EQ("SCRAM-SHA-256", password_format);
  }

  // Verify all users work regardless of password format
  ASSERT_OK(TestAuth("old_user1", "old_password1")); // MD5 from pre-upgrade
  ASSERT_OK(TestAuth("old_user2", "old_password2")); // MD5 from pre-upgrade
  ASSERT_OK(TestAuth("mid_upgrade_user", "mid_password")); // MD5 from during upgrade
  ASSERT_OK(TestAuth("new_user", "new_password")); // SCRAM from post-flag-update

  // Test password changes work correctly
  ASSERT_OK(ExecuteStatement("ALTER USER old_user1 PASSWORD 'updated_password1'"));
  {
    // Password should now be SCRAM since current default is SCRAM
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname='old_user1'"));
    ASSERT_EQ("SCRAM-SHA-256", password_format);
  }
  ASSERT_OK(TestAuth("old_user1", "updated_password1"));

  ASSERT_OK(ExecuteStatements({
    "DROP USER old_user1",
    "DROP USER old_user2",
    "DROP USER mid_upgrade_user",
    "DROP USER new_user"
  }));
}

// Test class for explicit SCRAM-SHA-256 password encryption configuration scenario
class YsqlMajorUpgradeTestWithSCRAM : public YsqlMajorUpgradeTest {
 public:
  YsqlMajorUpgradeTestWithSCRAM() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.enable_ysql_auth = true;
    YsqlMajorUpgradeTest::SetUpOptions(opts);

    std::string hba_conf_value =
        "host all yugabyte 0.0.0.0/0 trust,"
        "host all all 0.0.0.0/0 scram-sha-256";
    opts.extra_tserver_flags.push_back(
        "--ysql_hba_conf_csv=" + hba_conf_value);

    // Explicitly set password_encryption to scram-sha-256 for testing SCRAM password storage
    opts.extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=password_encryption=scram-sha-256");
  }

  Status ValidateUpgradeCompatibility(const std::string& user_name = "yugabyte") override {
    setenv("PGPASSWORD", "yugabyte", /*overwrite=*/true);
    return YsqlMajorUpgradeTest::ValidateUpgradeCompatibility(user_name);
  }
};

// Test scenario: Explicit SCRAM password encryption configuration effects during upgrade
TEST_F(YsqlMajorUpgradeTestWithSCRAM, PasswordEncryptionWithSCRAM) {
  // Verify current password_encryption setting
  // password_encryption should be scram-sha-256 due to ysql_pg_conf_csv configuration
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption_result = conn.FetchRow<std::string>(
        "SELECT setting FROM pg_settings WHERE name='password_encryption'");
    auto password_encryption = ASSERT_RESULT(std::move(password_encryption_result));
    // Should be scram-sha-256 due to ysql_pg_conf_csv=password_encryption=scram-sha-256
    ASSERT_EQ("scram-sha-256", password_encryption);
  }

  ASSERT_OK(ExecuteStatements({
    "CREATE USER scram_test_user1 WITH PASSWORD 'test_password1'",
    "CREATE USER scram_test_user2 WITH PASSWORD 'test_password2'"
  }));

  // Verify users have SCRAM passwords (due to ysql_pg_conf_csv setting)
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format_result = conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname='scram_test_user1'");
    auto password_format = ASSERT_RESULT(std::move(password_format_result));
    ASSERT_EQ("SCRAM-SHA-256", password_format);
  }

  auto test_scram_auth = [this](const std::string& username, const std::string& password) {
    auto conn_settings = pgwrapper::PGConnSettings{
        .host = cluster_->tablet_server(0)->bind_host(),
        .port = cluster_->tablet_server(0)->ysql_port(),
        .dbname = "yugabyte",
        .user = username,
        .password = password};
    return pgwrapper::PGConnBuilder(conn_settings).Connect();
  };

  ASSERT_OK(test_scram_auth("scram_test_user1", "test_password1"));
  ASSERT_OK(test_scram_auth("scram_test_user2", "test_password2"));

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  // Verify password_encryption setting is preserved after upgrade
  // Should still be scram-sha-256 due to ysql_pg_conf_csv configuration being preserved
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_encryption_result = conn.FetchRow<std::string>(
        "SELECT setting FROM pg_settings WHERE name='password_encryption'");
    auto password_encryption = ASSERT_RESULT(std::move(password_encryption_result));
    ASSERT_EQ("scram-sha-256", password_encryption);
  }

  // Verify existing users still work after upgrade
  ASSERT_OK(test_scram_auth("scram_test_user1", "test_password1"));
  ASSERT_OK(test_scram_auth("scram_test_user2", "test_password2"));

  // Create new user after upgrade and verify password format
  // Since we explicitly set password_encryption=scram-sha-256 via ysql_pg_conf_csv,
  // new users should still use SCRAM format even after upgrade
  ASSERT_OK(ExecuteStatement("CREATE USER post_upgrade_scram_user WITH PASSWORD 'post_password'"));
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    auto password_format_result = conn.FetchRow<std::string>(
        "SELECT LEFT(rolpassword, 13) FROM pg_authid WHERE rolname='post_upgrade_scram_user'");
    auto password_format = ASSERT_RESULT(std::move(password_format_result));
    ASSERT_EQ("SCRAM-SHA-256", password_format);
  }

  ASSERT_OK(test_scram_auth("post_upgrade_scram_user", "post_password"));

  ASSERT_OK(ExecuteStatements({
    "DROP USER scram_test_user1",
    "DROP USER scram_test_user2",
    "DROP USER post_upgrade_scram_user"
  }));
}

TEST_F(YsqlMajorUpgradeTest, GlobalBreakingDDL) {
  ASSERT_OK(ExecuteStatements(
    {"CREATE USER test",
     "DROP USER test"}));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));
}

TEST_F(YsqlMajorUpgradeTest, Indexes) {
  ASSERT_OK(ExecuteStatements(
    {"CREATE TABLE t1 (a int)",
     "INSERT INTO t1 VALUES (1),(2),(3),(4),(5)",
     "CREATE INDEX i1 ON t1 (a)",

     "CREATE TABLE t2 (a int)",
     "INSERT INTO t2 VALUES (1),(2),(3),(4),(5)",
     "CREATE UNIQUE INDEX i2 ON t2 (a)",

     "CREATE TABLE t3 (a int)",
     "INSERT INTO t3 VALUES (1),(2),(3),(4),(5)",
     "CREATE INDEX i3 ON t3 (a ASC)",

     "CREATE TABLE t4 (a int)",
     "INSERT INTO t4 VALUES (1),(2),(3),(4),(5)",
     "CREATE INDEX i4 ON t4 (a DESC)",
     }));

  auto check_indexes = [&](pgwrapper::PGConn& conn, const std::vector<int>& values_to_check) {
    // Check hash based indexes (i1, i2).
    for (const auto& value : values_to_check) {
      for (const auto& table : {"t1", "t2"}) {
        auto query = Format("SELECT a FROM $0 WHERE a = $1", table, value);
        ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
        auto result = ASSERT_RESULT(conn.FetchRows<int>(query));
        ASSERT_VECTORS_EQ(result, std::vector<int>{value});
      }
    }

    // Check range based indexes (i3, i4).
    for (const auto& table : {"t3", "t4"}) {
      auto query = Format("SELECT a FROM $0 WHERE a >= 1", table);
      ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
      auto result = ASSERT_RESULT(conn.FetchRows<int>(query));
      if (table == std::string("t4")) {
        // DESC index (i4) on table t4.
        std::vector<int> reversed_values(values_to_check.rbegin(), values_to_check.rend());
        ASSERT_VECTORS_EQ(result, reversed_values);
      } else {
        ASSERT_VECTORS_EQ(result, values_to_check);
      }
    }
  };

  ASSERT_OK(UpgradeClusterToMixedMode());

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    for (const auto& table : {"t1", "t2", "t3", "t4"}) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (15)", table));
    }
    ASSERT_NO_FATALS(check_indexes(conn, {1, 2, 3, 4, 5, 15}));
  }

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    for (const auto& table : {"t1", "t2", "t3", "t4"}) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (11)", table));
    }
    ASSERT_NO_FATALS(check_indexes(conn, {1, 2, 3, 4, 5, 11, 15}));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(check_indexes(conn, {1, 2, 3, 4, 5, 11, 15}));
  }
}

TEST_F(YsqlMajorUpgradeTest, ForeignKeyTest) {
  int a = 0;
  int b = 100;
  int c = 10000;

  std::vector<int> a_values = {};
  std::vector<int> b_values = {};
  std::vector<int> c_values = {};

  ASSERT_OK(ExecuteStatements({
    "CREATE TABLE a_values (a int primary key)",
    "CREATE TABLE b_values (b int primary key)",
    "CREATE TABLE c_values (c int primary key)",

    "CREATE TABLE referencing_table (a int references a_values(a), "
    "                                b int, "
    "                                c int, "
    "                                foreign key (b) references b_values(b))",
    "ALTER TABLE referencing_table ADD CONSTRAINT fk_c FOREIGN KEY (c) REFERENCES c_values(c)",
  }));

  for (size_t i = 0; i < 3; i++) {
    a_values.push_back(++a);
    b_values.push_back(++b);
    c_values.push_back(++c);

    ASSERT_OK(ExecuteStatement(Format("INSERT INTO a_values VALUES ($0)", a)));
    ASSERT_OK(ExecuteStatement(Format("INSERT INTO b_values VALUES ($0)", b)));
    ASSERT_OK(ExecuteStatement(Format("INSERT INTO c_values VALUES ($0)", c)));

    ASSERT_OK(ExecuteStatement(Format("INSERT INTO referencing_table VALUES ($0, $1, $2)",
                                      a, b, c)));
  }

  const auto add_and_check_new_row = [this, &a, &b, &c, &a_values, &b_values, &c_values]
      (const std::optional<size_t> tserver_idx) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));

    ASSERT_NOK_STR_CONTAINS(
        conn.ExecuteFormat("INSERT INTO referencing_table VALUES ($0, $1, $2)", a + 1, b, c),
        "is not present in table \"a_values\"");
    ASSERT_NOK_STR_CONTAINS(
        conn.ExecuteFormat("INSERT INTO referencing_table VALUES ($0, $1, $2)", a, b + 1, c),
        "is not present in table \"b_values\"");
    ASSERT_NOK_STR_CONTAINS(
        conn.ExecuteFormat("INSERT INTO referencing_table VALUES ($0, $1, $2)", a, b, c + 1),
        "is not present in table \"c_values\"");

    a_values.push_back(++a);
    b_values.push_back(++b);
    c_values.push_back(++c);

    ASSERT_OK(conn.ExecuteFormat("INSERT INTO a_values VALUES ($0)", a));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO b_values VALUES ($0)", b));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO c_values VALUES ($0)", c));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO referencing_table VALUES ($0, $1, $2)",
                                 a, b, c));

    for (std::string col : {"a", "b", "c"}) {
      const auto expected = ((col == "a") ? a_values : (col == "b") ? b_values : c_values);

      const auto actual = ASSERT_RESULT(conn.FetchRows<int>(
          Format("SELECT $0 FROM $0_values ORDER BY $0", col)));
      ASSERT_VECTORS_EQ(expected, actual);

      const auto actual_referencing = ASSERT_RESULT(conn.FetchRows<int>(
          Format("SELECT $0 FROM referencing_table ORDER BY $0", col)));
      ASSERT_VECTORS_EQ(expected, actual_referencing);
    }
  };

  ASSERT_NO_FATALS(add_and_check_new_row(kAnyTserver));

  ASSERT_OK(UpgradeClusterToMixedMode());

  ASSERT_NO_FATALS(add_and_check_new_row(kMixedModeTserverPg11));
  ASSERT_NO_FATALS(add_and_check_new_row(kMixedModeTserverPg15));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  ASSERT_NO_FATALS(add_and_check_new_row(kAnyTserver));
}

class Pg15UpgradeSequenceTest : public YsqlMajorUpgradeTest {
 public:
  Pg15UpgradeSequenceTest() = default;

  void SetUp() override {
    TEST_SETUP_SUPER(YsqlMajorUpgradeTest);

    ASSERT_OK(cluster_->AddAndSetExtraFlag("ysql_sequence_cache_minval", "1"));
    // As documented in the daemon->AddExtraFlag call, a restart is required to apply the flag.
    // We must Shutdown before we can Restart.
    cluster_->Shutdown();
    ASSERT_OK(cluster_->Restart());

    // Required to avoid a known issue documented in yb_catalog_version.sql
    ASSERT_NOK(ExecuteStatement("SELECT 1 FROM pg_yb_catalog_version"));
  }

  void Take3Values(pgwrapper::PGConn& conn, const std::string& sequence, int& seq_val) {
    for (int i = 0; i < 3; i++) {
      const auto result = ASSERT_RESULT(
          conn.FetchRow<pgwrapper::PGUint64>(Format("SELECT nextval('$0')", sequence)));
      ASSERT_EQ(seq_val++, result);
    }
  }

  void Add3Rows(pgwrapper::PGConn& conn, const std::string& sequence, int& seq_val) {
    for (int i = 0; i < 3; i++) {
      ASSERT_OK(conn.Execute(Format("INSERT INTO t_identity DEFAULT VALUES")));
      const auto result = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT max(col) FROM t_identity"));
      ASSERT_EQ(seq_val++, result);
    }
  }

  const std::string kSequencePg11 = "seq_pg11";
  const std::string kSequencePg15 = "seq_pg15";
  int seq_val_pg11_ = 1;
  int seq_val_pg15_ = 1;
};

TEST_F(YsqlMajorUpgradeTest, YbGinIndex) {
  const auto kGinTableName = "expression";
  const auto kGinIndex1 = "gin_idx_1";
  const auto kGinIndex2 = "gin_idx_2";
  const auto kGinIndex3 = "gin_idx_3";

  const std::map<std::string, std::string> kGinIndexes = {
    {kGinIndex1,
     Format("CREATE INDEX $0 ON $1 USING ybgin (tsvector_to_array(v))", kGinIndex1, kGinTableName)},
    {kGinIndex2,
     Format("CREATE INDEX $0 ON $1 USING ybgin (array_to_tsvector(a))", kGinIndex2, kGinTableName)},
    {kGinIndex3,
     Format("CREATE INDEX $0 ON $1 USING ybgin (jsonb_to_tsvector('simple', j, '[\"string\"]'))",
            kGinIndex3, kGinTableName)}};

  ASSERT_OK(ExecuteStatement(
      Format("CREATE TABLE $0 (v tsvector, a text[], j jsonb)", kGinTableName)));

  // We can expect that sometimes index creation will fail to due the explanation in #20959.
  const auto create_index_with_retry = [this](const std::string &idx_name,
                                              const std::string &create_stmt) {
    const auto kNRetries = 10;
    for (int retry = 0; retry < kNRetries; retry++) {
      if (ExecuteStatement(create_stmt).ok())
        return;

      // The index may have been partially created, so drop it.
      ASSERT_OK(ExecuteStatement(Format("DROP INDEX IF EXISTS $0", idx_name)));
    }

    FAIL() << "Failed to create index " << idx_name << " after " << kNRetries << " retries";
  };

  const auto create_indexes_with_retry = [create_index_with_retry, kGinIndexes]() {
    for (auto &[idx_name, create_stmt] : kGinIndexes) {
      create_index_with_retry(idx_name, create_stmt);
    }
  };

  ASSERT_NO_FATALS(create_indexes_with_retry());

  const auto check_and_insert_rows = [kGinTableName](pgwrapper::PGConn& conn, int &expected) {
    const auto kQueries = {
      "SELECT count(*) FROM $0 WHERE tsvector_to_array(v) && ARRAY['b']",
      "SELECT count(*) FROM $0 WHERE array_to_tsvector(a) @@ 'e'",
      "SELECT count(*) FROM $0 WHERE jsonb_to_tsvector('simple', j, '[\"string\"]') @@ 'h'",
    };

    for (const auto &query : kQueries) {
      const auto result = ASSERT_RESULT(
          conn.FetchRow<pgwrapper::PGUint64>(Format(query, kGinTableName)));
      ASSERT_EQ(result, expected);
    }

    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES "
        "  (to_tsvector('simple', 'a b c'), ARRAY['d', 'e', 'f'], '{\"g\":[\"h\",\"i\"]}')",
        kGinTableName
    ));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES (to_tsvector('simple', 'a a'), ARRAY['d', 'd'], '{\"g\":\"g\"}')",
        kGinTableName
    ));
    expected++;

    for (const auto &query : kQueries) {
      const auto result = ASSERT_RESULT(
          conn.FetchRow<pgwrapper::PGUint64>(Format(query, kGinTableName)));
      ASSERT_EQ(result, expected);
    }
  };

  int num_rows = 0;

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(check_and_insert_rows(conn, num_rows));
  }

  ASSERT_OK(UpgradeClusterToMixedMode());

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(check_and_insert_rows(conn, num_rows));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(check_and_insert_rows(conn, num_rows));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(check_and_insert_rows(conn, num_rows));

    // test dropping and recreating the indexes, to validate that these DDLs
    // still work as expected in pg15
    for (const auto &index : kGinIndexes) {
      ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", index.first));
    }
    ASSERT_NO_FATALS(create_indexes_with_retry());

    ASSERT_NO_FATALS(check_and_insert_rows(conn, num_rows));
  }
}

TEST_F(Pg15UpgradeSequenceTest, Sequences) {
  ASSERT_OK(ExecuteStatement(Format("CREATE SEQUENCE $0", kSequencePg11)));

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(Take3Values(conn, kSequencePg11, seq_val_pg11_));
  }

  ASSERT_OK(UpgradeClusterToMixedMode());

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(Take3Values(conn, kSequencePg11, seq_val_pg11_));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(Take3Values(conn, kSequencePg11, seq_val_pg11_));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // Take three values from a random tserver
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(Take3Values(conn, kSequencePg11, seq_val_pg11_));
  }

  ASSERT_OK(ExecuteStatement(Format("CREATE SEQUENCE $0 CACHE 1", kSequencePg15)));

  // Take three values from a random tserver, twice (to validate caching on the new sequence)
  for (int i = 0; i < 2; i++) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(Take3Values(conn, kSequencePg11, seq_val_pg11_));
    ASSERT_NO_FATALS(Take3Values(conn, kSequencePg15, seq_val_pg15_));
  }
}


TEST_F(Pg15UpgradeSequenceTest, IdentityColumn) {
  ASSERT_OK(ExecuteStatement("CREATE TABLE t_identity (col INT GENERATED ALWAYS AS IDENTITY)"));

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(Add3Rows(conn, kSequencePg11, seq_val_pg11_));
  }

  ASSERT_OK(UpgradeClusterToMixedMode());

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(Add3Rows(conn, kSequencePg11, seq_val_pg11_));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(Add3Rows(conn, kSequencePg11, seq_val_pg11_));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // Take three values from a random tserver
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(Add3Rows(conn, kSequencePg11, seq_val_pg11_));
  }
}

TEST_F(YsqlMajorUpgradeTest, Tablespaces) {
  const auto simple_spc = "ts_invalid";
  const auto pg11_spc = "ts11";
  const auto pg15_spc = "ts15";
  const auto simple_tbl = "tbl_invalid";
  const auto pg11_tbl = "tbl11";
  const auto pg15_tbl = "tbl15";
  const auto create_table = "CREATE TABLE $0 (a int) TABLESPACE $1";
  const auto create_tablespace = R"#(
    CREATE TABLESPACE $0 WITH (replica_placement='{
      "num_replicas" : 1,
      "placement_blocks": [{
        "cloud"            : "cloud1",
        "region"           : "datacenter1",
        "zone"             : "rack1",
        "min_num_replicas" : 1
      }]
    }')
  )#";

  std::map<std::string, int> expected_rows = {
    {simple_tbl, 0},
    {pg11_tbl, 0},
    {pg15_tbl, 0}
  };

  std::map<std::string, std::string> expected_tablespaces = {
    {simple_tbl, simple_spc},
    {pg11_tbl, pg11_spc},
  };

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLESPACE $0 LOCATION '/invalid'", simple_spc));
    ASSERT_OK(conn.ExecuteFormat(create_tablespace, pg11_spc));
    ASSERT_OK(conn.ExecuteFormat(create_table, pg11_tbl, pg11_spc));
    ASSERT_OK(conn.ExecuteFormat(create_table, simple_tbl, simple_spc));
  }

  const auto check_tablespace = [this, &expected_tablespaces, &expected_rows]() -> Status {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    for (const auto &[tbl, spc] : expected_tablespaces) {
      auto tblspace_name_result = VERIFY_RESULT(conn.FetchRow<std::string>(Format(
        "SELECT spcname FROM pg_tablespace ts "
        "INNER JOIN pg_class pg_c ON pg_c.reltablespace = ts.oid "
        "WHERE pg_c.relname = '$0'",
        tbl)));
      SCHECK_EQ(tblspace_name_result, spc, IllegalState, "Tablespace name mismatch");

      // Insert and validate row count
      RETURN_NOT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", tbl));
      SCHECK_EQ(
          ++expected_rows[tbl],
          VERIFY_RESULT(conn.FetchRow<pgwrapper::PGUint64>(Format("SELECT COUNT(*) FROM $0", tbl))),
          IllegalState,
          Format("Row count mismatch for table $0", tbl));
    }
    return Status::OK();
  };

  ASSERT_OK(check_tablespace());

  ASSERT_OK(UpgradeClusterToMixedMode());

  ASSERT_OK(check_tablespace());

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.ExecuteFormat(create_tablespace, pg15_spc));
    ASSERT_OK(conn.ExecuteFormat(create_table, pg15_tbl, pg15_spc));
    expected_tablespaces[pg15_tbl] = pg15_spc;
  }

  ASSERT_OK(check_tablespace());
}

TEST_F(YsqlMajorUpgradeTest, DroppedColumnTest) {
  const auto insert_stmt = "INSERT INTO t VALUES ($0)";
  const auto kT1SelectStmt = "SELECT * FROM t ORDER BY a";
  const auto kT2SelectStmt = "SELECT * FROM t2 ORDER BY a2";
  std::string t1_expected_rows = "1, 3, NULL, foo; 11, 13, NULL, foo; 21, 23, 24, foo";
  size_t next_row = 30;
  const auto kT2ExpectedRows = "2, 2000, 1";

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute("CREATE TABLE t (a int, b int, c int)"));
    ASSERT_OK(conn.ExecuteFormat(insert_stmt, "1, 2, 3"));
    ASSERT_OK(conn.ExecuteFormat(insert_stmt, "11, 12, 13"));
    ASSERT_OK(conn.Execute("ALTER TABLE t DROP COLUMN b"));
    ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN d int"));
    ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN e text DEFAULT 'foo'"));
    ASSERT_OK(conn.ExecuteFormat(insert_stmt, "21, 23, 24"));

    auto result = ASSERT_RESULT(conn.FetchAllAsString(kT1SelectStmt));
    ASSERT_EQ(result, t1_expected_rows);

    ASSERT_OK(conn.Execute(
        "CREATE TABLE t2 (a2 int, b2 int, c2 int, d2 int, PRIMARY KEY(d2 HASH, a2 ASC))"));
    ASSERT_OK(conn.Execute("ALTER TABLE t2 DROP COLUMN b2"));
    ASSERT_OK(conn.Execute("INSERT INTO t2 VALUES (2, 2000, 1)"));
  }

  auto run_validations = [&](std::optional<size_t> ts_id = std::nullopt) -> Status {
    const auto kT1ExpectedCols = "a, 1; ........pg.dropped.2........, 2; c, 3; d, 4; e, 5";
    const auto kT2ExpectedCols = "a2, 1; ........pg.dropped.2........, 2; c2, 3; d2, 4";

    auto conn = VERIFY_RESULT(CreateConnToTs(ts_id));
    auto result = VERIFY_RESULT(
        conn.FetchAllAsString("SELECT attname, attnum FROM pg_attribute WHERE attrelid = "
                              "'t'::pg_catalog.regclass AND attnum >= 0 ORDER BY attnum"));
    SCHECK_EQ(result, kT1ExpectedCols, IllegalState, "t1 validation failed");

    result = VERIFY_RESULT(
        conn.FetchAllAsString("SELECT attname, attnum FROM pg_attribute WHERE attrelid = "
                              "'t2'::pg_catalog.regclass AND attnum >= 0"));
    SCHECK_EQ(result, kT2ExpectedCols, IllegalState, "t2 validation failed");

    result = VERIFY_RESULT(conn.FetchAllAsString(kT2SelectStmt));
    SCHECK_EQ(result, kT2ExpectedRows, IllegalState, "Mismatch in t2 rows");

    result = VERIFY_RESULT(conn.FetchAllAsString(kT1SelectStmt));
    SCHECK_EQ(result, t1_expected_rows, IllegalState, "Mismatch in t1 rows");

    const auto next_value = Format("$0, $1, $2", next_row + 1, next_row + 3, next_row + 4);
    next_row += 10;
    RETURN_NOT_OK(conn.ExecuteFormat(insert_stmt, next_value));
    t1_expected_rows += Format("; $0, foo", next_value);

    result = VERIFY_RESULT(conn.FetchAllAsString(kT1SelectStmt));
    SCHECK_EQ(result, t1_expected_rows, IllegalState, "Mismatch in t1 rows after insert");

    return Status::OK();
  };

  ASSERT_OK(run_validations());

  ASSERT_OK(UpgradeClusterToMixedMode());

  ASSERT_OK(run_validations(kMixedModeTserverPg11));
  ASSERT_OK(run_validations(kMixedModeTserverPg15));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  ASSERT_OK(run_validations());
}

TEST_F(YsqlMajorUpgradeTest, YbSuperuserRole) {
  ASSERT_OK(ExecuteStatements(
      {"CREATE ROLE \"yb_superuser\" INHERIT CREATEROLE CREATEDB BYPASSRLS",
       "GRANT \"pg_read_all_stats\" TO \"yb_superuser\""}));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));
}

TEST_F(YsqlMajorUpgradeTest, Analyze) {
  constexpr std::string_view kStatsUpdateError =
      "YSQL DDLs, and catalog modifications are not allowed during a major YSQL upgrade";
  constexpr std::string_view kNoRandStateError = "Invalid sampling state, random state is missing";
  using ExpectedErrors = std::optional<const std::vector<std::string_view>>;
  auto check_analyze = [this](std::optional<size_t> server, ExpectedErrors expected_errors) {
    auto conn = ASSERT_RESULT(CreateConnToTs(server));
    auto status = conn.ExecuteFormat("ANALYZE $0", kSimpleTableName);
    if (!expected_errors) {
      ASSERT_OK(status);
    } else {
      ASSERT_NOK(status);
      for (const auto& err : *expected_errors) {
        if (status.ToString().find(err) != std::string::npos) {
          return;
        }
      }
      FAIL() << "Unexpected error " << status.ToString();
    }
  };
  ASSERT_OK(CreateSimpleTable());
  check_analyze(kAnyTserver, std::nullopt);
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());
  check_analyze(kAnyTserver, {{kStatsUpdateError}});
  LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in current version";
  auto mixed_mode_pg15_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
  ASSERT_OK(RestartTServerInCurrentVersion(
      *mixed_mode_pg15_tserver, /*wait_for_cluster_to_stabilize=*/true));
  check_analyze(kMixedModeTserverPg11, {{kNoRandStateError, kStatsUpdateError}});
  check_analyze(kMixedModeTserverPg15, {{kNoRandStateError, kStatsUpdateError}});
  ASSERT_OK(UpgradeAllTserversFromMixedMode());
  check_analyze(kAnyTserver, {{kStatsUpdateError}});
  ASSERT_OK(FinalizeUpgrade());
  check_analyze(kAnyTserver, std::nullopt);
}

TEST_F(YsqlMajorUpgradeTest, EnumTypes) {
  ASSERT_OK(ExecuteStatements({
    "CREATE TYPE color AS ENUM ('red', 'green', 'blue', 'yellow')",
    "CREATE TABLE paint_log (id serial, shade color) PARTITION BY HASH (shade)",
    "CREATE TABLE paint_log_p0 PARTITION OF paint_log FOR VALUES WITH (MODULUS 2, REMAINDER 0)",
    "CREATE TABLE paint_log_p1 PARTITION OF paint_log FOR VALUES WITH (MODULUS 2, REMAINDER 1)",
    "INSERT INTO paint_log (shade) VALUES ('red'), ('green'), ('blue'), ('yellow')"
  }));
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  auto type_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
    "SELECT oid FROM pg_type WHERE typname = 'color'"));

  const auto fetch_partition_data = [&](const std::string& partition) {
    return conn.FetchRows<int, std::string>(
        Format("SELECT id, shade::text FROM $0 ORDER BY shade", partition));
  };

  const auto fetch_enum_data = [&]() {
    return conn.FetchRows<pgwrapper::PGOid, float, std::string>(Format(
        "SELECT oid, enumsortorder, enumlabel FROM pg_enum WHERE enumtypid = $0"
        " ORDER BY enumsortorder", type_oid));
  };

  auto paint_log_p0_res = ASSERT_RESULT(fetch_partition_data("paint_log_p0"));
  auto paint_log_p1_res = ASSERT_RESULT(fetch_partition_data("paint_log_p1"));
  auto enum_oids = ASSERT_RESULT(fetch_enum_data());

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_VECTORS_EQ(ASSERT_RESULT(fetch_partition_data("paint_log_p0")), paint_log_p0_res);
  ASSERT_VECTORS_EQ(ASSERT_RESULT(fetch_partition_data("paint_log_p1")), paint_log_p1_res);
  ASSERT_VECTORS_EQ(ASSERT_RESULT(fetch_enum_data()), enum_oids);
}

TEST_F(YsqlMajorUpgradeTest, IndexWithRenamedColumn) {
  ASSERT_OK(ExecuteStatements(
      {"CREATE TABLE t (a int, b int, c int)",
       "INSERT INTO t VALUES (1, 1, 1), (2, 2, 2)",
       "CREATE INDEX i ON t (a, b)",
       "CREATE INDEX i2 ON t (b) INCLUDE (c)",
       "ALTER TABLE t RENAME COLUMN a TO a_new",
       "ALTER TABLE t RENAME COLUMN c TO c_new",
       "CREATE INDEX i3 ON t (a_new, c_new)",
       "CREATE INDEX i4 ON t ((a_new+c_new), c_new)",
       "ALTER TABLE t RENAME COLUMN a_new TO a_new2",
       "ALTER TABLE t RENAME COLUMN c_new TO c_new2"}));

  ASSERT_OK(ExecuteStatements(
      {"CREATE TABLE t_part (a int, b int, c int) PARTITION BY RANGE (a)",
       "CREATE TABLE t_part_1 PARTITION OF t_part FOR VALUES FROM (0) TO (10)",
       "INSERT INTO t VALUES (1, 1, 1), (2, 2, 2)",
       "CREATE INDEX i_part ON t_part (a, b)",
       "CREATE INDEX i2_part ON t_part (b) INCLUDE (c)",
       "ALTER TABLE t_part RENAME COLUMN a TO a_new",
       "ALTER TABLE t_part RENAME COLUMN c TO c_new",
       "CREATE INDEX i3_part ON t_part (a_new, c_new)",
       "CREATE INDEX i4_part ON t_part ((a_new+c_new), c_new)",
       "ALTER TABLE t_part RENAME COLUMN a_new TO a_new2",
       "ALTER TABLE t_part RENAME COLUMN c_new TO c_new2"}));

  auto check_index_schema = [this](const std::string& index_name,
                                   const std::vector<std::string>& expected_columns,
                                   const std::string& describe_output) {
    auto table_info = std::make_shared<client::YBTableInfo>();
    auto table_id = ASSERT_RESULT(GetTableIdByTableName(client_.get(), "yugabyte", index_name));

    // Verify the schema for the current table
    Synchronizer sync;
    ASSERT_OK(client_->GetTableSchemaById(table_id, table_info, sync.AsStatusCallback()));
    ASSERT_OK(sync.Wait());
    int columns_found = 0;
    for (size_t i = 0; i < expected_columns.size(); ++i) {
      for (const auto& col : table_info->schema.columns()) {
        if (col.name() == expected_columns[i]) {
          columns_found++;
          break;
        }
      }
    }
    ASSERT_EQ(columns_found, expected_columns.size());

    // Verify the pg_attribute entries
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto res = ASSERT_RESULT(conn.FetchRows<std::string>(
        Format("SELECT attname FROM pg_attribute WHERE attrelid = '$0'::regclass", index_name)));
    ASSERT_VECTORS_EQ(res, expected_columns);

    // Verify the output of \d command
    auto result = ASSERT_RESULT(ExecuteViaYsqlsh(Format("\\d $0", index_name)));
    ASSERT_EQ(result, describe_output);
  };

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  // Validate index schemas and pg_attribute entries with correct expected columns and output
  check_index_schema("i", {"a", "b"},
    "           Index \"public.i\"\n"
    " Column |  Type   | Key? | Definition \n"
    "--------+---------+------+------------\n"
    " a      | integer | yes  | a_new2\n"
    " b      | integer | yes  | b\n"
    "lsm, for table \"public.t\"\n\n");

  check_index_schema("i2", {"b", "c"},
    "          Index \"public.i2\"\n"
    " Column |  Type   | Key? | Definition \n"
    "--------+---------+------+------------\n"
    " b      | integer | yes  | b\n"
    " c      | integer | no   | c_new2\n"
    "lsm, for table \"public.t\"\n\n");

  check_index_schema("i3", {"a_new", "c_new"},
    "          Index \"public.i3\"\n"
    " Column |  Type   | Key? | Definition \n"
    "--------+---------+------+------------\n"
    " a_new  | integer | yes  | a_new2\n"
    " c_new  | integer | yes  | c_new2\n"
    "lsm, for table \"public.t\"\n\n");

  check_index_schema("i4", {"expr", "c_new"},
    "              Index \"public.i4\"\n"
    " Column |  Type   | Key? |    Definition     \n"
    "--------+---------+------+-------------------\n"
    " expr   | integer | yes  | (a_new2 + c_new2)\n"
    " c_new  | integer | yes  | c_new2\n"
    "lsm, for table \"public.t\"\n\n");

  check_index_schema("i_part", {"a", "b"},
    "  Partitioned index \"public.i_part\"\n"
    " Column |  Type   | Key? | Definition \n"
    "--------+---------+------+------------\n"
    " a      | integer | yes  | a_new2\n"
    " b      | integer | yes  | b\n"
    "lsm, for table \"public.t_part\"\n"
    "Number of partitions: 1 (Use \\d+ to list them.)\n\n");

  check_index_schema("i2_part", {"b", "c"},
    "  Partitioned index \"public.i2_part\"\n"
    " Column |  Type   | Key? | Definition \n"
    "--------+---------+------+------------\n"
    " b      | integer | yes  | b\n"
    " c      | integer | no   | c_new2\n"
    "lsm, for table \"public.t_part\"\n"
    "Number of partitions: 1 (Use \\d+ to list them.)\n\n");

  check_index_schema("i3_part", {"a_new", "c_new"},
    "  Partitioned index \"public.i3_part\"\n"
    " Column |  Type   | Key? | Definition \n"
    "--------+---------+------+------------\n"
    " a_new  | integer | yes  | a_new2\n"
    " c_new  | integer | yes  | c_new2\n"
    "lsm, for table \"public.t_part\"\n"
    "Number of partitions: 1 (Use \\d+ to list them.)\n\n");

  check_index_schema("i4_part", {"expr", "c_new"},
    "     Partitioned index \"public.i4_part\"\n"
    " Column |  Type   | Key? |    Definition     \n"
    "--------+---------+------+-------------------\n"
    " expr   | integer | yes  | (a_new2 + c_new2)\n"
    " c_new  | integer | yes  | c_new2\n"
    "lsm, for table \"public.t_part\"\n"
    "Number of partitions: 1 (Use \\d+ to list them.)\n\n");
}

TEST_F(YsqlMajorUpgradeTest, NamespaceMapConsistencyAfterFailedUpgrade) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  // Enable failure injection for second yugabyte creation attempt
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "TEST_fail_yugabyte_namespace_creation_on_second_attempt", "true"));

  // First upgrade attempt - will fail during pg_restore when creating yugabyte
  ASSERT_NOK(PerformYsqlMajorCatalogUpgrade());

  // Rollback the failed upgrade
  ASSERT_OK(RollbackYsqlMajorCatalogVersion());

  // Disable the failure injection
  ASSERT_OK(cluster_->SetFlagOnMasters(
      "TEST_fail_yugabyte_namespace_creation_on_second_attempt", "false"));

  // Second upgrade attempt should succeed (this would fail without the fix)
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  // Complete the upgrade
  ASSERT_OK(RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(FinalizeUpgrade());

  // Verify we can connect and query
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte"));
  ASSERT_OK(conn.Fetch("SELECT 1"));
}

TEST_F(YsqlMajorUpgradeTest, TestQuotationIndex) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

  ASSERT_OK(conn.Execute("CREATE TABLE t1(col int);"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX i1 on t1(col asc);"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD CONSTRAINT \"c1 new check\" UNIQUE USING INDEX i1;"));
  ASSERT_OK(UpgradeClusterToMixedMode());
}

}  // namespace yb
