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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;

import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

// In this test module we adjust the number of rows to be prefetched by PgGate and make sure that
// the result for the query are correct.
@RunWith(value=YBTestRunner.class)
public class TestPgPrefetchControl extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgPrefetchControl.class);

  @Override
  protected Integer getYsqlPrefetchLimit() {
    // Set the prefetch limit to 100 for this test.
    return 100;
  }

  protected void createPrefetchTable(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(h int, r text, vi int, vs text, " +
                                 "PRIMARY KEY (h, r))", tableName);
      LOG.info("Execute: " + sql);
      statement.execute(sql);
      LOG.info("Created: " + tableName);
    }
  }

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  private static String makeFillerString(int n) {
    char[] charArray = new char[n];
    Arrays.fill(charArray, 'a');
    return new String(charArray);
  }

  @Test
  public void testSimplePrefetch() throws Exception {
    String tableName = "TestPrefetch";
    createPrefetchTable(tableName);

    int tableRowCount = 5000;
    try (Statement statement = connection.createStatement()) {
      List<Row> insertedRows = new ArrayList<>();

      for (int i = 0; i < tableRowCount; i++) {
        int h = i;
        String r = String.format("range_%d", h);
        int vi = i + 10000;
        String vs = String.format("value_%d", vi);
        String stmt = String.format("INSERT INTO %s VALUES (%d, '%s', %d, '%s')",
                                    tableName, h, r, vi, vs);
        statement.execute(stmt);
        insertedRows.add(new Row(h, r, vi, vs));
      }

      // Check rows.
      String stmt = String.format("SELECT * FROM %s ORDER BY h", tableName);
      try (ResultSet rs = statement.executeQuery(stmt)) {
        assertEquals(insertedRows, getRowList(rs));
      }

      // Check that ysql_prefetch_limit is respected.
      ExplainAnalyzeUtils.testExplain(
        statement,
        stmt,
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greaterOrEqual(50))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder().build())
          .build());
    }
  }

  public void checkFetchLimitReadRequests(Statement statement, String stmt,
    int rowLimit, int sizeLimit, int expectedReadRequests) throws Exception {

    statement.execute(String.format("SET yb_fetch_row_limit=%d", rowLimit));
    statement.execute(String.format("SET yb_fetch_size_limit=%d", sizeLimit));
    ExplainAnalyzeUtils.testExplain(
      statement,
      stmt,
      makeTopLevelBuilder()
        .storageReadRequests(Checkers.equal(expectedReadRequests))
        .storageWriteRequests(Checkers.equal(0))
        .storageExecutionTime(Checkers.greaterOrEqual(0.0))
        .plan(makePlanBuilder().build())
        .build());
  }

  @Test
  public void testSizeBasedFetch() throws Exception {
    try (Statement statement = connection.createStatement()) {
      String tableName = "TestPrefetch";
      int charLength = 109;
      String fillerData = makeFillerString(charLength);

      String stmt = String.format("create table %s (k bigint, v char(%d), primary key (k asc))",
        tableName, charLength);
      statement.execute(stmt);
      stmt = String.format("insert into %s (select s, '%s' from generate_series(1, 100000) as s)",
        tableName, fillerData);
      statement.execute(stmt);

      stmt = String.format("SELECT * FROM %s", tableName);

      checkFetchLimitReadRequests(statement, stmt, 1024,  0,    98);
      checkFetchLimitReadRequests(statement, stmt, 0,     512,  25);
      checkFetchLimitReadRequests(statement, stmt, 0,     1024, 13);
      checkFetchLimitReadRequests(statement, stmt, 0,     0,    1);
    }
  }

  @Test
  public void testLimitPerformance() throws Exception {
    String tableName = "TestLimit";
    createPrefetchTable(tableName);

    // Insert multiple rows of the same value for column "vi".
    int tableRowCount = 5000;
    final int viConst = 777;
    try (Statement statement = connection.createStatement()) {
      String stmt = String.format("CREATE INDEX %s_ybindex ON %s (vs)", tableName, tableName);
      statement.execute(stmt);

      int i = 0;
      for (; i < tableRowCount; i++) {
        int h = i;
        String r = String.format("range_%d", h);
        int vi = viConst;
        String vs = String.format("value_%d", vi);
        stmt = String.format("INSERT INTO %s VALUES (%d, '%s', %d, '%s')",
                             tableName, h, r, viConst, vs);
        statement.execute(stmt);
      }

      for (int j = 0; j < tableRowCount; i++, j++) {
        int h = j;
        String r = String.format("range_%d", i);
        int vi = viConst + 1000;
        String vs = String.format("value_%d", vi);
        stmt = String.format("INSERT INTO %s VALUES (%d, '%s', %d, '%s')",
                             tableName, h, r, viConst, vs);
        statement.execute(stmt);
      }
    }

    // Setup a small second table to test subqueries.
    String tableName2 = "TestLimit2";
    createPrefetchTable(tableName2);

    tableRowCount = 200;
    try (Statement statement = connection.createStatement()) {
      for (int i = 0; i < tableRowCount; i++) {
        int h = i;
        String r = String.format("range_%d", h);
        int vi = i + 10000;
        String vs = String.format("value_%d", vi);
        String stmt = String.format("INSERT INTO %s VALUES (%d, '%s', %d, '%s')",
                                    tableName2, h, r, vi, vs);
        statement.execute(stmt);
      }
    }

    // Choose the maximum runtimes of the same SELECT statement with and without LIMIT.
    // For performance check, only runs on RELEASE build matters, so expected runtime for other
    // builds can be very over-estimated.
    // Cases where "LIMIT clause" can be optimized
    // LIMIT value is pushed down to YugaByte PgGate and DocDB for these cases.
    // - LIMIT SELECT without WHERE clause and other options.
    // Cases where "LIMIT clause" cannot be optimized.
    // - LIMIT SELECT with WHERE clause on non-key column.
    // - LIMIT SELECT with ORDER BY is not optimized.
    // - LIMIT SELECT with AGGREGATE is not optimized.

    final int queryRunCount = 5;
    int limitScanMaxRuntimeMillis = getPerfMaxRuntime(20, 100, 500, 500, 500);
    // Index scan is slower as it requires two reads.
    // - Postgres first read ID from INDEX.
    // - Postgres use the above ID to read from user-table.
    int limitIndexScanMaxRuntimeMillis = getPerfMaxRuntime(100, 400, 1000, 1000, 1000);
    // Full scan is slowest.
    int fullScanMaxRuntimeMillis = getPerfMaxRuntime(200, 3000, 10000, 10000, 10000);

    try (Statement statement = connection.createStatement()) {
      // LIMIT SELECT without WHERE clause and other options will be optimized by passing
      // LIMIT value to YugaByte PgGate and DocDB.
      String query = String.format("SELECT * FROM %s LIMIT 1", tableName);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      query = String.format("SELECT * FROM %s LIMIT 1 OFFSET 100", tableName);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      // LIMIT SELECT with WHERE clause is not optimized.
      query = String.format("SELECT * FROM %s WHERE vi = %d LIMIT 1", tableName, viConst);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      query = String.format("SELECT * FROM %s WHERE vi = %d LIMIT 1 OFFSET 100", tableName,
          viConst);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      // LIMIT SELECT with ORDER BY is not optimized.
      query = String.format("SELECT * FROM %s ORDER BY vi LIMIT 1", tableName);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          fullScanMaxRuntimeMillis * queryRunCount);

      query = String.format("SELECT * FROM %s ORDER BY vi LIMIT 1 OFFSET 100", tableName);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          fullScanMaxRuntimeMillis * queryRunCount);

      // LIMIT SELECT with AGGREGATE is not optimized.
      query = String.format("SELECT SUM(h) FROM %s GROUP BY vs LIMIT 1", tableName);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          fullScanMaxRuntimeMillis * queryRunCount);

      query = String.format("SELECT SUM(h) FROM %s GROUP BY vs LIMIT 1 OFFSET 100", tableName);
      assertQueryRuntimeWithRowCount(statement, query, 0 /* expectedRowCount */, queryRunCount,
          fullScanMaxRuntimeMillis * queryRunCount);

      // LIMIT SELECT with index scan is optimized because YugaByte processed the index.
      query = String.format("SELECT * FROM %s WHERE vs = 'value_%d' LIMIT 1",
                            tableName, viConst);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      // Index scan for 101 rows is slower as more data are sent back & forth.
      query = String.format("SELECT * FROM %s WHERE vs = 'value_%d' LIMIT 1 OFFSET 100",
                            tableName, viConst);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitIndexScanMaxRuntimeMillis * queryRunCount);

      // SELECT with PRIMARY KEY scan is ALWAYS optimized regardless whether there's a LIMIT clause.
      query = String.format("SELECT * FROM %s WHERE h = 7 AND r = 'range_7' LIMIT 1",
                            tableName, viConst);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      query = String.format("SELECT * FROM %s WHERE h = 7 AND r = 'range_7' LIMIT 1 OFFSET 100",
                            tableName, viConst);
      assertQueryRuntimeWithRowCount(statement, query, 0 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      // Union of LIMIT SELECTs.
      query = String.format("(SELECT * FROM %s LIMIT 1) UNION ALL (SELECT * FROM %s LIMIT 1)",
                            tableName, tableName2);
      assertQueryRuntimeWithRowCount(statement, query, 2 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      query = String.format("(SELECT * FROM %s LIMIT 1 OFFSET 100) UNION ALL" +
                            "  (SELECT * FROM %s LIMIT 1 OFFSET 100)",
                            tableName, tableName2);
      assertQueryRuntimeWithRowCount(statement, query, 2 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      // Union of Tables in a LIMIT SELECT.
      query = String.format("SELECT * FROM %s, %s LIMIT 1", tableName, tableName2);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);

      query = String.format("SELECT * FROM %s, %s LIMIT 1 OFFSET 100", tableName, tableName2);
      assertQueryRuntimeWithRowCount(statement, query, 1 /* expectedRowCount */, queryRunCount,
          limitScanMaxRuntimeMillis * queryRunCount);
    }
  }
}
