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

#include "yb/integration-tests/upgrade-tests/pg15_upgrade_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

struct UpgradeIncompatibilityCheck {
  std::initializer_list<std::string> setup_stmts;
  std::initializer_list<std::string> expected_errors;
  std::initializer_list<std::string> teardown_stmts;
};

using YsqlMajorUpgradeCheckTest = Pg15UpgradeTestBase;

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
     .teardown_stmts = {"DROP TABLE sql_identifier_test"}}};

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
  ASSERT_OK(cluster_->AddAndSetExtraFlag("ysql_yb_major_version_upgrade_compatibility", "11"));
  ASSERT_OK(ValidateUpgradeCompatibility());

  ASSERT_OK(cluster_->AddAndSetExtraFlag("ysql_yb_major_version_upgrade_compatibility", "0"));
  ASSERT_OK(ValidateUpgradeCompatibility());

  // However, when we actually run the YSQL upgrade, pg_upgrade will error since now
  // ysql_yb_major_version_upgrade_compatibility is not set.
  auto log_waiter =
      cluster_->GetMasterLogWaiter("yb_major_version_upgrade_compatibility must be set to 11");
  ASSERT_NOK_STR_CONTAINS(UpgradeClusterToMixedMode(), kPgUpgradeFailedError);
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
}  // namespace yb
