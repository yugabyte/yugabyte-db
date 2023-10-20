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
import java.util.List;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.pgsql.ExplainAnalyzeUtils.checkReadRequests;
import static org.yb.pgsql.ExplainAnalyzeUtils.setRowAndSizeLimit;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_LIMIT;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_YB_BATCHED_NESTED_LOOP;

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

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false /* nullify */);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false /* nullify */);
  }

  @Test
  public void testSimplePrefetch() throws Exception {
    String tableName = "testprefetch";
    createPrefetchTable(tableName);

    int tableRowCount = 5000;
    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(
        "INSERT INTO %s SELECT " +
        "  g, CONCAT('range_', g::TEXT), " +
        "  g + 10000, CONCAT('value_', (g + 10000)::TEXT) " +
        "FROM generate_series(0, %d) g",
        tableName, tableRowCount - 1));

      String seqScanQuery = String.format("SELECT * FROM %s", tableName);

      checkReadRequests(statement, seqScanQuery, NODE_SEQ_SCAN,
                        Checkers.equal(51), tableRowCount);
    }
  }

  @Test
  public void testSizeBasedFetch() throws Exception {
    try (Statement statement = connection.createStatement()) {
      String tableName = "testfetch";
      int charLength = 109;
      int tableRowCount = 100000;

      statement.execute(String.format(
          "create table %s (k bigint, v char(%d), primary key (k asc))",
          tableName, charLength));
      statement.execute(String.format(
          "insert into %s (select s, repeat('a', %d) from generate_series(1, %d) as s)",
          tableName, charLength, tableRowCount));

      statement.execute(String.format("create index on %s (v asc)", tableName));

      String seqScanQuery = String.format("SELECT * FROM %s", tableName);
      String indexScanQuery = String.format("SELECT * FROM %s WHERE k > 0", tableName);
      String indexOnlyScanQuery = String.format("SELECT v FROM %s WHERE v > ''", tableName);

      ExplainAnalyzeUtils.setRowAndSizeLimit(statement, 1024, 0);
      checkReadRequests(statement, seqScanQuery, NODE_SEQ_SCAN,
                        Checkers.equal(98), tableRowCount);
      checkReadRequests(statement, indexScanQuery, NODE_INDEX_SCAN,
                        Checkers.equal(98), tableRowCount);
      checkReadRequests(statement, indexOnlyScanQuery, NODE_INDEX_ONLY_SCAN,
                        Checkers.equal(98), tableRowCount);

      ExplainAnalyzeUtils.setRowAndSizeLimit(statement, 0, 0);
      checkReadRequests(statement, seqScanQuery, NODE_SEQ_SCAN,
                        Checkers.equal(1), tableRowCount);
      checkReadRequests(statement, indexScanQuery, NODE_INDEX_SCAN,
                        Checkers.equal(1), tableRowCount);
      checkReadRequests(statement, indexOnlyScanQuery, NODE_INDEX_ONLY_SCAN,
                        Checkers.equal(1), tableRowCount);

      // row buffer fills up faster in YbSeqScan
      ExplainAnalyzeUtils.setRowAndSizeLimit(statement, 0, 512 * 1024);
      checkReadRequests(statement, seqScanQuery, NODE_SEQ_SCAN,
                        Checkers.equal(25), tableRowCount);
      checkReadRequests(statement, indexScanQuery, NODE_INDEX_SCAN,
                        Checkers.equal(25), tableRowCount);
      checkReadRequests(statement, indexOnlyScanQuery, NODE_INDEX_ONLY_SCAN,
                        Checkers.equal(23), tableRowCount);

      ExplainAnalyzeUtils.setRowAndSizeLimit(statement, 0, 1024 * 1024);
      checkReadRequests(statement, seqScanQuery, NODE_SEQ_SCAN,
                        Checkers.equal(13), tableRowCount);
      checkReadRequests(statement, indexScanQuery, NODE_INDEX_SCAN,
                        Checkers.equal(13), tableRowCount);
      checkReadRequests(statement, indexOnlyScanQuery, NODE_INDEX_ONLY_SCAN,
                        Checkers.equal(12), tableRowCount);
    }
  }

  @Test
  public void testBnlWithSizeLimit() throws Exception {
    // #18708: BNL with small LIMIT and yb_fetch_size_limit > 0 and yb_fetch_row_limit = 0
    // used the small limit for each request to the outer table. This resulted in
    // (yb_bnl_batch_size / query_limit) RPCs to fill the first batch.
    int tableRowCount = 1000;
    int limitCount = 10;
    String createStatement = "CREATE TABLE %s (a INT PRIMARY KEY, b INT)";
    String insertStatement = "INSERT INTO %s SELECT i, i FROM generate_series(1, %d) i";
    String tableName1 = "tb1";
    String tableName2 = "tb2";
    String query = String.format(
        "/*+ Set(yb_bnl_batch_size 1024) YbBatchedNL(t1 t2) */ " +
        "SELECT t1.a, t2.b FROM %s AS t1 JOIN %s AS t2 ON t1.a = t2.a LIMIT %d",
        tableName1, tableName2, limitCount);
    int innerTableRequests = 1;
    int outerTableRequests = 4;

    try (Statement statement = connection.createStatement()) {
      statement.execute(String.format(createStatement, tableName1));
      statement.execute(String.format(createStatement, tableName2));
      statement.execute(String.format(insertStatement, tableName1, tableRowCount));
      statement.execute(String.format(insertStatement, tableName2, tableRowCount));
      PlanCheckerBuilder limitChecker = makePlanBuilder()
        .nodeType(NODE_LIMIT)
        .actualRows(Checkers.equal(limitCount));

      PlanCheckerBuilder batchNestedLoopNodeChecker = makePlanBuilder()
        .nodeType(NODE_YB_BATCHED_NESTED_LOOP)
        .actualRows(Checkers.equal(limitCount));

      PlanCheckerBuilder outerTableSeqScanChecker = makePlanBuilder()
          .nodeType(NODE_SEQ_SCAN)
          .relationName(tableName1)
          .storageTableReadRequests(Checkers.equal(outerTableRequests));

      PlanCheckerBuilder innerTableIndexScanChecker = makePlanBuilder()
          .nodeType(NODE_INDEX_SCAN)
          .relationName(tableName2)
          .storageTableReadRequests(Checkers.equal(innerTableRequests))
          .actualLoops(Checkers.equal(1));

      Checker checker = makeTopLevelBuilder()
          .plan(limitChecker.plans(batchNestedLoopNodeChecker
                  .plans(outerTableSeqScanChecker.build(), innerTableIndexScanChecker.build())
                  .build())
              .build())
          .storageReadRequests(Checkers.equal(innerTableRequests + outerTableRequests))
          .build();

      // Test with row limit (and no size limit) - this is default behaviour
      ExplainAnalyzeUtils.setRowAndSizeLimit(statement, 1024, 0);
      ExplainAnalyzeUtils.testExplain(statement, query, checker);

      // Test with size limit (and no row limit). 1MB is large enough that we can expect the same
      // behaviour as with the row limit.
      ExplainAnalyzeUtils.setRowAndSizeLimit(statement, 0, 1024 * 1024);
      ExplainAnalyzeUtils.testExplain(statement, query, checker);
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
