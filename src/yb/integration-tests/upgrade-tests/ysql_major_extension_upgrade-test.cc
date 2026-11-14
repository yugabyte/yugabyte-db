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

namespace {

// An extension to carry through a YSQL major upgrade. All of them share one upgrade, since each
// upgrade takes over a minute.
struct ExtensionUpgradeCase {
  std::string name;
  std::function<void()> setup;
  std::function<void()> check_before_upgrade = [] {};
  // Runs once yb-tserver kMixedModeTserverPg15 is on the current version and the other yb-tservers
  // are on the old version.
  std::function<void()> check_in_mixed_mode = [] {};
  std::function<void()> check_after_upgrade = [] {};
};

}  // namespace

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
      .check_after_upgrade = [this] {
        // pg_upgrade's ALTER EXTENSION UPDATE takes pg_partman through its update scripts.
        auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
        ASSERT_TRUE(ASSERT_RESULT(conn.FetchRow<bool>(
            "SELECT extversion = default_version FROM pg_extension "
            "JOIN pg_available_extensions ON name = extname WHERE extname = 'pg_partman'")));
      },
    };
  }

  // An extension whose objects behave the same on the old and the current version.
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
  // yb-tservers on the current version, and on the old ones once the upgrade is finalized.
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

  // hstore_hash_extended() is in hstore on both versions.
  ExtensionUpgradeCase HStore() {
    return UnchangedCase("HStore", "hstore", [](pgwrapper::PGConn& conn) {
      auto result = ASSERT_RESULT((conn.FetchRows<int64_t, int64_t>(
          "SELECT hstore_hash_extended('\"a key\" =>1'::hstore, 0),"
          "hstore_hash_extended('\"a key\" =>1'::hstore, 1)")));
      ASSERT_FALSE(result.empty());
    });
  }

  ExtensionUpgradeCase PostgresFdw() {
    static const auto kQuery =
        "SELECT server_name FROM postgres_fdw_get_connections(true) ORDER BY 1;";
    // Global views already install postgres_fdw in pg_catalog.
    return NewObjectCase(
        "PostgresFdw", "IF NOT EXISTS postgres_fdw", kQuery,
        "function postgres_fdw_get_connections(boolean) does not exist",
        [](pgwrapper::PGConn& conn) {
          auto result = ASSERT_RESULT((conn.FetchRows<std::string>(kQuery)));
          ASSERT_VECTORS_EQ(result, (decltype(result){}));
        });
  }

  // hypopg_hidden_indexes() is in hypopg on both versions.
  ExtensionUpgradeCase HypoPG() {
    return UnchangedCase("HypoPG", "hypopg", [](pgwrapper::PGConn& conn) {
      auto result = ASSERT_RESULT((conn.FetchRows<pgwrapper::PGUint64>(
          "SELECT COUNT(*) FROM hypopg_hidden_indexes();")));
      ASSERT_VECTORS_EQ(result, (decltype(result){0}));
    });
  }

  // pg_upgrade updates orafce in each database, and the update creates a cluster-wide role.
  ExtensionUpgradeCase Orafce() {
    static const auto kOtherDb = "orafce_db2";
    static const auto kQueryNew =
        "SELECT oracle.sys_extract_utc('2026-01-01 05:00:00+05'::timestamptz)::text AS result";
    auto check_query = [this](std::optional<size_t> tserver_idx, bool should_fail) {
      for (const auto& db : {std::string("yugabyte"), std::string(kOtherDb)}) {
        auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db, tserver_idx));
        // The objects that the old version already had keep working everywhere.
        auto res = ASSERT_RESULT(conn.FetchRows<std::string>("SELECT * FROM oracle.user_tables"));
        ASSERT_FALSE(res.empty());
        if (should_fail) {
          ASSERT_NOK_STR_CONTAINS(conn.Execute(kQueryNew),
              "function oracle.sys_extract_utc(timestamp with time zone) does not exist");
        } else {
          auto result_new = ASSERT_RESULT(conn.FetchRows<std::string>(kQueryNew));
          ASSERT_VECTORS_EQ(result_new, (decltype(result_new){"2026-01-01 00:00:00"}));
        }
      }
    };
    return {
      .name = "Orafce",
      .setup = [this] {
        ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0", kOtherDb)));
        for (const auto& db : {std::string("yugabyte"), std::string(kOtherDb)}) {
          auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db));
          ASSERT_OK(conn.Execute("CREATE EXTENSION orafce"));
        }
      },
      .check_before_upgrade = [check_query] {
        ASSERT_NO_FATALS(check_query(kAnyTserver, /*should_fail=*/true));
      },
      .check_in_mixed_mode = [check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg15, /*should_fail=*/false));
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/true));
      },
      .check_after_upgrade = [this, check_query] {
        ASSERT_NO_FATALS(check_query(kMixedModeTserverPg11, /*should_fail=*/false));
        auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
        auto role_count = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
            "SELECT count(*) FROM pg_roles WHERE rolname = 'orafce_set_umask'"));
        ASSERT_EQ(role_count, 1U);
      },
    };
  }

  // fuzzystrmatch 1.2 adds daitch_mokotoff().
  ExtensionUpgradeCase FuzzyStrMatch() {
    static const auto kQueryNew = "SELECT daitch_mokotoff('Augsburg'::text)::text";
    auto c = NewObjectCase(
        "FuzzyStrMatch", "fuzzystrmatch", kQueryNew,
        "function daitch_mokotoff(text) does not exist",
        [](pgwrapper::PGConn& conn) {
          auto result_new = ASSERT_RESULT(conn.FetchRows<std::string>(kQueryNew));
          ASSERT_VECTORS_EQ(result_new, (decltype(result_new){"{054795}"}));
        });
    // The objects that the old version already had keep working everywhere.
    auto check_old_objects = [this](std::optional<size_t> tserver_idx) {
      auto conn = ASSERT_RESULT(CreateConnToTs(tserver_idx));
      auto result = ASSERT_RESULT((conn.FetchRows<std::string>("SELECT soundex('hello world!');")));
      ASSERT_VECTORS_EQ(result, (decltype(result){{"H464"}}));
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

  // YB_TODO_PG19MERGE: no pg_hint_plan update path from 1.5.1-yb-1.0, so Upgrade leaves this case
  // out.
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
  // YB_TODO_PG19MERGE: postgresql_anonymizer is not built on pg19, so Upgrade leaves this case out.
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
      const auto select_stats_since = "SELECT count(stats_since) FROM pg_stat_statements";
      auto conn = ASSERT_RESULT(ConnectToDb(kDb, tserver_idx));
      auto result = ASSERT_RESULT(conn.FetchRows<std::string>(select_pg_stat_stmts));
      if (after_upgrade) {
        ASSERT_TRUE(result.empty());
        const auto delete_stmt = "DELETE FROM test";
        ASSERT_OK(conn.Execute(delete_stmt));
        result = ASSERT_RESULT(conn.FetchRows<std::string>(select_pg_stat_stmts));
        ASSERT_VECTORS_EQ(result, (decltype(result){delete_stmt}));
        auto stats_since = ASSERT_RESULT(conn.FetchRows<int64_t>(select_stats_since));
        ASSERT_FALSE(stats_since.empty());
      } else {
        ASSERT_VECTORS_EQ(result, (decltype(result){kCreateTableStmt}));
        ASSERT_NOK_STR_CONTAINS(conn.Execute(select_stats_since),
            "column \"stats_since\" does not exist");
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
            // Since PG13, CREATE LANGUAGE creates the extension, which DROP LANGUAGE cannot drop.
            "DROP EXTENSION plpgsql CASCADE",
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
      PgStatStatements(), PostgresFdw(), HypoPG(), Orafce(), PlPgsql(), YbYcqlUtils()});
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

} // namespace yb
