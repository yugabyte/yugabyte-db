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

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_APPEND;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_LIMIT;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_YB_BATCHED_NESTED_LOOP;

import java.sql.Statement;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;
import org.yb.util.json.ObjectCheckerBuilder;
import org.yb.util.json.ValueChecker;

@RunWith(value = YBTestRunner.class)
public class TestPgExplainAnalyzeVectorMetrics extends BasePgExplainAnalyzeTest {
  private static final String VECTOR_TABLE = "vector_test_metrics";
  private static final String VECTOR_INDEX = "vector_test_metrics_hnsw_idx";
  private static final String META_TABLE = "vector_test_metrics_meta";
  private static final String NODE_SUBQUERY_SCAN = "Subquery Scan";

  private interface TopLevelCheckerBuilder extends ObjectCheckerBuilder {
    TopLevelCheckerBuilder plan(ObjectChecker checker);
    TopLevelCheckerBuilder readMetrics(ObjectChecker checker);
  }

  private interface PlanCheckerBuilder extends ObjectCheckerBuilder {
    PlanCheckerBuilder nodeType(String value);
    PlanCheckerBuilder plans(Checker... checker);
    PlanCheckerBuilder readMetrics(ObjectChecker checker);
  }

  private interface MetricsCheckerBuilder extends ObjectCheckerBuilder {
    MetricsCheckerBuilder metric(String key, ValueChecker<Long> checker);
  }

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  private static MetricsCheckerBuilder makeMetricsBuilder() {
    return JsonUtil.makeCheckerBuilder(MetricsCheckerBuilder.class, false);
  }

  private static final PlanCheckerBuilder VECTOR_INDEX_SCAN_PLAN =
      makePlanBuilder()
          .nodeType(NODE_INDEX_SCAN)
          .readMetrics(makeMetricsBuilder()
                  .metric("rocksdb_number_db_seek", Checkers.greaterOrEqual(2))
                  .metric("rocksdb_number_db_next", Checkers.greater(0))
                  .metric("docdb_keys_found", Checkers.equal(2))
                  .build());

  private static final PlanCheckerBuilder META_TABLE_SCAN_PLAN =
      makePlanBuilder()
          .nodeType(NODE_INDEX_SCAN)
          .readMetrics(makeMetricsBuilder()
                  .metric("rocksdb_number_db_seek", Checkers.equal(3))
                  .metric("rocksdb_number_db_next", Checkers.greater(0))
                  .metric("docdb_keys_found", Checkers.equal(4))
                  .build());

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS vector");
      stmt.execute("DROP TABLE IF EXISTS " + VECTOR_TABLE);
      stmt.execute("DROP TABLE IF EXISTS " + META_TABLE);
      stmt.execute("DROP INDEX IF EXISTS " + VECTOR_INDEX);
      stmt.execute(String.format(
          "CREATE TABLE %s (id BIGINT PRIMARY KEY, embedding vector(3))", VECTOR_TABLE));
      stmt.execute(String.format("CREATE INDEX %s ON %s USING hnsw (embedding vector_l2_ops)",
          VECTOR_INDEX, VECTOR_TABLE));
      stmt.execute(String.format("INSERT INTO %s VALUES "
              + "(1, '[0.0,0.0,0.0]'), "
              + "(2, '[0.1,0.1,0.1]'), "
              + "(3, '[0.2,0.2,0.2]'), "
              + "(4, '[0.9,0.9,0.9]')",
          VECTOR_TABLE));
      stmt.execute(
          String.format("CREATE TABLE %s (id BIGINT PRIMARY KEY, payload TEXT)", META_TABLE));
      stmt.execute(String.format("INSERT INTO %s VALUES "
              + "(1, 'a'), (2, 'b'), (3, 'c'), (4, 'd')",
          META_TABLE));
    }
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_analyze_dump_metrics", "true");
    return flagMap;
  }

  @Test
  public void testVectorIndexReadMetrics() throws Exception {
    final String vectorQuery =
        String.format("SELECT id FROM %s ORDER BY embedding <-> '[0.15,0.15,0.15]'::vector LIMIT 2",
            VECTOR_TABLE);

    Checker checker = makeTopLevelBuilder()
                          .readMetrics(makeMetricsBuilder()
                                  .metric("rocksdb_number_db_seek", Checkers.greaterOrEqual(2))
                                  .metric("rocksdb_number_db_next", Checkers.greater(0))
                                  .metric("docdb_keys_found", Checkers.equal(2))
                                  .build())
                          .plan(makePlanBuilder()
                                  .nodeType(NODE_LIMIT)
                                  .plans(VECTOR_INDEX_SCAN_PLAN.build())
                                  .build())
                          .build();
    testExplainDebug(vectorQuery, checker);
  }

  @Test
  public void testVectorIndexReadMetricsMultipleScans() throws Exception {
    final String multiScanQuery = String.format(
        "(SELECT id FROM %s ORDER BY embedding <-> '[0.15,0.15,0.15]'::vector LIMIT 2) "
            + "UNION ALL "
            + "(SELECT id FROM %s ORDER BY embedding <-> '[0.15,0.15,0.15]'::vector LIMIT 2)",
        VECTOR_TABLE, VECTOR_TABLE);

    Checker checker = makeTopLevelBuilder()
                          .readMetrics(makeMetricsBuilder()
                                  .metric("rocksdb_number_db_seek", Checkers.greaterOrEqual(4))
                                  .metric("rocksdb_number_db_next", Checkers.greater(0))
                                  .metric("docdb_keys_found", Checkers.equal(4))
                                  .build())
                          .plan(makePlanBuilder()
                                  .nodeType(NODE_APPEND)
                                  .plans(makePlanBuilder()
                                             .nodeType(NODE_SUBQUERY_SCAN)
                                             .plans(makePlanBuilder()
                                                     .nodeType(NODE_LIMIT)
                                                     .plans(VECTOR_INDEX_SCAN_PLAN.build())
                                                     .build())
                                             .build(),
                                      makePlanBuilder()
                                          .nodeType(NODE_SUBQUERY_SCAN)
                                          .plans(makePlanBuilder()
                                                  .nodeType(NODE_LIMIT)
                                                  .plans(VECTOR_INDEX_SCAN_PLAN.build())
                                                  .build())
                                          .build())
                                  .build())
                          .build();

    testExplainDebug(multiScanQuery, checker);
  }

  @Test
  public void testVectorIndexReadMetricsJoinWithMetaTable() throws Exception {
    final String joinQuery = String.format("SELECT v.id, m.payload "
            + "FROM %s v JOIN %s m ON v.id = m.id "
            + "ORDER BY v.embedding <-> '[0.15,0.15,0.15]'::vector LIMIT 2",
        VECTOR_TABLE, META_TABLE);

    Checker checker = makeTopLevelBuilder()
                          .readMetrics(makeMetricsBuilder()
                                  .metric("rocksdb_number_db_seek", Checkers.greaterOrEqual(5))
                                  .metric("rocksdb_number_db_next", Checkers.greater(0))
                                  .metric("docdb_keys_found", Checkers.equal(6))
                                  .build())
                          .plan(makePlanBuilder()
                                  .nodeType(NODE_LIMIT)
                                  .plans(makePlanBuilder()
                                          .nodeType("Result")
                                          .plans(makePlanBuilder()
                                                  .nodeType(NODE_YB_BATCHED_NESTED_LOOP)
                                                  .plans(VECTOR_INDEX_SCAN_PLAN.build(),
                                                      META_TABLE_SCAN_PLAN.build())
                                                  .build())
                                          .build())
                                  .build())
                          .build();

    testExplainDebug(joinQuery, checker);
  }
}
