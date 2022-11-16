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

import static org.yb.AssertionWrappers.assertTrue;
import java.sql.Statement;
import java.util.List;
import java.util.Map;

import com.google.gson.JsonElement;
import com.google.gson.JsonParser;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;
import org.yb.util.json.Checker;
import org.yb.util.json.ObjectCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;
import org.yb.util.json.ValueChecker;

/**
 * Test EXPLAIN ANALYZE command. Just verify non-zero values for volatile measures
 * such as RPC wait times.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgExplainAnalyze extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgExplainAnalyze.class);
  private static final String TABLE_NAME = "explain_test_table";
  private static final String INDEX_NAME = String.format("i_%s_c3_c2", TABLE_NAME);
  private static final String PK_INDEX_NAME = String.format("%s_pkey", TABLE_NAME);
  private static final String NODE_SEQ_SCAN = "Seq Scan";
  private static final String NODE_INDEX_SCAN = "Index Scan";
  private static final String NODE_INDEX_ONLY_SCAN = "Index Only Scan";
  private static final String NODE_VALUES_SCAN = "Values Scan";
  private static final String NODE_NESTED_LOOP = "Nested Loop";
  private static final String NODE_MODIFY_TABLE = "ModifyTable";
  private static final String NODE_FUNCTION_SCAN = "Function Scan";
  private static final String NODE_RESULT = "Result";
  private static final int TABLE_ROWS = 5000;

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_prefetch_limit", "1024");
    flagMap.put("ysql_session_max_batch_size", "512");
    flagMap.put("TEST_use_monotime_for_rpc_wait_time", "true");
    return flagMap;
  }

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

  private interface TopLevelCheckerBuilder extends ObjectCheckerBuilder {
    TopLevelCheckerBuilder storageReadRequests(ValueChecker<Long> checker);
    TopLevelCheckerBuilder storageWriteRequests(ValueChecker<Long> checker);
    TopLevelCheckerBuilder storageExecutionTime(ValueChecker<Double> checker);
    TopLevelCheckerBuilder plan(ObjectChecker checker);
  }

  private interface PlanCheckerBuilder extends ObjectCheckerBuilder {
    PlanCheckerBuilder nodeType(String value);
    PlanCheckerBuilder relationName(String value);
    PlanCheckerBuilder alias(String value);
    PlanCheckerBuilder indexName(String value);
    PlanCheckerBuilder storageTableReadRequests(ValueChecker<Long> checker);
    PlanCheckerBuilder storageTableExecutionTime(ValueChecker<Double> checker);
    PlanCheckerBuilder storageIndexReadRequests(ValueChecker<Long> checker);
    PlanCheckerBuilder storageIndexExecutionTime(ValueChecker<Double> checker);
    PlanCheckerBuilder plans(Checker... checker);
  }

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class);
  }

  private void testExplain(
      Statement stmt, String query, Checker checker, boolean timing) throws Exception {
    LOG.info("Query: " + query);
    JsonElement json = new JsonParser().parse(getSingleRow(
        stmt,
        String.format(
            "EXPLAIN (FORMAT json, ANALYZE true, SUMMARY true, DIST true, TIMING %b) %s",
            timing, query)).get(0).toString());
    LOG.info("Response:\n" + JsonUtil.asPrettyString(json));
    List<String> conflicts = JsonUtil.findConflicts(json.getAsJsonArray().get(0), checker);
    assertTrue(
        "Json conflicts:\n" + String.join("\n", conflicts),
        conflicts.isEmpty());
  }

  private void testExplain(Statement stmt, String query, Checker checker) throws Exception {
    testExplain(stmt, query, checker, true);
  }

  private void testExplainNoTiming(Statement stmt, String query, Checker checker) throws Exception {
    testExplain(stmt, query, checker, false);
  }

  private void testExplain(String query, Checker checker, boolean timing) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      testExplain(stmt, query, checker, timing);
    }
  }

  private void testExplain(String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      testExplain(stmt, query, checker, true);
    }
  }

  private void testExplainNoTiming(String query, Checker checker) throws Exception {
    testExplain(query, checker, false);
  }

  @Test
  public void testSeqScan() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      Checker checker = makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greater(0.0))
          .plan(makePlanBuilder()
              .nodeType(NODE_SEQ_SCAN)
              .relationName(TABLE_NAME)
              .alias(TABLE_NAME)
              .storageTableReadRequests(Checkers.equal(5))
              .storageTableExecutionTime(Checkers.greater(0.0))
              .build())
          .build();

      // Seq Scan (ybc_fdw ForeignScan)
      testExplain(stmt, String.format("SELECT * FROM %s", TABLE_NAME), checker);

      // real Seq Scan
      testExplain(stmt,
                  String.format("/*+ SeqScan(texpl) */SELECT * FROM %s", TABLE_NAME),
                  checker);
    }
  }

  @Test
  public void testPKScan() throws Exception {
    testExplain(
        String.format("SELECT * FROM %s WHERE c1 = 10", TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.greater(0))
            .storageWriteRequests(Checkers.equal(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_INDEX_SCAN)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .indexName(PK_INDEX_NAME)
                .storageIndexReadRequests(Checkers.equal(1))
                .storageIndexExecutionTime(Checkers.greater(0.0))
                .build())
            .build());
  }

  @Test
  public void testIndexScan() throws Exception {
    final String alias = "t";
    testExplain(
        String.format(
            "/*+ IndexScan(t %s) */SELECT * FROM %s AS %s WHERE c3 <= 15",
            INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.greater(0))
            .storageWriteRequests(Checkers.equal(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_INDEX_SCAN)
                .relationName(TABLE_NAME)
                .alias(alias)
                .indexName(INDEX_NAME)
                .storageTableReadRequests(Checkers.equal(4))
                .storageTableExecutionTime(Checkers.greater(0.0))
                .storageIndexReadRequests(Checkers.equal(4))
                .storageIndexExecutionTime(Checkers.greater(0.0))
                .build())
            .build());
  }

  @Test
  public void testIndexOnlyScan() throws Exception {
    final String alias = "t";
    testExplain(
        String.format(
            "/*+ IndexOnlyScan(t %s) */SELECT c2, c3 FROM %s AS %s WHERE c3 <= 15",
            INDEX_NAME, TABLE_NAME, alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.greater(0))
            .storageWriteRequests(Checkers.equal(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_INDEX_ONLY_SCAN)
                .relationName(TABLE_NAME)
                .alias(alias)
                .indexName(INDEX_NAME)
                .storageIndexReadRequests(Checkers.equal(4))
                .storageIndexExecutionTime(Checkers.greater(0.0))
                .build())
            .build());
  }

  @Test
  public void testNestedLoop() throws Exception {
    // NestLoop accesses the inner table as many times as the rows from the outer
    final String t1Alias = "t1";
    final String t2Alias = "t2";
    testExplain(
        String.format(
            "/*+ IndexScan(t1 %s) IndexScan(t2 %s) Leading((t1 t2)) NestLoop(t1 t2) */" +
            "SELECT * FROM %s AS %s JOIN %3$s AS %s ON t1.c2 <= t2.c3 AND t1.c1 = 1",
            PK_INDEX_NAME, INDEX_NAME, TABLE_NAME, t1Alias, t2Alias),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.greater(0))
            .storageWriteRequests(Checkers.equal(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_NESTED_LOOP)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(t1Alias)
                        .storageIndexReadRequests(Checkers.equal(1))
                        .storageIndexExecutionTime(Checkers.greater(0.0))
                        .build(),
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(INDEX_NAME)
                        .alias(t2Alias)
                        .storageTableReadRequests(Checkers.equal(4))
                        .storageTableExecutionTime(Checkers.greater(0.0))
                        .storageIndexReadRequests(Checkers.equal(4))
                        .storageIndexExecutionTime(Checkers.greater(0.0))
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
            .storageReadRequests(Checkers.greater(0))
            .storageWriteRequests(Checkers.equal(0))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_NESTED_LOOP)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(t1Alias)
                        .storageIndexReadRequests(Checkers.equal(1))
                        .storageIndexExecutionTime(Checkers.greater(0.0))
                        .build(),
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(INDEX_NAME)
                        .alias(t2Alias)
                        .build())
                .build())
            .build());
  }

  @Test
  public void testInsertValues() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // reduce the batch size to avoid 0 wait time
      stmt.execute("SET ysql_session_max_batch_size = 4");
      ObjectChecker planChecker =
          makePlanBuilder()
              .nodeType(NODE_MODIFY_TABLE)
              .relationName(TABLE_NAME)
              .alias(TABLE_NAME)
              .plans(
                  makePlanBuilder()
                      .nodeType(NODE_VALUES_SCAN)
                      .alias("*VALUES*")
                      .build())
              .build();

      testExplain(
        stmt,
        String.format(
            "INSERT INTO %s VALUES (1001, 0, 0, 'xyz'), (1002, 0, 0, 'wxy'), " +
            "(1003, 0, 0, 'vwx'), (1004, 0, 0, 'vwx')",
            TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(0))
            .storageWriteRequests(Checkers.equal(2))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(planChecker)
            .build());

      // no buffering
      stmt.execute("SET ysql_session_max_batch_size = 1");
      testExplain(
        stmt,
        String.format(
            "INSERT INTO %s VALUES (1601, 0, 0, 'xyz'), (1602, 0, 0, 'wxy'), " +
            "(1603, 0, 0, 'vwx'), (1604, 0, 0, 'vwx')",
            TABLE_NAME),
        makeTopLevelBuilder()
            .storageReadRequests(Checkers.equal(0))
            .storageWriteRequests(Checkers.equal(8))
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
            .storageWriteRequests(Checkers.equal(10))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_FUNCTION_SCAN)
                        .alias(alias)
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
            .storageReadRequests(Checkers.greater(0))
            .storageWriteRequests(Checkers.equal(206))
            .storageExecutionTime(Checkers.greater(0.0))
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
                        .storageTableExecutionTime(Checkers.greater(0.0))
                        .storageIndexReadRequests(Checkers.equal(1))
                        .storageIndexExecutionTime(Checkers.greater(0.0))
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
            .storageReadRequests(Checkers.greater(0))
            .storageWriteRequests(Checkers.equal(1))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(alias)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(alias)
                        .storageIndexReadRequests(Checkers.equal(1))
                        .storageIndexExecutionTime(Checkers.greater(0.0))
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
          stmt,
          query,
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.greater(0))
              .storageWriteRequests(Checkers.equal(20))
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
                          .storageTableReadRequests(Checkers.equal(5))
                          .storageTableExecutionTime(Checkers.greater(0.0))
                          .build())
                  .build())
              .build());

      // Do it again - should be no writes
      testExplain(
          stmt,
          query,
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.greater(0))
              .storageWriteRequests(Checkers.equal(0))
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
                          .storageTableExecutionTime(Checkers.greater(0.0))
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
            .storageWriteRequests(Checkers.equal(206))
            .storageExecutionTime(Checkers.greater(0.0))
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
                        .storageIndexReadRequests(Checkers.equal(1))
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
            .storageWriteRequests(Checkers.equal(1))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_RESULT)
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
            .storageWriteRequests(Checkers.equal(6))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(TABLE_NAME)
                        .storageIndexReadRequests(Checkers.equal(1))
                        .storageIndexExecutionTime(Checkers.greater(0.0))
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
            .storageWriteRequests(Checkers.equal(10))
            .storageExecutionTime(Checkers.greater(0.0))
            .plan(makePlanBuilder()
                .nodeType(NODE_MODIFY_TABLE)
                .relationName(TABLE_NAME)
                .alias(alias)
                .plans(
                    makePlanBuilder()
                        .nodeType(NODE_INDEX_SCAN)
                        .relationName(TABLE_NAME)
                        .indexName(PK_INDEX_NAME)
                        .alias(alias)
                        .storageIndexReadRequests(Checkers.equal(3))
                        .storageIndexExecutionTime(Checkers.greater(0.0))
                        .build())
                .build())
            .build());
  }
}
