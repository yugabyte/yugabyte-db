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

#include <regex>

#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

struct UpgradeIncompatibilityCheck {
  std::initializer_list<std::string> setup_stmts;
  std::initializer_list<std::string> expected_errors;
  std::initializer_list<std::string> teardown_stmts;
};

using YsqlMajorUpgradeCheckTest = YsqlMajorUpgradeTestBase;

static const std::initializer_list<UpgradeIncompatibilityCheck> kCheckList{
    {// check_proper_datallowconn
     .setup_stmts = {"ALTER DATABASE postgres WITH allow_connections FALSE"},
     .expected_errors = {"postgres", "All non-template0 databases must allow connections"},
     .teardown_stmts = {"ALTER DATABASE postgres WITH allow_connections TRUE"}},

    {// check_for_composite_data_type_usage
     .setup_stmts = {"CREATE TABLE system_composite_test (id int primary key, authid pg_authid)"},
     .expected_errors =
         {"public.system_composite_test.authid",
          "Your installation contains system-defined composite type(s) in user tables"},
     .teardown_stmts = {"DROP TABLE system_composite_test"}},

    {// check_for_reg_data_type_usage
     .setup_stmts = {"CREATE TABLE reg_check (a int, b regproc)"},
     .expected_errors =
         {"public.reg_check.b",
          "Your installation contains one of the reg* data types in user tables"},
     .teardown_stmts = {"DROP TABLE reg_check"}},

    {// check_for_user_defined_postfix_ops
     .setup_stmts =
         {"CREATE FUNCTION ident(integer) "
          "RETURNS integer "
          "AS $$ "
          "BEGIN "
          "    RETURN $1; "
          "END; "
          "$$ LANGUAGE plpgsql",
          "CREATE OPERATOR !!! (LEFTARG = integer, PROCEDURE = ident)"},
     .expected_errors =
         {"public.!!! (pg_catalog.int4, NONE)",
          "Your installation contains user-defined postfix operators"},
     .teardown_stmts = {"DROP FUNCTION ident(integer) CASCADE"}},

    {// check_for_incompatible_polymorphics
     .setup_stmts = {"CREATE AGGREGATE array_accum (ANYELEMENT)(sfunc = array_append, stype = "
                     "ANYARRAY, initcond = '{}')"},
     .expected_errors =
         {"aggregate: public.array_accum(anyelement)",
          "Your installation contains user-defined objects that refer to internal",
          "polymorphic functions with arguments of type \"anyarray\" or \"anyelement\""},
     .teardown_stmts = {"DROP AGGREGATE array_accum (ANYELEMENT);"}},

    {// old_11_check_for_sql_identifier_data_type_usage
     .setup_stmts = {"CREATE TABLE sql_identifier_test (id int primary key, d "
                     "information_schema.sql_identifier)"},
     .expected_errors =
         {"public.sql_identifier_test.d",
          "Your installation contains the \"sql_identifier\" data type"},
     .teardown_stmts = {"DROP TABLE sql_identifier_test"}},

    {// yb_check_invalid_indexes
     .setup_stmts =
         {"CREATE TABLE invalid_index_test (id int primary key, data text)",
          "CREATE INDEX idx_invalid ON invalid_index_test (data)",
          // Simulate a failed CREATE INDEX by manually setting indisvalid to false
          "UPDATE pg_index SET indisvalid = false WHERE indexrelid = 'idx_invalid'::regclass"},
     .expected_errors =
         {"public.idx_invalid",
          "Your installation contains invalid indexes that must be fixed",
          "\\c yugabyte",
          "DROP INDEX public.idx_invalid;"},
     .teardown_stmts =
         {"DROP TABLE invalid_index_test"}},

    {// yb_check_yb_role_prefix
     .setup_stmts = {"CREATE ROLE yb_test_role"},
     .expected_errors =
         {"yb_test_role",
          "Your installation contains roles starting with \"yb_\"."},
     .teardown_stmts = {"DROP ROLE yb_test_role"}}};

// The following checks are not used in YugabyteDB:
// check_for_prepared_transactions
// check_for_isn_and_int8_passing_mismatch
// check_for_user_defined_encoding_conversions
// check_for_tables_with_oids
// old_9_6_check_for_unknown_data_type_usage, old_9_6_invalidate_hash_indexes
// check_for_pg_role_prefix
// check_for_jsonb_9_4_usage
// old_9_3_check_for_line_data_type_usage

TEST_F(YsqlMajorUpgradeCheckTest, PgUpgradeChecks) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

  // Run each check in isolation.
  for (const auto& check : kCheckList) {
    for (const auto& setup_stmt : check.setup_stmts) {
      ASSERT_OK(conn.Execute(setup_stmt));
    }

    ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>(check.expected_errors)));

    // Make sure we can recover from the error.
    for (const auto& teardown_stmt : check.teardown_stmts) {
      ASSERT_OK(conn.Execute(teardown_stmt));
    }

    ASSERT_OK(ValidateUpgradeCompatibility());
  }

// Disabled the re-upgrade step on debug builds because it times out.
#ifndef NDEBUG
  return;
#endif

  // Setup all failures at once.
  std::vector<std::string> all_errors;
  for (const auto& check : kCheckList) {
    for (const auto& setup_stmt : check.setup_stmts) {
      ASSERT_OK(conn.Execute(setup_stmt));
    }

    all_errors.insert(all_errors.end(), check.expected_errors.begin(), check.expected_errors.end());
  }

  // Everything should have failed.
  ASSERT_OK(ValidateUpgradeCompatibilityFailure(all_errors));

  for (const auto& check : kCheckList) {
    for (const auto& teardown_stmt : check.teardown_stmts) {
      ASSERT_OK(conn.Execute(teardown_stmt));
    }
  }

  ASSERT_OK(ValidateUpgradeCompatibility());

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  // Running validation on the upgraded cluster should fail since its already on the higher version.
  ASSERT_OK(ValidateUpgradeCompatibilityFailure(
      "This version of the utility can only be used for checking YSQL version 11. The cluster is "
      "currently on YSQL version 15"));
}

TEST_F(YsqlMajorUpgradeCheckTest, CheckUpgradeCompatibilityGuc) {
  // Whether or not yb_major_version_upgrade_compatibility is enabled, pg_upgrade --check will not
  // error.

  ASSERT_OK(
      SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kBackwardsCompatible));
  ASSERT_OK(ValidateUpgradeCompatibility());

  ASSERT_OK(SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kNone));
  ASSERT_OK(ValidateUpgradeCompatibility());

  // However, when we actually run the YSQL upgrade, pg_upgrade will error since now
  // ysql_yb_major_version_upgrade_compatibility is not set.
  for (auto* master : cluster_->master_daemons()) {
    ASSERT_OK(RestartMasterInCurrentVersion(*master, /*wait_for_cluster_to_stabilize=*/false));
  }
  ASSERT_OK(WaitForClusterToStabilize());

  auto log_waiter =
      cluster_->GetMasterLogWaiter("yb_major_version_upgrade_compatibility must be set to 11");
  ASSERT_NOK_STR_CONTAINS(PerformYsqlMajorCatalogUpgrade(), kPgUpgradeFailedError);
  ASSERT_TRUE(log_waiter.IsEventOccurred());
}

TEST_F(YsqlMajorUpgradeCheckTest, UsersAndRoles) {
  auto escape_single_quote = [](const std::string& str) {
    return std::regex_replace(str, std::regex("'"), "''");
  };
  auto escape_double_quote = [](const std::string& str) {
    return std::regex_replace(str, std::regex("\""), "\"\"");
  };

  auto ts = cluster_->tablet_server(0);

  // Make sure pg_upgrade --check fails if the yugabyte user is not a superuser.
  {
    const auto postgres_user = "postgres";
    const auto pg_conn_settings = pgwrapper::PGConnSettings{
        .host = ts->bind_host(),
        .port = ts->ysql_port(),
        .dbname = "yugabyte",
        .user = postgres_user};

    auto pg_conn = ASSERT_RESULT(pgwrapper::PGConnBuilder(pg_conn_settings).Connect());
    ASSERT_OK(pg_conn.Execute("DROP USER yugabyte"));
    ASSERT_OK(ValidateUpgradeCompatibilityFailure("The 'yugabyte' user is missing", postgres_user));

    ASSERT_OK(pg_conn.Execute("CREATE USER yugabyte"));

    ASSERT_OK(ValidateUpgradeCompatibilityFailure(
        "The 'yugabyte' user is missing the 'rolsuper' attribute", postgres_user));

    ASSERT_OK(pg_conn.Execute("DROP USER yugabyte"));
    ASSERT_OK(pg_conn.Execute(
        "CREATE USER yugabyte SUPERUSER INHERIT CREATEROLE CREATEDB LOGIN REPLICATION BYPASSRLS"));
    ASSERT_OK(ValidateUpgradeCompatibility(postgres_user));
  }

  // Change the yugabyte password to make sure if works after the upgrade.
  // Including quotes in password to make sure it works.
  const auto new_yb_password = "yb_\"secure\"\"_'pass''";
  {
    // Escape single quotes in the sql string.
    ASSERT_OK(ExecuteStatement(
        Format("ALTER USER yugabyte PASSWORD '$0'", escape_single_quote(new_yb_password))));
  }

  const auto conn_settings = pgwrapper::PGConnSettings{
      .host = ts->bind_host(),
      .port = ts->ysql_port(),
      .dbname = "yugabyte",
      .user = "yugabyte",
      .password = escape_double_quote(new_yb_password)};

  auto conn = ASSERT_RESULT(pgwrapper::PGConnBuilder(conn_settings).Connect());

  // Create users with special characters in their names.
  auto special_role_names = {"user with space", "user_\"_with_\"\"_different' quotes''"};
  for (const auto& role_name : special_role_names) {
    // Escape double quotes in the sql string.
    ASSERT_OK(conn.ExecuteFormat("CREATE ROLE \"$0\"", escape_double_quote(role_name)));
  }

  // Create roles alice and bob, and role carol with membership in alice. Make sure table created by
  // alice can be accessed by alice and carol but not bob.
  ASSERT_OK(conn.Execute("CREATE ROLE alice LOGIN"));
  ASSERT_OK(conn.Execute("CREATE ROLE bob LOGIN"));
  ASSERT_OK(conn.Execute("CREATE ROLE carol IN ROLE alice LOGIN"));

  ASSERT_OK(conn.Execute("SET ROLE alice"));
  ASSERT_OK(conn.Execute("CREATE TABLE t_alice (a int)"));
  ASSERT_OK(conn.Execute("INSERT INTO t_alice VALUES (1)"));
  ASSERT_OK(conn.Execute("RESET ROLE"));

  auto check_roles = [&](pgwrapper::PGConn& conn) {
    auto get_table_count = [&conn]() {
      return conn.FetchRow<pgwrapper::PGUint64>("SELECT COUNT(*) FROM t_alice");
    };
    ASSERT_OK(conn.Execute("SET ROLE alice"));
    auto count = ASSERT_RESULT(get_table_count());
    ASSERT_EQ(count, 1);

    ASSERT_OK(conn.Execute("SET ROLE bob"));
    ASSERT_NOK_STR_CONTAINS(get_table_count(), "ERROR:  permission denied for table t_alice");

    ASSERT_OK(conn.Execute("SET ROLE carol"));
    count = ASSERT_RESULT(get_table_count());
    ASSERT_EQ(count, 1);

    ASSERT_OK(conn.Execute("RESET ROLE"));
  };

  ASSERT_NO_FATALS(check_roles(conn));

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  conn = ASSERT_RESULT(pgwrapper::PGConnBuilder(conn_settings).Connect());
  for (const auto& role_name : special_role_names) {
    LOG(INFO) << "Checking role: " << role_name;
    auto res_role_name = ASSERT_RESULT(conn.FetchRow<std::string>(Format(
        "SELECT rolname FROM pg_roles WHERE rolname = '$0'", escape_single_quote(role_name))));
    ASSERT_STR_EQ(role_name, res_role_name);
  }

  ASSERT_NO_FATALS(check_roles(conn));
}

TEST_F(YsqlMajorUpgradeCheckTest, InvalidIndexes) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  TestThreadHolder thread_holder;

  // Create test databases and schemas
  ASSERT_OK(conn.Execute("CREATE DATABASE testdb1"));
  ASSERT_OK(conn.Execute("CREATE DATABASE testdb2"));
  ASSERT_OK(conn.Execute("CREATE SCHEMA testschema"));

  // Create invalid indexes in different databases and schemas

  // Default database, public schema
  ASSERT_OK(conn.Execute("CREATE TABLE test_invalid_public (id int primary key, data text)"));
  ASSERT_OK(conn.Execute("CREATE INDEX idx_same_name ON test_invalid_public (data)"));
  ASSERT_OK(conn.Execute(
      "UPDATE pg_index SET indisvalid = false WHERE indexrelid = 'idx_same_name'::regclass"));
  ASSERT_OK(conn.Execute("CREATE INDEX valid_index ON test_invalid_public (data)"));

  // Default database, custom schema
  ASSERT_OK(conn.Execute(
      "CREATE TABLE testschema.test_invalid_schema (id int primary key, data text)"));
  ASSERT_OK(conn.Execute("CREATE INDEX idx_same_name ON testschema.test_invalid_schema (data)"));
  ASSERT_OK(conn.Execute("UPDATE pg_index SET indisready = false WHERE "
    "indexrelid = 'testschema.idx_same_name'::regclass"));
  ASSERT_OK(conn.Execute("CREATE INDEX valid_index ON testschema.test_invalid_schema (data)"));

  // Different database 1
  auto conn_db1 = ASSERT_RESULT(cluster_->ConnectToDB("testdb1"));
  ASSERT_OK(conn_db1.Execute("CREATE TABLE test_invalid_db1 (id int primary key, data text)"));
  ASSERT_OK(conn_db1.Execute("INSERT INTO test_invalid_db1 VALUES (1, 'dup'), (2, 'dup')"));
  ASSERT_NOK(conn_db1.Execute("CREATE UNIQUE INDEX idx_unique_fail ON test_invalid_db1 (data)"));
  ASSERT_OK(conn_db1.Execute("CREATE INDEX valid_index ON test_invalid_db1 (data)"));

  // Different database 2
  auto conn_db2 = ASSERT_RESULT(cluster_->ConnectToDB("testdb2"));
  ASSERT_OK(conn_db2.Execute("CREATE SCHEMA otherschema"));
  ASSERT_OK(conn_db2.Execute(
      "CREATE TABLE otherschema.test_invalid_db2 (id int primary key, data text)"));
  ASSERT_OK(conn_db2.Execute(
      "CREATE INDEX idx_db2_invalid ON otherschema.test_invalid_db2 (data)"));
  ASSERT_OK(conn_db2.Execute("UPDATE pg_index SET indisvalid = false WHERE "
      "indexrelid = 'otherschema.idx_db2_invalid'::regclass"));
  ASSERT_OK(conn_db2.Execute("CREATE INDEX valid_index ON otherschema.test_invalid_db2 (data)"));

  // Test partitioned table (should pass)
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_partitioned (id int, data text) PARTITION BY RANGE (id)"));
  ASSERT_OK(conn.Execute("CREATE INDEX idx_part_invalid ON ONLY test_partitioned (data)"));
  ASSERT_OK(conn.Execute("CREATE INDEX idx_part_valid ON test_partitioned (data)"));

  // Create index, but block it before indisvalid is set.
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "postbackfill"));
  Status index_creation_status;
  std::promise<Status> index_creation_promise;
  auto index_creation_future = index_creation_promise.get_future();
  thread_holder.AddThreadFunctor([&conn, &index_creation_promise] {
    index_creation_promise.set_value(conn.Execute(
        "CREATE INDEX blocked_index ON test_invalid_public (data)"));
  });

  // Check that upgrade validation fails with comprehensive error message including DROP commands
  ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>{
      "public.idx_same_name",
      "testschema.idx_same_name",
      "public.idx_unique_fail",
      "otherschema.idx_db2_invalid",
      "Your installation contains invalid indexes that must be fixed",
      "\\c yugabyte",
      "DROP INDEX public.idx_same_name;",
      "DROP INDEX testschema.idx_same_name;",
      "\\c testdb1",
      "DROP INDEX public.idx_unique_fail;",
      "\\c testdb2",
      "DROP INDEX otherschema.idx_db2_invalid;"}));

  // Unblock index creation
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
  ASSERT_EQ(index_creation_future.wait_for(5min), std::future_status::ready);
  ASSERT_OK(index_creation_future.get());

  // Clean up invalid indexes using the DROP commands
  ASSERT_OK(conn.Execute("DROP INDEX public.idx_same_name"));
  ASSERT_OK(conn.Execute("DROP INDEX testschema.idx_same_name"));
  ASSERT_OK(conn_db1.Execute("DROP INDEX public.idx_unique_fail"));
  ASSERT_OK(conn_db2.Execute("DROP INDEX otherschema.idx_db2_invalid"));

  // Verify that validation now succeeds (partitioned table invalid index should still be allowed)
  ASSERT_OK(ValidateUpgradeCompatibility());
}

TEST_F(YsqlMajorUpgradeCheckTest, YbPrefixRoles) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

  // Create roles with "yb_" prefix
  ASSERT_OK(conn.Execute("CREATE ROLE yb_test_role1"));
  ASSERT_OK(conn.Execute("CREATE ROLE yb_test_role2 LOGIN"));

  ASSERT_OK(conn.Execute("CREATE ROLE normal_role"));

  // Check that upgrade validation fails with appropriate error message
  ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>{
      "yb_test_role1",
      "yb_test_role2",
      "Your installation contains roles starting with \"yb_\"."}));

  // Drop the roles
  ASSERT_OK(conn.Execute("DROP ROLE yb_test_role1"));
  ASSERT_OK(conn.Execute("DROP ROLE yb_test_role2"));

  // Verify that validation now succeeds
  ASSERT_OK(ValidateUpgradeCompatibility());
}
}  // namespace yb
