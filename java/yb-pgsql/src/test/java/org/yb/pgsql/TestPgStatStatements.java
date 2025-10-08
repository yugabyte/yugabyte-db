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

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.junit.Before;
import org.junit.Test;
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
  private static final int TABLE_ROWS = 5000;
  private static final String INDEX_NAME = String.format("i_%s_c3_c2", TABLE_NAME);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "yb_enable_pg_stat_statements_rpc_stats=true");
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

      String insertQuery = String.format("INSERT INTO %s_%%d VALUES " +
          "(1, 1, 1, 'abc'), (1000, 1000, 1000, 'abc'), (50, 50, 50, 'def')",
          TABLE_NAME);

      stmt.execute(String.format(insertQuery, 1));
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

  @Test
  public void testSelectRpcStats() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      String selectQuery = String.format("SELECT * FROM %s_%%d WHERE " +
          "c1 < 100 AND c4 = CONCAT('ab', 'c')", TABLE_NAME);

      stmt.execute(String.format(selectQuery, 1));

      ResultSet rs = stmt.executeQuery(String.format(
          "SELECT docdb_read_operations, docdb_read_rpcs, catalog_wait_time, " +
          "docdb_wait_time, docdb_rows_scanned FROM pg_stat_statements WHERE " +
          "query LIKE 'SELECT * FROM %s%%'", TABLE_NAME));

      while (rs.next()) {
        long docdbReadOperations = rs.getLong("docdb_read_operations");
        long docdbReadRpcs = rs.getLong("docdb_read_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");
        long docdbRowsScanned = rs.getLong("docdb_rows_scanned");

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
                  .build())
              .build());
      }
    }
  }

  @Test
  public void testInsertRpcStats() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      String insertQuery = String.format("INSERT INTO %s_%%d VALUES " +
          "(10, 10, 10, 'abc'), (10000, 10000, 10000, 'abc'), (500, 500, 500, 'def')",
          TABLE_NAME);

      stmt.execute(String.format(insertQuery, 1));

      ResultSet rs = stmt.executeQuery(String.format(
          "SELECT docdb_write_operations, docdb_write_rpcs, catalog_wait_time, " +
          "docdb_wait_time FROM pg_stat_statements WHERE query = 'INSERT INTO %s_1%%'",
          TABLE_NAME));

      while (rs.next()) {
        long docdbWriteOperations = rs.getLong("docdb_write_operations");
        long docdbWriteRpcs = rs.getLong("docdb_write_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");

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
          "docdb_write_rpcs, catalog_wait_time, docdb_wait_time, docdb_rows_scanned " +
          "FROM pg_stat_statements WHERE query LIKE 'UPDATE %s%%'",
          TABLE_NAME));

      while (rs.next()) {
        long docdbReadOperations = rs.getLong("docdb_read_operations");
        long docdbReadRpcs = rs.getLong("docdb_read_rpcs");
        long docdbWriteOperations = rs.getLong("docdb_write_operations");
        long docdbWriteRpcs = rs.getLong("docdb_write_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");
        long docdbRowsScanned = rs.getLong("docdb_rows_scanned");

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
                      .build())
                  .build())
              .build());
      }
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
          "docdb_write_rpcs, catalog_wait_time, docdb_wait_time, docdb_rows_scanned " +
          "FROM pg_stat_statements WHERE query LIKE 'UPDATE %s%%'",
          TABLE_NAME));

      while (rs.next()) {
        long docdbReadOperations = rs.getLong("docdb_read_operations");
        long docdbReadRpcs = rs.getLong("docdb_read_rpcs");
        long docdbWriteOperations = rs.getLong("docdb_write_operations");
        long docdbWriteRpcs = rs.getLong("docdb_write_rpcs");
        double catalogWaitTime = rs.getDouble("catalog_wait_time");
        double docdbWaitTime = rs.getDouble("docdb_wait_time");
        long docdbRowsScanned = rs.getLong("docdb_rows_scanned");

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
                      .build())
                  .build())
              .build());
      }
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
}
