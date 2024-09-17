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

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_VALUES_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_NESTED_LOOP;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_MODIFY_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_FUNCTION_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_RESULT;
import static org.yb.pgsql.ExplainAnalyzeUtils.OPERATION_INSERT;
import static org.yb.pgsql.ExplainAnalyzeUtils.RELATIONSHIP_INNER_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.RELATIONSHIP_OUTER_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.setHideNonDeterministicFields;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplainWithOptions;

import java.sql.Statement;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;
import org.yb.pgsql.ExplainAnalyzeUtils.ExplainAnalyzeOptionsBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

import org.yb.YBTestRunner;

/**
 * Test EXPLAIN ANALYZE command. Just verify non-zero values for volatile measures
 * such as RPC wait times.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgExplainAnalyze extends BasePgExplainAnalyzeTest {
  private static final String TABLE_NAME = "explain_test_table";
  private static final String INDEX_NAME = String.format("i_%s_c3_c2", TABLE_NAME);
  private static final String PK_INDEX_NAME = String.format("%s_pkey", TABLE_NAME);
  private static final int TABLE_ROWS = 5000;

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format(
          "CREATE TABLE %s (c1 bigint, c2 bigint, c3 bigint, c4 text, " +
          "PRIMARY KEY(c1 ASC, c2 ASC, c3 ASC))",
          TABLE_NAME));

      stmt.execute(String.format(
          "INSERT INTO %s SELECT i %% 1000, i %% 11, i %% 20, rpad(i::text, 256, '#') " +
          "FROM generate_series(1, %d) AS i",
          TABLE_NAME, TABLE_ROWS));

      stmt.execute(String.format(
          "CREATE INDEX %s ON %s (c3 ASC, c2 ASC)", INDEX_NAME, TABLE_NAME));
    }
  }

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  private static ExplainAnalyzeOptionsBuilder makeOptionsBuilder() {
    return new ExplainAnalyzeOptionsBuilder();
  }

  public void testExplain(String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ExplainAnalyzeUtils.testExplain(stmt, query, checker);
    }
  }

  public void testExplainNoTiming(String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ExplainAnalyzeUtils.testExplainNoTiming(stmt, query, checker);
    }
  }

  @Test
  public void testSeqScan() throws Exception {
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.ROUND_ROBIN);
    try (Statement stmt = connection.createStatement()) {
      Checker checker = makeTopLevelBuilder()
          .storageReadRequests(Checkers.equal(5))
          .storageReadExecutionTime(Checkers.greater(0.0))
          .storageRowsScanned(Checkers.equal(TABLE_ROWS))
          .storageWriteRequests(Checkers.equal(0))
          .storageFlushRequests(Checkers.equal(0))
          .catalogReadRequests(Checkers.equal(0))
          .catalogWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greater(0.0))
          .plan(makePlanBuilder()
              .nodeType(NODE_SEQ_SCAN)
              .relationName(TABLE_NAME)
              .alias(TABLE_NAME)
              .storageTableReadRequests(Checkers.equal(5))
              .storageTableReadExecutionTime(Checkers.greater(0.0))
              .storageTableRowsScanned(Checkers.equal(TABLE_ROWS))
              .actualRows(Checkers.equal(TABLE_ROWS))
              .build())
          .build();

      // Warm up the cache to get catalog requests out of the way.
      testExplain(String.format("SELECT * FROM %s", TABLE_NAME), null);

      // Additionally warmup the cache for all backends when Connection Manager
      // is in round-robin warmup mode.
      if (isConnMgrWarmupRoundRobinMode()) {
        for(int i = 0; i < CONN_MGR_WARMUP_BACKEND_COUNT; i++) {
            testExplain(String.format("SELECT * FROM %s", TABLE_NAME), null);
        }
      }

      // Seq Scan (ybc_fdw ForeignScan)
      testExplain(String.format("SELECT * FROM %s", TABLE_NAME), checker);

      // real Seq Scan
      testExplain(String.format("/*+ SeqScan(texpl) */SELECT * FROM %s", TABLE_NAME),
                  checker);
    }
  }

  @Test
  public void testPKScan() throws Exception {

    try (Statement stmt = connection.createStatement()) {
      testExplain(
        String.format("SELECT * FROM %s WHERE c1 = 10", TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(1))
            .storageReadExecutionTime(Checkers.greater(0.0))
            .storageRowsScanned(Checkers.equal(5))
            .storageWriteRequests(Checkers.equal(0))
            .storageFlushRequests(Checkers.equal(0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_INDEX_SCAN)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .indexName(PK_INDEX_NAME)
                .storageTableReadRequests(Checkers.equal(1))
                .storageTableReadExecutionTime(Checkers.greater(0.0))
                .storageTableRowsScanned(Checkers.equal(5))
                .actualRows(Checkers.equal(5))
                .build())
            .build());
    }
  }

  @Test
  public void testIndexScan() throws Exception {
    final String alias = "t";

    try (Statement stmt = connection.createStatement()) {

      testExplain(
        String.format(
          "/*+ IndexScan(t %s) */SELECT * FROM %s AS %s WHERE c3 <= 15",
          INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.equal(8))
          .storageReadExecutionTime(Checkers.greater(0.0))
          .storageRowsScanned(Checkers.equal(8000))
          .storageWriteRequests(Checkers.equal(0))
          .storageFlushRequests(Checkers.equal(0))
          .catalogReadRequests(Checkers.greater(0))
          .catalogReadExecutionTime(Checkers.greater(0.0))
          .catalogWriteRequests(Checkers.greaterOrEqual(0))
          .storageExecutionTime(Checkers.greater(0.0))
          .plan(makePlanBuilder()
              .nodeType(NODE_INDEX_SCAN)
              .relationName(TABLE_NAME)
              .alias(alias)
              .indexName(INDEX_NAME)
              .storageTableReadRequests(Checkers.equal(4))
              .storageTableReadExecutionTime(Checkers.greater(0.0))
              .storageTableRowsScanned(Checkers.equal(4000))
              .storageIndexReadRequests(Checkers.equal(4))
              .storageIndexReadExecutionTime(Checkers.greater(0.0))
              .storageIndexRowsScanned(Checkers.equal(4000))
              .actualRows(Checkers.equal(4000))
              .build())
          .build());
    }
  }

  @Test
  public void testIndexOnlyScan() throws Exception {
    final String alias = "t";

    try (Statement stmt = connection.createStatement()) {
      // Warm up the cache to get catalog requests out of the way.
      stmt.execute(String.format("SELECT * FROM %s", TABLE_NAME));

      testExplain(
        String.format(
            "/*+ IndexOnlyScan(t %s) */SELECT c2, c3 FROM %s AS %s WHERE c3 <= 15",
            INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(4))
            .storageReadExecutionTime(Checkers.greater(0.0))
            .storageRowsScanned(Checkers.equal(4000))
            .storageWriteRequests(Checkers.equal(0))
            .storageFlushRequests(Checkers.equal(0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_INDEX_ONLY_SCAN)
                .relationName(TABLE_NAME)
                .alias(alias)
                .indexName(INDEX_NAME)
                .storageIndexReadRequests(Checkers.equal(4))
                .storageIndexReadExecutionTime(Checkers.greater(0.0))
                .storageIndexRowsScanned(Checkers.equal(4000))
                .actualRows(Checkers.equal(4000))
                .build())
            .build());
    }
  }

  @Test
  public void testNestedLoop() throws Exception {
    // NestLoop accesses the inner table as many times as the rows from the outer
    final String t1Alias = "t1";
    final String t2Alias = "t2";
    final int numLoops = 5;

    testExplain(
      String.format(
          "/*+ IndexScan(t1 %s) IndexScan(t2 %s) Leading((t1 t2)) NestLoop(t1 t2) */" +
          "SELECT * FROM %s AS %s JOIN %3$s AS %s ON t1.c2 <= t2.c3 AND t1.c1 = 1",
          PK_INDEX_NAME, INDEX_NAME, TABLE_NAME, t1Alias, t2Alias),
      makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(((numLoops - 1) * 8) + 1))
          .storageReadExecutionTime(Checkers.greater(0.0))
          .storageRowsScanned(Checkers.equal(36000 + 5))
          .storageWriteRequests(Checkers.equal(0))
          .storageFlushRequests(Checkers.equal(0))
          .catalogReadRequests(Checkers.greater(0))
          .catalogReadExecutionTime(Checkers.greater(0.0))
          .catalogWriteRequests(Checkers.greaterOrEqual(0))
          .storageExecutionTime(Checkers.greater(0.0))
          .plan(makePlanBuilder()
              .nodeType(NODE_NESTED_LOOP)
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .relationName(TABLE_NAME)
                      .indexName(PK_INDEX_NAME)
                      .parentRelationship(RELATIONSHIP_OUTER_TABLE)
                      .alias(t1Alias)
                      .storageTableReadRequests(Checkers.equal(1))
                      .storageTableReadExecutionTime(Checkers.greater(0.0))
                      .storageTableRowsScanned(Checkers.equal(5))
                      .actualRows(Checkers.equal(5))
                      .build(),
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .relationName(TABLE_NAME)
                      .indexName(INDEX_NAME)
                      .actualLoops(Checkers.equal(numLoops))
                      .parentRelationship(RELATIONSHIP_INNER_TABLE)
                      .alias(t2Alias)
                      .storageTableReadRequests(Checkers.equal(4))
                      .storageTableReadExecutionTime(Checkers.greater(0.0))
                      .storageTableRowsScanned(Checkers.equal(3600))
                      .storageIndexReadRequests(Checkers.equal(4))
                      .storageIndexReadExecutionTime(Checkers.greater(0.0))
                      .storageIndexRowsScanned(Checkers.equal(3600))
                      .actualRows(Checkers.equal(3600))
                      .build())
              .build())
          .build());
  }

  @Test
  public void testEmptyNestedLoop() throws Exception {
    // Inner table never executed
    final String t1Alias = "t1";
    final String t2Alias = "t2";

    testExplain(
      String.format(
          "/*+ IndexScan(t1 %s) IndexScan(t2 %s) Leading((t1 t2)) NestLoop(t1 t2) */" +
          "SELECT * FROM %s AS %s JOIN %3$s AS %s ON t1.c2 <= t2.c3 AND t1.c1 = -1",
          PK_INDEX_NAME, INDEX_NAME, TABLE_NAME, t1Alias, t2Alias),
      makeTopLevelBuilder()
          .storageReadRequests(Checkers.equal(1))
          .storageReadExecutionTime(Checkers.greater(0.0))
          .storageRowsScanned(Checkers.equal(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageFlushRequests(Checkers.equal(0))
          .catalogReadRequests(Checkers.greater(0))
          .catalogReadExecutionTime(Checkers.greater(0.0))
          .catalogWriteRequests(Checkers.greaterOrEqual(0))
          .storageExecutionTime(Checkers.greater(0.0))
          .plan(makePlanBuilder()
              .nodeType(NODE_NESTED_LOOP)
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .relationName(TABLE_NAME)
                      .indexName(PK_INDEX_NAME)
                      .alias(t1Alias)
                      .storageTableReadRequests(Checkers.equal(1))
                      .storageTableReadExecutionTime(Checkers.greater(0.0))
                      .actualRows(Checkers.equal(0))
                      .build(),
                  makePlanBuilder()
                      .nodeType(NODE_INDEX_SCAN)
                      .relationName(TABLE_NAME)
                      .indexName(INDEX_NAME)
                      .alias(t2Alias)
                      .actualRows(Checkers.equal(0))
                      .build())
              .build())
          .build());
  }

  @Test
  public void testInsertValues() throws Exception {
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.ROUND_ROBIN);
    try (Statement stmt = connection.createStatement()) {
      // reduce the batch size to avoid 0 wait time
      stmt.execute("SET ysql_session_max_batch_size = 4");

      // Warm up the cache to get catalog requests out of the way.
      testExplain(String.format("SELECT * FROM %s", TABLE_NAME), null);

      // Additionally warmup the cache for all backends when Connection Manager
      // is in round-robin warmup mode.
      if (isConnMgrWarmupRoundRobinMode()) {
        for(int i = 0; i < CONN_MGR_WARMUP_BACKEND_COUNT; i++) {
            testExplain(String.format("SELECT * FROM %s", TABLE_NAME), null);
        }
      }

      ObjectChecker planChecker =
          makePlanBuilder()
              .nodeType(NODE_MODIFY_TABLE)
              .operation(OPERATION_INSERT)
              .relationName(TABLE_NAME)
              .alias(TABLE_NAME)
              .actualRows(Checkers.equal(0))
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_VALUES_SCAN)
                      .alias("*VALUES*")
                      .actualRows(Checkers.equal(4))
                      .build())
              .build();

      testExplain(
        String.format(
            "INSERT INTO %s VALUES (1001, 0, 0, 'xyz'), (1002, 0, 0, 'wxy'), " +
            "(1003, 0, 0, 'vwx'), (1004, 0, 0, 'vwx')", TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(0))
            .storageRowsScanned(Checkers.equal(0))
            .storageWriteRequests(Checkers.equal(8))
            .storageFlushRequests(Checkers.equal(2))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.equal(0))
            .catalogWriteRequests(Checkers.equal(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(planChecker)
            .build());

      // no buffering
      stmt.execute("SET ysql_session_max_batch_size = 1");
      testExplain(
        String.format(
            "INSERT INTO %s VALUES (1601, 0, 0, 'xyz'), (1602, 0, 0, 'wxy'), " +
            "(1603, 0, 0, 'vwx'), (1604, 0, 0, 'vwx')",
            TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(0))
            .storageRowsScanned(Checkers.equal(0))
            .storageWriteRequests(Checkers.equal(8))
            .storageFlushRequests(Checkers.equal(8))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.equal(0))
            .catalogWriteRequests(Checkers.equal(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(planChecker)
            .build());
    }
  }

  @Test
  public void testInsertFromSelect() throws Exception {
    final String alias = "i";
    testExplain(
        String.format(
            "INSERT INTO %s SELECT %d + %s %% 1000, %3$s %% 11, %3$s %% 20, " +
            "rpad(%3$s::text, 256, '#') FROM generate_series(%d, %d) AS %3$s",
            TABLE_NAME, TABLE_ROWS, alias, TABLE_ROWS + 1, (int)(1.5 * TABLE_ROWS)),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(0))
            .storageRowsScanned(Checkers.equal(0))
            .storageWriteRequests(Checkers.equal(5000))
            .storageFlushRequests(Checkers.equal(10))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .actualRows(Checkers.equal(0))
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_FUNCTION_SCAN)
                        .alias(alias)
                        .actualRows(Checkers.equal((int)(0.5 * TABLE_ROWS)))
                        .build())
                .build())
            .build());
  }

  @Test
  public void testUpdateUsingIndex() throws Exception {
    final String alias = "t";
    testExplain(
        String.format(
            "/*+ IndexScan(t %s) */" +
            "UPDATE %s AS %s SET c4 = rpad(c1::text, 256, '@') WHERE c2 = 3 AND c3 <= 8",
            INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(2))
            .storageReadExecutionTime(Checkers.greater(0.0))
            .storageRowsScanned(Checkers.equal(410))
            .storageWriteRequests(Checkers.greater(1))
            .storageFlushRequests(Checkers.equal(1))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(alias)
                .actualRows(Checkers.equal(0))
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(INDEX_NAME)
                        .alias(alias)
                        .storageTableReadRequests(Checkers.equal(1))
                        .storageTableReadExecutionTime(Checkers.greater(0.0))
                        .storageTableRowsScanned(Checkers.equal(205))
                        .storageIndexReadRequests(Checkers.equal(1))
                        .storageIndexReadExecutionTime(Checkers.greater(0.0))
                        .storageIndexRowsScanned(Checkers.equal(205))
                        .storageTableWriteRequests(Checkers.equal(205))
                        .actualRows(Checkers.equal(205))
                        .build())
                .build())
            .build());
  }

  @Test
  public void testDeleteUsingIndex() throws Exception {
    final String alias = "t";
    testExplain(
        String.format(
            "/*+ IndexScan(t %s) */DELETE FROM %s AS %s WHERE c1 >= 990",
            PK_INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(1))
            .storageReadExecutionTime(Checkers.greater(0.0))
            .storageRowsScanned(Checkers.equal(50))
            .storageWriteRequests(Checkers.equal(50 + 50))
            .storageFlushRequests(Checkers.equal(1))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(alias)
                .actualRows(Checkers.equal(0))
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(alias)
                        .storageTableReadRequests(Checkers.equal(1))
                        .storageTableReadExecutionTime(Checkers.greater(0.0))
                        .storageTableRowsScanned(Checkers.equal(50))
                        .storageTableWriteRequests(Checkers.equal(50))
                        .storageIndexWriteRequests(Checkers.equal(50))
                        .actualRows(Checkers.equal(50))
                        .build())
                .build())
            .build());
  }

  @Test
  public void testDeleteAll() throws Exception {
    String query = String.format("DELETE FROM %s", TABLE_NAME);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("BEGIN");
      testExplain(
          query,
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.equal(5))
              .storageReadExecutionTime(Checkers.greater(0.0))
              .storageRowsScanned(Checkers.equal(TABLE_ROWS))
              .storageWriteRequests(Checkers.equal(5000 + 5000))
              .storageFlushRequests(Checkers.equal(20))
              .storageFlushExecutionTime(Checkers.greater(0.0))
              .catalogReadRequests(Checkers.greater(0))
              .catalogReadExecutionTime(Checkers.greater(0.0))
              .catalogWriteRequests(Checkers.greaterOrEqual(0))
              .storageExecutionTime(Checkers.greater(0.0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_MODIFY_TABLE)
                  .relationName(TABLE_NAME)
                  .alias(TABLE_NAME)
                  .actualRows(Checkers.equal(0))
                  .plans(
                      makePlanBuilder()
                          .nodeType(NODE_SEQ_SCAN)
                          .relationName(TABLE_NAME)
                          .alias(TABLE_NAME)
                          .storageTableReadRequests(Checkers.equal(5))
                          .storageTableReadExecutionTime(Checkers.greater(0.0))
                          .storageTableRowsScanned(Checkers.equal(TABLE_ROWS))
                          .storageIndexWriteRequests(Checkers.equal(TABLE_ROWS))
                          .storageTableWriteRequests(Checkers.equal(TABLE_ROWS))
                          .actualRows(Checkers.equal(TABLE_ROWS))
                          .build())
                  .build())
              .build());

      // Do it again - should be no writes
      testExplain(
          query,
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.equal(1))
              .storageReadExecutionTime(Checkers.greater(0.0))
              .storageRowsScanned(Checkers.equal(0))
              .storageWriteRequests(Checkers.equal(0))
              .storageFlushRequests(Checkers.equal(0))
              .catalogReadRequests(Checkers.equal(0))
              .catalogWriteRequests(Checkers.greaterOrEqual(0))
              .storageExecutionTime(Checkers.greater(0.0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_MODIFY_TABLE)
                  .relationName(TABLE_NAME)
                  .alias(TABLE_NAME)
                  .plans(
                      makePlanBuilder()
                          .nodeType(NODE_SEQ_SCAN)
                          .relationName(TABLE_NAME)
                          .alias(TABLE_NAME)
                          .storageTableReadRequests(Checkers.equal(1))
                          .storageTableReadExecutionTime(Checkers.greater(0.0))
                          .build())
                  .build())
              .build());
        stmt.execute("ROLLBACK");
    }
  }

  @Test
  public void testNoTiming() throws Exception {
    final String alias = "t";
    testExplainNoTiming(
        String.format(
            "/*+ IndexScan(t %s) */" +
            "UPDATE %s AS %s SET c4 = rpad(c1::text, 256, '@') WHERE c2 = 1 AND c3 <= 8",
            INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(2))
            .storageRowsScanned(Checkers.equal(410))
            .storageWriteRequests(Checkers.greater(1))
            .storageFlushRequests(Checkers.equal(1))
            .catalogReadRequests(Checkers.greater(0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(alias)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(INDEX_NAME)
                        .alias(alias)
                        .storageTableReadRequests(Checkers.equal(1))
                        .storageTableRowsScanned(Checkers.equal(205))
                        .storageIndexReadRequests(Checkers.equal(1))
                        .storageIndexRowsScanned(Checkers.equal(205))
                        .actualRows(Checkers.equal(205))
                        .build())
                .build())
            .build());
  }

  @Test
  public void testInsertReturning() throws Exception {
    testExplain(
        String.format("INSERT INTO %s VALUES (1001, 0, 0, 'abc') RETURNING *", TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(0))
            .storageRowsScanned(Checkers.equal(0))
            .storageWriteRequests(Checkers.equal(2))
            .storageFlushRequests(Checkers.equal(1))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .storageTableWriteRequests(Checkers.equal(1))
                .storageIndexWriteRequests(Checkers.equal(1))
                .actualRows(Checkers.equal(1))
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_RESULT)
                        .actualRows(Checkers.equal(1))
                        .build())
                .build())
            .build());
  }

  @Test
  public void testUpdateReturning() throws Exception {
    testExplain(
        String.format(
            "UPDATE %s SET c4 = rpad(c1::text, 256, '*') WHERE c1 = 999 RETURNING *",
            TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(1))
            .storageReadExecutionTime(Checkers.greater(0.0))
            .storageRowsScanned(Checkers.equal(5))
            .storageWriteRequests(Checkers.equal(5))
            .storageFlushRequests(Checkers.equal(1))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .storageTableWriteRequests(Checkers.equal(5))
                .actualRows(Checkers.equal(5))
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(TABLE_NAME)
                        .storageTableReadRequests(Checkers.equal(1))
                        .storageTableReadExecutionTime(Checkers.greater(0.0))
                        .storageTableRowsScanned(Checkers.equal(5))
                        .actualRows(Checkers.equal(5))
                        .build())
                .build())
            .build());
  }

  @Test
  public void testDeleteReturning() throws Exception {
    final String alias = "t";
    testExplain(
        String.format(
            "/*+ IndexScan(t %s) */DELETE FROM %s AS %s WHERE c1 >= 500 RETURNING *",
            PK_INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(3))
            .storageReadExecutionTime(Checkers.greater(0.0))
            .storageRowsScanned(Checkers.equal(2500))
            .storageWriteRequests(Checkers.equal(5000))
            .storageFlushRequests(Checkers.equal(10))
            .storageFlushExecutionTime(Checkers.greater(0.0))
            .catalogReadRequests(Checkers.greater(0))
            .catalogReadExecutionTime(Checkers.greater(0.0))
            .catalogWriteRequests(Checkers.greaterOrEqual(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(alias)
                .storageTableWriteRequests(Checkers.equal(2500))
                .storageIndexWriteRequests(Checkers.equal(2500))
                .actualRows(Checkers.equal(2500))
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(alias)
                        .storageTableReadRequests(Checkers.equal(3))
                        .storageTableReadExecutionTime(Checkers.greater(0.0))
                        .storageTableRowsScanned(Checkers.equal(2500))
                        .actualRows(Checkers.equal(2500))
                        .build())
                .build())
            .build());
  }

  @Test
  public void testExplainAnalyzeOptions() throws Exception {
    String query = String.format("SELECT * FROM %s", TABLE_NAME);
    try (Statement stmt = connection.createStatement()) {
        setHideNonDeterministicFields(stmt, true);

        // Vanilla EXPLAIN (ANALYZE, DIST) with non-deterministic fields hidden
        testExplainWithOptions(
            stmt,
            makeOptionsBuilder()
                .analyze(true)
                .dist(true),
            query,
            makeTopLevelBuilder()
                .storageReadRequests(Checkers.equal(5))
                .storageRowsScanned(Checkers.equal(TABLE_ROWS))
                .storageWriteRequests(Checkers.equal(0))
                .storageFlushRequests(Checkers.equal(0))
                .plan(makePlanBuilder()
                    .nodeType(NODE_SEQ_SCAN)
                    .relationName(TABLE_NAME)
                    .alias(TABLE_NAME)
                    .planRows(Checkers.greater(0))
                    .actualRows(Checkers.equal(5000))
                    .storageTableReadRequests(Checkers.equal(5))
                    .storageTableRowsScanned(Checkers.equal(TABLE_ROWS))
                    .build())
                .build());

        // EXPLAIN (ANALYZE, DIST, SUMMARY OFF) with non-deterministic fields hidden
        testExplainWithOptions(
            stmt,
            makeOptionsBuilder()
                .analyze(true)
                .dist(true)
                .summary(false),
            query,
            makeTopLevelBuilder()
                .plan(makePlanBuilder()
                    .nodeType(NODE_SEQ_SCAN)
                    .relationName(TABLE_NAME)
                    .alias(TABLE_NAME)
                    .planRows(Checkers.greater(0))
                    .actualRows(Checkers.equal(5000))
                    .storageTableReadRequests(Checkers.equal(5))
                    .storageTableRowsScanned(Checkers.equal(TABLE_ROWS))
                    .build())
                .build());

        setHideNonDeterministicFields(stmt, false);

        // Vanilla EXPLAIN (ANALYZE, DIST) with non-deterministic fields visible
        testExplainWithOptions(
            stmt,
            makeOptionsBuilder()
                .analyze(true)
                .dist(true)
                .timing(true),
            query,
            makeTopLevelBuilder()
                .storageReadRequests(Checkers.equal(5))
                .storageReadExecutionTime(Checkers.greater(0.0))
                .storageRowsScanned(Checkers.equal(TABLE_ROWS))
                .storageWriteRequests(Checkers.equal(0))
                .storageFlushRequests(Checkers.equal(0))
                .catalogReadRequests(Checkers.equal(0))
                .catalogWriteRequests(Checkers.equal(0))
                .storageExecutionTime(Checkers.greater(0.0))
                .plan(makePlanBuilder()
                    .nodeType(NODE_SEQ_SCAN)
                    .relationName(TABLE_NAME)
                    .alias(TABLE_NAME)
                    .planRows(Checkers.greater(0))
                    .actualRows(Checkers.equal(5000))
                    .storageTableReadRequests(Checkers.equal(5))
                    .storageTableReadExecutionTime(Checkers.greater(0.0))
                    .storageTableRowsScanned(Checkers.equal(TABLE_ROWS))
                    .build())
                .build());

        // EXPLAIN (ANALYZE ON, DIST ON, COSTS OFF, SUMMARY OFF) with non-deterministic
        // fields visible
        testExplainWithOptions(
            stmt,
            makeOptionsBuilder()
                .analyze(true)
                .dist(true)
                .summary(false)
                .timing(true)
                .costs(false),
            query,
            makeTopLevelBuilder()
                .plan(makePlanBuilder()
                    .nodeType(NODE_SEQ_SCAN)
                    .relationName(TABLE_NAME)
                    .alias(TABLE_NAME)
                    .storageTableReadRequests(Checkers.equal(5))
                    .storageTableReadExecutionTime(Checkers.greater(0.0))
                    .storageTableRowsScanned(Checkers.equal(TABLE_ROWS))
                    .actualRows(Checkers.equal(5000))
                    .build())
                .build());

        // EXPLAIN (ANALYZE ON, DIST ON, VERBOSE ON) with non-deterministic fields visible
        stmt.execute("SET yb_enable_base_scans_cost_model = TRUE");
        stmt.execute("SET enable_bitmapscan = FALSE");
        testExplainWithOptions(
            stmt,
            makeOptionsBuilder()
                .analyze(true)
                .dist(true)
                .timing(true)
                .verbose(true),
            String.format("SELECT * FROM %s WHERE c1 = 1", TABLE_NAME),
            makeTopLevelBuilder()
                .storageReadRequests(Checkers.equal(1))
                .storageReadExecutionTime(Checkers.greater(0.0))
                .storageRowsScanned(Checkers.equal(5))
                .storageWriteRequests(Checkers.equal(0))
                .storageFlushRequests(Checkers.equal(0))
                .catalogReadRequests(Checkers.greaterOrEqual(0))
                .catalogReadExecutionTime(Checkers.greater(0.0))
                .catalogWriteRequests(Checkers.equal(0))
                .storageExecutionTime(Checkers.greater(0.0))
                .plan(makePlanBuilder()
                    .nodeType(NODE_INDEX_SCAN)
                    .relationName(TABLE_NAME)
                    .alias(TABLE_NAME)
                    .planRows(Checkers.greater(0))
                    .actualRows(Checkers.equal(5))
                    .storageTableReadRequests(Checkers.equal(1))
                    .storageTableReadExecutionTime(Checkers.greater(0.0))
                    .storageTableRowsScanned(Checkers.equal(5))
                    .build())
                .build());
    }
  }
}
