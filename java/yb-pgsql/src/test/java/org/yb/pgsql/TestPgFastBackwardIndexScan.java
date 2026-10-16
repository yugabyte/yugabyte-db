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
import org.yb.pgsql.ExplainAnalyzeUtils.MetricsCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ValueChecker;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_LIMIT;
import static org.yb.pgsql.ExplainAnalyzeUtils.INDEX_SCAN_DIRECTION_BACKWARD;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplainDebug;

@RunWith(value=YBTestRunner.class)
public class TestPgFastBackwardIndexScan extends BasePgSQLTest {
  private static final String METRIC_NUM_DB_SEEK = "rocksdb_number_db_seek";
  private static final String METRIC_NUM_DB_NEXT = "rocksdb_number_db_next";
  private static final String METRIC_NUM_DB_PREV = "rocksdb_number_db_prev";
  private static final double SEEK_FAULT_TOLERANCE_OFFSET = 1;
  private static final double SEEK_FAULT_TOLERANCE_RATE = 0.1;
  private static final double SEEK_LOWER_BOUND_FACTOR = 1 - SEEK_FAULT_TOLERANCE_RATE;
  private static final double SEEK_UPPER_BOUND_FACTOR = 1 + SEEK_FAULT_TOLERANCE_RATE;
  private static final double NEXT_PREV_FAULT_TOLERANCE_OFFSET = 2;
  private static final double NEXT_PREV_FAULT_TOLERANCE_RATE = 0.1;
  private static final double NEXT_PREV_LOWER_BOUND_FACTOR = 1 - NEXT_PREV_FAULT_TOLERANCE_RATE;
  private static final double NEXT_PREV_UPPER_BOUND_FACTOR = 1 + NEXT_PREV_FAULT_TOLERANCE_RATE;

  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgFastBackwardIndexScan.class);

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  private static MetricsCheckerBuilder makeMetricsBuilder() {
    return JsonUtil.makeCheckerBuilder(MetricsCheckerBuilder.class, false);
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET yb_enable_base_scans_cost_model = true");
      // ANALYZE samples 300 * default_statistics_target rows. The test table has 90000 rows, so a
      // target of 300 makes the sample cover the whole table, which keeps the selectivity (and
      // hence the seek and prev estimates) deterministic instead of varying with the sample.
      stmt.execute("SET default_statistics_target = 300");
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
    flagMap.put("use_fast_backward_scan", "true");
    flagMap.put("ysql_enable_packed_row", "true");
    flagMap.put("ysql_enable_packed_row_for_colocated_table", "true");
    // Disable auto analyze to prevent query plan from changing.
    flagMap.put("ysql_enable_auto_analyze", "false");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("use_fast_backward_scan", "true");
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

  private ValueChecker<Double> expectedNextsOrPrevsRange(double expected_nexts_or_prevs) {
    double expected_lower_bound = expected_nexts_or_prevs * NEXT_PREV_LOWER_BOUND_FACTOR
        - NEXT_PREV_FAULT_TOLERANCE_OFFSET;
    double expected_upper_bound = expected_nexts_or_prevs * NEXT_PREV_UPPER_BOUND_FACTOR
        + NEXT_PREV_FAULT_TOLERANCE_OFFSET;
    return Checkers.closed(expected_lower_bound, expected_upper_bound);
  }

  private void testSeekAndNextEstimationIndexOnlyScanBackwardHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      double expected_seeks,
      double expected_prevs) throws Exception {
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
                  .estimatedNexts(expectedNextsOrPrevsRange(0))
                  .estimatedPrevs(expectedNextsOrPrevsRange(expected_prevs))
                  .readMetrics(makeMetricsBuilder()
                    .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
                    // EXPLAIN omits zero-valued metrics, so no nexts means the metric is absent.
                    .metric(METRIC_NUM_DB_NEXT, JsonUtil.absenceCheckerOnNull(null))
                    .metric(METRIC_NUM_DB_PREV, expectedNextsOrPrevsRange(expected_prevs))
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

  private void testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      double expected_seeks,
      double expected_prevs) throws Exception {
    try {
      /*
       * Planner doesn't know that LIMIT is getting pushed down to DocDB. This is why the estimated
       * seeks, nexts and prevs are not expected to match the actual values reported in EXPLAIN.
       */
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
                    .estimatedNexts(expectedNextsOrPrevsRange(0))
                    .estimatedPrevs(expectedNextsOrPrevsRange(expected_prevs))
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

      /* Index Scan node isn't aware of LIMIT on top, expects to return all 90k rows */
      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 DESC LIMIT 5000",
        "t1", "t1_idx_1", 89, 90000);
      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 DESC LIMIT 10000",
        "t1", "t1_idx_1", 89, 90000);

      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 < 100 ORDER BY k1 DESC",
        "t1", "t1_idx_1",30, 30000);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 > 200 ORDER BY k1 DESC",
        "t1", "t1_idx_1",30, 30000);

      /* When filter on k2, additional seeks are needed for skip scan */
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 < 100 ORDER BY k1 DESC",
        "t1", "t1_idx_1",330, 30000);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 > 200 ORDER BY k1 DESC",
        "t1", "t1_idx_1",330, 30000);

      stmt.execute("DROP INDEX t1_idx_1");

      stmt.execute("CREATE INDEX t1_idx_2 ON t1 (k1 DESC, k2 ASC)");
      stmt.execute("ANALYZE t1");

      /* Index Scan node isn't aware of LIMIT on top, expects to return all 90k rows */
      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 ASC LIMIT 5000",
        "t1", "t1_idx_2", 89, 90000);
      testSeekAndNextEstimationLimitIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 ORDER BY k1 ASC LIMIT 10000",
        "t1", "t1_idx_2", 89, 90000);

      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 < 100 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 30, 30000);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k1 > 200 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 30, 30000);

      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 < 100 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 330, 30000);
      testSeekAndNextEstimationIndexOnlyScanBackwardHelper(stmt,
        "SELECT k1, k2 FROM t1 WHERE k2 > 200 ORDER BY k1 ASC",
        "t1", "t1_idx_2", 330, 30000);
    }
  }
}
