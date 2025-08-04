package org.yb.pgsql;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Map;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ValueChecker;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_LIMIT;
import static org.yb.pgsql.ExplainAnalyzeUtils.INDEX_SCAN_DIRECTION_BACKWARD;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplainDebug;

@RunWith(value=YBTestRunner.class)
public class TestPgBackwardIndexScan extends BasePgSQLTest {

  private static final double SEEK_FAULT_TOLERANCE_OFFSET = 1;
  private static final double SEEK_FAULT_TOLERANCE_RATE = 0.2;
  private static final double SEEK_LOWER_BOUND_FACTOR = 1 - SEEK_FAULT_TOLERANCE_RATE;
  private static final double SEEK_UPPER_BOUND_FACTOR = 1 + SEEK_FAULT_TOLERANCE_RATE;
  private static final double NEXT_FAULT_TOLERANCE_OFFSET = 2;
  private static final double NEXT_FAULT_TOLERANCE_RATE = 0.5;
  private static final double NEXT_LOWER_BOUND_FACTOR = 1 - NEXT_FAULT_TOLERANCE_RATE;
  private static final double NEXT_UPPER_BOUND_FACTOR = 1 + NEXT_FAULT_TOLERANCE_RATE;

  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgBackwardIndexScan.class);

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET yb_enable_base_scans_cost_model = true");
    }
  }

  @After
  public void tearDown() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS test");
    }
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("use_fast_backward_scan", "false");
    flagMap.put("ysql_enable_packed_row_for_colocated_table", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("use_fast_backward_scan", "false");
    flagMap.put("ysql_enable_packed_row_for_colocated_table", "true");
    return flagMap;
  }

  private ValueChecker<Double> expectedSeeksRange(double expected_seeks) {
    double expected_lower_bound = expected_seeks * SEEK_LOWER_BOUND_FACTOR
        - SEEK_FAULT_TOLERANCE_OFFSET;
    double expected_upper_bound = expected_seeks * SEEK_UPPER_BOUND_FACTOR
        + SEEK_FAULT_TOLERANCE_OFFSET;
    return Checkers.closed(expected_lower_bound, expected_upper_bound);
  }

  private ValueChecker<Double> expectedNextsRange(double expected_nexts) {
    double expected_lower_bound = expected_nexts * NEXT_LOWER_BOUND_FACTOR
        - NEXT_FAULT_TOLERANCE_OFFSET;
    double expected_upper_bound = expected_nexts * NEXT_UPPER_BOUND_FACTOR
        + NEXT_FAULT_TOLERANCE_OFFSET;
    return Checkers.closed(expected_lower_bound, expected_upper_bound);
  }

  private void testSeekAndNextEstimationIndexOnlyScanBackwardHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      double expected_seeks,
      double expected_nexts) throws Exception {
    try {
      testExplainDebug(stmt,
          String.format("/*+ Set(enable_sort off) IndexOnlyScan(%s %s) */ %s",
              table_name, index_name, query),
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_INDEX_ONLY_SCAN)
                  .scanDirection(INDEX_SCAN_DIRECTION_BACKWARD)
                  .relationName(table_name)
                  .indexName(index_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      double expected_seeks,
      double expected_nexts) throws Exception {
    try {
      testExplainDebug(stmt,
          String.format("/*+ IndexOnlyScan(%s %s) */ %s", table_name, index_name, query),
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_LIMIT)
                  .plans(makePlanBuilder()
                    .nodeType(NODE_INDEX_ONLY_SCAN)
                    .scanDirection(INDEX_SCAN_DIRECTION_BACKWARD)
                    .relationName(table_name)
                    .indexName(index_name)
                    .estimatedSeeks(expectedSeeksRange(expected_seeks))
                    .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                    .build())
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  @Test
  public void testSeekNextEstimationIndexScan() throws Exception {
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.ROUND_ROBIN);
    boolean isConnMgr = isTestRunningWithConnectionManager();
    if (isConnMgr) {
      setUp();
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1 (k1 INT, k2 INT, v1 INT)");
      stmt.execute("INSERT INTO t1 SELECT s1, s2, s2 FROM "
        + "generate_series(1, 300) s1, generate_series(1, 300) s2");

      stmt.execute("CREATE INDEX t1_idx_1 ON t1 (k1 ASC, k2 ASC)");
      stmt.execute("ANALYZE t1");

      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 DESC LIMIT 5000",
        "t1", "t1_idx_1", 180000, 180000);
      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 DESC LIMIT 10000",
        "t1", "t1_idx_1", 180000, 180000);

      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 < 10 ORDER BY k1 DESC",
        "t1", "t1_idx_1", 4800, 4800);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 > 290 ORDER BY k1 DESC",
        "t1", "t1_idx_1", 6419, 6419);

      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 < 10 ORDER BY k1 DESC",
        "t1", "t1_idx_1", 4805, 5251);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 > 290 ORDER BY k1 DESC",
        "t1", "t1_idx_1", 6773, 6835);

      stmt.execute("DROP INDEX t1_idx_1");

      stmt.execute("CREATE INDEX t1_idx_2 ON t1 (k1 DESC, k2 ASC)");
      stmt.execute("ANALYZE t1");

      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 ASC LIMIT 5000",
        "t1", "t1_idx_2", 180000, 180000);
      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 ASC LIMIT 10000",
        "t1", "t1_idx_2", 180000, 180000);

      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 < 10 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 4800, 4800);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 > 290 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 6419, 6419);

      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 < 10 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 4805, 5251);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 > 290 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 6773, 6835);
    }
  }
}
