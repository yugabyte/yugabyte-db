package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_MODIFY_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_RESULT;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_YB_BATCHED_NESTED_LOOP;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_VALUES_SCAN;

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

@RunWith(value=YBTestRunner.class)
public class TestPgExplainAnalyzeMetrics extends BasePgExplainAnalyzeTest {
  private static final String MAIN_TABLE = "foo";
  private static final int NUM_PAGES = 10;

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s (k INT PRIMARY KEY, v1 INT, "
          + "v2 INT, v3 INT, v4 INT, v5 INT) SPLIT INTO 2 TABLETS", MAIN_TABLE));

      // Populate the table
      stmt.execute(String.format("INSERT INTO %s (SELECT i, i, i %% 1024, i, i, "
        + "i %% 128 FROM generate_series(0, %d) i)", MAIN_TABLE, (NUM_PAGES * 1024 - 1)));
    }
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_analyze_dump_metrics", "true");
    return flagMap;
  }

  private interface TopLevelCheckerBuilder extends ObjectCheckerBuilder {
    TopLevelCheckerBuilder plan(ObjectChecker checker);
    TopLevelCheckerBuilder readMetrics(ObjectChecker checker);
    TopLevelCheckerBuilder writeMetrics(ObjectChecker checker);
  }

  private interface PlanCheckerBuilder extends ObjectCheckerBuilder {
    PlanCheckerBuilder alias(String value);
    PlanCheckerBuilder readMetrics(ObjectChecker checker);
    PlanCheckerBuilder writeMetrics(ObjectChecker checker);
    PlanCheckerBuilder nodeType(String value);
    PlanCheckerBuilder plans(Checker... checker);
    PlanCheckerBuilder relationName(String value);
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

  @Test
  public void testSeqScan() throws Exception {
    final String simpleQuery = String.format("SELECT * FROM %s", MAIN_TABLE);

    PlanCheckerBuilder SEQ_SCAN_PLAN = makePlanBuilder()
        .nodeType(NODE_SEQ_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .readMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.greater(0))
            .metric("docdb_keys_found", Checkers.greater(0))
            .build());

    Checker checker = makeTopLevelBuilder()
        .readMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.greater(0))
            .metric("docdb_keys_found", Checkers.greater(0))
            .build())
        .plan(SEQ_SCAN_PLAN.build())
        .build();

    testExplainDebug(simpleQuery, checker);
  }

  @Test
  public void testInsert() throws Exception {
    final int primaryKey = NUM_PAGES * 1024;
    final String simpleInsert = String.format("INSERT INTO %s VALUES (%d, %d, "
        + "%d, %d, %d, %d)", MAIN_TABLE, primaryKey, primaryKey, primaryKey % 1024,
        primaryKey, primaryKey, primaryKey % 128);

    PlanCheckerBuilder insertNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE);

    PlanCheckerBuilder resultNodeChecker = makePlanBuilder()
        .nodeType(NODE_RESULT)
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(1))
            .build());

    Checker checker = makeTopLevelBuilder()
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(1))
            .build())
        .plan(insertNodeChecker
            .plans(resultNodeChecker.build())
            .build())
        .build();

    testExplainDebug(simpleInsert, checker);
  }

  @Test
  public void testUpdate() throws Exception {
    final String simpleUpdate = String.format("UPDATE %s SET v1 = v1 + 1 "
        + "WHERE k = 0", MAIN_TABLE);

    PlanCheckerBuilder updateNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE);

    PlanCheckerBuilder resultNodeChecker = makePlanBuilder()
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(1))
            .build());

    Checker checker = makeTopLevelBuilder()
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(1))
            .build())
        .plan(updateNodeChecker
            .plans(resultNodeChecker.build())
            .build())
        .build();

    testExplainDebug(simpleUpdate, checker);
  }

  @Test
  public void testDelete() throws Exception {
    final String simpleDelete = String.format("DELETE FROM %s "
        + "WHERE k = 0", MAIN_TABLE);

    PlanCheckerBuilder deleteNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE);

    PlanCheckerBuilder resultNodeChecker = makePlanBuilder()
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(1))
            .build());

    Checker checker = makeTopLevelBuilder()
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(1))
            .build())
        .plan(deleteNodeChecker
            .plans(resultNodeChecker.build())
            .build())
        .build();

    testExplainDebug(simpleDelete, checker);
  }

  @Test
  public void testReadWrite() throws Exception {
    final String tableName = "test_table";
    try (Statement stmt = connection.createStatement()) {
        stmt.execute(String.format("CREATE TABLE %s (k INT, v INT, PRIMARY KEY (k ASC)) "
            + "SPLIT AT VALUES ((100))", tableName));
        stmt.execute(String.format("INSERT INTO %s VALUES (1, 1)", tableName));
    }

    final String readWriteQuery = String.format(
        "WITH cte AS (INSERT INTO %s VALUES (50, 50), (150, 150) RETURNING k)"
        + "SELECT * FROM %s",
        tableName, tableName);

    Checker checker = makeTopLevelBuilder()
        .readMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(2))
            .metric("docdb_keys_found", Checkers.equal(1))
            .build())
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(2))
            .build())
        .plan(makePlanBuilder()
            .nodeType(NODE_SEQ_SCAN)
            .plans(
                makePlanBuilder()
                    .nodeType(NODE_MODIFY_TABLE)
                    .plans(
                        makePlanBuilder()
                            .nodeType(NODE_VALUES_SCAN)
                            .build())
                    .build())
            .build())
        .build();
    testExplainDebug(readWriteQuery, checker);
  }
}
