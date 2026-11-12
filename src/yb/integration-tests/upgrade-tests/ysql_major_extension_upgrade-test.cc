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

#include "yb/util/env_util.h"
#include "yb/util/logging_test_util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

class YsqlMajorExtensionUpgradeTest : public YsqlMajorUpgradeTestBase {
 public:
  YsqlMajorExtensionUpgradeTest() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    // YB_TODO_PG19MERGE: postgresql_anonymizer is not built on pg19, see
    // external_extension_subdirs in build_postgres.py. Preloading a library that does not exist
    // stops the upgraded cluster's postmaster from starting, which failed every test in this file.
    // Restore these once the extension is ported.
    opts.extra_tserver_flags.push_back(Format(
        "--ysql_pg_conf_csv=\"shared_preload_libraries=passwordcheck\""));
    opts.extra_tserver_flags.push_back("--enable_pg_cron=true");
    opts.extra_master_flags.push_back("--enable_pg_cron=true");
    // TODO: Exclude passwordcheck for now, as upgrade fails with it enabled. Add separate test for
    // passwordcheck (see GH#26618).
    // opts.extra_master_flags.push_back(Format(
    //     "--ysql_pg_conf_csv=\"shared_preload_libraries=anon\""));
    YsqlMajorUpgradeTestBase::SetUpOptions(opts);
  }
};

TEST_F(YsqlMajorExtensionUpgradeTest, Simple) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION sslinfo")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION tablefunc")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION \"uuid-ossp\"")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION hll")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION pg_partman")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION pg_cron")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION pgaudit")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION cube")));
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION earthdistance")));
  ASSERT_OK(UpgradeClusterToCurrentVersion());

  // pg_upgrade's ALTER EXTENSION UPDATE takes pg_partman through its update scripts.
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
      "SELECT extversion = default_version FROM pg_extension "
      "JOIN pg_available_extensions ON name = extname WHERE extname = 'pg_partman'")));
}

TEST_F(YsqlMajorExtensionUpgradeTest, HStore) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION hstore")));
  auto check_query = [&](const std::optional<size_t> tserver_idx) {
    const auto query =
      "SELECT hstore_hash_extended('\"a key\" =>1'::hstore, 0),"
      "hstore_hash_extended('\"a key\" =>1'::hstore, 1)";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto result = ASSERT_RESULT((conn.FetchRows<int64_t, int64_t>(query)));
    ASSERT_FALSE(result.empty());
  };

  ASSERT_NO_FATALS(check_query(kAnyTserver));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kAnyTserver));
}

TEST_F(YsqlMajorExtensionUpgradeTest, FileFdw) {
  const auto test_file_1 = JoinPathSegments(env_util::GetRootDir("postgres_build"),
    "postgres_build/contrib/file_fdw/data/list1.csv");
  const auto test_file_2 = JoinPathSegments(env_util::GetRootDir("postgres_build"),
    "postgres_build/contrib/file_fdw/data/list2.csv");
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION file_fdw")));
  ASSERT_OK(ExecuteStatement(Format("CREATE SERVER file_server FOREIGN DATA WRAPPER file_fdw")));
  ASSERT_OK(ExecuteStatement(Format("CREATE FOREIGN DATA WRAPPER file_fdw2 HANDLER "
    "file_fdw_handler VALIDATOR file_fdw_validator")));
  ASSERT_OK(ExecuteStatement(Format(
    "CREATE FOREIGN TABLE sample_data_foreign(id integer, name text) "
    "SERVER file_server "
    "OPTIONS (format 'csv', delimiter ',', filename '$0')", test_file_1)));
  auto query = "SELECT * FROM sample_data_foreign";
  auto check_query = [&](const std::optional<size_t> tserver_idx) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto result = ASSERT_RESULT((conn.FetchRows<int, std::string>(query)));
    ASSERT_VECTORS_EQ(result, (decltype(result){{1, "foo"}, {1, "bar"}}));
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(2));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE FOREIGN TABLE sample_data_foreign2(id integer, name text) "
        "SERVER file_server "
        "OPTIONS (format 'csv', delimiter ',', filename '$0')", test_file_2));
    auto result = ASSERT_RESULT(
        (conn.FetchRows<int, std::string>("SELECT * FROM sample_data_foreign2")));
    ASSERT_VECTORS_EQ(result, (decltype(result){{2, "baz"}, {2, "qux"}}));
  }
}

TEST_F(YsqlMajorExtensionUpgradeTest, FuzzyStrMatch) {
    ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION fuzzystrmatch")));
    auto query = "SELECT soundex('hello world!');";
    // fuzzystrmatch 1.2 adds daitch_mokotoff().
    const auto query_new = "SELECT daitch_mokotoff('Augsburg'::text)::text";
    auto check_query = [&](const std::optional<size_t> tserver_idx, bool should_fail) {
        auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
        auto result = ASSERT_RESULT((conn.FetchRows<std::string>(query)));
        ASSERT_VECTORS_EQ(result, (decltype(result){{"H464"}}));
        if (should_fail) {
            ASSERT_NOK_STR_CONTAINS(conn.Execute(query_new),
                "function daitch_mokotoff(text) does not exist");
        } else {
            auto result_new = ASSERT_RESULT(conn.FetchRows<std::string>(query_new));
            ASSERT_VECTORS_EQ(result_new, (decltype(result_new){"{054795}"}));
        }
    };
    ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/ true));
    ASSERT_OK(UpgradeClusterToMixedMode());
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*should_fail=*/ false));
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ true));
    ASSERT_OK(FinalizeUpgradeFromMixedMode());
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ false));
}


TEST_F(YsqlMajorExtensionUpgradeTest, PasswordCheck) {
  ASSERT_OK(ExecuteStatement("CREATE USER user1"));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));
  const auto query = "ALTER USER user1 PASSWORD 'xyzuser1';";
  auto conn = ASSERT_RESULT(CreateConnToTs(0));
  ASSERT_NOK_STR_CONTAINS(conn.Execute(query), "password must not contain user name");
}

TEST_F(YsqlMajorExtensionUpgradeTest, PgCrypto) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION pgcrypto")));
  const auto query =
    "select encode(decrypt(encrypt('foo', '0123456', '3des'), '0123456', '3des'), 'escape');";
  auto check_query = [&](const std::optional<size_t> tserver_idx) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto result = ASSERT_RESULT((conn.FetchRows<std::string>(query)));
    ASSERT_VECTORS_EQ(result, (decltype(result){"foo"}));
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kAnyTserver));
}

TEST_F(YsqlMajorExtensionUpgradeTest, PgStatStatements) {
  const auto select_pg_stat_stmts =
      "SELECT query FROM pg_stat_statements WHERE query NOT LIKE 'SELECT%' ORDER BY query";
  const auto select_stats_since = "SELECT count(stats_since) FROM pg_stat_statements";
  const auto create_table_stmt = "CREATE TABLE test (t int)";
  auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
  ASSERT_OK(conn.Execute("CREATE EXTENSION IF NOT EXISTS pg_stat_statements"));
  ASSERT_OK(conn.Fetch("SELECT pg_stat_statements_reset()"));

  ASSERT_OK(conn.Execute(create_table_stmt));
  auto check_query = [&](const std::optional<size_t> tserver_idx, bool after_upgrade) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto result = ASSERT_RESULT(conn.FetchRows<std::string>(select_pg_stat_stmts));
    if (after_upgrade) {
        ASSERT_TRUE(result.empty());
        const auto delete_stmt = "DELETE FROM test";
        ASSERT_OK(conn.Execute(delete_stmt));
        result = ASSERT_RESULT(conn.FetchRows<std::string>(select_pg_stat_stmts));
        ASSERT_VECTORS_EQ(result, (decltype(result){delete_stmt}));
    } else {
        ASSERT_VECTORS_EQ(result, (decltype(result){create_table_stmt}));
    }
    if (after_upgrade) {
        auto result_new = ASSERT_RESULT(conn.FetchRows<int64_t>(select_stats_since));
        ASSERT_FALSE(result.empty());
    } else {
        ASSERT_NOK_STR_CONTAINS(conn.Execute(select_stats_since),
            "column \"stats_since\" does not exist");
    }
  };
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*after_upgrade=*/ false));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*after_upgrade=*/ true));
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*after_upgrade=*/ false));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*after_upgrade=*/ true));
}

TEST_F(YsqlMajorExtensionUpgradeTest, PostgresFdw) {
  // Global views already install postgres_fdw in pg_catalog.
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION IF NOT EXISTS postgres_fdw")));
  auto check_query = [&](const std::optional<size_t> tserver_idx, bool should_fail) {
    const auto query = "SELECT server_name FROM postgres_fdw_get_connections(true) ORDER BY 1;";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    if (should_fail) {
      ASSERT_NOK_STR_CONTAINS(conn.Execute(query),
          "function postgres_fdw_get_connections(boolean) does not exist");
    } else {
      auto result = ASSERT_RESULT((conn.FetchRows<std::string>(query)));
      ASSERT_VECTORS_EQ(result, (decltype(result){}));
    }
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/ true));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*should_fail=*/ false));
  // should fail as the new function still doesn't exist.
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ true));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ false));
}

TEST_F(YsqlMajorExtensionUpgradeTest, HypoPG) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION hypopg")));
  auto check_query = [&](const std::optional<size_t> tserver_idx) {
    const auto query = "SELECT COUNT(*) FROM hypopg_hidden_indexes();";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(query)));
    ASSERT_VECTORS_EQ(result, (decltype(result){0}));
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
}

TEST_F(YsqlMajorExtensionUpgradeTest, Orafce) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION orafce")));
  auto check_query = [&](const std::optional<size_t> tserver_idx, bool should_fail) {
    const auto query = "SELECT * FROM oracle.user_tables";
    const auto query_new =
        "SELECT oracle.sys_extract_utc('2026-01-01 05:00:00+05'::timestamptz)::text AS result";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto res = ASSERT_RESULT(conn.FetchRows<std::string>(query));
    ASSERT_FALSE(res.empty());
    if (should_fail) {
        ASSERT_NOK_STR_CONTAINS(conn.Execute(query_new),
            "function oracle.sys_extract_utc(timestamp with time zone) does not exist");
    } else {
        auto result_new = ASSERT_RESULT(conn.FetchRows<std::string>(query_new));
        ASSERT_VECTORS_EQ(result_new, (decltype(result_new){"2026-01-01 00:00:00"}));
    }
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/ true));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*should_fail=*/ false));
  // should fail as the new function still doesn't exist.
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ true));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ false));
}

// YB_TODO_PG19MERGE: no pg_hint_plan update path from 1.5.1-yb-1.0.
TEST_F(YsqlMajorExtensionUpgradeTest, YB_DISABLE_TEST(PgHintPlan)) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION pg_hint_plan")));
  std::vector<std::string> tables = {"t_hint", "t_hint2", "t_hint3"};
  for (const auto& table : tables) {
    ASSERT_OK(ExecuteStatement(Format("CREATE TABLE $0 (a int)", table)));
    ASSERT_OK(ExecuteStatement(Format("CREATE INDEX ON $0 (a)", table)));
  }
  auto check_query = [&](const std::optional<size_t> tserver_idx, size_t expected_count) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    ASSERT_OK(conn.Execute("SET pg_hint_plan.enable_hint_table=on"));
    for (size_t i = 0; i < expected_count; ++i) {
      ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(Format("SELECT a FROM $0", tables[i]))));
    }
    auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(
      "SELECT count(*) FROM hint_plan.hints")));
    ASSERT_VECTORS_EQ(result, (decltype(result){expected_count}));
  };
  ASSERT_OK(ExecuteStatement(Format(
    "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)"
    " VALUES ('EXPLAIN SELECT a FROM t_hint', '', 'IndexOnlyScan(t_hint t_hint_a_idx)');")));
  auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
  const auto hints_table_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT oid FROM pg_class WHERE relname = '$0'", "hints")));
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_NO_FATALS(check_query(kAnyTserver, 1));
  }
  ASSERT_OK(UpgradeClusterToMixedMode());
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
    ASSERT_OK(conn.Execute(
      "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)"
      " VALUES ('EXPLAIN SELECT a FROM t_hint2', '', 'IndexOnlyScan(t_hint2 t_hint2_a_idx)');"));
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, 2));
  }
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
    ASSERT_OK(conn.Execute(
      "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)"
      " VALUES ('EXPLAIN SELECT a FROM t_hint3', '', 'IndexOnlyScan(t_hint3 t_hint3_a_idx)');"));
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, 3));

    // Check that the hint cache invalidation trigger exists.
    auto result = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
        "SELECT COUNT(*) FROM pg_trigger "
        "WHERE tgname = 'yb_invalidate_hint_plan_cache'"));
    ASSERT_EQ(result, 1);
  }
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  {
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, 3));
    auto conn = ASSERT_RESULT(CreateConnToTs(2));
    auto hints_table_oid_after = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        Format("SELECT oid FROM pg_class WHERE relname = '$0'", "hints")));
    ASSERT_EQ(hints_table_oid, hints_table_oid_after);
  }
}

// YugabyteDB releases based on PG19 or later do not include pg_stat_monitor, so the upgrade check
// refuses the upgrade while the old cluster still uses it: installed in a database, or named in
// shared_preload_libraries or session_preload_libraries, in the server configuration or in a role
// or database setting.
TEST_F(YsqlMajorExtensionUpgradeTest, PgStatMonitor) {
  const std::vector<std::string> fix_message = {
      "Your installation uses the pg_stat_monitor extension, which the new",
      "version does not include, so it cannot be reinstalled after the",
      "upgrade. Before upgrading, remove every use listed above:",
      "  - Installed in database: run DROP EXTENSION pg_stat_monitor in that",
      "  - Server setting: in the ysql_pg_conf_csv flag (or the deprecated",
      "    ysql_pg_conf flag) of all YB-TServers and YB-Masters:",
      "      - remove pg_stat_monitor from that setting, or the whole setting",
      "  - Setting for a role or database: remove pg_stat_monitor from it with",
  };
  // Runs the upgrade check, which must refuse the upgrade, print the given lines and the fix, and
  // print none of the absent lines.
  const auto check_refused = [this, &fix_message](
      std::vector<std::string> lines, const std::vector<std::string>& absent) -> Status {
    std::vector<std::unique_ptr<StringWaiterLogSink>> absent_sinks;
    for (const auto& line : absent) {
      absent_sinks.push_back(std::make_unique<StringWaiterLogSink>(line));
    }
    lines.insert(lines.end(), fix_message.begin(), fix_message.end());
    RETURN_NOT_OK(ValidateUpgradeCompatibilityFailure(lines));
    for (size_t i = 0; i < absent.size(); ++i) {
      SCHECK_FORMAT(
          !absent_sinks[i]->IsEventOccurred(), IllegalState, "The check printed $0", absent[i]);
    }
    return Status::OK();
  };
  constexpr auto kSharedPreloadLine = "Server setting: shared_preload_libraries = '";
  // The flag below spells this setting's name in mixed case; the check prints it in lowercase.
  constexpr auto kSessionPreloadLine =
      "Server setting: session_preload_libraries = 'pg_stat_monitor'";
  // Each role or database setting as the check prints it, and the command that removes it.
  // The values cover quoted entries, a $libdir/ path, a .so suffix and a list with another library.
  const std::vector<std::pair<std::string, std::string>> role_settings = {
      {"Setting for all roles: session_preload_libraries = 'pg_stat_monitor'",
       "ALTER ROLE ALL RESET session_preload_libraries"},
      {"Setting for database yugabyte: session_preload_libraries = 'pg_stat_monitor'",
       "ALTER DATABASE yugabyte RESET session_preload_libraries"},
      {"Setting for role pgsm_user: session_preload_libraries = "
       "'\"$libdir/pg_stat_monitor.so\", auto_explain'",
       "ALTER ROLE pgsm_user SET session_preload_libraries = 'auto_explain'"},
      {"Setting for role pgsm_user in database yugabyte: session_preload_libraries = "
       "'\"pg_stat_monitor.so\"'",
       "ALTER ROLE pgsm_user IN DATABASE yugabyte RESET session_preload_libraries"},
  };

  // This flag comes after the one from SetUpOptions, so it replaces it. The tab before the library
  // and the trailing slash do not stop postgres from loading it, so the check must still find it.
  cluster_->AddExtraFlagOnTServers(
      "ysql_pg_conf_csv",
      "\"shared_preload_libraries=\t$libdir/pg_stat_monitor/\","
      "\"Session_Preload_Libraries=pg_stat_monitor\"");
  ASSERT_OK(RestartAllTServersInOldVersion(kNoDelayBetweenNodes));

  ASSERT_OK(ExecuteStatement("CREATE EXTENSION pg_stat_monitor"));
  // The generic extension check refuses isn, and must leave pg_stat_monitor to this check.
  ASSERT_OK(ExecuteStatement("CREATE EXTENSION isn"));
  ASSERT_OK(ExecuteStatement("CREATE ROLE pgsm_user"));
  ASSERT_OK(ExecuteStatement("ALTER ROLE ALL SET session_preload_libraries = 'pg_stat_monitor'"));
  ASSERT_OK(ExecuteStatement(
      "ALTER DATABASE yugabyte SET session_preload_libraries = 'pg_stat_monitor'"));
  ASSERT_OK(ExecuteStatement(
      "ALTER ROLE pgsm_user SET session_preload_libraries = "
      "'$libdir/pg_stat_monitor.so', 'auto_explain'"));
  ASSERT_OK(ExecuteStatement(
      "ALTER ROLE pgsm_user IN DATABASE yugabyte SET session_preload_libraries = "
      "'pg_stat_monitor.so'"));

  std::vector<std::string> expected = {
      "Installed in database: yugabyte",
      kSharedPreloadLine,
      kSessionPreloadLine,
      "Your installation contains extensions that are not compatible",
      "  isn",
  };
  for (const auto& [setting, command] : role_settings) {
    expected.push_back(setting);
  }
  // The generic extension check lists each extension as "  <name>"; it must not list
  // pg_stat_monitor.
  ASSERT_OK(check_refused(expected, {"  pg_stat_monitor"}));

  // Remove the uses as the message says: first the extensions and the role and database settings.
  // Only the server settings remain.
  ASSERT_OK(ExecuteStatement("DROP EXTENSION pg_stat_monitor"));
  ASSERT_OK(ExecuteStatement("DROP EXTENSION isn"));
  for (const auto& [setting, command] : role_settings) {
    ASSERT_OK(ExecuteStatement(command));
  }
  ASSERT_OK(check_refused(
      {kSharedPreloadLine, kSessionPreloadLine},
      {"Installed in database: yugabyte", "  isn", "Setting for all roles",
       "Setting for database yugabyte", "Setting for role pgsm_user"}));

  // Clear the flag at runtime only, like yb-ts-cli set_flag. ysql_pg.conf no longer names
  // pg_stat_monitor, but postgres has not restarted, so the shared_preload_libraries it loaded
  // still does, and the check must still refuse.
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_pg_conf_csv", ""));
  ASSERT_OK(check_refused({kSharedPreloadLine}, {kSessionPreloadLine}));

  // Then the server settings. This also drops the preload from SetUpOptions.
  cluster_->RemoveExtraFlagOnTServers("ysql_pg_conf_csv");
  ASSERT_OK(RestartAllTServersInOldVersion(kNoDelayBetweenNodes));

  // Each other kind of use alone must refuse the upgrade too: a role setting,
  ASSERT_OK(ExecuteStatement(
      "ALTER ROLE pgsm_user SET session_preload_libraries = 'pg_stat_monitor'"));
  ASSERT_OK(check_refused(
      {"Setting for role pgsm_user: session_preload_libraries = 'pg_stat_monitor'"},
      {kSharedPreloadLine, kSessionPreloadLine, "Installed in database: yugabyte"}));
  ASSERT_OK(ExecuteStatement("ALTER ROLE pgsm_user RESET session_preload_libraries"));

  // and the extension, which can be installed without the preload.
  ASSERT_OK(ExecuteStatement("CREATE EXTENSION pg_stat_monitor"));
  ASSERT_OK(check_refused(
      {"Installed in database: yugabyte"},
      {kSharedPreloadLine, kSessionPreloadLine, "Setting for role pgsm_user"}));
  ASSERT_OK(ExecuteStatement("DROP EXTENSION pg_stat_monitor"));

  ASSERT_OK(UpgradeClusterToCurrentVersion());
  ASSERT_NOK_STR_CONTAINS(
      ExecuteStatement("CREATE EXTENSION pg_stat_monitor"),
      "extension \"pg_stat_monitor\" is not available");
}

// YB_TODO_PG19MERGE: postgresql_anonymizer is not built on pg19.
TEST_F(YsqlMajorExtensionUpgradeTest, YB_DISABLE_TEST(Anon)) {
  ASSERT_OK(ExecuteStatement("CREATE EXTENSION anon"));
  auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = TRUE"));
  auto result = ASSERT_RESULT(conn.FetchRows<bool>("SELECT anon.start_dynamic_masking()"));
  ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = FALSE"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_EQ(result, (decltype(result){true}));
  ASSERT_OK(ExecuteStatement("CREATE TABLE test (name text)"));
  ASSERT_OK(ExecuteStatement("CREATE ROLE test_role LOGIN"));
  ASSERT_OK(ExecuteStatement("SECURITY LABEL FOR anon ON ROLE test_role IS 'MASKED'"));
  ASSERT_OK(ExecuteStatement("GRANT SELECT ON test TO test_role"));
  ASSERT_OK(ExecuteStatement(
      "SECURITY LABEL FOR anon ON COLUMN test.name IS 'MASKED WITH VALUE ''CONFIDENTIAL'''"));
  ASSERT_OK(ExecuteStatement("INSERT INTO test VALUES ('hi'), ('bye')"));
  auto check_query = [&](const std::optional<size_t> tserver_idx) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx, "test_role"));
    auto result = ASSERT_RESULT((conn.FetchRows<std::string>("SELECT * FROM test")));
    ASSERT_VECTORS_EQ(result, (decltype(result){"CONFIDENTIAL", "CONFIDENTIAL"}));
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kAnyTserver));
}

TEST_F(YsqlMajorExtensionUpgradeTest, PlPgsql) {
  ASSERT_OK(ExecuteStatements({
    "DROP EXTENSION plpgsql CASCADE",
    "CREATE LANGUAGE plpgsql",
    "CREATE FUNCTION test() RETURNS INTEGER AS $$begin return 1; end$$ LANGUAGE plpgsql",
    // Since PG13, CREATE LANGUAGE creates the extension, which DROP LANGUAGE cannot drop.
    "DROP EXTENSION plpgsql CASCADE",
    "CREATE EXTENSION plpgsql",
  }));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
}

TEST_F(YsqlMajorExtensionUpgradeTest, YbYcqlUtils) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION yb_ycql_utils")));
  auto check_query = [&](const std::optional<size_t> tserver_idx) {
    const auto query = "SELECT COUNT(*) FROM ycql_stat_statements;";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(query)));
    ASSERT_FALSE(result.empty());
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kAnyTserver));
}

} // namespace yb
