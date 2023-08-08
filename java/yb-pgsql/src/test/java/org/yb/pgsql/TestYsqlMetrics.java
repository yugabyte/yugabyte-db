// Copyright (c) YugaByte, Inc.
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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

import org.apache.commons.lang3.time.StopWatch;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

import org.yb.minicluster.MiniYBCluster;

@RunWith(value=YBTestRunner.class)
public class TestYsqlMetrics extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestYsqlMetrics.class);

  private static final String PEAK_MEM_FIELD = "Peak Memory Usage";

  @Test
  public void testMetrics() throws Exception {
    Statement statement = connection.createStatement();

    // DDL is non-txn.
    verifyStatementMetric(statement, "CREATE TABLE test (k int PRIMARY KEY, v int)",
                          OTHER_STMT_METRIC, 1, 0, 1, true);

    // Select uses txn.
    verifyStatementMetric(statement, "SELECT * FROM test",
                          SELECT_STMT_METRIC, 1, 0, 1, true);

    // Non-txn insert.
    verifyStatementMetric(statement, "INSERT INTO test VALUES (1, 1)",
                          INSERT_STMT_METRIC, 1, 1, 1, true);
    // Txn insert.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (2, 2)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 1, true);

    // Limit query on complex view (issue #3811).
    verifyStatementMetric(statement, "SELECT * FROM information_schema.key_column_usage LIMIT 1",
                          SELECT_STMT_METRIC, 1, 0, 1, true);

    // Non-txn update.
    verifyStatementMetric(statement, "UPDATE test SET v = 2 WHERE k = 1",
                          UPDATE_STMT_METRIC, 1, 1, 1, true);
    // Txn update.
    verifyStatementMetric(statement, "UPDATE test SET v = 3",
                          UPDATE_STMT_METRIC, 1, 0, 1, true);

    // Non-txn delete.
    verifyStatementMetric(statement, "DELETE FROM test WHERE k = 2",
                          DELETE_STMT_METRIC, 1, 1, 1, true);
    // Txn delete.
    verifyStatementMetric(statement, "DELETE FROM test",
                          DELETE_STMT_METRIC, 1, 0, 1, true);

    // Invalid statement should not update metrics.
    verifyStatementMetric(statement, "INSERT INTO invalid_table VALUES (1)",
                          INSERT_STMT_METRIC, 0, 0, 0, false);

    // DML queries transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (3, 3)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (4, 4)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 1, true);

    // DDL queries transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "CREATE TABLE test2 (a int)",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "DROP TABLE test2",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 0, true);

    // Set session variable in transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "SET yb_debug_report_error_stacktrace=true;",
                          OTHER_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "COMMIT",
                          COMMIT_STMT_METRIC, 1, 0, 1, true);

    // DML/DDL queries transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (5, 5)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "CREATE TABLE test2 (a int)",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (6, 6)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "UPDATE test SET k = 600 WHERE k = 6",
                          UPDATE_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "ALTER TABLE test2 ADD COLUMN b INT",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "DROP TABLE test2",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "COMMIT",
                          COMMIT_STMT_METRIC, 1, 0, 1, true);

    // DML/DDL queries transaction block with rollback.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (7, 7)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "CREATE TABLE test2 (a int)",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (8, 8)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "UPDATE test SET k = 800 WHERE k = 8",
                          UPDATE_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "ALTER TABLE test2 ADD COLUMN b INT",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "DROP TABLE test2",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "ROLLBACK",
                          ROLLBACK_STMT_METRIC, 1, 0, 0, true);

    // Nested transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "CREATE TABLE test2 (a int)",
                          OTHER_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (9, 9)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 1, true);

    // Nested transaction block with empty inner transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "DELETE FROM test WHERE k = 9;",
                          DELETE_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 1, true);

    // Invalid transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO invalid_table VALUES (1)",
                          INSERT_STMT_METRIC, 0, 0, 0, false);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 0, true);

    // Empty transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 0, true);

    // Empty transaction block with DML execution prior to BEGIN.
    verifyStatementMetric(statement, "INSERT INTO test VALUES (10, 10)",
                          INSERT_STMT_METRIC, 1, 1, 1, true);
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 0, true);

    // Empty nested transaction block.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "END",
                          COMMIT_STMT_METRIC, 1, 0, 0, true);

    // Extra COMMIT statement.
    verifyStatementMetric(statement, "BEGIN",
                          BEGIN_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "INSERT INTO test VALUES (11, 11)",
                          INSERT_STMT_METRIC, 1, 0, 0, true);
    verifyStatementMetric(statement, "COMMIT",
                          COMMIT_STMT_METRIC, 1, 0, 1, true);
    verifyStatementMetric(statement, "COMMIT",
                          COMMIT_STMT_METRIC, 1, 0, 0, true);
  }

  @Test
  public void testMetricRows() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      verifyStatementMetricRows(
        stmt,"CREATE TABLE test (k INT PRIMARY KEY, v INT)",
        OTHER_STMT_METRIC, 1, 0);

      verifyStatementMetricRows(
        stmt, "INSERT INTO test VALUES (1, 1), (2, 2), (3, 3), (4, 4), (5, 5)",
        INSERT_STMT_METRIC, 1, 5);

      verifyStatementMetricRows(
        stmt, "UPDATE test SET v = v + 1 WHERE v % 2 = 0",
        UPDATE_STMT_METRIC, 1, 2);

      verifyStatementMetricRows(
        stmt, "SELECT count(k) FROM test",
        AGGREGATE_PUSHDOWNS_METRIC, 1, 1);

      verifyStatementMetricRows(
        stmt, "SELECT * FROM test",
        SELECT_STMT_METRIC, 1, 5);

      verifyStatementMetricRows(
        stmt, "INSERT INTO test VALUES (6, 6), (7, 7)",
        SINGLE_SHARD_TRANSACTIONS_METRIC, 0, 0);

      // Single row transaction.
      verifyStatementMetricRows(
        stmt, "INSERT INTO test VALUES (8, 8)",
        SINGLE_SHARD_TRANSACTIONS_METRIC, 1, 1);

      verifyStatementMetricRows(
        stmt, "DELETE FROM test",
        DELETE_STMT_METRIC, 1, 8);

      // Testing catalog cache.
      long miss_init = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);
      stmt.execute("SELECT ln(2)");
      long miss1 = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);

      // Misses should strictly increase, we check that miss1 >= miss_init + 1.
      assertGreaterThanOrEqualTo(
          String.format("Expected misses to increase after " +
                        "first function call. Before: %d, After %d",
                        miss_init, miss1), miss1, miss_init+1);


      // Lookups done to resolve ln should've been cached.
      stmt.execute("SELECT ln(2)");
      long miss2 = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);
      assertEquals(miss1, miss2);

      // Invalidate the cached resolution for ln from another connection.
      Connection connection2 = getConnectionBuilder().connect();
      try (Statement stmt2 = connection2.createStatement()) {

        long misses_before_second_cxn_call = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);
        assertGreaterThanOrEqualTo(
            String.format("Misses shouldn't decrease after making a new " +
                        "connection. Before: %d, After %d",
                        miss2,
                        misses_before_second_cxn_call),
                        misses_before_second_cxn_call,
                        miss2);

        stmt2.execute("SELECT ln(2)");

        // Making sure that miss counts from two different connections
        // add onto each other.
        long misses_after_second_cxn_call = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);
        assertGreaterThanOrEqualTo(
            String.format("Expected misses to increase after " +
                        "second connection's first cache miss. Before: %d, After %d",
                        misses_before_second_cxn_call,
                        misses_after_second_cxn_call),
                        misses_after_second_cxn_call,
                        misses_before_second_cxn_call+1);

        // Make a type that should invalid cached resolution for ln.
        stmt2.execute("CREATE TYPE ln AS (a INTEGER)");
        long misses_post_invalidate_before = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);

        // Wait for catalog change to register, needed in debug mode.
        Thread.sleep(5 * MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS);

        // This should miss on the cache now and cause a master lookup.
        stmt.execute("SELECT ln(2)");
        long misses_post_invalidate_after = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);
        assertGreaterThanOrEqualTo(
            String.format("Expected misses to increase after " +
                        "cache invalidation. Before: %d, After %d",
                        misses_post_invalidate_before,
                        misses_post_invalidate_after),
                        misses_post_invalidate_after,
                        misses_post_invalidate_before+1);

        stmt.execute("SELECT ln(2)");

        // No misses should occur again.
        long misses_post_invalidate_after2 = getMetricCounter(CATALOG_CACHE_MISSES_METRICS);
        assertEquals(misses_post_invalidate_after, misses_post_invalidate_after2);
      }
    }
  }

  @Test
  public void testStatementStats() throws Exception {
    Statement statement = connection.createStatement();

    String stmt, statStmt;

    // DDL is non-txn.
    stmt     = "CREATE TABLE test (k int PRIMARY KEY, v int)";
    statStmt = "CREATE TABLE test (k int PRIMARY KEY, v int)";
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Select uses txn.
    stmt = "SELECT * FROM test";
    statStmt = stmt;
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Select uses txn - test reset.
    verifyStatementStatWithReset(statement, stmt, statStmt, 100, 200);

    // Non-txn insert.
    stmt     = "INSERT INTO test VALUES (1, 1)";
    statStmt = "INSERT INTO test VALUES ($1, $2)";
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Multiple non-txn inserts to check statement fingerprinting.
    final int numNonTxnInserts = 10;
    final int offset = 100;
    for (int i = 0; i < numNonTxnInserts; i++) {
      int j = offset + i;
      stmt = "INSERT INTO test VALUES (" + j + ", " + j + ")";
      // Use same statStmt.
      verifyStatementStat(statement, stmt, statStmt, 1, true);
    }

    // Txn insert.
    statement.execute("BEGIN");
    stmt     = "INSERT INTO test VALUES (2, 2)";
    statStmt = "INSERT INTO test VALUES ($1, $2)";
    verifyStatementStat(statement, stmt, statStmt, 1, true);
    statement.execute("END");

    // Non-txn update.
    stmt     = "UPDATE test SET v = 2 WHERE k = 1";
    statStmt = "UPDATE test SET v = $1 WHERE k = $2";
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Txn update.
    stmt     = "UPDATE test SET v = 3";
    statStmt = "UPDATE test SET v = $1";
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Non-txn delete.
    stmt     = "DELETE FROM test WHERE k = 2";
    statStmt = "DELETE FROM test WHERE k = $1";
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Txn delete.
    stmt     = "DELETE FROM test";
    statStmt = stmt;
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Limit query on complex view (issue #3811).
    stmt     = "SELECT * FROM information_schema.key_column_usage LIMIT 1";
    statStmt = "SELECT * FROM information_schema.key_column_usage LIMIT $1";
    verifyStatementStat(statement, stmt, statStmt, 1, true);

    // Invalid statement should not update metrics.
    stmt     = "INSERT INTO invalid_table VALUES (1)";
    statStmt = "INSERT INTO invalid_table VALUES ($1)";
    verifyStatementStat(statement, stmt, statStmt, 0, false);
  }

  @Test
  public void testStatementTime() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test(k INT, v VARCHAR)");
      String preparedStmtSql =
          "PREPARE foo(INT, VARCHAR, INT, VARCHAR, INT, VARCHAR, INT, VARCHAR, INT, VARCHAR) " +
          "AS INSERT INTO test VALUES($1, $2), ($3, $4), ($5, $6), ($7, $8), ($9, $10)";
      statement.execute(preparedStmtSql);
      statement.execute(
          "CREATE PROCEDURE proc(n INT) LANGUAGE PLPGSQL AS $$ DECLARE c INT := 0; BEGIN " +
          "WHILE c < n LOOP c := c + 1; INSERT INTO test VALUES(c, 'value'); END LOOP; END; $$");
      testStatement(statement,
          "INSERT INTO test VALUES(1, '1'), (2, '2'), (3, '3'), (4, '4'), (5, '5')",
          INSERT_STMT_METRIC,
          "INSERT INTO test VALUES($1, $2), ($3, $4), ($5, $6), ($7, $8), ($9, $10)");
      testStatement(statement,
                    "EXECUTE foo(1, '1', 2, '2', 3, '3', 4, '4', 5, '5')",
                    INSERT_STMT_METRIC,
                    preparedStmtSql);
      testStatement(statement, "CALL proc(40)", OTHER_STMT_METRIC);
      testStatement(statement, "DO $$ BEGIN CALL proc(40); END $$", OTHER_STMT_METRIC);
    }
  }

  @Test
  public void testExplainTime() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test(k INT, v VARCHAR)");
      statement.execute(
          "CREATE OR REPLACE FUNCTION func(n INT) RETURNS INT AS $$ DECLARE c INT := 0; BEGIN " +
          "WHILE c < n LOOP c := c + 1; insert into test values(c, 'value'); END LOOP; " +
          "RETURN 0; END; $$ LANGUAGE PLPGSQL");
      final String query = "EXPLAIN(COSTS OFF, ANALYZE) SELECT func(500)";
      ResultSet result = statement.executeQuery(query);
      AggregatedValue stat = getStatementStat(query);
      assertEquals(1, stat.count);
      while(result.next()) {
        final String row = result.getString(1);
        if(row.contains("Execution Time")) {
          double query_time = Double.parseDouble(result.getString(1).replaceAll("[^\\d.]", ""));
          // As stat.total_time indicates total time of EXPLAIN query,
          // actual query total time is a little bit less.
          // It is expected that query time is not less than 90% of stat.total_time.
          assertQueryTime(query, query_time, 0.9 * stat.value);
        }
      }
    }
  }

  /**
   * This test does memory stats verification in EXPLAIN ANALYZE's output.
   * First, it does a rough validation on the stats by comparing queries that consume different
   * amounts of memory and validating their max memory outputs.
   * Second, it does a more accurate validation on the stats against the sorting memory, to validate
   * that max memory is slightly higher than sorting's memory.
   * It also does basic tests to ensure the newly added cutomized logic
   * for memory stats doesn't break existing EXPLAIN ANALYZE's execution for DDLs.
   */
  @Test
  public void testExplainMaxMemory() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE tst (c1 INT);");
      statement.execute("PREPARE demo(int) AS "
        + "SELECT m1 FROM "
        + "(SELECT MAX(c1) AS m1 FROM "
        + "(SELECT * FROM tst LIMIT $1 "
        + ") AS t0 "
        + "GROUP BY c1) AS t1 "
        + "ORDER BY m1;"
      );

      // Verify INSERT output
      {
        final boolean hasRs = statement.execute(
                "EXPLAIN ANALYZE INSERT INTO tst SELECT s FROM generate_series(1, 1000000) s;");
        assertTrue(hasRs);
        final ResultSet insertResult = statement.getResultSet();
        final long maxMemInsert = findMaxMemInExplain(insertResult);
        assertTrue(maxMemInsert != 0);
      }

      // Verify that the absolute and relative values of the max memory output are within
      // expectation.
      {
        final long maxMem_1 = runExplainAnalyze(statement, 1);
        final long maxMem_1K = runExplainAnalyze(statement, 1000);
        final long maxMem_1M = runExplainAnalyze(statement, 1000 * 1000);
        assertTrue(maxMem_1 < maxMem_1K && maxMem_1K < maxMem_1M);
      }

      // Verify that there is no memory leakage in the tracking system.
      // If the tracking logic is not accurate and has errors, it will accumulate and shows in the
      // output.
      {
        final int N_WARMUP_RUNS = 5;
        final int N_UNMEASURED_RUNS = 80;
        final int N_MEASURED_RUNS = 20;
        final int N_ALLOWABLE_FAILURES = 4;
        final int LIMIT = 1000;

        // The first few runs have a higher max memory - we want to use a the lowest value.
        long maxMemSimpleStart = Long.MAX_VALUE;
        for (int i = 0; i < N_WARMUP_RUNS; i++) {
          long maxMem = runExplainAnalyze(statement, LIMIT);
          if (maxMem < maxMemSimpleStart)
            maxMemSimpleStart = maxMem;
        }

        // Run the query to allow for any potential memory leak to accumulate.
        for (int i = 0; i < N_UNMEASURED_RUNS; i++) {
          runExplainAnalyze(statement, LIMIT);
        }

        // There is some slight variation to the counter, but we expect the max memory to be
        // consistent in most runs.
        int numberOfFailures = 0;
        for (int i = 0; i < N_MEASURED_RUNS; i++) {
          final long maxMemCurrent = runExplainAnalyze(statement, LIMIT);
          if (maxMemCurrent != maxMemSimpleStart) {
            numberOfFailures++;
            LOG.info(String.format(
              "[Run %d]: expected %d (current max mem) to be equal to %d (starting max mem)",
              i, maxMemCurrent, maxMemSimpleStart));
          }
        }

        assertLessThanOrEqualTo("Too many runs had an unexpected max memory.",
                                numberOfFailures, N_ALLOWABLE_FAILURES);
      }

      // Run an accurate max-memory validation by including a single memory consumption operator
      // like ORDER BY, which dominates the memory consumption during execution. The operator's
      // memory consumption should be roughly equal to (or smaller than) the max memory consumption.
      {
        // Set the work_mem to a high value so that the memory doesn't get capped during testing.
        statement.execute("SET work_mem=\"1000MB\";");
        final ResultSet queryRs =
          statement.executeQuery("EXPLAIN ANALYZE SELECT c1 FROM tst ORDER BY c1 LIMIT 1000000;");
        final long sortMemKb = findSortMemUsageInExplain(queryRs);
        final long maxMemKb = findMaxMemInExplain(queryRs);
        assertTrue(maxMemKb > 0 && sortMemKb > 0);
        // Validate that the max memory usage is within the expected range.
        assertTrue(maxMemKb < sortMemKb * 1.1 && maxMemKb >= sortMemKb);
      }

      // The following tests only verifies EXPLAIN ANALYZE can be properly executed
      // as the actual output values are highly platform dependent.
      statement.execute("EXPLAIN ANALYZE UPDATE tst SET c1 = c1 + 1 WHERE c1 < 1000;");
      statement.execute("EXPLAIN ANALYZE DELETE FROM tst WHERE c1 < 1000;");

      statement.execute("BEGIN;");
      statement.execute("EXPLAIN ANALYZE DECLARE decl CURSOR FOR SELECT * FROM tst limit 1000;");
      statement.execute("END;");

      statement.execute("EXPLAIN ANALYZE VALUES (1), (2), (3);");
      statement.execute(
        "EXPLAIN ANALYZE CREATE TABLE cre_tst AS SELECT * FROM tst limit 100;");
      statement.execute(
        "EXPLAIN ANALYZE CREATE MATERIALIZED VIEW cre_view AS SELECT * FROM tst limit 100;");
    }
  }

  /**
   * Test "Peak Memory" is not shown in EXPLAIN ANALYZE when yb_enable_memory_tracking
   * flag is off.
   * @throws Exception
   */
  @Test
  public void testExplainPeakMemWhenMemoryTrackingOff() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("SET yb_enable_memory_tracking = OFF");
      final String query = "EXPLAIN (ANALYZE) SELECT 1";
      ResultSet result = statement.executeQuery(query);
      while(result.next()) {
        final String row = result.getString(1);
        assertFalse(row.contains(PEAK_MEM_FIELD));
      }
    }
  }

  /**
   * Validate the EXPLAIN ANALYZE output, and return maximum memory consumption found.
   **/
  private long runExplainAnalyze(Statement statement, final int limit) throws Exception {
    final String explainQuery = buildExplainAnalyzeDemoQuery(limit);
    final ResultSet result = statement.executeQuery(explainQuery);
    return findMaxMemInExplain(result);
  }

  private final String buildExplainAnalyzeDemoQuery(final int limit) {
    return "explain analyze execute demo(" + limit + ");";
  }

  /**
   * Find the max memory in the EXPLAIN ANALYZE output in kilo-bytes. Throws exception if there is
   * no max mem found.
   */
  private long findMaxMemInExplain(final ResultSet result) throws Exception {
    while(result.next()) {
      final String row = result.getString(1);
      if (row.contains(PEAK_MEM_FIELD)) {
        final String[] tks = row.split(" ");
        long maxMem = Long.valueOf(tks[tks.length - 2]);
        return maxMem;
      }
    }

    throw new Exception("No max memory consumption found in the EXPLAIN output.");
  }

  /**
   * Find the sort memory in the EXPLAIN ANALYZE output in kilo-bytes. Throws exception if there is
   * no memory usage found.
   */
  private long findSortMemUsageInExplain(final ResultSet result) throws Exception {
    while(result.next()) {
      final String row = result.getString(1);
      if (row.contains("Sort Method") && row.contains("Memory")) {
        final String[] tks = row.split(" ");
        final String kbs = tks[tks.length - 1];
        final long memKb = Long.valueOf(kbs.replace("kB", ""));
        return memKb;
      }
    }

    throw new Exception("No sort memory consumption found in the EXPLAIN ANALYZE output.");
  }

  private void testStatement(Statement statement,
                             String query,
                             String metric_name) throws Exception {
    testStatement(statement, query, metric_name, query);
  }

  /**
   * Function executes query and compare time elapsed locally with query statistics.
   * <p>
   * Local time always will be greater due to client-server communication.
   * <p>
   * To reduce client-server communication delays query is executed multiple times as a batch.
   */
  private void testStatement(Statement statement,
                             String query,
                             String metricName,
                             String statName) throws Exception {
    final int count = 200;
    for (int i = 0; i < count; ++i) {
      statement.addBatch(query);
    }
    AggregatedValue metricBefore = getMetric(metricName);
    StopWatch sw = StopWatch.createStarted();
    statement.executeBatch();
    final long elapsedLocalTime = sw.getTime();
    LOG.info("Batch execution of {} queries took {} ms ({})",
        count, elapsedLocalTime, query);
    AggregatedValue metricAfter = getMetric(metricName);
    AggregatedValue stat = getStatementStat(statName);
    assertEquals(String.format("Calls count for query %s", query),
        count, stat.count);
    assertEquals(String.format("'%s' count for query %s", metricName, query),
        count, metricAfter.count - metricBefore.count);

    // Due to client-server communications delay local time
    // is always greater than actual query time.
    // But we expect communications delay is less than 20%.
    final double timeLowerBound = 0.8 * elapsedLocalTime;
    assertQueryTime(query, stat.value, timeLowerBound);
    final long metricValue = Math.round(metricAfter.value - metricBefore.value);
    // Make metric lower bound a little smaller than stat.value due to rounding
    final long metricLowerBound = Math.round(0.95 * stat.value * 1000);
    assertGreaterThanOrEqualTo(
        String.format("Expected '%s' %d to be >= %d for query '%s'",
                      metricName,
                      metricValue,
                      metricLowerBound,
                      query),
        metricValue,
        metricLowerBound);
  }

  private void assertQueryTime(String query, double queryTime, double timeLowerBound) {
    assertGreaterThanOrEqualTo(
        String.format("Expected total time %f to be >= %f for query '%s'",
                      queryTime,
                      timeLowerBound,
                      query),
        queryTime,
        timeLowerBound);
  }
}
