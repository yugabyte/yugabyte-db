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

#include <string>

#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(ysql_catalog_preload_additional_tables);
DECLARE_string(ysql_catalog_preload_additional_table_list);
DECLARE_bool(ysql_use_relcache_file);
DECLARE_int32(tserver_yb_client_default_timeout_ms);
DECLARE_string(vmodule);

namespace yb::pgwrapper {

class PgRelcacheFaultToleranceTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vmodule)="libpq_utils=1";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_relcache_file) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_yb_client_default_timeout_ms) = 120000;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }

  // Corrupt pg_class.relnatts for a given table.
  Status CorruptRelnatts(PGConn& conn, const std::string& table_name, int new_relnatts) {
    RETURN_NOT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = true"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "UPDATE pg_class SET relnatts = $0 WHERE relname = '$1'",
        new_relnatts, table_name));
    RETURN_NOT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = false"));
    return Status::OK();
  }

  // Run a DDL on a separate table to bump catalog version and invalidate the
  // relcache init file.
  Status InvalidateRelcacheInitFile(PGConn& conn) {
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE IF NOT EXISTS relcache_invalidator (id int)"));
    RETURN_NOT_OK(conn.Execute(
        "ALTER TABLE relcache_invalidator ADD COLUMN IF NOT EXISTS dummy int"));
    return Status::OK();
  }

  // Open a fresh connection and verify it works by running a simple query.
  Result<PGConn> ConnectAndVerify() {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("SELECT 1"));
    return conn;
  }

  struct TimedConnectionResult {
    PGConn conn;
    MonoDelta elapsed;
  };

  Result<TimedConnectionResult> TimedConnectAndQuery(const std::string& query) {
    auto start = MonoTime::Now();
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Fetch(query));
    auto elapsed = MonoTime::Now() - start;
    return TimedConnectionResult{std::move(conn), elapsed};
  }

  // Create a rich schema (partitions, FKs, check constraints, partial indexes,
  // inheritance, triggers, RLS, materialized views) so that every corruption
  // test exercises the recovery path with non-trivial catalog state.
  Status CreateFullSchema(PGConn& conn) {
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE orders(id int, region text, amount numeric "
        "CHECK (amount >= 0)) PARTITION BY LIST (region)"));
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE orders_us PARTITION OF orders FOR VALUES IN ('us')"));
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE orders_eu PARTITION OF orders FOR VALUES IN ('eu')"));
    RETURN_NOT_OK(conn.Execute(
        "INSERT INTO orders VALUES (1, 'us', 100), (2, 'eu', 200), (3, 'us', 50)"));

    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE customers(cid int PRIMARY KEY, name text NOT NULL, oid_ref int)"));
    RETURN_NOT_OK(conn.Execute("INSERT INTO customers VALUES (1, 'Alice', 1)"));

    RETURN_NOT_OK(conn.Execute(
        "CREATE INDEX orders_large_idx ON orders_us (amount) WHERE amount > 75"));

    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE base_log(ts timestamptz DEFAULT now(), msg text)"));
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE app_log(severity int) INHERITS (base_log)"));
    RETURN_NOT_OK(conn.Execute(
        "INSERT INTO app_log(msg, severity) VALUES ('boot', 1)"));

    RETURN_NOT_OK(conn.Execute(
        "CREATE OR REPLACE FUNCTION trg_noop() RETURNS trigger "
        "LANGUAGE plpgsql AS $$ BEGIN RETURN NEW; END; $$"));
    RETURN_NOT_OK(conn.Execute(
        "CREATE TRIGGER customers_trg BEFORE INSERT ON customers "
        "FOR EACH ROW EXECUTE FUNCTION trg_noop()"));

    RETURN_NOT_OK(conn.Execute("ALTER TABLE customers ENABLE ROW LEVEL SECURITY"));
    RETURN_NOT_OK(conn.Execute(
        "CREATE POLICY customers_pol ON customers FOR SELECT USING (true)"));

    RETURN_NOT_OK(conn.Execute(
        "CREATE MATERIALIZED VIEW orders_summary AS "
        "SELECT region, sum(amount) as total FROM orders GROUP BY region"));

    return Status::OK();
  }

  void VerifyFullSchema(PGConn& conn) {
    auto rows = ASSERT_RESULT(conn.FetchAllAsString("SELECT count(*) FROM orders"));
    LOG(INFO) << "orders count: " << rows;

    rows = ASSERT_RESULT(conn.FetchAllAsString("SELECT name FROM customers"));
    LOG(INFO) << "customers: " << rows;

    rows = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM app_log"));
    LOG(INFO) << "app_log: " << rows;

    rows = ASSERT_RESULT(conn.FetchAllAsString(
        "SELECT * FROM orders_summary ORDER BY region"));
    LOG(INFO) << "orders_summary: " << rows;
  }

  /*
   * relnatts lower than actual columns: triggers "catalog is missing N attribute(s)"
   * during preload when additional tables are preloaded; otherwise lazy RelationBuildDesc.
   */
  void RunRelnattsLowerTest(const std::string& table_name) {
    auto setup_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateFullSchema(setup_conn));
    ASSERT_OK(setup_conn.ExecuteFormat("CREATE TABLE $0 (col1 int, col2 int)", table_name));
    ASSERT_OK(setup_conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 2), (3, 4)", table_name));

    ASSERT_OK(CorruptRelnatts(setup_conn, table_name, 1));
    ASSERT_OK(InvalidateRelcacheInitFile(setup_conn));

    auto result = ASSERT_RESULT(TimedConnectAndQuery("SELECT 1"));
    LOG(INFO) << "Connection + simple query took " << result.elapsed;

    VerifyFullSchema(result.conn);

    auto status = result.conn.FetchFormat(
        "SELECT col1 + col2 FROM $0 ORDER BY col2 DESC", table_name);
    ASSERT_FALSE(status.ok()) << status.status();
    LOG(INFO) << "Query on corrupt table (expected error): " << status.status();

    ASSERT_OK(CorruptRelnatts(setup_conn, table_name, 2));
    ASSERT_OK(InvalidateRelcacheInitFile(setup_conn));

    auto verify_conn = ASSERT_RESULT(Connect());
    auto rows = ASSERT_RESULT(verify_conn.FetchAllAsString(
        Format("SELECT col1 + col2 FROM $0 ORDER BY col2 DESC", table_name)));
    LOG(INFO) << "After fix, query result: " << rows;
  }

  /*
   * relnatts higher than actual columns: triggers "invalid attribute number" in YbApplyAttr
   * when those rows are loaded during preload.
   */
  void RunRelnattsHigherTest(const std::string& table_name) {
    auto setup_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateFullSchema(setup_conn));
    ASSERT_OK(setup_conn.ExecuteFormat("CREATE TABLE $0 (col1 int, col2 int)", table_name));
    ASSERT_OK(setup_conn.ExecuteFormat("INSERT INTO $0 VALUES (10, 20), (30, 40)", table_name));

    ASSERT_OK(CorruptRelnatts(setup_conn, table_name, 3));
    ASSERT_OK(InvalidateRelcacheInitFile(setup_conn));

    auto result = ASSERT_RESULT(TimedConnectAndQuery("SELECT 1"));
    LOG(INFO) << "Connection + simple query took " << result.elapsed;

    VerifyFullSchema(result.conn);

    auto status = result.conn.FetchFormat(
        "SELECT col1 + col2 FROM $0 ORDER BY col2 DESC", table_name);
    ASSERT_FALSE(status.ok()) << status.status();
    LOG(INFO) << "Query on corrupt table (expected error): " << status.status();

    ASSERT_OK(CorruptRelnatts(setup_conn, table_name, 2));
    ASSERT_OK(InvalidateRelcacheInitFile(setup_conn));

    auto verify_conn = ASSERT_RESULT(Connect());
    auto rows = ASSERT_RESULT(verify_conn.FetchAllAsString(
        Format("SELECT col1 + col2 FROM $0 ORDER BY col2 DESC", table_name)));
    LOG(INFO) << "After fix, query result: " << rows;
  }

  void RunPartitionTwoDefaultsTest() {
    auto setup_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateFullSchema(setup_conn));

    ASSERT_OK(setup_conn.Execute(
        "CREATE TABLE t_def_helper(id int) PARTITION BY LIST (id)"));
    ASSERT_OK(setup_conn.Execute(
        "CREATE TABLE t_def_helper_default PARTITION OF t_def_helper DEFAULT"));
    auto default_bound = ASSERT_RESULT(setup_conn.FetchRowAsString(
        "SELECT relpartbound::text FROM pg_class WHERE relname = 't_def_helper_default'"));
    LOG(INFO) << "Captured default partition bound: " << default_bound;
    ASSERT_OK(setup_conn.Execute("DROP TABLE t_def_helper CASCADE"));

    ASSERT_OK(setup_conn.Execute(
        "CREATE TABLE t_def_part(id int, val text) PARTITION BY LIST (id)"));
    ASSERT_OK(setup_conn.Execute(
        "CREATE TABLE t_def_part_1 PARTITION OF t_def_part FOR VALUES IN (1, 2, 3)"));
    ASSERT_OK(setup_conn.Execute(
        "CREATE TABLE t_def_part_default PARTITION OF t_def_part DEFAULT"));
    ASSERT_OK(setup_conn.Execute(
        "CREATE TABLE t_def_part_extra PARTITION OF t_def_part FOR VALUES IN (4, 5)"));
    ASSERT_OK(setup_conn.Execute(
        "INSERT INTO t_def_part VALUES (1, 'a'), (4, 'b'), (99, 'default_val')"));

    ASSERT_OK(setup_conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = true"));
    ASSERT_OK(setup_conn.Execute("SET ysql_upgrade_mode = true"));
    ASSERT_OK(setup_conn.ExecuteFormat(
        "UPDATE pg_class SET relpartbound = '$0' WHERE relname = 't_def_part_extra'",
        default_bound));
    ASSERT_OK(setup_conn.Execute("SET ysql_upgrade_mode = false"));
    ASSERT_OK(setup_conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed = false"));
    ASSERT_OK(InvalidateRelcacheInitFile(setup_conn));

    auto result = ASSERT_RESULT(TimedConnectAndQuery("SELECT 1"));
    LOG(INFO) << "Connection + simple query took " << result.elapsed;

    VerifyFullSchema(result.conn);

    auto status = result.conn.Fetch("SELECT * FROM t_def_part ORDER BY id");
    LOG(INFO) << "Query on partition table status: "
              << (status.ok() ? "OK" : status.status().ToString());
  }
};

class PgRelcacheFaultToleranceWithPreloadTest : public PgRelcacheFaultToleranceTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_tables) = true;
    // Match pg_catalog_perf-test additional preload list so relcache init prefetches extra
    // catalogs.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_table_list) =
        "pg_cast,pg_inherits,pg_policy,pg_proc,pg_tablespace,pg_trigger";
    PgRelcacheFaultToleranceTest::SetUp();
  }
};

TEST_F(PgRelcacheFaultToleranceTest, RelnattsLower) {
  RunRelnattsLowerTest("t_relnatts_low");
}

TEST_F(PgRelcacheFaultToleranceWithPreloadTest, RelnattsLower) {
  RunRelnattsLowerTest("t_relnatts_low");
}

TEST_F(PgRelcacheFaultToleranceTest, RelnattsHigher) {
  RunRelnattsHigherTest("t_relnatts_high");
}

TEST_F(PgRelcacheFaultToleranceWithPreloadTest, RelnattsHigher) {
  RunRelnattsHigherTest("t_relnatts_high");
}

TEST_F(PgRelcacheFaultToleranceTest, PartitionTwoDefaults) {
  RunPartitionTwoDefaultsTest();
}

TEST_F(PgRelcacheFaultToleranceWithPreloadTest, PartitionTwoDefaults) {
  RunPartitionTwoDefaultsTest();
}

}  // namespace yb::pgwrapper
