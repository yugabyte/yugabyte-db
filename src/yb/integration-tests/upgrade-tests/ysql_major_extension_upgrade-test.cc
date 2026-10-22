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

namespace {

// An extension to carry through a YSQL major upgrade. All of them share one upgrade, since each
// upgrade takes over a minute.
struct ExtensionUpgradeCase {
  std::string name;
  std::function<void()> setup;
  std::function<void()> check_before_upgrade = [] {};
  // Runs once yb-tserver kMixedModeTserverPg15 is on PG15 and the other yb-tservers are on PG11.
  std::function<void()> check_in_mixed_mode = [] {};
  std::function<void()> check_after_upgrade = [] {};
};

}  // namespace

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

 protected:
  Result<pgwrapper::PGConn> ConnectToDb(
      const std::string& db_name, std::optional<size_t> ts_id,
      const std::string& user = "postgres") {
    return cluster_->ConnectToDB(db_name, ts_id, /*simple_query_protocol=*/false, user);
  }

  void TestUpgrade(const std::vector<ExtensionUpgradeCase>& cases) {
    const auto for_each_case = [&cases](const auto& step) {
      for (const auto& c : cases) {
        SCOPED_TRACE(c.name);
        ASSERT_NO_FATALS(step(c));
      }
    };
    ASSERT_NO_FATALS(for_each_case([](const auto& c) { c.setup(); }));
    ASSERT_NO_FATALS(for_each_case([](const auto& c) { c.check_before_upgrade(); }));
    ASSERT_OK(UpgradeClusterToMixedMode());
    ASSERT_NO_FATALS(for_each_case([](const auto& c) { c.check_in_mixed_mode(); }));
    ASSERT_OK(FinalizeUpgradeFromMixedMode());
    ASSERT_NO_FATALS(for_each_case([](const auto& c) { c.check_after_upgrade(); }));
  }

  ExtensionUpgradeCase Simple() {
    return {
      .name = "Simple",
      .setup = [this] {
        for (const auto& extension : {"sslinfo", "tablefunc", "\"uuid-ossp\"", "hll",
                                      "pg_partman", "pg_cron", "pgaudit", "cube",
                                      "earthdistance"}) {
          ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION $0", extension)));
        }
      },
    };
  }

  // An extension whose objects behave the same on PG11 and PG15.
  ExtensionUpgradeCase UnchangedCase(
      std::string name, std::string extension, std::function<void(pgwrapper::PGConn&)> check) {
    auto check_query = [this, check](std::optional<size_t> tserver_idx) {
      auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
      ASSERT_NO_FATALS(check(conn));
    };
    return {
      .name = std::move(name),
      .setup = [this, extension] {
        ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION $0", extension)));
      },
      .check_before_upgrade = [check_query] { ASSERT_NO_FATALS(check_query(kAnyTserver)); },
      .check_in_mixed_mode = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
      },
      .check_after_upgrade = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
      },
    };
  }

  // An extension whose newer version adds an object, which `query` uses. The query only works on
  // PG15 yb-tservers, and on the PG11 ones once the upgrade is finalized.
  ExtensionUpgradeCase NewObjectCase(
      std::string name, std::string extension, std::string query, std::string missing_error,
      std::function<void(pgwrapper::PGConn&)> check_result) {
    auto check_query = [this, query, missing_error, check_result](
        std::optional<size_t> tserver_idx, bool should_fail) {
      auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
      if (should_fail) {
        ASSERT_NOK_STR_CONTAINS(conn.Execute(query), missing_error);
      } else {
        ASSERT_NO_FATALS(check_result(conn));
      }
    };
    return {
      .name = std::move(name),
      .setup = [this, extension] {
        ASSERT_OK(ExecuteStatement(Format("CREATE EXTENSION $0", extension)));
      },
      .check_before_upgrade = [check_query] {
        ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/true));
      },
      .check_in_mixed_mode = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*should_fail=*/false));
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/true));
      },
      .check_after_upgrade = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/false));
      },
    };
  }

  ExtensionUpgradeCase HStore() {
    static const auto kQuery =
        "SELECT hstore_hash_extended('\"a key\" =>1'::hstore, 0),"
        "hstore_hash_extended('\"a key\" =>1'::hstore, 1)";
    return NewObjectCase(
        "HStore", "hstore", kQuery,
        "function hstore_hash_extended(hstore, integer) does not exist",
        [](pgwrapper::PGConn& conn) {
          auto result = ASSERT_RESULT((conn.FetchRows<int64_t, int64_t>(kQuery)));
          ASSERT_FALSE(result.empty());
        });
  }

  ExtensionUpgradeCase PostgresFdw() {
    static const auto kQuery = "SELECT server_name FROM postgres_fdw_get_connections() ORDER BY 1;";
    return NewObjectCase(
        "PostgresFdw", "postgres_fdw", kQuery,
        "function postgres_fdw_get_connections() does not exist",
        [](pgwrapper::PGConn& conn) {
          auto result = ASSERT_RESULT((conn.FetchRows<std::string>(kQuery)));
          ASSERT_VECTORS_EQ(result, (decltype(result){}));
        });
  }

  ExtensionUpgradeCase HypoPG() {
    static const auto kQuery = "SELECT COUNT(*) FROM hypopg_hidden_indexes();";
    return NewObjectCase(
        "HypoPG", "hypopg", kQuery, "function hypopg_hidden_indexes() does not exist",
        [](pgwrapper::PGConn& conn) {
          auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(kQuery)));
          ASSERT_VECTORS_EQ(result, (decltype(result){0}));
        });
  }

  ExtensionUpgradeCase Orafce() {
    static const auto kQueryNew = "SELECT oracle.greatest(10, 20, 30) AS result";
    auto c = NewObjectCase(
        "Orafce", "orafce", kQueryNew,
        "function oracle.greatest(integer, integer, integer) does not exist",
        [](pgwrapper::PGConn& conn) {
          auto result_new = ASSERT_RESULT(conn.FetchRows<int>(kQueryNew));
          ASSERT_VECTORS_EQ(result_new, (decltype(result_new){30}));
        });
    // The objects that the old version already had keep working everywhere.
    auto check_old_objects = [this](std::optional<size_t> tserver_idx) {
      auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
      auto res = ASSERT_RESULT(conn.FetchRows<std::string>("SELECT * FROM oracle.user_tables"));
      ASSERT_FALSE(res.empty());
    };
    c.check_before_upgrade = [check_old_objects, check = c.check_before_upgrade] {
      ASSERT_NO_FATALS(check_old_objects(kAnyTserver));
      ASSERT_NO_FATALS(check());
    };
    c.check_in_mixed_mode = [check_old_objects, check = c.check_in_mixed_mode] {
      ASSERT_NO_FATALS(check_old_objects(kMixedModeTserverPg15));
      ASSERT_NO_FATALS(check_old_objects(kMixedModeTserverPg11));
      ASSERT_NO_FATALS(check());
    };
    c.check_after_upgrade = [check_old_objects, check = c.check_after_upgrade] {
      ASSERT_NO_FATALS(check_old_objects(kMixedModeTserverPg11));
      ASSERT_NO_FATALS(check());
    };
    return c;
  }

  ExtensionUpgradeCase FuzzyStrMatch() {
    return UnchangedCase("FuzzyStrMatch", "fuzzystrmatch", [](pgwrapper::PGConn& conn) {
      auto result = ASSERT_RESULT((conn.FetchRows<std::string>("SELECT soundex('hello world!');")));
      ASSERT_VECTORS_EQ(result, (decltype(result){{"H464"}}));
    });
  }

  ExtensionUpgradeCase PgCrypto() {
    return UnchangedCase("PgCrypto", "pgcrypto", [](pgwrapper::PGConn& conn) {
      auto result = ASSERT_RESULT((conn.FetchRows<std::string>(
          "select encode(decrypt(encrypt('foo', '0123456', '3des'), '0123456', '3des'), "
          "'escape');")));
      ASSERT_VECTORS_EQ(result, (decltype(result){"foo"}));
    });
  }

  ExtensionUpgradeCase YbYcqlUtils() {
    return UnchangedCase("YbYcqlUtils", "yb_ycql_utils", [](pgwrapper::PGConn& conn) {
      auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(
          "SELECT COUNT(*) FROM ycql_stat_statements;")));
      ASSERT_FALSE(result.empty());
    });
  }

  ExtensionUpgradeCase FileFdw() {
    const auto test_file_1 = JoinPathSegments(env_util::GetRootDir("postgres_build"),
      "postgres_build/contrib/file_fdw/data/list1.csv");
    const auto test_file_2 = JoinPathSegments(env_util::GetRootDir("postgres_build"),
      "postgres_build/contrib/file_fdw/data/list2.csv");
    auto check_query = [this](std::optional<size_t> tserver_idx) {
      auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
      auto result = ASSERT_RESULT(
          (conn.FetchRows<int, std::string>("SELECT * FROM sample_data_foreign")));
      ASSERT_VECTORS_EQ(result, (decltype(result){{1, "foo"}, {1, "bar"}}));
    };
    return {
      .name = "FileFdw",
      .setup = [this, test_file_1] {
        ASSERT_OK(ExecuteStatements({
          "CREATE EXTENSION file_fdw",
          "CREATE SERVER file_server FOREIGN DATA WRAPPER file_fdw",
          "CREATE FOREIGN DATA WRAPPER file_fdw2 HANDLER file_fdw_handler "
          "VALIDATOR file_fdw_validator",
          Format("CREATE FOREIGN TABLE sample_data_foreign(id integer, name text) "
                 "SERVER file_server OPTIONS (format 'csv', delimiter ',', filename '$0')",
                 test_file_1),
        }));
      },
      .check_before_upgrade = [check_query] { ASSERT_NO_FATALS(check_query(kAnyTserver)); },
      .check_in_mixed_mode = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
      },
      .check_after_upgrade = [this, check_query, test_file_2] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
        auto conn = ASSERT_RESULT(CreateConnToTs(2));
        ASSERT_OK(conn.ExecuteFormat(
            "CREATE FOREIGN TABLE sample_data_foreign2(id integer, name text) "
            "SERVER file_server OPTIONS (format 'csv', delimiter ',', filename '$0')",
            test_file_2));
        auto result = ASSERT_RESULT(
            (conn.FetchRows<int, std::string>("SELECT * FROM sample_data_foreign2")));
        ASSERT_VECTORS_EQ(result, (decltype(result){{2, "baz"}, {2, "qux"}}));
      },
    };
  }

  ExtensionUpgradeCase PasswordCheck() {
    return {
      .name = "PasswordCheck",
      .setup = [this] { ASSERT_OK(ExecuteStatement("CREATE USER user1")); },
      .check_after_upgrade = [this] {
        auto conn = ASSERT_RESULT(CreateConnToTs(0));
        ASSERT_NOK_STR_CONTAINS(
            conn.Execute("ALTER USER user1 PASSWORD 'xyzuser1';"),
            "password must not contain user name");
      },
    };
  }

  ExtensionUpgradeCase PgHintPlan() {
    static const std::vector<std::string> kTables = {"t_hint", "t_hint2", "t_hint3"};
    auto check_query = [this](std::optional<size_t> tserver_idx, size_t expected_count) {
      auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
      ASSERT_OK(conn.Execute("SET pg_hint_plan.enable_hint_table=on"));
      for (size_t i = 0; i < expected_count; ++i) {
        ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(Format("SELECT a FROM $0", kTables[i]))));
      }
      auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(
        "SELECT count(*) FROM hint_plan.hints")));
      ASSERT_VECTORS_EQ(result, (decltype(result){expected_count}));
    };
    auto get_hints_table_oid = [this](size_t tserver_idx) -> Result<uint32_t> {
      auto conn = VERIFY_RESULT(CreateConnToTs(tserver_idx));
      return conn.FetchRow<pgwrapper::PGOid>("SELECT oid FROM pg_class WHERE relname = 'hints'");
    };
    auto hints_table_oid = std::make_shared<uint32_t>();
    return {
      .name = "PgHintPlan",
      .setup = [this, get_hints_table_oid, hints_table_oid] {
        ASSERT_OK(ExecuteStatement("CREATE EXTENSION pg_hint_plan"));
        for (const auto& table : kTables) {
          ASSERT_OK(ExecuteStatement(Format("CREATE TABLE $0 (a int)", table)));
          ASSERT_OK(ExecuteStatement(Format("CREATE INDEX ON $0 (a)", table)));
        }
        ASSERT_OK(ExecuteStatement(
            "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)"
            " VALUES ('EXPLAIN SELECT a FROM t_hint', '', 'IndexOnlyScan(t_hint t_hint_a_idx)');"));
        *hints_table_oid = ASSERT_RESULT(get_hints_table_oid(kMixedModeTserverPg11));
      },
      .check_before_upgrade = [check_query] { ASSERT_NO_FATALS(check_query(kAnyTserver, 1)); },
      .check_in_mixed_mode = [this, check_query] {
        {
          auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg11));
          ASSERT_OK(conn.Execute(
              "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) VALUES "
              "('EXPLAIN SELECT a FROM t_hint2', '', 'IndexOnlyScan(t_hint2 t_hint2_a_idx)');"));
          ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, 2));
        }
        auto conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));
        ASSERT_OK(conn.Execute(
            "INSERT INTO hint_plan.hints (norm_query_string, application_name, hints) VALUES "
            "('EXPLAIN SELECT a FROM t_hint3', '', 'IndexOnlyScan(t_hint3 t_hint3_a_idx)');"));
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, 3));

        // Check that the hint cache invalidation trigger exists.
        auto result = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
            "SELECT COUNT(*) FROM pg_trigger "
            "WHERE tgname = 'yb_invalidate_hint_plan_cache'"));
        ASSERT_EQ(result, 1);
      },
      .check_after_upgrade = [check_query, get_hints_table_oid, hints_table_oid] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, 3));
        ASSERT_EQ(*hints_table_oid, ASSERT_RESULT(get_hints_table_oid(2)));
      },
    };
  }

  // Anon's dynamic masking adds event triggers that fire on every DDL in its database, so it gets
  // a database of its own.
  ExtensionUpgradeCase Anon() {
    static const auto kDb = "anon_db";
    auto check_query = [this](std::optional<size_t> tserver_idx) {
      auto conn = ASSERT_RESULT(ConnectToDb(kDb, tserver_idx, "test_role"));
      auto result = ASSERT_RESULT((conn.FetchRows<std::string>("SELECT * FROM test")));
      ASSERT_VECTORS_EQ(result, (decltype(result){"CONFIDENTIAL", "CONFIDENTIAL"}));
    };
    return {
      .name = "Anon",
      .setup = [this] {
        ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0", kDb)));
        auto conn = ASSERT_RESULT(ConnectToDb(kDb, kAnyTserver));
        ASSERT_OK(conn.Execute("CREATE EXTENSION anon"));
        ASSERT_OK(conn.Execute("BEGIN"));
        ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = TRUE"));
        auto result = ASSERT_RESULT(conn.FetchRows<bool>("SELECT anon.start_dynamic_masking()"));
        ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = FALSE"));
        ASSERT_OK(conn.Execute("COMMIT"));
        ASSERT_EQ(result, (decltype(result){true}));
        ASSERT_OK(conn.Execute("CREATE TABLE test (name text)"));
        ASSERT_OK(conn.Execute("CREATE ROLE test_role LOGIN"));
        ASSERT_OK(conn.Execute("SECURITY LABEL FOR anon ON ROLE test_role IS 'MASKED'"));
        ASSERT_OK(conn.Execute("GRANT SELECT ON test TO test_role"));
        ASSERT_OK(conn.Execute(
            "SECURITY LABEL FOR anon ON COLUMN test.name IS "
            "'MASKED WITH VALUE ''CONFIDENTIAL'''"));
        ASSERT_OK(conn.Execute("INSERT INTO test VALUES ('hi'), ('bye')"));
      },
      .check_before_upgrade = [check_query] { ASSERT_NO_FATALS(check_query(kAnyTserver)); },
      .check_in_mixed_mode = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15));
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11));
      },
      .check_after_upgrade = [check_query] { ASSERT_NO_FATALS(check_query(kAnyTserver)); },
    };
  }

  // pg_stat_statements counts the statements of every database, so this case gets a database of
  // its own and only looks at its statements.
  ExtensionUpgradeCase PgStatStatements() {
    static const auto kDb = "pgss_db";
    static const auto kCreateTableStmt = "CREATE TABLE test (t int)";
    auto check_query = [this](std::optional<size_t> tserver_idx, bool after_upgrade) {
      const auto select_pg_stat_stmts =
          "SELECT query FROM pg_stat_statements WHERE query NOT LIKE 'SELECT%' AND dbid = "
          "(SELECT oid FROM pg_database WHERE datname = current_database()) ORDER BY query";
      const auto select_pg_stat_stmts_info = "SELECT dealloc FROM pg_stat_statements_info";
      auto conn = ASSERT_RESULT(ConnectToDb(kDb, tserver_idx));
      auto result = ASSERT_RESULT(conn.FetchRows<std::string>(select_pg_stat_stmts));
      if (after_upgrade) {
        ASSERT_TRUE(result.empty());
        const auto delete_stmt = "DELETE FROM test";
        ASSERT_OK(conn.Execute(delete_stmt));
        result = ASSERT_RESULT(conn.FetchRows<std::string>(select_pg_stat_stmts));
        ASSERT_VECTORS_EQ(result, (decltype(result){delete_stmt}));
        auto info = ASSERT_RESULT(conn.FetchRows<int64_t>(select_pg_stat_stmts_info));
        ASSERT_FALSE(info.empty());
      } else {
        ASSERT_VECTORS_EQ(result, (decltype(result){kCreateTableStmt}));
        ASSERT_NOK_STR_CONTAINS(conn.Execute(select_pg_stat_stmts_info),
            "relation \"pg_stat_statements_info\" does not exist");
      }
    };
    return {
      .name = "PgStatStatements",
      .setup = [this] {
        ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0", kDb)));
        auto conn = ASSERT_RESULT(ConnectToDb(kDb, kMixedModeTserverPg11));
        ASSERT_OK(conn.Execute("CREATE EXTENSION IF NOT EXISTS pg_stat_statements"));
        ASSERT_OK(conn.Fetch("SELECT pg_stat_statements_reset()"));
        ASSERT_OK(conn.Execute(kCreateTableStmt));
      },
      .check_before_upgrade = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*after_upgrade=*/false));
      },
      .check_in_mixed_mode = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*after_upgrade=*/true));
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*after_upgrade=*/false));
      },
      .check_after_upgrade = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*after_upgrade=*/true));
      },
    };
  }

  // Dropping plpgsql would drop the plpgsql functions of other extensions, so this case gets a
  // database of its own.
  ExtensionUpgradeCase PlPgsql() {
    static const auto kDb = "plpgsql_db";
    return {
      .name = "PlPgsql",
      .setup = [this] {
        ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0", kDb)));
        auto conn = ASSERT_RESULT(ConnectToDb(kDb, kAnyTserver));
        for (const auto& statement : {
            "DROP EXTENSION plpgsql CASCADE",
            "CREATE LANGUAGE plpgsql",
            "CREATE FUNCTION test() RETURNS INTEGER AS $$begin return 1; end$$ LANGUAGE plpgsql",
            "DROP LANGUAGE plpgsql CASCADE",
            "CREATE EXTENSION plpgsql"}) {
          ASSERT_OK(conn.Execute(statement));
        }
      },
    };
  }
};

TEST_F(YsqlMajorExtensionUpgradeTest, Upgrade) {
  TestUpgrade({
      Simple(), HStore(), FileFdw(), FuzzyStrMatch(), PasswordCheck(), PgCrypto(),
      PgStatStatements(), PostgresFdw(), HypoPG(), Orafce(), PgHintPlan(), Anon(), PlPgsql(),
      YbYcqlUtils()});
}

// A single pg_upgrade --check reports every incompatible extension.
TEST_F(YsqlMajorExtensionUpgradeTest, IncompatibleExtensions) {
  ASSERT_OK(ExecuteStatement("CREATE EXTENSION pg_stat_monitor"));
  ASSERT_OK(ExecuteStatement("CREATE EXTENSION pgcrypto SCHEMA pg_catalog"));
  ASSERT_OK(ValidateUpgradeCompatibilityFailure(std::vector<std::string>{
      "In database: yugabyte",
      "  pg_stat_monitor",
      "Your installation contains extensions that are not compatible",
      "with YSQL major version upgrade. Please uninstall the extensions",
      "using DROP EXTENSION, and reinstall them after the upgrade. A list of",
      "extensions with problems is printed above and in the file:",
      "  pgcrypto installed in pg_catalog schema",
      "Your installation contains the 'pgcrypto' extension in the",
      "conflicting 'pg_catalog' schema. To proceed with the upgrade, please",
      "uninstall the extension using DROP EXTENSION, and reinstall it into",
      "a different schema (e.g. public).",
  }));
  // pg_catalog prohibits ALTER EXTENSION, so pgcrypto has to be dropped and recreated.
  ASSERT_OK(ExecuteStatements({
      "DROP EXTENSION pg_stat_monitor",
      "DROP EXTENSION pgcrypto",
      "CREATE EXTENSION pgcrypto SCHEMA public",
  }));
  ASSERT_OK(UpgradeClusterToCurrentVersion());
  ASSERT_OK(ExecuteStatement("CREATE EXTENSION pg_stat_monitor"));
}

} // namespace yb
