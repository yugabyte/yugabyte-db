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

#include "yb/integration-tests/upgrade-tests/pg15_upgrade_test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {

class Pg15UpgradeTest : public Pg15UpgradeTestBase {
 public:
  Pg15UpgradeTest() = default;

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

TEST_F(Pg15UpgradeTest, CheckVersion) {
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

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    auto version = ASSERT_RESULT(conn.FetchRowAsString(kSelectVersion));
    ASSERT_STR_CONTAINS(version, current_version_info().version_number());
  }

  ysql_catalog_config = ASSERT_RESULT(DumpYsqlCatalogConfig());
  ASSERT_STR_CONTAINS(ysql_catalog_config, "catalog_version: 15");
}

TEST_F(Pg15UpgradeTest, SimpleTableUpgrade) { ASSERT_OK(TestUpgradeWithSimpleTable()); }

TEST_F(Pg15UpgradeTest, SimpleTableRollback) { ASSERT_OK(TestRollbackWithSimpleTable()); }

TEST_F(Pg15UpgradeTest, BackslashD) {
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

  result = ASSERT_RESULT(ExecuteViaYsqlshOnTs(kBackslashD, kMixedModeTserverPg15));
  ASSERT_EQ(result, kExpectedResult);
  result = ASSERT_RESULT(ExecuteViaYsqlshOnTs(kBackslashD, kMixedModeTserverPg11));
  ASSERT_EQ(result, kExpectedResult);

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // Verify the result from a random tserver.
  result = ASSERT_RESULT(ExecuteViaYsqlsh(kBackslashD));
  ASSERT_EQ(result, kExpectedResult);
}

TEST_F(Pg15UpgradeTest, Comments) {
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

TEST_F(Pg15UpgradeTest, Schemas) {
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

TEST_F(Pg15UpgradeTest, Sequences) {
  ASSERT_OK(cluster_->AddAndSetExtraFlag("ysql_sequence_cache_minval", "1"));
  // As documented in the daemon->AddExtraFlag call, a restart is required to apply the flag.
  // We must Shutdown before we can Restart.
  cluster_->Shutdown();
  ASSERT_OK(cluster_->Restart());

  const auto kSelectNextVal = "SELECT nextval('$0')";
  const auto kSequencePg11 = "seq_pg11";
  const auto kSequencePg15 = "seq_pg15";
  int seq_val_pg11 = 1;
  int seq_val_pg15 = 1;

  ASSERT_OK(ExecuteStatement(Format("CREATE SEQUENCE $0", kSequencePg11)));

  auto take_3_values = [&kSelectNextVal](pgwrapper::PGConn& conn, const std::string& sequence,
                                         int& seq_val) {
    for (int i = 0; i < 3; i++) {
      const auto result = ASSERT_RESULT(
          conn.FetchRow<pgwrapper::PGUint64>(Format(kSelectNextVal, sequence)));
      ASSERT_EQ(seq_val++, result);
    }
  };

  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(take_3_values(conn, kSequencePg11, seq_val_pg11));
  }

  ASSERT_OK(UpgradeClusterToMixedMode());

  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(take_3_values(conn, kSequencePg11, seq_val_pg11));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(take_3_values(conn, kSequencePg11, seq_val_pg11));
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // Take three values from a random tserver
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(take_3_values(conn, kSequencePg11, seq_val_pg11));
  }

  ASSERT_OK(ExecuteStatement(Format("CREATE SEQUENCE $0 CACHE 1", kSequencePg15)));

  // Take three values from a random tserver, twice (to validate caching on the new sequence)
  for (int i = 0; i < 2; i++) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(take_3_values(conn, kSequencePg11, seq_val_pg11));
    ASSERT_NO_FATALS(take_3_values(conn, kSequencePg15, seq_val_pg15));
  }
}

TEST_F(Pg15UpgradeTest, MultipleDatabases) {
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

TEST_F(Pg15UpgradeTest, DatabaseWithDisallowedConnections) {
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
  ASSERT_NOK_STR_CONTAINS(UpgradeClusterToMixedMode(), "Failed to run pg_upgrade");
}

TEST_F(Pg15UpgradeTest, Template1) {
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
        auto result = ASSERT_RESULT(ExecuteViaYsqlshOnTs("\\dy", tserver, db_name));
        ASSERT_STR_CONTAINS(result, kEventTrigger);
      }
      // Check that objects created in the schema are visible only in that schema.
      {
        auto result = ASSERT_RESULT(ExecuteViaYsqlshOnTs("\\df", tserver, db_name));
        ASSERT_STR_NOT_CONTAINS(result, kSchema);
        ASSERT_STR_NOT_CONTAINS(result, kFunctionInSchema);

        auto result_schema = ASSERT_RESULT(ExecuteViaYsqlshOnTs(Format("\\df $0.*", kSchema),
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
      auto all_functions = ASSERT_RESULT(ExecuteViaYsqlsh(Format("\\df *.*", kSchema), db_name));
      ASSERT_STR_NOT_CONTAINS(all_functions, kFunctionInSchema);
    }
  }
}

TEST_F(Pg15UpgradeTest, FunctionWithSemicolons) {
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

TEST_F(Pg15UpgradeTest, Matviews) {
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

TEST_F(Pg15UpgradeTest, PartitionedTables) {
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

class Pg15UpgradeTestWithAuth : public Pg15UpgradeTest {
 public:
  Pg15UpgradeTestWithAuth() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.enable_ysql_auth = true;
    Pg15UpgradeTest::SetUpOptions(opts);
  }
};

// Make sure upgrade succeeds in non auth universes even if there is no tserver on the master node.
TEST_F(Pg15UpgradeTest, NoTserverOnMasterNode) {
  static const MonoDelta no_delay_between_nodes = 0s;
  ASSERT_OK(RestartAllMastersInCurrentVersion(no_delay_between_nodes));

  auto master_tserver = ASSERT_RESULT(StopMasterLeaderTServer());

  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());
  ASSERT_OK(master_tserver->Restart());
  ASSERT_OK(WaitForClusterToStabilize());

  ASSERT_OK(RestartAllTServersInCurrentVersion(no_delay_between_nodes));
  ASSERT_OK(FinalizeUpgrade());
}

// Make sure upgrade fails in auth enabled universes if there is no tserver on the master node.
TEST_F(Pg15UpgradeTestWithAuth, NoTserverOnMasterNode) {
  static const MonoDelta no_delay_between_nodes = 0s;
  ASSERT_OK(RestartAllMastersInCurrentVersion(no_delay_between_nodes));

  auto master_tserver = ASSERT_RESULT(StopMasterLeaderTServer());

  ASSERT_NOK_STR_CONTAINS(PerformYsqlMajorCatalogUpgrade(), "Failed to run pg_upgrade");
  ASSERT_OK(master_tserver->Restart());
}

TEST_F(Pg15UpgradeTestWithAuth, UpgradeAuthEnabledUniverse) {
  ASSERT_OK(TestUpgradeWithSimpleTable());
}

TEST_F(Pg15UpgradeTest, GlobalBreakingDDL) {
  ASSERT_OK(ExecuteStatements(
    {"CREATE USER test",
     "DROP USER test"}));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
}

TEST_F(Pg15UpgradeTest, Indexes) {
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
}  // namespace yb
