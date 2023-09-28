package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;

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
    TopLevelCheckerBuilder metric(String key, ValueChecker<Long> checker);
  }

  private interface PlanCheckerBuilder extends ObjectCheckerBuilder {
    PlanCheckerBuilder alias(String value);
    PlanCheckerBuilder metric(String key, ValueChecker<Long> checker);
    PlanCheckerBuilder nodeType(String value);
    PlanCheckerBuilder plans(Checker... checker);
    PlanCheckerBuilder relationName(String value);
  }

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  @Test
  public void testSeqScan() throws Exception {
    final String simpleQuery = String.format("SELECT * FROM %s", MAIN_TABLE);
    final String queryWithExpr = String.format("SELECT * FROM %s WHERE v5 < 128", MAIN_TABLE);

    PlanCheckerBuilder SEQ_SCAN_PLAN = makePlanBuilder()
        .nodeType(NODE_SEQ_SCAN)
        .relationName(MAIN_TABLE)
        .alias(MAIN_TABLE)
        .metric("rocksdb_number_db_seek", Checkers.greater(0))
        .metric("docdb_keys_found", Checkers.greater(0));

    Checker checker = makeTopLevelBuilder()
        .metric("rocksdb_number_db_seek", Checkers.greater(0))
        .metric("docdb_keys_found", Checkers.greater(0))
        .plan(SEQ_SCAN_PLAN.build())
        .build();

    testExplainDebug(simpleQuery, checker);
  }
}
