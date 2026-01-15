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
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

class YsqlMajorExtensionUpgradeTest : public YsqlMajorUpgradeTestBase {
 public:
  YsqlMajorExtensionUpgradeTest() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.extra_tserver_flags.push_back(Format(
        "--ysql_pg_conf_csv=\"shared_preload_libraries=passwordcheck,pg_stat_monitor,anon\""));
    opts.extra_tserver_flags.push_back("--enable_pg_cron=true");
    opts.extra_master_flags.push_back("--enable_pg_cron=true");
    // TODO: Exclude passwordcheck for now, as upgrade fails with it enabled. Add separate test for
    // passwordcheck (see GH#26618).
    opts.extra_master_flags.push_back(Format(
        "--ysql_pg_conf_csv=\"shared_preload_libraries=pg_stat_monitor,anon\""));
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

}

TEST_F(YsqlMajorExtensionUpgradeTest, HStore) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION hstore")));
  auto check_query = [&](const std::optional<size_t> tserver_idx, bool should_fail) {
    // Test a function that was added in a newer version of the extension.
    const auto query =
      "SELECT hstore_hash_extended('\"a key\" =>1'::hstore, 0),"
      "hstore_hash_extended('\"a key\" =>1'::hstore, 1)";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    if (should_fail) {
      ASSERT_NOK_STR_CONTAINS(conn.Execute(query),
          "function hstore_hash_extended(hstore, integer) does not exist");
    } else {
      auto result = ASSERT_RESULT((conn.FetchRows<int64_t, int64_t>(query)));
      ASSERT_FALSE(result.empty());
    }
  };

  ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/ true));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*should_fail=*/ false));
  // should fail as the new function still doesn't exist.
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ true));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/ false));
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
    auto check_query = [&](const std::optional<size_t> tserver_idx) {
        auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
        auto result = ASSERT_RESULT((conn.FetchRows<std::string>(query)));
        ASSERT_VECTORS_EQ(result, (decltype(result){{"H464"}}));
    };
    ASSERT_NO_FATALS(check_query(kAnyTserver));
    ASSERT_OK(UpgradeClusterToMixedMode());
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
    ASSERT_OK(FinalizeUpgradeFromMixedMode());
    ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
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
  const auto select_pg_stat_stmts_info = "SELECT dealloc FROM pg_stat_statements_info";
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
        auto result_new = ASSERT_RESULT(conn.FetchRows<int64_t>(select_pg_stat_stmts_info));
        ASSERT_FALSE(result.empty());
    } else {
        ASSERT_NOK_STR_CONTAINS(conn.Execute(select_pg_stat_stmts_info),
            "relation \"pg_stat_statements_info\" does not exist");
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
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION postgres_fdw")));
  auto check_query = [&](const std::optional<size_t> tserver_idx, bool should_fail) {
    const auto query = "SELECT server_name FROM postgres_fdw_get_connections() ORDER BY 1;";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    if (should_fail) {
      ASSERT_NOK_STR_CONTAINS(conn.Execute(query),
          "function postgres_fdw_get_connections() does not exist");
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
  auto check_query = [&](const std::optional<size_t> tserver_idx, bool should_fail) {
    const auto query = "SELECT COUNT(*) FROM hypopg_hidden_indexes();";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    if (should_fail) {
      ASSERT_NOK_STR_CONTAINS(conn.Execute(query),
          "function hypopg_hidden_indexes() does not exist");
    } else {
      auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(query)));
      ASSERT_VECTORS_EQ(result, (decltype(result){0}));
    }
  };
  ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/ true));
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*should_fail=*/ false));
  // should fail as the new view still doesn't exist.
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ true));
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/ false));
}

TEST_F(YsqlMajorExtensionUpgradeTest, Orafce) {
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION orafce")));
  auto check_query = [&](const std::optional<size_t> tserver_idx, bool should_fail) {
    const auto query = "SELECT * FROM oracle.user_tables";
    const auto query_new = "SELECT oracle.greatest(10, 20, 30) AS result";
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
    auto res = ASSERT_RESULT(conn.FetchRows<std::string>(query));
    ASSERT_FALSE(res.empty());
    if (should_fail) {
        ASSERT_NOK_STR_CONTAINS(conn.Execute(query_new),
            "function oracle.greatest(integer, integer, integer) does not exist");
    } else {
        auto result_new = ASSERT_RESULT(conn.FetchRows<int>(query_new));
        ASSERT_VECTORS_EQ(result_new, (decltype(result_new){30}));
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

TEST_F(YsqlMajorExtensionUpgradeTest, PgHintPlan) {
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

TEST_F(YsqlMajorExtensionUpgradeTest, PgStatMonitor) {
  const std::vector<std::string> expected_error = {
      "In database: yugabyte",
      "  pg_stat_monitor",
      "Your installation contains extensions that are not compatible",
      "with YSQL major version upgrade. Please uninstall the extensions",
      "using DROP EXTENSION, and reinstall them after the upgrade. A list of",
      "extensions with problems is printed above and in the file:",
  };
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION pg_stat_monitor")));
  ASSERT_OK(ValidateUpgradeCompatibilityFailure(expected_error));
  ASSERT_OK(ExecuteStatement("DROP EXTENSION pg_stat_monitor"));
  ASSERT_OK(UpgradeClusterToCurrentVersion());
  ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION pg_stat_monitor")));
}

TEST_F(YsqlMajorExtensionUpgradeTest, Anon) {
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
    "DROP LANGUAGE plpgsql CASCADE",
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
