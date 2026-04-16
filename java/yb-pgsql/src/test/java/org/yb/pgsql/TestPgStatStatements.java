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
package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_MODIFY_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_VALUES_SCAN;
import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;
import java.io.ByteArrayOutputStream;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.yugabyte.copy.CopyManager;
import com.yugabyte.core.BaseConnection;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestName;
import org.junit.runner.RunWith;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

import org.yb.YBTestRunner;

/**
 * Test RPC stats in pg_stat_statements.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgStatStatements extends BasePgExplainAnalyzeTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgStatStatements.class);
  private static final String TABLE_NAME = "test_table";
  private static final String METRIC_NUM_DB_SEEK = "Metric rocksdb_number_db_seek";
  private static final String METRIC_NUM_DB_NEXT = "Metric rocksdb_number_db_next";
  private static final String METRIC_NUM_DB_PREV = "Metric rocksdb_number_db_prev";

  @Rule
  public TestName testName = new TestName();

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "yb_enable_pg_stat_statements_docdb_metrics=true");
    return flagMap;
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format(
          "CREATE TABLE %s_1 (c1 bigint, c2 bigint, c3 bigint, c4 text, " +
          "PRIMARY KEY(c1 ASC, c2 ASC, c3 ASC))",
          TABLE_NAME));

      stmt.execute(String.format(
          "CREATE TABLE %s_2 (c1 bigint, c2 bigint, c3 bigint, c4 text, " +
          "PRIMARY KEY(c1 ASC, c2 ASC, c3 ASC))",
          TABLE_NAME));

      stmt.execute(String.format("CREATE TABLE %s_3 (k SERIAL PRIMARY KEY, v INT)",
          TABLE_NAME));

      if (!testName.getMethodName().equals("testInsertRpcStats")) {
        String insertQuery = String.format("INSERT INTO %s_%%d VALUES " +
            "(1, 1, 1, 'abc'), (1000, 1000, 1000, 'abc'), (50, 50, 50, 'def')",
            TABLE_NAME);

        stmt.execute(String.format(insertQuery, 1));
        stmt.execute(String.format(insertQuery, 2));
      }
    }
  }

  private void resetTable2(Statement stmt) throws Exception {
    stmt.execute("TRUNCATE TABLE " + TABLE_NAME + "_2");

    if (!testName.getMethodName().equals("testInsertRpcStats")) {
        String insertQuery = String.format("INSERT INTO %s_%%d VALUES " +
                "(1, 1, 1, 'abc'), (1000, 1000, 1000, 'abc'), (50, 50, 50, 'def')",
                TABLE_NAME);

        stmt.execute(String.format(insertQuery, 2));
    }
  }

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  public void testExplain(String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ExplainAnalyzeUtils.testExplain(stmt, query, checker);
    }
  }

  /*
   * Helper class to hold DocDB metrics.
   */
  private static class DocDBMetrics {
    final long seeks;
    final long nexts;
    final long prevs;

    DocDBMetrics(long seeks, long nexts, long prevs) {
      this.seeks = seeks;
      this.nexts = nexts;
      this.prevs = prevs;
    }
  }

  /*
   * Helper method to extract a specific metric value from the metrics JSON array object.
   * Returns 0 if the metric is not found.
   */
  private static long getMetricValue(JsonObject metricsObj, String metricName) {
      if (metricsObj == null || !metricsObj.has(metricName)) {
          return 0L;
      }
      return metricsObj.get(metricName).getAsLong();
  }

  /*
   * Helper method to extract DocDB metrics from a metrics JSON object.
   */
  private static DocDBMetrics extractDocdbMetrics(JsonObject metricsObj) {
    if (metricsObj == null) {
      return new DocDBMetrics(0L, 0L, 0L);
    }
    long seeks = getMetricValue(metricsObj, METRIC_NUM_DB_SEEK);
    long nexts = getMetricValue(metricsObj, METRIC_NUM_DB_NEXT);
    long prevs = getMetricValue(metricsObj, METRIC_NUM_DB_PREV);
    return new DocDBMetrics(seeks, nexts, prevs);
  }

  /*
   * Helper method to verify that docdbSeeks equals the sum of read and write metrics seeks.
   * Executes EXPLAIN ANALYZE and extracts the metrics to perform the verification.
   */
  private void verifyMetrics(String query, long expectedDocdbSeeks,
          long expectedDocdbNexts, long expectedDocdbPrevs) throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      String explainQuery = String.format(
              "EXPLAIN (FORMAT json, ANALYZE true, DIST true, DEBUG true) %s", query);

      ResultSet rs = stmt.executeQuery(explainQuery);
      rs.next();
      JsonElement json = JsonParser.parseString(rs.getString(1));
      JsonObject root = json.getAsJsonArray().get(0).getAsJsonObject();

      // Extract read and write metrics
      JsonObject readMetricsObj = root.has("Read Metrics") ?
                                  root.getAsJsonObject("Read Metrics") : null;
      JsonObject writeMetricsObj = root.has("Write Metrics") ?
                                   root.getAsJsonObject("Write Metrics") : null;

      LOG.info("Read Metrics: {}", JsonUtil.asPrettyString(readMetricsObj));
      LOG.info("Write Metrics: {}", JsonUtil.asPrettyString(writeMetricsObj));

      LOG.info("Expected Docdb Seeks: {}, Expected Docdb Nexts: {}, Expected Docdb Prevs: {}",
          expectedDocdbSeeks, expectedDocdbNexts, expectedDocdbPrevs);

      DocDBMetrics readMetrics = extractDocdbMetrics(readMetricsObj);
      DocDBMetrics writeMetrics = extractDocdbMetrics(writeMetricsObj);

      assertEquals(expectedDocdbSeeks, readMetrics.seeks + writeMetrics.seeks);
      assertEquals(expectedDocdbNexts, readMetrics.nexts + writeMetrics.nexts);
      assertEquals(expectedDocdbPrevs, readMetrics.prevs + writeMetrics.prevs);

      //Reset test_table_2's state to utilize testExplain
      resetTable2(stmt);
    }
  }

  private void testDocdbRowsReturned(
      Statement stmt, String query, String normalizedQuery, long expectedDocdbRowsReturned)
      throws Exception {
    // Handle COPY commands with CopyManager
    if (query.trim().toUpperCase().startsWith("COPY")) {
      CopyManager copyManager = new CopyManager((BaseConnection) stmt.getConnection());
      ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
      copyManager.copyOut(query, outputStream);
    } else {
      stmt.execute(query);
    }

    ResultSet rs = stmt.executeQuery(String.format(
        "SELECT docdb_rows_returned FROM pg_stat_statements WHERE " +
        "query = '%s'", normalizedQuery));

    while (rs.next()) {
      long docdbRowsReturned = rs.getLong("docdb_rows_returned");
      assertEquals(expectedDocdbRowsReturned, docdbRowsReturned);
    }
  }

  @Test
  public void testSelectRpcStats() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      String selectQuery = String.format("SELECT * FROM %s_%%d WHERE " +
          "c1 < 100 AND c4 = CONCAT('ab', 'c')", TABLE_NAME);

      stmt.execute(String.format(selectQuery, 1));

      ResultSet rs = stmt.executeQuery(String.format(
          "SELECT docdb_read_operations, docdb_read_rpcs, catalog_wait_time, " +
          "docdb_wait_time, docdb_rows_scanned, rows, docdb_rows_returned, " +
          "docdb_nexts, docdb_prevs, docdb_seeks " +
          "FROM pg_stat_statements WHERE query LIKE 'SELECT * FROM %s%%'", TABLE_NAME));

      int rowCount = 0;
      while (rs.next()) {
        rowCount++;
        long docdbReadOperations = rs.getLong("docdb_read_operations");
        long docdbReadRpcs = rs.getLong("docdb_read_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");
        long docdbRowsScanned = rs.getLong("docdb_rows_scanned");
        long rows = rs.getLong("rows");
        long docdbRowsReturned = rs.getLong("docdb_rows_returned");
        long docdbNexts = rs.getLong("docdb_nexts");
        long docdbPrevs = rs.getLong("docdb_prevs");
        long docdbSeeks = rs.getLong("docdb_seeks");

        verifyMetrics(String.format(selectQuery, 2), docdbSeeks, docdbNexts, docdbPrevs);

        testExplain(
          String.format(selectQuery, 2),
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.equal(docdbReadRpcs))
              .storageReadExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .storageReadOps(Checkers.equal(docdbReadOperations))
              .storageRowsScanned(Checkers.equal(docdbRowsScanned))
              .storageFlushRequests(Checkers.equal(0))
              .storageWriteRequests(Checkers.equal(0))
              .storageExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogReadRequests(
                  catalogWaitTime > 0.0 ? Checkers.greater(0) : Checkers.equal(0))
              .catalogReadExecutionTime(
                  catalogWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogWriteRequests(Checkers.equal(0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_INDEX_SCAN)
                  .relationName(String.format("%s_2", TABLE_NAME))
                  .storageTableReadRequests(Checkers.equal(docdbReadRpcs))
                  .storageTableReadExecutionTime(
                      docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
                  .storageTableReadOps(Checkers.equal(docdbReadOperations))
                  .rowsRemovedByFilter(Checkers.equal(docdbRowsReturned - rows))
                  .build())
              .build());
      }

      assertEquals("Expected exactly one row in pg_stat_statements", 1, rowCount);
    }
  }

  @Test
  public void testInsertRpcStats() throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      String insertQuery = String.format("INSERT INTO %s_%%d VALUES " +
          "(10, 10, 10, 'abc'), (10000, 10000, 10000, 'abc'), (500, 500, 500, 'def')",
          TABLE_NAME);

      stmt.execute(String.format(insertQuery, 1));

      ResultSet rs = stmt.executeQuery(String.format(
          "SELECT docdb_write_operations, docdb_write_rpcs, catalog_wait_time, " +
          "docdb_wait_time, docdb_nexts, docdb_prevs, docdb_seeks " +
          "FROM pg_stat_statements WHERE query LIKE 'INSERT INTO %s_1%%'",
          TABLE_NAME));

      int rowCount = 0;
      while (rs.next()) {
        ++rowCount;
        long docdbWriteOperations = rs.getLong("docdb_write_operations");
        long docdbWriteRpcs = rs.getLong("docdb_write_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");
        long docdbNexts = rs.getLong("docdb_nexts");
        long docdbPrevs = rs.getLong("docdb_prevs");
        long docdbSeeks = rs.getLong("docdb_seeks");

        verifyMetrics(String.format(insertQuery, 2), docdbSeeks, docdbNexts, docdbPrevs);

        testExplain(
          String.format(insertQuery, 2),
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.equal(0))
              .storageReadOps(Checkers.equal(0))
              .storageRowsScanned(Checkers.equal(0))
              .storageWriteRequests(Checkers.equal(docdbWriteOperations))
              .storageFlushRequests(Checkers.equal(docdbWriteRpcs))
              .storageFlushExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .storageExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogReadRequests(
                  catalogWaitTime > 0.0 ? Checkers.greater(0) : Checkers.equal(0))
              .catalogReadExecutionTime(
                  catalogWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogWriteRequests(Checkers.equal(0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_MODIFY_TABLE)
                  .relationName(String.format("%s_2", TABLE_NAME))
                  .plans(makePlanBuilder()
                      .nodeType(NODE_VALUES_SCAN)
                      .storageTableWriteRequests(Checkers.equal(docdbWriteOperations))
                      .build())
                  .build())
              .build());
      }

      assertEquals("Expected exactly one row in pg_stat_statements", 1, rowCount);
    }
  }

  @Test
  public void testUpdateRpcStats() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      String updateQuery = String.format("UPDATE %s_%%d SET " +
          "c1 = c1 * 10 WHERE c1 < 100 AND c4 = CONCAT('ab', 'c')", TABLE_NAME);

      stmt.execute(String.format(updateQuery, 1));

      ResultSet rs = stmt.executeQuery(String.format(
          "SELECT docdb_read_operations, docdb_read_rpcs, docdb_write_operations, " +
          "docdb_write_rpcs, catalog_wait_time, docdb_wait_time, docdb_rows_scanned, " +
          "docdb_rows_returned, docdb_nexts, docdb_prevs, docdb_seeks " +
          "FROM pg_stat_statements WHERE query LIKE 'UPDATE %s%%'",
          TABLE_NAME));

      int rowCount = 0;
      while (rs.next()) {
        ++rowCount;
        long docdbReadOperations = rs.getLong("docdb_read_operations");
        long docdbReadRpcs = rs.getLong("docdb_read_rpcs");
        long docdbWriteOperations = rs.getLong("docdb_write_operations");
        long docdbWriteRpcs = rs.getLong("docdb_write_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");
        long docdbRowsScanned = rs.getLong("docdb_rows_scanned");
        long docdbRowsReturned = rs.getLong("docdb_rows_returned");
        long docdbNexts = rs.getLong("docdb_nexts");
        long docdbPrevs = rs.getLong("docdb_prevs");
        long docdbSeeks = rs.getLong("docdb_seeks");

        verifyMetrics(String.format(updateQuery, 2), docdbSeeks, docdbNexts, docdbPrevs);

        testExplain(
          String.format(updateQuery, 2),
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.equal(docdbReadRpcs))
              .storageReadExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .storageReadOps(Checkers.equal(docdbReadOperations))
              .storageRowsScanned(Checkers.equal(docdbRowsScanned))
              .storageWriteRequests(Checkers.equal(docdbWriteOperations))
              .storageFlushRequests(Checkers.equal(docdbWriteRpcs))
              .storageFlushExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .storageExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogReadRequests(
                  catalogWaitTime > 0.0 ? Checkers.greater(0) : Checkers.equal(0))
              .catalogReadExecutionTime(
                  catalogWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogWriteRequests(Checkers.equal(0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_MODIFY_TABLE)
                  .relationName(String.format("%s_2", TABLE_NAME))
                  .plans(makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .storageTableReadRequests(Checkers.equal(docdbReadRpcs))
                      .storageTableReadExecutionTime(
                          docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
                      .storageTableReadOps(Checkers.equal(docdbReadOperations))
                      .storageTableWriteRequests(Checkers.equal(docdbWriteOperations))
                      .rowsRemovedByFilter(Checkers.equal(docdbRowsReturned - 1))
                      .build())
                  .build())
              .build());
      }

      assertEquals("Expected exactly one row in pg_stat_statements", 1, rowCount);
    }
  }

  @Test
  public void testDeleteRpcStats() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      String deleteQuery = String.format("DELETE FROM %s_%%d WHERE " +
          "c1 > 100 AND c4 = CONCAT('ab', 'c')", TABLE_NAME);

      stmt.execute(String.format(deleteQuery, 1));

      ResultSet rs = stmt.executeQuery(String.format(
          "SELECT docdb_read_operations, docdb_read_rpcs, docdb_write_operations, " +
          "docdb_write_rpcs, catalog_wait_time, docdb_wait_time, docdb_rows_scanned, " +
          "docdb_rows_returned, docdb_nexts, docdb_prevs, docdb_seeks " +
          "FROM pg_stat_statements WHERE query LIKE 'DELETE FROM %s%%'",
          TABLE_NAME));

      int rowCount = 0;
      while (rs.next()) {
        ++rowCount;
        long docdbReadOperations = rs.getLong("docdb_read_operations");
        long docdbReadRpcs = rs.getLong("docdb_read_rpcs");
        long docdbWriteOperations = rs.getLong("docdb_write_operations");
        long docdbWriteRpcs = rs.getLong("docdb_write_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");
        long docdbRowsScanned = rs.getLong("docdb_rows_scanned");
        long docdbRowsReturned = rs.getLong("docdb_rows_returned");
        long docdbNexts = rs.getLong("docdb_nexts");
        long docdbPrevs = rs.getLong("docdb_prevs");
        long docdbSeeks = rs.getLong("docdb_seeks");

        verifyMetrics(String.format(deleteQuery, 2), docdbSeeks, docdbNexts, docdbPrevs);

        testExplain(
          String.format(deleteQuery, 2),
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.equal(docdbReadRpcs))
              .storageReadExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .storageReadOps(Checkers.equal(docdbReadOperations))
              .storageRowsScanned(Checkers.equal(docdbRowsScanned))
              .storageWriteRequests(Checkers.equal(docdbWriteOperations))
              .storageFlushRequests(Checkers.equal(docdbWriteRpcs))
              .storageFlushExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .storageExecutionTime(
                  docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogReadRequests(
                  catalogWaitTime > 0.0 ? Checkers.greater(0) : Checkers.equal(0))
              .catalogReadExecutionTime(
                  catalogWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
              .catalogWriteRequests(Checkers.equal(0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_MODIFY_TABLE)
                  .relationName(String.format("%s_2", TABLE_NAME))
                  .plans(makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .storageTableReadRequests(Checkers.equal(docdbReadRpcs))
                      .storageTableReadExecutionTime(
                          docdbWaitTime > 0.0 ? Checkers.greater(0.0) : Checkers.equal(0.0))
                      .storageTableReadOps(Checkers.equal(docdbReadOperations))
                      .storageTableWriteRequests(Checkers.equal(docdbWriteOperations))
                      .rowsRemovedByFilter(Checkers.equal(docdbRowsReturned - 1))
                      .build())
                  .build())
              .build());
      }

      assertEquals("Expected exactly one row in pg_stat_statements", 1, rowCount);
    }
  }

  @Test
  public void testDdlRpcStats() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Test CREATE TABLE DDL
      stmt.execute("CREATE TABLE ddl_test_table " +
          "(id SERIAL PRIMARY KEY, name TEXT, age INT)");

      // Test CREATE INDEX DDL
      stmt.execute("CREATE INDEX ddl_test_idx ON ddl_test_table (name)");

      // Test ALTER TABLE DDL
      stmt.execute("ALTER TABLE ddl_test_table ADD COLUMN email TEXT");

      // Test DROP INDEX DDL
      stmt.execute("DROP INDEX ddl_test_idx");

      // Test DROP TABLE DDL
      stmt.execute("DROP TABLE ddl_test_table");

      // Query pg_stat_statements for DDL operations
      ResultSet rs = stmt.executeQuery(
          "SELECT query, catalog_wait_time " +
          "FROM pg_stat_statements WHERE " +
          "query LIKE '%TABLE ddl_test_table%' OR " +
          "query LIKE '%INDEX ddl_test_idx%' OR " +
          "query LIKE '%ddl_test_table%' " +
          "ORDER BY query");

      while (rs.next()) {
        String query = rs.getString("query");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");

        assertTrue(String.format("Expected catalog_wait_time > 0 for DDL query: %s, but got: %f",
            query, catalogWaitTime), catalogWaitTime > 0.0);
      }
    }
  }

  @Test
  public void testDocdbRowsReturnedWithSelects() throws Exception {
    String selectNoRows = String.format("SELECT * FROM %s_3 WHERE k < 1", TABLE_NAME);
    String selectNoRowsNormalized = String.format("SELECT * FROM %s_3 WHERE k < $1", TABLE_NAME);

    String selectSomeRows = String.format("SELECT * FROM %s_3 WHERE k > 50", TABLE_NAME);
    String selectSomeNormalized = String.format("SELECT * FROM %s_3 WHERE k > $1", TABLE_NAME);

    String selectAllRows = String.format("SELECT * FROM %s_3", TABLE_NAME);
    String selectAllNormalized = String.format("SELECT * FROM %s_3", TABLE_NAME);

    String tempTable = "tmp_tbl";
    String selectTempTable = String.format("SELECT * FROM %s", tempTable);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("INSERT INTO %s_3 VALUES (generate_series(1, 100))", TABLE_NAME));
      stmt.execute(String.format("CREATE TEMP TABLE %s (k INT PRIMARY KEY, v INT)", tempTable));
      stmt.execute(String.format("INSERT INTO %s VALUES (generate_series(1, 100))", tempTable));

      testDocdbRowsReturned(
          stmt, selectNoRows, selectNoRowsNormalized, /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, selectSomeRows, selectSomeNormalized, /* expectedDocdbRowsReturned */ 50);
      testDocdbRowsReturned(
          stmt, selectAllRows, selectAllNormalized, /* expectedDocdbRowsReturned */ 100);

      testDocdbRowsReturned(
          stmt, selectTempTable, selectTempTable, /* expectedDocdbRowsReturned */ 0);
    }
  }

  @Test
  public void testRowColumnsWithJoins() throws Exception {
    String joinQuery = String.format(
        "SELECT t1.c1, t1.c2, t1.c3, t2.c1, t2.c2, t2.c3 " +
        "FROM %s_1 t1 INNER JOIN %s_2 t2 ON t1.c1 = t2.c1", TABLE_NAME, TABLE_NAME);

    try (Statement stmt = connection.createStatement()) {
      // 3 rows returned by each table
      testDocdbRowsReturned(stmt, joinQuery, joinQuery, /* expectedDocdbRowsReturned */ 3 + 3);

      ResultSet rs = stmt.executeQuery(String.format(
          "SELECT rows FROM pg_stat_statements WHERE " +
          "query = '%s'", joinQuery));

      while (rs.next()) {
        long rows = rs.getLong("rows");
        assertEquals(3, rows);
      }
    }
  }

  @Test
  public void testDocdbRowsReturnedWithAggregates() throws Exception {
    long numTablets = 3;
    String sumQuery = String.format("SELECT sum(k) FROM %s_4", TABLE_NAME);
    String maxQuery = String.format("SELECT max(k) FROM %s_4", TABLE_NAME);
    String minQuery = String.format("SELECT min(k) FROM %s_4", TABLE_NAME);
    String countQuery = String.format("SELECT count(*) FROM %s_4", TABLE_NAME);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s_4 (k INT) SPLIT INTO %d TABLETS",
          TABLE_NAME, numTablets));

      stmt.execute(String.format("INSERT INTO %s_4 VALUES (generate_series(1, 1000))",
          TABLE_NAME));

      long expectedDocdbRowsReturned = numTablets;

      testDocdbRowsReturned(stmt, sumQuery, sumQuery, expectedDocdbRowsReturned);
      testDocdbRowsReturned(stmt, maxQuery, maxQuery, expectedDocdbRowsReturned);
      testDocdbRowsReturned(stmt, minQuery, minQuery, expectedDocdbRowsReturned);
      testDocdbRowsReturned(stmt, countQuery, countQuery, expectedDocdbRowsReturned);
    }
  }

  @Test
  public void testDocdbRowsReturnedWithInserts() throws Exception {
    // single value inserts
    String insertQuery1 = String.format("INSERT INTO %s_1 VALUES (10, 10, 10, 'abc')", TABLE_NAME);
    String normalizedQuery1 = String.format("INSERT INTO %s_1 VALUES ($1, $2, $3, $4)",
        TABLE_NAME);

    // multi value inserts
    String insertQuery2 = String.format("INSERT INTO %s_1 VALUES " +
        "(100, 100, 100, 'abc'), (10000, 10000, 10000, 'abc'), (500, 500, 500, 'def')",
        TABLE_NAME);
    String normalizedQuery2 = String.format("INSERT INTO %s_1 VALUES " +
        "($1, $2, $3, $4), ($5, $6, $7, $8), ($9, $10, $11, $12)", TABLE_NAME);

    // generate column inserts
    // The SERIAL values is fetched using PgClient::FetchSequenceTuple API which is not counted
    // in docdb_rows_returned.
    String generatedCols = String.format("INSERT INTO %s_3 (v) VALUES (1)", TABLE_NAME);
    String generatedColsNormalized = String.format("INSERT INTO %s_3 (v) VALUES ($1)", TABLE_NAME);

    // on conflict
    String OnConflictDoUpdate = String.format(
        "INSERT INTO %s_3 VALUES (2, 2) ON CONFLICT (k) DO UPDATE SET k = 2", TABLE_NAME);
    String OnConflictDoUpdateNormalized = String.format(
        "INSERT INTO %s_3 VALUES ($1, $2) ON CONFLICT (k) DO UPDATE SET k = $3", TABLE_NAME);
    String OnConflictDoNothing = String.format(
        "INSERT INTO %s_3 VALUES (3, 3) ON CONFLICT (k) DO NOTHING", TABLE_NAME);
    String OnConflictDoNothingNormalized = String.format(
        "INSERT INTO %s_3 VALUES ($1, $2) ON CONFLICT (k) DO NOTHING", TABLE_NAME);

    // returning clause
    String returningInsert = String.format(
        "INSERT INTO %s_3 VALUES (4, 4) RETURNING *", TABLE_NAME);
    String returningInsertNormalized = String.format(
        "INSERT INTO %s_3 VALUES ($1, $2) RETURNING *", TABLE_NAME);


    try (Statement stmt = connection.createStatement()) {
      testDocdbRowsReturned(
          stmt, insertQuery1, normalizedQuery1, /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, insertQuery2, normalizedQuery2, /* expectedDocdbRowsReturned */ 0);

      testDocdbRowsReturned(
          stmt, generatedCols, generatedColsNormalized, /* expectedDocdbRowsReturned */ 0);

      testDocdbRowsReturned(
          stmt, OnConflictDoUpdate, OnConflictDoUpdateNormalized,
          /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, OnConflictDoUpdate, OnConflictDoUpdateNormalized,
          /* expectedDocdbRowsReturned */ 1);
      testDocdbRowsReturned(
          stmt, OnConflictDoNothing, OnConflictDoNothingNormalized,
          /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, OnConflictDoNothing, OnConflictDoNothingNormalized,
          /* expectedDocdbRowsReturned */ 1);

      testDocdbRowsReturned(
          stmt, returningInsert, returningInsertNormalized, /* expectedDocdbRowsReturned */ 0);
    }
  }

  @Test
  public void testDocdbRowsReturnedWithUpdates() throws Exception {
    String noRowsUpdate = String.format("UPDATE %s_1 SET c4 = 'xyz' WHERE c1 < 1", TABLE_NAME);
    String noRowsUpdateNormalized =
        String.format("UPDATE %s_1 SET c4 = $1 WHERE c1 < $2", TABLE_NAME);

    String noRowsUpdateReturning =
        String.format("UPDATE %s_1 SET c4 = 'xyz' WHERE c1 < 1 RETURNING *", TABLE_NAME);
    String noRowsUpdateReturningNormalized =
        String.format("UPDATE %s_1 SET c4 = $1 WHERE c1 < $2 RETURNING *", TABLE_NAME);

    String someRowsUpdate = String.format("UPDATE %s_1 SET c4 = 'xyz' WHERE c1 = 50", TABLE_NAME);
    String someRowsUpdateNormalized =
        String.format("UPDATE %s_1 SET c4 = $1 WHERE c1 = $2", TABLE_NAME);

    String someRowsUpdateReturning =
        String.format("UPDATE %s_1 SET c4 = 'xyz' WHERE c1 = 50 RETURNING *", TABLE_NAME);
    String someRowsUpdateReturningNormalized =
        String.format("UPDATE %s_1 SET c4 = $1 WHERE c1 = $2 RETURNING *", TABLE_NAME);

    String allRowsUpdate = String.format("UPDATE %s_1 SET c4 = 'xyz'", TABLE_NAME);
    String allRowsUpdateNormalized = String.format("UPDATE %s_1 SET c4 = $1", TABLE_NAME);

    String allRowsUpdateReturning =
        String.format("UPDATE %s_1 SET c4 = 'xyz' RETURNING *", TABLE_NAME);
    String allRowsUpdateReturningNormalized =
        String.format("UPDATE %s_1 SET c4 = $1 RETURNING *", TABLE_NAME);

    try (Statement stmt = connection.createStatement()) {
      testDocdbRowsReturned(
          stmt, noRowsUpdate, noRowsUpdateNormalized, /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, someRowsUpdate, someRowsUpdateNormalized, /* expectedDocdbRowsReturned */ 1);
      testDocdbRowsReturned(
          stmt, allRowsUpdate, allRowsUpdateNormalized, /* expectedDocdbRowsReturned */ 3);

      testDocdbRowsReturned(
          stmt, noRowsUpdateReturning, noRowsUpdateReturningNormalized,
          /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, someRowsUpdateReturning, someRowsUpdateReturningNormalized,
          /* expectedDocdbRowsReturned */ 1);
      testDocdbRowsReturned(
          stmt, allRowsUpdateReturning, allRowsUpdateReturningNormalized,
          /* expectedDocdbRowsReturned */ 3);
    }
  }

  @Test
  public void testDocdbRowsReturnedWithDeletes() throws Exception {
    String noRowsDelete = String.format("DELETE FROM %s_1 WHERE c1 < 1", TABLE_NAME);
    String noRowsDeleteNormalized =
        String.format("DELETE FROM %s_1 WHERE c1 < $1", TABLE_NAME);

    String noRowsDeleteReturning =
        String.format("DELETE FROM %s_1 WHERE c1 < 1 RETURNING *", TABLE_NAME);
    String noRowsDeleteReturningNormalized =
        String.format("DELETE FROM %s_1 WHERE c1 < $1 RETURNING *", TABLE_NAME);

    String someRowsDelete = String.format("DELETE FROM %s_1 WHERE c1 = 50", TABLE_NAME);
    String someRowsDeleteNormalized =
        String.format("DELETE FROM %s_1 WHERE c1 = $1", TABLE_NAME);

    String someRowsDeleteReturning =
        String.format("DELETE FROM %s_1 WHERE c1 = 50 RETURNING *", TABLE_NAME);
    String someRowsDeleteReturningNormalized =
        String.format("DELETE FROM %s_1 WHERE c1 = $1 RETURNING *", TABLE_NAME);

    String allRowsDelete = String.format("DELETE FROM %s_1", TABLE_NAME);
    String allRowsDeleteNormalized = String.format("DELETE FROM %s_1", TABLE_NAME);

    String allRowsDeleteReturning =
        String.format("DELETE FROM %s_1 RETURNING *", TABLE_NAME);
    String allRowsDeleteReturningNormalized =
        String.format("DELETE FROM %s_1 RETURNING *", TABLE_NAME);

    try (Statement stmt = connection.createStatement()) {
      testDocdbRowsReturned(
          stmt, noRowsDelete, noRowsDeleteNormalized, /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, someRowsDelete, someRowsDeleteNormalized, /* expectedDocdbRowsReturned */ 1);
      testDocdbRowsReturned(
          stmt, allRowsDelete, allRowsDeleteNormalized, /* expectedDocdbRowsReturned */ 2);

      // again insert to test for RETURNING clause
      stmt.execute(String.format("INSERT INTO %s_1 VALUES " +
          "(1, 1, 1, 'abc'), (1000, 1000, 1000, 'abc'), (50, 50, 50, 'def')",
          TABLE_NAME));

      testDocdbRowsReturned(
          stmt, noRowsDeleteReturning, noRowsDeleteReturningNormalized,
          /* expectedDocdbRowsReturned */ 0);
      testDocdbRowsReturned(
          stmt, someRowsDeleteReturning, someRowsDeleteReturningNormalized,
          /* expectedDocdbRowsReturned */ 1);
      testDocdbRowsReturned(
          stmt, allRowsDeleteReturning, allRowsDeleteReturningNormalized,
          /* expectedDocdbRowsReturned */ 2);
    }
  }

  @Test
  public void testDocdbRowsReturnedWithDdls() throws Exception {
    String insert = String.format("INSERT INTO %s_3 " +
        "SELECT i, i FROM generate_series(%%d, %%d) AS i", TABLE_NAME);

    String copyToStdout = String.format("COPY %s_3 TO STDOUT", TABLE_NAME);

    String createMatView = String.format("CREATE MATERIALIZED VIEW %s_3_mv AS SELECT * FROM %s_3",
        TABLE_NAME, TABLE_NAME);

    String selectInto = String.format("SELECT * INTO %s_3_select_into FROM %s_3",
        TABLE_NAME, TABLE_NAME);

    String refreshMatView = String.format("REFRESH MATERIALIZED VIEW %s_3_mv", TABLE_NAME);

    String createTableAs = String.format("CREATE TABLE %s_3_ctas AS SELECT * FROM %s_3",
        TABLE_NAME, TABLE_NAME);

    String fetch = "FETCH 50 FROM c1";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format(insert, 1, 100));

      testDocdbRowsReturned(
          stmt, copyToStdout, copyToStdout, /* expectedDocdbRowsReturned */ 100);

      testDocdbRowsReturned(
          stmt, createMatView, createMatView, /* expectedDocdbRowsReturned */ 100);

      testDocdbRowsReturned(
          stmt, selectInto, selectInto, /* expectedDocdbRowsReturned */ 100);

      stmt.execute(String.format(insert, 101, 200));

      testDocdbRowsReturned(
          stmt, refreshMatView, refreshMatView, /* expectedDocdbRowsReturned */ 200);

      testDocdbRowsReturned(
          stmt, createTableAs, createTableAs, /* expectedDocdbRowsReturned */ 200);

      stmt.execute("BEGIN");
      stmt.execute(String.format("DECLARE c1 CURSOR FOR SELECT * FROM %s_3", TABLE_NAME));

      testDocdbRowsReturned(
          stmt, fetch, fetch, /* expectedDocdbRowsReturned */ 200);

      stmt.execute("COMMIT");

      stmt.execute(String.format("CREATE INDEX %s_3_idx ON %s_3 (v)", TABLE_NAME, TABLE_NAME));

      ResultSet rs = stmt.executeQuery(
          "SELECT SUM(docdb_rows_returned) AS docdb_rows_returned FROM pg_stat_statements WHERE " +
          "query LIKE 'BACKFILL%'");

      while (rs.next()) {
        long docdbRowsReturned = rs.getLong("docdb_rows_returned");
        assertEquals(200, docdbRowsReturned);
      }
    }
  }
}
