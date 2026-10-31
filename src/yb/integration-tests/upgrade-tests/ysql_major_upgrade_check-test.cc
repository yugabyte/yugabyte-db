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
#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/path_util.h"
#include "yb/util/subprocess.h"
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
          "Your installation contains system-defined composite types in user tables"},
     .teardown_stmts = {"DROP TABLE system_composite_test"}},

    {// check_for_reg_data_type_usage
     .setup_stmts = {"CREATE TABLE reg_check (a int, b regproc)"},
     .expected_errors =
         {"public.reg_check.b",
          "Your installation contains one of the reg* data types in user tables"},
     .teardown_stmts = {"DROP TABLE reg_check"}},

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

// The following checks only apply to PG13 or older sources, so a PG15 source skips them:
// check_for_user_defined_postfix_ops, check_for_incompatible_polymorphics,
// the sql_identifier check in data_types_usage_checks

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

TEST_F(YsqlMajorUpgradeCheckTest, StaleFunctionAclGrantors) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

  auto phase1_single_function = [this, &conn]() {
    // Phase 1: single function with stale grantor after ALTER FUNCTION OWNER TO.
    ASSERT_OK(conn.Execute("CREATE ROLE test_role1"));
    ASSERT_OK(conn.Execute("CREATE ROLE test_role2"));
    ASSERT_OK(conn.Execute("GRANT CREATE ON SCHEMA public TO test_role1"));

    ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION test_role1"));
    ASSERT_OK(conn.Execute(
        "CREATE FUNCTION public.test_func1(text) RETURNS bool "
        "AS 'SELECT true' LANGUAGE sql"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func1(text) TO PUBLIC"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func1(text) TO test_role2"));
    ASSERT_OK(conn.Execute("RESET SESSION AUTHORIZATION"));
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func1(text) OWNER TO test_role2"));

    // Verify pg_upgrade suggests the full repair script for the function.
    ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>{
        // test_func1(text)
        "ALTER FUNCTION \"public\".\"test_func1\"(text) OWNER TO \"test_role1\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func1\"(text) FROM PUBLIC;",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func1\"(text) FROM \"test_role1\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func1\"(text) FROM \"test_role2\";",
        "ALTER FUNCTION \"public\".\"test_func1\"(text) OWNER TO \"test_role2\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func1\"(text) TO PUBLIC;",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func1\"(text) TO \"test_role1\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func1\"(text) TO \"test_role2\";",
    }));

    // Mitigate the function and confirm check passes.
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func1(text) OWNER TO test_role1"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func1(text) FROM PUBLIC"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func1(text) FROM test_role1"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func1(text) FROM test_role2"));
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func1(text) OWNER TO test_role2"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func1(text) TO PUBLIC"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func1(text) TO test_role1"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func1(text) TO test_role2"));
    ASSERT_OK(ValidateUpgradeCompatibility());
  };

  auto phase2_overloaded_functions = [this, &conn]() {
    // Phase 2: overloaded functions with stale grantor after ALTER FUNCTION OWNER TO.
    ASSERT_OK(conn.Execute("CREATE ROLE test_role3"));
    ASSERT_OK(conn.Execute("CREATE ROLE test_role4"));
    ASSERT_OK(conn.Execute("GRANT CREATE ON SCHEMA public TO test_role3"));

    ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION test_role3"));
    ASSERT_OK(conn.Execute(
        "CREATE FUNCTION public.test_func2(text) RETURNS bool "
        "AS 'SELECT true' LANGUAGE sql"));
    ASSERT_OK(conn.Execute(
        "CREATE FUNCTION public.test_func2(int) RETURNS bool "
        "AS 'SELECT true' LANGUAGE sql"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func2(text) TO PUBLIC"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func2(int) TO test_role4"));
    ASSERT_OK(conn.Execute("RESET SESSION AUTHORIZATION"));
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func2(text) OWNER TO test_role4"));
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func2(int) OWNER TO test_role4"));

    // Verify pg_upgrade suggests per-signature repair scripts for each overload.
    ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>{
        // test_func2(integer)
        "ALTER FUNCTION \"public\".\"test_func2\"(integer) OWNER TO \"test_role3\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func2\"(integer) FROM PUBLIC;",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func2\"(integer) FROM \"test_role3\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func2\"(integer) FROM \"test_role4\";",
        "ALTER FUNCTION \"public\".\"test_func2\"(integer) OWNER TO \"test_role4\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func2\"(integer) TO PUBLIC;",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func2\"(integer) TO \"test_role3\";",

        // test_func2(text)
        "ALTER FUNCTION \"public\".\"test_func2\"(text) OWNER TO \"test_role3\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func2\"(text) FROM PUBLIC;",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func2\"(text) FROM \"test_role3\";",
        "ALTER FUNCTION \"public\".\"test_func2\"(text) OWNER TO \"test_role4\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func2\"(text) TO PUBLIC;",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func2\"(text) TO \"test_role3\";",
    }));

    // Mitigate both overloads and confirm check passes.
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func2(integer) OWNER TO test_role3"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func2(integer) FROM PUBLIC"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func2(integer) FROM test_role3"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func2(integer) FROM test_role4"));
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func2(integer) OWNER TO test_role4"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func2(integer) TO PUBLIC"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func2(integer) TO test_role3"));

    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func2(text) OWNER TO test_role3"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func2(text) FROM PUBLIC"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION public.test_func2(text) FROM test_role3"));
    ASSERT_OK(conn.Execute("ALTER FUNCTION public.test_func2(text) OWNER TO test_role4"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func2(text) TO PUBLIC"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION public.test_func2(text) TO test_role3"));
    ASSERT_OK(ValidateUpgradeCompatibility());
  };

  auto phase3_custom_schema = [this, &conn]() {
    // Phase 3: stale function in a custom schema.
    ASSERT_OK(conn.Execute("CREATE SCHEMA test_schema"));
    ASSERT_OK(conn.Execute("CREATE ROLE test_role5"));
    ASSERT_OK(conn.Execute("CREATE ROLE test_role6"));
    ASSERT_OK(conn.Execute("GRANT USAGE, CREATE ON SCHEMA test_schema TO test_role5"));

    ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION test_role5"));
    ASSERT_OK(conn.Execute(
        "CREATE FUNCTION test_schema.test_func3(text) RETURNS bool "
        "AS 'SELECT true' LANGUAGE sql"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION test_schema.test_func3(text) TO PUBLIC"));
    ASSERT_OK(conn.Execute(
        "GRANT EXECUTE ON FUNCTION test_schema.test_func3(text) TO test_role6"));
    ASSERT_OK(conn.Execute("RESET SESSION AUTHORIZATION"));
    ASSERT_OK(conn.Execute(
        "ALTER FUNCTION test_schema.test_func3(text) OWNER TO test_role6"));

    // Verify pg_upgrade suggests the full repair script with schema quoting.
    ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>{
        // test_func3(text) in test_schema
        "ALTER FUNCTION \"test_schema\".\"test_func3\"(text) OWNER TO \"test_role5\";",
        "REVOKE ALL ON FUNCTION \"test_schema\".\"test_func3\"(text) FROM PUBLIC;",
        "REVOKE ALL ON FUNCTION \"test_schema\".\"test_func3\"(text) FROM \"test_role5\";",
        "REVOKE ALL ON FUNCTION \"test_schema\".\"test_func3\"(text) FROM \"test_role6\";",
        "ALTER FUNCTION \"test_schema\".\"test_func3\"(text) OWNER TO \"test_role6\";",
        "GRANT EXECUTE ON FUNCTION \"test_schema\".\"test_func3\"(text) TO PUBLIC;",
        "GRANT EXECUTE ON FUNCTION \"test_schema\".\"test_func3\"(text) TO \"test_role5\";",
        "GRANT EXECUTE ON FUNCTION \"test_schema\".\"test_func3\"(text) TO \"test_role6\";",
    }));

    // Mitigate the function and confirm check passes.
    ASSERT_OK(conn.Execute("ALTER FUNCTION test_schema.test_func3(text) OWNER TO test_role5"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION test_schema.test_func3(text) FROM PUBLIC"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION test_schema.test_func3(text) FROM test_role5"));
    ASSERT_OK(conn.Execute("REVOKE ALL ON FUNCTION test_schema.test_func3(text) FROM test_role6"));
    ASSERT_OK(conn.Execute("ALTER FUNCTION test_schema.test_func3(text) OWNER TO test_role6"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION test_schema.test_func3(text) TO PUBLIC"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION test_schema.test_func3(text) TO test_role5"));
    ASSERT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION test_schema.test_func3(text) TO test_role6"));
    ASSERT_OK(ValidateUpgradeCompatibility());

    ASSERT_OK(conn.Execute("DROP FUNCTION test_schema.test_func3(text)"));
    ASSERT_OK(conn.Execute("REVOKE USAGE, CREATE ON SCHEMA test_schema FROM test_role5"));
    ASSERT_OK(conn.Execute("DROP SCHEMA test_schema"));
  };

  auto phase4_multiple_databases = [this, &conn]() {
    // Phase 4: stale functions in two databases sharing the same roles.
    constexpr const char* kTestDb1 = "test_db1";
    constexpr const char* kTestDb2 = "test_db2";

    ASSERT_OK(conn.Execute("CREATE DATABASE test_db1"));
    ASSERT_OK(conn.Execute("CREATE DATABASE test_db2"));
    ASSERT_OK(conn.Execute("CREATE ROLE test_role7"));
    ASSERT_OK(conn.Execute("CREATE ROLE test_role8"));

    auto setup_stale_function = [](pgwrapper::PGConn& conn_db) {
      ASSERT_OK(conn_db.Execute("GRANT CREATE ON SCHEMA public TO test_role7"));
      ASSERT_OK(conn_db.Execute("SET SESSION AUTHORIZATION test_role7"));
      ASSERT_OK(conn_db.Execute(
          "CREATE FUNCTION public.test_func4(text) RETURNS bool "
          "AS 'SELECT true' LANGUAGE sql"));
      ASSERT_OK(conn_db.Execute("GRANT EXECUTE ON FUNCTION public.test_func4(text) TO PUBLIC"));
      ASSERT_OK(conn_db.Execute("GRANT EXECUTE ON FUNCTION public.test_func4(text) TO test_role8"));
      ASSERT_OK(conn_db.Execute("RESET SESSION AUTHORIZATION"));
      ASSERT_OK(conn_db.Execute("ALTER FUNCTION public.test_func4(text) OWNER TO test_role8"));
    };

    auto conn_db1 = ASSERT_RESULT(cluster_->ConnectToDB(kTestDb1));
    setup_stale_function(conn_db1);

    auto conn_db2 = ASSERT_RESULT(cluster_->ConnectToDB(kTestDb2));
    setup_stale_function(conn_db2);

    // Verify pg_upgrade suggests the full repair script in each database.
    ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>{
        // test_db1
        "In database: test_db1",
        "ALTER FUNCTION \"public\".\"test_func4\"(text) OWNER TO \"test_role7\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func4\"(text) FROM PUBLIC;",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func4\"(text) FROM \"test_role7\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func4\"(text) FROM \"test_role8\";",
        "ALTER FUNCTION \"public\".\"test_func4\"(text) OWNER TO \"test_role8\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func4\"(text) TO PUBLIC;",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func4\"(text) TO \"test_role7\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func4\"(text) TO \"test_role8\";",

        // test_db2
        "In database: test_db2",
        "ALTER FUNCTION \"public\".\"test_func4\"(text) OWNER TO \"test_role7\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func4\"(text) FROM PUBLIC;",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func4\"(text) FROM \"test_role7\";",
        "REVOKE ALL ON FUNCTION \"public\".\"test_func4\"(text) FROM \"test_role8\";",
        "ALTER FUNCTION \"public\".\"test_func4\"(text) OWNER TO \"test_role8\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func4\"(text) TO PUBLIC;",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func4\"(text) TO \"test_role7\";",
        "GRANT EXECUTE ON FUNCTION \"public\".\"test_func4\"(text) TO \"test_role8\";",
    }));

    auto mitigate_stale_function = [](pgwrapper::PGConn& conn_db) {
      ASSERT_OK(conn_db.Execute("ALTER FUNCTION public.test_func4(text) OWNER TO test_role7"));
      ASSERT_OK(conn_db.Execute("REVOKE ALL ON FUNCTION public.test_func4(text) FROM PUBLIC"));
      ASSERT_OK(conn_db.Execute("REVOKE ALL ON FUNCTION public.test_func4(text) FROM test_role7"));
      ASSERT_OK(conn_db.Execute("REVOKE ALL ON FUNCTION public.test_func4(text) FROM test_role8"));
      ASSERT_OK(conn_db.Execute("ALTER FUNCTION public.test_func4(text) OWNER TO test_role8"));
      ASSERT_OK(conn_db.Execute("GRANT EXECUTE ON FUNCTION public.test_func4(text) TO PUBLIC"));
      ASSERT_OK(conn_db.Execute("GRANT EXECUTE ON FUNCTION public.test_func4(text) TO test_role7"));
      ASSERT_OK(conn_db.Execute("GRANT EXECUTE ON FUNCTION public.test_func4(text) TO test_role8"));
    };

    mitigate_stale_function(conn_db1);
    mitigate_stale_function(conn_db2);
    ASSERT_OK(ValidateUpgradeCompatibility());

    ASSERT_OK(conn_db1.Execute("DROP FUNCTION public.test_func4(text)"));
    ASSERT_OK(conn_db2.Execute("DROP FUNCTION public.test_func4(text)"));
  };

  auto cleanup = [&conn]() {
    ASSERT_OK(conn.Execute("DROP FUNCTION public.test_func1(text), "
      "public.test_func2(text), public.test_func2(int)"));
    ASSERT_OK(conn.Execute("DROP DATABASE test_db1"));
    ASSERT_OK(conn.Execute("DROP DATABASE test_db2"));
    ASSERT_OK(conn.Execute("REVOKE CREATE ON SCHEMA public FROM test_role1"));
    ASSERT_OK(conn.Execute("REVOKE CREATE ON SCHEMA public FROM test_role3"));
    ASSERT_OK(conn.Execute("DROP ROLE test_role1, test_role2, test_role3, test_role4"));
    ASSERT_OK(conn.Execute("DROP ROLE test_role5, test_role6"));
    ASSERT_OK(conn.Execute("DROP ROLE test_role7, test_role8"));
  };

  ASSERT_NO_FATALS(phase1_single_function());
  ASSERT_NO_FATALS(phase2_overloaded_functions());
  ASSERT_NO_FATALS(phase3_custom_schema());
  ASSERT_NO_FATALS(phase4_multiple_databases());

  // Cleanup functions, schema grants, roles, and the extra databases.
  ASSERT_NO_FATALS(cleanup());
}

// The mitigation the check prints, verbatim.
const std::string kFunctionMitigationSql =
    "    WITH idents AS (\n"
    "  SELECT p.oid AS oid,\n"
    "         p.proname || '(' ||\n"
    "         coalesce(string_agg(t.typname::text, ', ' ORDER BY a.ord), '') || ')' AS obj\n"
    "  FROM pg_proc p\n  LEFT JOIN LATERAL unnest(p.proargtypes) WITH ORDINALITY AS a(typid, ord)"
    " ON true\n  LEFT JOIN pg_type t ON t.oid = a.typid\n"
    "  WHERE p.pronamespace = 'pg_catalog'::regnamespace\n"
    "  GROUP BY p.oid, p.proname)\n"
    "    UPDATE pg_proc SET proacl = (\n"
    "      SELECT initprivs FROM pg_init_privs pip\n"
    "      WHERE pip.objoid = pg_proc.oid\n"
    "        AND pip.classoid = 'pg_proc'::regclass\n"
    "        AND pip.objsubid = 0)\n"
    "    WHERE pg_proc.oid IN (SELECT oid FROM idents WHERE obj = ANY('{"
    "\"pg_get_publication_tables(text)\","
    "\"pg_stat_get_bgwriter_buf_written_checkpoints()\","
    "\"pg_stat_get_bgwriter_requested_checkpoints()\","
    "\"pg_stat_get_bgwriter_timed_checkpoints()\","
    "\"pg_stat_get_buf_fsync_backend()\","
    "\"pg_stat_get_buf_written_backend()\","
    "\"pg_stat_get_checkpoint_sync_time()\","
    "\"pg_stat_get_checkpoint_write_time()\","
    "\"interval_accum(_interval, interval)\","
    "\"interval_accum_inv(_interval, interval)\","
    "\"interval_avg(_interval)\","
    "\"interval_combine(_interval, _interval)\","
    "\"pg_logical_emit_message(bool, text, bytea)\","
    "\"pg_logical_emit_message(bool, text, text)\","
    "\"pg_read_file_old(text, int8, int8)\","
    "\"pg_rotate_logfile_old()\","
    "\"pg_create_logical_replication_slot(name, name, bool, bool, name, name)\","
    "\"json_strip_nulls(json)\","
    "\"jsonb_strip_nulls(jsonb)\","
    "\"pg_stat_have_stats(text, oid, oid)\","
    "\"pg_timezone_abbrevs()\","
    "\"big5_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"euc_cn_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"euc_jp_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"euc_kr_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"euc_tw_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"iso_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"koi8r_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"latin1_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"latin2_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"latin3_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"latin4_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_big5(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_euc_cn(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_euc_jp(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_euc_kr(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_euc_tw(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_iso(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_koi8r(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_latin1(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_latin2(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_latin3(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_latin4(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_sjis(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_win1250(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_win1251(int4, int4, cstring, internal, int4, bool)\","
    "\"mic_to_win866(int4, int4, cstring, internal, int4, bool)\","
    "\"sjis_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"win1250_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"win1251_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"win866_to_mic(int4, int4, cstring, internal, int4, bool)\","
    "\"pg_replication_origin_session_setup(text)\"}'::text[]))\n"
    "  AND NOT EXISTS (\n      SELECT 1 FROM pg_depend d\n"
    "      WHERE d.classid = 'pg_proc'::regclass\n"
    "        AND d.objid = pg_proc.oid\n"
    "        AND d.objsubid = 0\n"
    "        AND d.deptype = 'e')\n"
    "  AND proacl IS DISTINCT FROM (\n"
    "      SELECT initprivs FROM pg_init_privs pip\n"
    "      WHERE pip.objoid = pg_proc.oid\n"
    "        AND pip.classoid = 'pg_proc'::regclass\n"
    "        AND pip.objsubid = 0)\n"
    "    ;\n";

const std::string kColumnMitigationSql =
    "    UPDATE pg_attribute a SET attacl = (\n"
    "      SELECT initprivs FROM pg_init_privs pip\n"
    "      WHERE pip.objoid = a.attrelid\n"
    "        AND pip.classoid = 'pg_class'::regclass\n"
    "        AND pip.objsubid = a.attnum)\n"
    "    WHERE a.attrelid IN (\n"
    "          SELECT c.oid FROM pg_class c\n"
    "          WHERE c.relnamespace = 'pg_catalog'::regnamespace)\n"
    "      AND a.attnum > 0 AND NOT a.attisdropped\n"
    "      AND (SELECT c.relname FROM pg_class c WHERE c.oid = a.attrelid)\n"
    "          || '.' || a.attname = ANY('{"
    "\"pg_collation.colliculocale\","
    "\"pg_database.daticulocale\","
    "\"pg_stat_bgwriter.buffers_backend\","
    "\"pg_stat_bgwriter.buffers_backend_fsync\","
    "\"pg_stat_bgwriter.buffers_checkpoint\","
    "\"pg_stat_bgwriter.checkpoint_sync_time\","
    "\"pg_stat_bgwriter.checkpoint_write_time\","
    "\"pg_stat_bgwriter.checkpoints_req\","
    "\"pg_stat_bgwriter.checkpoints_timed\","
    "\"pg_stat_progress_vacuum.max_dead_tuples\","
    "\"pg_stat_progress_vacuum.num_dead_tuples\","
    "\"pg_attribute.attcacheoff\","
    "\"pg_backend_memory_contexts.parent\","
    "\"pg_stat_wal.wal_sync\","
    "\"pg_stat_wal.wal_sync_time\","
    "\"pg_stat_wal.wal_write\","
    "\"pg_stat_wal.wal_write_time\","
    "\"pg_stat_subscription_stats.sync_error_count\","
    "\"gv$pg_stat_statements.blk_read_time\","
    "\"gv$pg_stat_statements.blk_write_time\","
    "\"pg_stat_statements_with_server_uuid.blk_read_time\","
    "\"pg_stat_statements_with_server_uuid.blk_write_time\"}'::text[])\n"
    "  AND NOT EXISTS (\n      SELECT 1 FROM pg_depend d\n"
    "      WHERE d.classid = 'pg_class'::regclass\n"
    "        AND d.objid = a.attrelid\n"
    "        AND d.objsubid = 0\n"
    "        AND d.deptype = 'e')\n"
    "  AND a.attacl IS DISTINCT FROM (\n"
    "      SELECT initprivs FROM pg_init_privs pip\n"
    "      WHERE pip.objoid = a.attrelid\n"
    "        AND pip.classoid = 'pg_class'::regclass\n"
    "        AND pip.objsubid = a.attnum)\n"
    "    ;\n";

// Reconnects because the write bypasses catalog cache invalidation.
Status ResetAcls(
    ExternalMiniCluster* cluster, pgwrapper::PGConn* conn, const std::string& sql,
    const std::string& db_name = "yugabyte") {
  RETURN_NOT_OK(conn->Execute("SET search_path = pg_catalog"));
  RETURN_NOT_OK(conn->Execute("SET yb_non_ddl_txn_for_sys_tables_allowed TO on"));
  RETURN_NOT_OK(conn->Execute(sql));
  RETURN_NOT_OK(conn->Execute("RESET yb_non_ddl_txn_for_sys_tables_allowed"));
  RETURN_NOT_OK(conn->Execute("RESET search_path"));
  *conn = VERIFY_RESULT(cluster->ConnectToDB(db_name));
  return Status::OK();
}

TEST_F(YsqlMajorUpgradeCheckTest, RemovedCatalogAclsFunctions) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("CREATE ROLE test_role1"));

  const std::vector<std::string> expected_function_failure = {
      "Your installation contains modified ACL entries on pg_catalog",
      "In database: yugabyte",
      // Removed outright.
      "Function: pg_catalog.pg_stat_get_checkpoint_write_time()",
      "Function: pg_catalog.interval_accum(_interval, interval)",
      "Function: pg_catalog.mic_to_latin1(int4, int4, cstring, internal, int4, bool)",
      // Name survives, arguments do not.
      "Function: pg_catalog.json_strip_nulls(json)",
      "Function: pg_catalog.pg_logical_emit_message(bool, text, text)",
      // Removed identity that also carries OUT columns.
      "Function: pg_catalog.pg_timezone_abbrevs()",
      "    UPDATE pg_proc SET proacl = (",
  };

  // Sub-test 1: GRANT EXECUTE on every pg_catalog function to a named role. Every function drifts,
  // so the check must still skip the functions that survive into PG19, such as the two below.
  auto function_grant = [this, &conn, &expected_function_failure]() -> Status {
    RETURN_NOT_OK(
        conn.Execute("GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA pg_catalog TO test_role1"));

    StringWaiterLogSink surviving_date("Function: pg_catalog.date(timestamptz)");
    StringWaiterLogSink surviving_point("Function: pg_catalog.point(box)");

    RETURN_NOT_OK(ValidateUpgradeCompatibilityFailure(expected_function_failure));

    SCHECK(
        !surviving_date.IsEventOccurred(), IllegalState,
        "date(timestamp with time zone) survives PG19 and must not be reported");
    SCHECK(
        !surviving_point.IsEventOccurred(), IllegalState,
        "point(box) survives PG19 and must not be reported");

    RETURN_NOT_OK(ResetAcls(cluster_.get(), &conn, kFunctionMitigationSql));
    return ValidateUpgradeCompatibility();
  };

  // Sub-test 2: the same drift from the other direction, over the same objects.
  auto function_revoke = [this, &conn, &expected_function_failure]() -> Status {
    RETURN_NOT_OK(conn.Execute("REVOKE ALL ON ALL FUNCTIONS IN SCHEMA pg_catalog FROM PUBLIC"));

    RETURN_NOT_OK(ValidateUpgradeCompatibilityFailure(expected_function_failure));

    RETURN_NOT_OK(ResetAcls(cluster_.get(), &conn, kFunctionMitigationSql));
    return ValidateUpgradeCompatibility();
  };

  // Sub-test 3: a grant on one removed overload only.
  auto single_overload_grant = [this, &conn]() -> Status {
    RETURN_NOT_OK(conn.Execute(
        "GRANT EXECUTE ON FUNCTION pg_catalog.json_strip_nulls(json) TO test_role1"));

    RETURN_NOT_OK(ValidateUpgradeCompatibilityFailure(
        std::vector<std::string>{
            "Function: pg_catalog.json_strip_nulls(json)",
            "    UPDATE pg_proc SET proacl = ("}));

    // A function that survives unchanged must not trip the check at all.
    RETURN_NOT_OK(ResetAcls(cluster_.get(), &conn, kFunctionMitigationSql));
    RETURN_NOT_OK(conn.Execute("GRANT EXECUTE ON FUNCTION pg_catalog.abs(integer) TO test_role1"));
    RETURN_NOT_OK(ValidateUpgradeCompatibility());

    RETURN_NOT_OK(ResetAcls(cluster_.get(), &conn, kFunctionMitigationSql));
    return ValidateUpgradeCompatibility();
  };

  // Sub-test 4: an empty ACL array is drift too, and no GRANT or REVOKE can produce one.
  auto empty_acl = [this, &conn]() -> Status {
    RETURN_NOT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed TO on"));
    RETURN_NOT_OK(conn.Execute(
        "UPDATE pg_proc SET proacl = '{}'::aclitem[] "
        "WHERE proname = 'pg_stat_get_checkpoint_write_time' "
        "AND pronamespace = 'pg_catalog'::regnamespace"));
    RETURN_NOT_OK(conn.Execute("RESET yb_non_ddl_txn_for_sys_tables_allowed"));

    RETURN_NOT_OK(ValidateUpgradeCompatibilityFailure(
        std::vector<std::string>{
            "Function: pg_catalog.pg_stat_get_checkpoint_write_time()",
            "    UPDATE pg_proc SET proacl = ("}));

    RETURN_NOT_OK(ResetAcls(cluster_.get(), &conn, kFunctionMitigationSql));
    return ValidateUpgradeCompatibility();
  };

  ASSERT_OK(function_grant());
  ASSERT_OK(function_revoke());
  ASSERT_OK(single_overload_grant());
  ASSERT_OK(empty_acl());
}

TEST_F(YsqlMajorUpgradeCheckTest, RemovedCatalogAclsColumns) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("CREATE ROLE test_role1"));

  // One dropped catalog column, one dropped view column. A REVOKE would be a no-op.
  ASSERT_OK(conn.Execute("GRANT SELECT (colliculocale) ON pg_catalog.pg_collation TO test_role1"));
  ASSERT_OK(conn.Execute(
      "GRANT SELECT (checkpoints_timed) ON pg_catalog.pg_stat_bgwriter TO test_role1"));

  ASSERT_OK(ValidateUpgradeCompatibilityFailure(
      std::vector<std::string>{
          "Column: pg_catalog.pg_collation.colliculocale",
          "Column: pg_catalog.pg_stat_bgwriter.checkpoints_timed",
          "    UPDATE pg_attribute a SET attacl = ("}));

  ASSERT_OK(ResetAcls(cluster_.get(), &conn, kColumnMitigationSql));
  ASSERT_OK(ValidateUpgradeCompatibility());
}

TEST_F(YsqlMajorUpgradeCheckTest, RemovedCatalogAclsMultipleDatabases) {
  auto conn_yugabyte = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte"));
  auto conn_postgres = ASSERT_RESULT(cluster_->ConnectToDB("postgres"));
  ASSERT_OK(conn_yugabyte.Execute("CREATE ROLE test_role1"));

  // One class broken in each of two databases.
  ASSERT_OK(
      conn_yugabyte.Execute("GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA pg_catalog TO test_role1"));
  ASSERT_OK(conn_postgres.Execute(
      "GRANT SELECT (colliculocale) ON pg_catalog.pg_collation TO test_role1"));

  ASSERT_OK(ValidateUpgradeCompatibilityFailure(
      std::vector<std::string>{
          "In database: yugabyte", "In database: postgres",
          "Function: pg_catalog.json_strip_nulls(json)", "    UPDATE pg_proc SET proacl = (",
          "Column: pg_catalog.pg_collation.colliculocale",
          "    UPDATE pg_attribute a SET attacl = ("}));

  ASSERT_OK(ResetAcls(cluster_.get(), &conn_yugabyte, kFunctionMitigationSql));
  ASSERT_OK(ResetAcls(cluster_.get(), &conn_postgres, kColumnMitigationSql, "postgres"));
  ASSERT_OK(ValidateUpgradeCompatibility());
}

TEST_F(YsqlMajorUpgradeCheckTest, CollectSchemaDump) {
  const auto pg_data_dir =
      JoinPathSegments(cluster_->tablet_server(0)->GetDataDirs().front(), "../../pg_data");
  const auto output_root = JoinPathSegments(pg_data_dir, "pg_upgrade_output.d");
  const auto dump_dir = JoinPathSegments(output_root, "schema_dump");
  const std::vector<std::string> dump_args = {"--yb-collect-schema-dump"};

  // pg_upgrade --check connects to tserver 0, so run the setup DDL through the same node.
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte", /*tserver_index=*/0));
  ASSERT_OK(conn.Execute("CREATE TABLE schema_dump_test (id int PRIMARY KEY, val text)"));
  ASSERT_OK(conn.Execute("CREATE ROLE schema_dump_role"));

  // The dump outlives a run, so clear it first or a stale one satisfies the assertions.
  auto reset = [&]() -> Status {
    if (Env::Default()->FileExists(output_root)) {
      RETURN_NOT_OK(Env::Default()->DeleteRecursively(output_root));
    }
    return Status::OK();
  };

  auto dump_contains = [](const std::string& path, const std::string& substr) -> Status {
    faststring contents;
    RETURN_NOT_OK(ReadFileToString(Env::Default(), path, &contents));
    SCHECK_FORMAT(
        contents.ToString().find(substr) != std::string::npos, NotFound,
        "$0 does not contain '$1'", path, substr);
    return Status::OK();
  };

  auto check_dump = [&]() -> Status {
    RETURN_NOT_OK(
        dump_contains(JoinPathSegments(dump_dir, "globals_dump.sql"), "schema_dump_role"));

    const auto databases_file = JoinPathSegments(dump_dir, "all_databases_dump.sql");
    RETURN_NOT_OK(dump_contains(databases_file, "schema_dump_test"));
    // The dump only predicts pg_upgrade's restore if it pins OIDs the same way.
    return dump_contains(databases_file, "binary_upgrade_set_next_heap_pg_class_oid");
  };

  auto collected_when_checks_pass = [&]() -> Status {
    RETURN_NOT_OK(reset());
    RETURN_NOT_OK(ValidateUpgradeCompatibilityWithArgs(dump_args));
    RETURN_NOT_OK(check_dump());

    // Collecting a dump must not change how the per-run log directories are retained.
    auto entries = VERIFY_RESULT(Env::Default()->GetChildren(output_root, ExcludeDots::kTrue));
    SCHECK_EQ(
        entries.size(), 1, IllegalState, "Expected the passing check to leave only the dump");
    return Status::OK();
  };

  // The dump is collected even when a check fails, which is when it is most wanted.
  auto collected_when_a_check_fails = [&]() -> Status {
    RETURN_NOT_OK(reset());
    // A reg* column fails check_for_reg_data_type_usage, which defers to the check verdict.
    RETURN_NOT_OK(conn.Execute("CREATE TABLE reg_check (a int, b regproc)"));

    auto status = ValidateUpgradeCompatibilityWithArgs(dump_args);
    SCHECK(!status.ok(), IllegalState, "Expected pg_upgrade --check to fail");
    RETURN_NOT_OK(check_dump());

    return conn.Execute("DROP TABLE reg_check");
  };

  // Nothing is dumped without the flag.
  auto not_collected_without_the_flag = [&]() -> Status {
    RETURN_NOT_OK(reset());
    RETURN_NOT_OK(ValidateUpgradeCompatibility());
    SCHECK(
        !Env::Default()->FileExists(dump_dir), IllegalState,
        "Expected no schema dump without --yb-collect-schema-dump");
    return Status::OK();
  };

  // Outside --check the flag is rejected rather than quietly ignored.
  auto rejected_without_check = [&]() -> Status {
    const auto tserver = cluster_->tablet_server(0);
    const std::vector<std::string> args = {
        GetPgToolPath("pg_upgrade"),
        "--old-datadir", pg_data_dir,
        "--old-host", tserver->bind_host(),
        "--old-port", AsString(tserver->pgsql_rpc_port()),
        "--username", "yugabyte",
        "--yb-collect-schema-dump"};

    StringWaiterLogSink log_waiter("--yb-collect-schema-dump can only be used with --check");
    auto status = Subprocess::Call(args, /*log_stdout_and_stderr=*/true);
    SCHECK(!status.ok(), IllegalState, "Expected pg_upgrade to reject the flag");
    SCHECK(log_waiter.IsEventOccurred(), IllegalState, "Expected the --check requirement error");
    return Status::OK();
  };

  ASSERT_OK(collected_when_checks_pass());
  ASSERT_OK(collected_when_a_check_fails());
  ASSERT_OK(not_collected_without_the_flag());
  ASSERT_OK(rejected_without_check());
}

}  // namespace yb
