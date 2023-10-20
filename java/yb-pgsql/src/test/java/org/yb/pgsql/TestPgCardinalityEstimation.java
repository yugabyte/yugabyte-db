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

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_AGGREGATE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_HASH;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_HASH_JOIN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_LIMIT;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_MERGE_JOIN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SORT;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

import java.sql.Statement;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;

@RunWith(value=YBTestRunner.class)
public class TestPgCardinalityEstimation extends BasePgSQLTest {
  private static final String TABLE_NAME = "t50000";
  private static final int TABLE_ROWS = 50000;

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format(
          "CREATE TABLE %s AS " +
          "SELECT c_int, " +
          "(CASE WHEN c_int %% 2 = 0 THEN TRUE ELSE FALSE END) AS c_bool, " +
          "(c_int + 0.0001)::TEXT AS c_text, " +
          "(c_int + 0.0002)::VARCHAR AS c_varchar, " +
          "(c_int + 0.1)::DECIMAL AS c_decimal, " +
          "(c_int + 0.2)::FLOAT AS c_float, " +
          "(c_int + 0.3)::REAL AS c_real, " +
          "(c_int + 0.4)::MONEY AS c_money " +
          "FROM generate_series (1, %d) c_int",
          TABLE_NAME, TABLE_ROWS));

      stmt.execute(String.format("ANALYZE %s", TABLE_NAME));
      stmt.execute("SET yb_enable_optimizer_statistics = true");
    }
  }

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
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
  public void testHashJoinWithIndex() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format(
          "CREATE INDEX i1 ON %s (c_float, c_text, c_varchar)",
          TABLE_NAME));

      testExplain(String.format(
          "/*+ HashJoin(t1 t2) */" +
          "SELECT COUNT(*) FROM %1$s t1 INNER JOIN %1$s t2 " +
          "ON t1.c_text = t2.c_text WHERE  t2.c_int < 44107",
          TABLE_NAME),
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.greater(0))
              .storageWriteRequests(Checkers.equal(0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_AGGREGATE)
                  .planRows(Checkers.equal(1))
                  .plans(makePlanBuilder()
                      .nodeType(NODE_HASH_JOIN)
                      .planRows(Checkers.greater(1))
                      .plans(
                          makePlanBuilder()
                              .nodeType(NODE_SEQ_SCAN)
                              .relationName(TABLE_NAME)
                              .alias("t1")
                              .planRows(Checkers.equal(TABLE_ROWS))
                              .storageTableReadRequests(Checkers.greater(1))
                              .build(),
                          makePlanBuilder()
                              .nodeType(NODE_HASH)
                              .planRows(Checkers.greater(1))
                              .plans(makePlanBuilder()
                                  .nodeType(NODE_SEQ_SCAN)
                                  .relationName(TABLE_NAME)
                                  .alias("t2")
                                  .planRows(Checkers.greater(1))
                                  .storageTableReadRequests(Checkers.greater(1))
                                  .build())
                              .build())
                      .build())
                  .build())
              .build());
    }
  }

  @Test
  public void testMergeJoinWithIndex() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      long QUERY_LIMIT = 1000;

      stmt.execute(String.format(
          "CREATE INDEX i2 ON %s (c_float, c_decimal, c_varchar)",
          TABLE_NAME));

      testExplain(String.format(
          "/*+ Leading (t1 t3 t2) MergeJoin(t1 t3) MergeJoin(t1 t3 %1$s) " +
          "SeqScan(t1) SeqScan(t3) SeqScan(t2) */ " +
          "SELECT DISTINCT t1.c_float, " +
          "t2.c_text, " +
          "t3.c_varchar " +
          "FROM %1$s t1 FULL JOIN %1$s t2 " +
          "ON t1.c_float = t2.c_float FULL JOIN %1$s t3 " +
          "ON t1.c_float = t3.c_float " +
          "WHERE t1.c_int > 21965 " +
          "ORDER BY t1.c_float, t2.c_text, t3.c_varchar DESC LIMIT %2$d OFFSET 50",
          TABLE_NAME, QUERY_LIMIT),
          makeTopLevelBuilder()
              .storageReadRequests(Checkers.greater(1))
              .storageWriteRequests(Checkers.equal(0))
              .plan(makePlanBuilder()
                  .nodeType(NODE_LIMIT)
                  .planRows(Checkers.equal(QUERY_LIMIT))
                  .plans(makePlanBuilder()
                      .nodeType(NODE_SORT)
                      .planRows(Checkers.greater(1))
                      .plans(makePlanBuilder()
                          .nodeType(NODE_AGGREGATE)
                          .planRows(Checkers.greater(1))
                          .plans(makePlanBuilder()
                              .nodeType(NODE_HASH_JOIN)
                              .planRows(Checkers.greater(1))
                              .plans(
                                  makePlanBuilder()
                                      .nodeType(NODE_SEQ_SCAN)
                                      .relationName(TABLE_NAME)
                                      .alias("t2")
                                      .planRows(Checkers.equal(TABLE_ROWS))
                                      .storageTableReadRequests(Checkers.greater(1))
                                      .storageTableReadExecutionTime(Checkers.greater(0.0))
                                      .build(),
                                  makePlanBuilder()
                                      .nodeType(NODE_HASH)
                                      .planRows(Checkers.greater(1))
                                      .plans(makePlanBuilder()
                                          .nodeType(NODE_MERGE_JOIN)
                                          .planRows(Checkers.greater(1))
                                          .plans(
                                              makePlanBuilder()
                                                  .nodeType(NODE_SORT)
                                                  .planRows(Checkers.greater(1))
                                                  .plans(makePlanBuilder()
                                                      .nodeType(NODE_SEQ_SCAN)
                                                      .relationName(TABLE_NAME)
                                                      .alias("t1")
                                                      .planRows(Checkers.greater(1))
                                                      .storageTableReadRequests(
                                                        Checkers.greater(1))
                                                      .storageTableReadExecutionTime(
                                                        Checkers.greater(0.0))
                                                      .build())
                                                  .build(),
                                              makePlanBuilder()
                                                  .nodeType(NODE_SORT)
                                                  .planRows(Checkers.equal(TABLE_ROWS))
                                                  .plans(makePlanBuilder()
                                                      .nodeType(NODE_SEQ_SCAN)
                                                      .relationName(TABLE_NAME)
                                                      .alias("t3")
                                                      .planRows(Checkers.equal(TABLE_ROWS))
                                                      .storageTableReadRequests(
                                                        Checkers.greater(1))
                                                      .storageTableReadExecutionTime(
                                                        Checkers.greater(0.0))
                                                      .build())
                                                  .build())
                                          .build())
                                      .build())
                              .build())
                          .build())
                      .build())
                  .build())
              .build());
    }
  }
}
