package org.yb.pgsql;

import static org.junit.Assume.assumeTrue;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_BITMAP_AND;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_BITMAP_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_BITMAP_OR;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_YB_BATCHED_NESTED_LOOP;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_YB_BITMAP_TABLE_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplainDebug;

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
import org.yb.client.TestUtils;
import org.yb.pgsql.ExplainAnalyzeUtils.MetricsCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;
import org.yb.util.json.ValueChecker;

@RunWith(value=YBTestRunner.class)
public class TestPgCostModelSeekNextEstimation extends BasePgSQLTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgCostModelSeekNextEstimation.class);

  private static final String DATABASE_NAME = "colocated_db";
  private static final String T1_NAME = "t1";
  private static final String T1_INDEX_NAME = T1_NAME + "_pkey";
  private static final String T2_NAME = "t2";
  private static final String T2_INDEX_NAME = T2_NAME + "_pkey";
  private static final String T3_NAME = "t3";
  private static final String T3_INDEX_NAME = T3_NAME + "_pkey";
  private static final String T4_NAME = "t4";
  private static final String T4_INDEX_NAME = T4_NAME + "_pkey";
  private static final String T2_NO_PKEY_NAME = "t2_no_pkey";
  private static final String T2_NO_PKEY_SINDEX_K1_NAME = "t2_i_k1";
  private static final String T2_NO_PKEY_SINDEX_K2_NAME = "t2_i_k2";
  private static final String T4_NO_PKEY_NAME = "t4_no_pkey";
  private static final String T4_NO_PKEY_SINDEX_3_NAME = "t4_i_3";
  private static final String T_NO_PKEY_NAME = "t_no_pkey";
  private static final String T_K1_INDEX_NAME = "t_k1_idx";
  private static final String T_K2_INDEX_NAME = "t_k2_idx";
  private static final double SEEK_FAULT_TOLERANCE_OFFSET = 1;
  private static final double SEEK_FAULT_TOLERANCE_RATE = 0.2;
  private static final double SEEK_LOWER_BOUND_FACTOR = 1 - SEEK_FAULT_TOLERANCE_RATE;
  private static final double SEEK_UPPER_BOUND_FACTOR = 1 + SEEK_FAULT_TOLERANCE_RATE;
  private static final double NEXT_FAULT_TOLERANCE_OFFSET = 2;
  private static final double NEXT_FAULT_TOLERANCE_RATE = 0.5;
  private static final double NEXT_LOWER_BOUND_FACTOR = 1 - NEXT_FAULT_TOLERANCE_RATE;
  private static final double NEXT_UPPER_BOUND_FACTOR = 1 + NEXT_FAULT_TOLERANCE_RATE;
  private static final String METRIC_NUM_DB_SEEK = "rocksdb_number_db_seek";
  private static final String METRIC_NUM_DB_NEXT = "rocksdb_number_db_next";

  private static final String T5_NAME = "t5";
  private static final String T5_K1_INDEX_NAME = "t5_k1_idx";
  private static final String T5_K2_INDEX_NAME = "t5_k2_idx";

  private Connection connection2;

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  private static MetricsCheckerBuilder makeMetricsBuilder() {
    return JsonUtil.makeCheckerBuilder(MetricsCheckerBuilder.class, false);
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

  // If expected_roundtrips==0, then the checker will verify the actual value
  // is exactly 0. If non-zero, then the actual value should be in the range 1 to
  // max Double (so the checker is only verifying the value is non-zero).
  private ValueChecker<Double> expectedRoundtripsRange(double expected_roundtrips) {

    ValueChecker<Double> checker;

    if (expected_roundtrips == 0)
    {
      checker = Checkers.closed(0.0d, 0.0d);
    }
    else
    {
      checker = Checkers.closed(1.0, Integer.MAX_VALUE);
    }

    return checker;
  }

  private void testSeekAndNextEstimationIndexScanHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      double expected_seeks,
      double expected_nexts,
      double expected_index_roundtrips,
      double expected_table_roundtrips,
      Integer expected_docdb_result_width) throws Exception {
    testSeekAndNextEstimationIndexScanHelper(stmt, query, table_name, index_name,
        NODE_INDEX_SCAN, expected_seeks, expected_nexts, expected_index_roundtrips,
        expected_table_roundtrips,expected_docdb_result_width);
  }

  private void testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(
    Statement stmt, String query,
    String table_name, String index_name,
    double expected_seeks,
    double expected_nexts,
    double expected_index_roundtrips,
    double expected_table_roundtrips,
    Integer expected_docdb_result_width) throws Exception {
  testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt, query, table_name, index_name,
      NODE_INDEX_SCAN, expected_seeks, expected_nexts, expected_index_roundtrips,
      expected_table_roundtrips, expected_docdb_result_width);
}

  private void testSeekAndNextEstimationIndexOnlyScanHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      double expected_seeks,
      double expected_nexts,
      double expected_index_roundtrips,
      double expected_table_roundtrips,
      Integer expected_docdb_result_width) throws Exception {
    testSeekAndNextEstimationIndexScanHelper(stmt, query, table_name, index_name,
        NODE_INDEX_ONLY_SCAN, expected_seeks, expected_nexts, expected_index_roundtrips,
        expected_table_roundtrips,expected_docdb_result_width);
  }

  private void testSeekAndNextEstimationIndexOnlyScanHelper_IgnoreActualResults(
    Statement stmt, String query,
    String table_name, String index_name,
    double expected_seeks,
    double expected_nexts,
    double expected_index_roundtrips,
    double expected_table_roundtrips,
    Integer expected_docdb_result_width) throws Exception {
  testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt, query, table_name, index_name,
      NODE_INDEX_ONLY_SCAN, expected_seeks, expected_nexts, expected_index_roundtrips,
      expected_table_roundtrips,expected_docdb_result_width);
}

  private void WarmupCatalogCache(Statement stmt, String query) throws Exception {
      testExplainDebug(stmt, query, null);
  }

  private void testSeekAndNextEstimationIndexScanHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      String node_type,
      double expected_seeks,
      double expected_nexts,
      double expected_index_roundtrips,
      double expected_table_roundtrips,
      Integer expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(node_type)
                  .relationName(table_name)
                  .indexName(index_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                  .estimatedIndexRoundtrips(expectedRoundtripsRange(expected_index_roundtrips))
                  .estimatedTableRoundtrips(expectedRoundtripsRange(expected_table_roundtrips))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .readMetrics(makeMetricsBuilder()
                    .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
                    .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
                    .build())
                  .totalCost(Checkers.greater(0))
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private ObjectChecker makeBitmapIndexScanChecker(
      String index_name,
      double expected_seeks,
      double expected_nexts) {
    return makePlanBuilder()
      .nodeType(NODE_BITMAP_INDEX_SCAN)
      .indexName(index_name)
      .estimatedSeeks(expectedSeeksRange(expected_seeks))
      .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
      .readMetrics(makeMetricsBuilder()
        .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
        .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
        .build())
      .build();
  }

  private ObjectChecker makeBitmapIndexScanChecker_IgnoreActualResults(
      String index_name,
      double expected_seeks,
      double expected_nexts,
      double expected_index_roundtrips) {
    return makePlanBuilder()
      .nodeType(NODE_BITMAP_INDEX_SCAN)
      .indexName(index_name)
      .estimatedSeeks(expectedSeeksRange(expected_seeks))
      .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
      .estimatedTableRoundtrips(JsonUtil.absenceCheckerOnNull(null))
      .estimatedIndexRoundtrips(expectedRoundtripsRange(expected_index_roundtrips))
      .build();
  }

  private void testSeekAndNextEstimationBitmapScanHelper(
      Statement stmt, String query,
      String table_name,
      double expected_seeks,
      double expected_nexts,
      Integer expected_docdb_result_width,
      ObjectChecker bitmap_index_checker) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
                  .relationName(table_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
// TODO(#28919): Fix cost model to take into account changes in DocDB seek/next behaviour made by
// https://github.com/yugabyte/yugabyte-db/issues/28616 and uncomment this line:
//                   .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .readMetrics(makeMetricsBuilder()
                    .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
                    .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
                    .build())
                  .plans(bitmap_index_checker)
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(
      Statement stmt, String query,
      String table_name,
      double expected_seeks,
      double expected_nexts,
      Integer expected_docdb_result_width,
      ObjectChecker bitmap_index_checker) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_YB_BITMAP_TABLE_SCAN)
                  .relationName(table_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .totalCost(Checkers.greater(0))
                  .plans(bitmap_index_checker)
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(
      Statement stmt, String query,
      String table_name, String index_name,
      String node_type,
      double expected_seeks,
      double expected_nexts,
      double expected_index_roundtrips,
      double expected_table_roundtrips,
      Integer expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(node_type)
                  .relationName(table_name)
                  .indexName(index_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                  .estimatedIndexRoundtrips(expectedRoundtripsRange(expected_index_roundtrips))
                  .estimatedTableRoundtrips(expectedRoundtripsRange(expected_table_roundtrips))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .totalCost(Checkers.greater(0))
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testSeekAndNextEstimationSeqScanHelper(
      Statement stmt, String query,
      String table_name, double expected_seeks,
      double expected_nexts,
      double expected_table_roundtrips,
      long expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_SEQ_SCAN)
                  .relationName(table_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                  .estimatedIndexRoundtrips(expectedRoundtripsRange(0))
                  .estimatedTableRoundtrips(expectedRoundtripsRange(expected_table_roundtrips))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .readMetrics(makeMetricsBuilder()
                    .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
                    .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
                    .build())
                  .totalCost(Checkers.greater(0))
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testSeekAndNextEstimationSeqScanHelper_IgnoreActualResults(
      Statement stmt, String query,
      String table_name, double expected_seeks,
      double expected_nexts,
      double expected_table_roundtrips,
      long expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_SEQ_SCAN)
                  .relationName(table_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNextsAndPrevs(expectedNextsRange(expected_nexts))
                  .estimatedTableRoundtrips(expectedRoundtripsRange(expected_table_roundtrips))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .totalCost(Checkers.greater(0))
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testSeekAndNextEstimationJoinHelper_IgnoreActualResults(
      Statement stmt, String query,
      String join_type,
      String outer_table_name, String outer_table_scan_type,
      double outer_expected_seeks, double outer_expected_nexts,
      String inner_table_name, String inner_table_scan_type,
      double inner_expected_seeks, double inner_expected_nexts) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(join_type)
                  .plans(
                      makePlanBuilder()
                          .relationName(outer_table_name)
                          .nodeType(outer_table_scan_type)
                          .estimatedSeeks(expectedSeeksRange(outer_expected_seeks))
                          .estimatedNextsAndPrevs(expectedNextsRange(outer_expected_nexts))
                          .totalCost(Checkers.greater(0))
                          .build(),
                      makePlanBuilder()
                          .relationName(inner_table_name)
                          .nodeType(inner_table_scan_type)
                          .estimatedSeeks(expectedSeeksRange(inner_expected_seeks))
                          .estimatedNextsAndPrevs(expectedNextsRange(inner_expected_nexts))
                          .totalCost(Checkers.greater(0))
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

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s WITH COLOCATION = true", DATABASE_NAME));
    }
    // If --ysql_enable_relcache_init_optimization=false, then connection2 is the first
    // connection to the test database and it needs to take care of buliding the relinit
    // file. As part of that it will preload a set of catalog tables.
    // If --ysql_enable_relcache_init_optimization=true, then connection2 will be
    // the second connection to the test database because there will be an internal
    // connection comes first which takes care of building the relinit file. In this
    // case connection2 will only preload a few core catalog tables.
    // Because the test result depends upon some catalog caches beyond the core catalog
    // tables being preloaded, we use catalog cache warmup to make up the difference
    // (see WarmupCatalogCache).
    Connection tmpConnection = getConnectionBuilder().withDatabase(DATABASE_NAME).connect();

    this.connection2 = getConnectionBuilder().withDatabase(DATABASE_NAME).connect();

    try (Statement stmt = connection2.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, PRIMARY KEY (k1 ASC))", T1_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1 FROM generate_series(1, 20) s1",
                                 T1_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, PRIMARY KEY (k1 ASC, k2 "
                                 + "ASC))", T2_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2 FROM generate_series(1, 20) s1, "
                                 + "generate_series(1, 20) s2", T2_NAME));
      stmt.execute(String.format("CREATE STATISTICS %s_stx ON k1, k2 FROM %s", T2_NAME, T2_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, k3 INT,"
                                 + "PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC))", T3_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2, s3 FROM "
        + "generate_series(1, 20) s1, generate_series(1, 20) s2, generate_series(1, 20) s3"
                                 , T3_NAME));
      stmt.execute(String.format("CREATE STATISTICS %s_stx ON k1, k2, k3 FROM %s",
        T3_NAME, T3_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, k3 INT, k4 INT, "
        + "PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC, k4 ASC))", T4_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2, s3, s4 FROM "
        + "generate_series(1, 20) s1, generate_series(1, 20) s2, generate_series(1, 20) s3, "
        + "generate_series(1, 20) s4", T4_NAME));
      stmt.execute(String.format("CREATE STATISTICS %s_stx ON k1, k2, k3, k4 FROM %s",
        T4_NAME, T4_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT)",
        T2_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE INDEX %s on %s (k1 ASC)",
        T2_NO_PKEY_SINDEX_K1_NAME, T2_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE INDEX %s on %s (k2 ASC)",
        T2_NO_PKEY_SINDEX_K2_NAME, T2_NO_PKEY_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2 FROM "
        + "generate_series(1, 20) s1, generate_series(1, 20) s2", T2_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE STATISTICS %s_stx ON k1, k2 FROM %s",
        T2_NO_PKEY_NAME, T2_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, k3 INT, k4 INT)",
        T4_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE INDEX %s on %s (k1 ASC, k2 ASC, k3 ASC)",
        T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2, s3, s4 FROM "
        + "generate_series(1, 20) s1, generate_series(1, 20) s2, generate_series(1, 20) s3, "
        + "generate_series(1, 20) s4", T4_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE STATISTICS %s_stx ON k1, k2, k3, k4 FROM %s",
        T4_NO_PKEY_NAME, T4_NO_PKEY_NAME));
      // Create a non-colocated table.
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT) "
        + "WITH (colocation = false)", T5_NAME));
      stmt.execute(String.format("CREATE INDEX %s on %s (k1 ASC)",
        T5_K1_INDEX_NAME, T5_NAME));
      stmt.execute(String.format("CREATE INDEX %s on %s (k2 ASC)",
        T5_K2_INDEX_NAME, T5_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT k1, k2 FROM %s",
        T5_NAME, T2_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE STATISTICS %s_stx ON k1, k2 FROM %s",
        T5_NAME, T5_NAME));
      stmt.execute(String.format("ANALYZE %s, %s, %s, %s, %s, %s, %s;",
        T1_NAME, T2_NAME, T3_NAME, T4_NAME, T4_NO_PKEY_NAME, T2_NO_PKEY_NAME, T5_NAME));
      stmt.execute("SET yb_enable_optimizer_statistics = true");
      stmt.execute("SET yb_enable_base_scans_cost_model = true");
      stmt.execute("SET yb_bnl_batch_size = 1024");
      stmt.execute("SET enable_bitmapscan = true");
      stmt.execute("SET yb_enable_bitmapscan = true");
    }
  }

  @After
  public void tearDown() throws Exception {
    if (connection2 != null && !connection2.isClosed()) {
      connection2.close();
    }
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_analyze_dump_metrics", "true");
    flagMap.put("ysql_enable_packed_row", "true");
    flagMap.put("ysql_enable_packed_row_for_colocated_table", "true");
    // Disable auto analyze for CBO seek and next metrics test.
    flagMap.put("ysql_enable_auto_analyze", "false");
    flagMap.put("vmodule", "transaction=2");
    return flagMap;
  }

  // This test is to test our cost-model estimated num of seeks and nexts.
  // If this test fails due to the difference between expected num and actual num,
  // then there might be some new non-trivial changes in DocDB.
  // In this case, we should update our cost model to reflect DocDB changes.
  // If this test fails due to the difference between expected num and estimated num,
  // then there might be a regression introduced in cost model estimations.
  // Testcases are from TAQO seek-next-estimation workload in the order of simple_in, complex_in,
  // first_col_range, last_col_range, complex_range, complex_mix.
  // Extra tests are added as well.
  // Expected seeks and nexts are hardcoded values based on DocDB actual seek and next counts
  // in Nov/2023.
  @Test
  public void testSeekNextEstimationIndexScan() throws Exception {
    // (DB-12674) Allow tests to run in round-robin allocation mode when
    // using a pool of warmed up connections to allow for deterministic results.
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.ROUND_ROBIN);
    boolean isConnMgr = isTestRunningWithConnectionManager();
    if (isConnMgr) {
      setUp();
    }
    try (Statement stmt = this.connection2.createStatement()) {
      String query = String.format("/*+IndexScan(%s)*/ SELECT * "
          + "FROM %s WHERE k1 IN (4, 8)", T1_NAME, T1_NAME);
      WarmupCatalogCache(stmt, query);
      testExplainDebug(stmt, query, null);
      // Warmup all backends in round-robin mode when Connection Manager is enabled.
      if (isConnMgrWarmupRoundRobinMode()) {
        for (int i = 0; i < CONN_MGR_WARMUP_BACKEND_COUNT - 1; i++) {
          WarmupCatalogCache(stmt, query);
        }
      }
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 2, 4, 1, 0, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 3, 7, 1, 0, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 4, 10, 1, 0, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 4, 86, 1, 0, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 101, 280, 1, 0, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 6007, 16808, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 6505, 17804, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 22, 6440, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 1, 10, 1, 0, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 1, 200, 1, 0, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 5, 4000, 1, 0, 15);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 79, 80000, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 41, 280, 1, 0, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 804, 5600, 1, 0, 15);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 16079, 112000, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 879, 81600, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 120, 80000, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 440, 40800, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 5, 1606, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 5, 1606, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 2002, 4000, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 5, 8, 1, 0, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 10, 18, 1, 0, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 50, 98, 1, 0, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16)", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 301, 600, 1, 0, 15);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 4844, 9680, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 35, 32037, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 129, 115744, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 76, 68084, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 59, 40077, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 100, 68132, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 22, 6436, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 453, 116392, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 440, 40839, 1, 0, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 747, 69426, 1, 0, 20);

      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format("/*+IndexOnlyScan(%s %s)*/ SELECT k2 FROM %s "
          + "WHERE k1 >= 4 AND k3 >= 4",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 452, 116394, 1, 0, 5);
      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format("/*+IndexOnlyScan(%s %s)*/ SELECT k2 FROM %s "
          + "WHERE k1 >= 4 AND k3 = 4",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 686, 8168, 1, 0, 5);
      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format("/*+IndexOnlyScan(%s %s)*/ SELECT k2 FROM %s "
          + "WHERE k1 >= 4 AND k3 IN (4, 8, 12)",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 1379, 23141, 1, 0, 5);
      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format("/*+IndexOnlyScan(%s %s)*/ SELECT k2 FROM %s "
          + "WHERE k1 = 4 AND k3 IN (4, 8, 12)",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 81, 1363, 1, 0, 5);
      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format("/*+IndexOnlyScan(%s %s)*/ SELECT k2 FROM %s "
          + "WHERE k1 IN (4, 8, 12) AND k3 IN (4, 8, 12)",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 246, 4091, 1, 0, 5);
      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format("/*+IndexOnlyScan(%s %s)*/ SELECT k2 FROM %s "
          + "WHERE k1 IN (4, 8, 12) AND k3 >= 4",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 82, 20547, 1, 0, 5);
      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format("/*+IndexOnlyScan(%s %s)*/ SELECT k2 FROM %s "
          + "WHERE k1 IN (4, 8, 12) AND k3 = 4",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 124, 1389, 1, 0, 5);
      testSeekAndNextEstimationIndexOnlyScanHelper(stmt,
        String.format(" SELECT k2 FROM %s "
          + "WHERE k1 = 4 AND k3 = 4",
          T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, T4_NO_PKEY_NAME),
        T4_NO_PKEY_NAME, T4_NO_PKEY_SINDEX_3_NAME, 40, 482, 1, 0, 5);

      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        String.format("/*+IndexScan(%s %s)*/ SELECT * FROM %s WHERE k1 = 4",
          T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K1_NAME, T2_NO_PKEY_NAME),
        T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K1_NAME, 21, 20, 0, 1, 10);
        testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        String.format("/*+IndexScan(%s %s)*/ SELECT * FROM %s WHERE k1 >= 4",
          T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K1_NAME, T2_NO_PKEY_NAME),
        T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K1_NAME, 341, 340, 0, 1, 10);
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        String.format("/*+IndexScan(%s %s)*/ SELECT * FROM %s WHERE k1 IN (4, 8, 12)",
          T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K1_NAME, T2_NO_PKEY_NAME),
        T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K1_NAME, 63, 63, 0, 1, 10);
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        String.format("/*+IndexScan(%s %s)*/ SELECT * FROM %s WHERE k2 = 4",
          T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K2_NAME, T2_NO_PKEY_NAME),
        T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K2_NAME, 21, 20, 0, 1, 10);
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        String.format("/*+IndexScan(%s %s)*/ SELECT * FROM %s WHERE k2 >= 4",
          T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K2_NAME, T2_NO_PKEY_NAME),
        T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K2_NAME, 341, 340, 0, 1, 10);
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        String.format("/*+IndexScan(%s %s)*/ SELECT * FROM %s WHERE k2 IN (4, 8, 12)",
          T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K2_NAME, T2_NO_PKEY_NAME),
        T2_NO_PKEY_NAME, T2_NO_PKEY_SINDEX_K2_NAME, 63, 63, 0, 1, 10);
      // Try a non-colocated table with a secondary index.
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        String.format("/*+IndexScan(%s %s)*/ SELECT * FROM %s "
        + "WHERE k1 < 10 /* t5 query 1 */", T5_NAME, T5_K1_INDEX_NAME, T5_NAME),
        T5_NAME, T5_K1_INDEX_NAME, 93, 450, 1, 1, 10);
    }
  }

  @Test
  public void testSeekNextEstimationBitmapScan() throws Exception {
    assumeTrue("BitmapScan has much fewer nexts in fastdebug (#22052)", TestUtils.isReleaseBuild());

    // (DB-12674) Allow tests to run in round-robin allocation mode when
    // using a pool of warmed up connections to allow for deterministic results.
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.ROUND_ROBIN);
    boolean isConnMgr = isTestRunningWithConnectionManager();
    if (isConnMgr) {
      setUp();
    }
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute("SET work_mem TO '1GB'"); /* avoid getting close to work_mem */
      String query = String.format("/*+BitmapScan(%s)*/ SELECT * "
          + "FROM %s WHERE k1 IN (4, 8)", T1_NAME, T1_NAME);
      WarmupCatalogCache(stmt, query);
      // Warmup all backends in round-robin mode when Connection Manager is enabled.
      if (isConnMgrWarmupRoundRobinMode()) {
        for (int i = 0; i < CONN_MGR_WARMUP_BACKEND_COUNT - 1; i++) {
          WarmupCatalogCache(stmt, query);
        }
      }
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8)", T1_NAME, T1_NAME),
        T1_NAME, 2, 4, 5, makeBitmapIndexScanChecker(T1_INDEX_NAME, 2, 4));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12)", T1_NAME, T1_NAME),
        T1_NAME, 3, 7, 5, makeBitmapIndexScanChecker(T1_INDEX_NAME, 3, 7));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T1_NAME, T1_NAME),
        T1_NAME, 4, 10, 5, makeBitmapIndexScanChecker(T1_INDEX_NAME, 4, 10));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 80, 100, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 4, 86));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 80, 100, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 101, 280));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 4800, 4900, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 6007, 16808));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 4800, 4900, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 6505, 17804));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 6400, 6540, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 22, 6436));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, 10, 30, 5, makeBitmapIndexScanChecker(T1_INDEX_NAME, 1, 10));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, 200, 220, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 1, 200));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4000, 4080, 15, makeBitmapIndexScanChecker(T3_INDEX_NAME, 4, 4000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 81580, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 79, 80000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, 200, 220, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 41, 280));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4000, 4080, 15, makeBitmapIndexScanChecker(T3_INDEX_NAME, 804, 5600));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 81580, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 16079, 112000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 81580, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 879, 81600));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 81580, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 120, 80000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40000, 40800, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 440, 40800));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 1600, 1640, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 5, 1606));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, 1600, 1640, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 5, 1606));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, 1600, 1640, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 2002, 4000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 4, 12, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 5, 8));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 8, 24, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 10, 18));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 40, 120, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 50, 98));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16)", T3_NAME, T3_NAME),
        T3_NAME, 240, 260, 15, makeBitmapIndexScanChecker(T3_INDEX_NAME, 301, 600));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 3600, 3680, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 4844, 9680));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 32000, 32640, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 35, 32037));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 68000, 69340, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 76, 68084));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40000, 40802, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 59, 40077));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 68000, 69340, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 100, 68132));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 6400, 6450, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 22, 6436));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40000, 40800, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 440, 40839));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 68000, 69340, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 747, 69426));
      stmt.execute("RESET work_mem");
    }
  }

  @Test
  public void testSeekNextEstimationBitmapScanWithOr() throws Exception {
    try (Statement stmt = this.connection2.createStatement()) {
      final String query = "/*+ BitmapScan(t) */ SELECT * FROM %s AS t WHERE %s";
      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T2_NAME, "k1 IN (4, 8, 12) OR k2 IN (4, 8, 12)"),
        T2_NAME, 111, 333, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 3, 68, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 80, 162, 1)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T2_NAME, "k1 < 2 OR k2 < 4"),
        T2_NAME, 77, 231, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 1, 22, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 20, 102, 1)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 IN (4, 8, 12) OR k2 IN (4, 8, 12)"),
        T4_NAME, 44400, 133200, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 10, 24000, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 86, 24000, 1)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 IN (4, 8, 12) OR k3 IN (4, 8, 12)"),
        T4_NAME, 44400, 133200, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 10, 24000, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 1600, 24000, 1)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 IN (4, 8, 12) OR k4 IN (4, 8, 12)"),
        T4_NAME, 44400, 133200, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 10, 24000, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 32000, 88250, 1)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 < 2 OR k2 < 4"),
        T4_NAME, 30800, 92400, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 2, 8000, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 20, 24000, 1)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 < 2 OR k3 < 4"),
        T4_NAME, 30800, 92400, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 2, 8000, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 400, 24000, 1)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 < 2 OR k4 < 4"),
        T4_NAME, 30800, 92400, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 2, 8000, 1),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 8000, 40000, 1)).build());

      // Secondary index on both colocated and non-colocated table.
      // Use smaller fetch size to see effects of the pagination estimates.
      stmt.execute("set yb_fetch_row_limit = 0");
      stmt.execute("set yb_fetch_size_limit = 512");

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T2_NO_PKEY_NAME, "k1 <= 4 OR k2 IN (1, 5, 10, 15, 20)"),
        T2_NO_PKEY_NAME, 160, 480, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_NO_PKEY_SINDEX_K1_NAME,
                                                         8, 80, 8),
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_NO_PKEY_SINDEX_K2_NAME,
                                                         14, 105, 10)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T5_NAME, "k1 <= 4 OR k2 IN (1, 5, 10, 15, 20)"),
        T5_NAME, 160, 480, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T5_K1_INDEX_NAME,
                                                         8, 80, 8),
          makeBitmapIndexScanChecker_IgnoreActualResults(T5_K2_INDEX_NAME,
                                                         14, 105, 10)).build());
    }
  }

  // There's a middle ground for queries with AND. If few rows are to be
  // selected, then it's cheap to apply remote filters. If a lot of rows are to
  // be selected, then it's pointless to gather a large portion of the table for
  // each Bitmap Index Scan under a BitmapAnd.
  @Test
  public void testSeekNextEstimationBitmapScanWithAnd() throws Exception {
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT)", T_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE INDEX %s ON %s (k1 ASC)",
                                 T_K1_INDEX_NAME, T_NO_PKEY_NAME));
      stmt.execute(String.format("CREATE INDEX %s ON %s (k2 ASC)",
                                 T_K2_INDEX_NAME, T_NO_PKEY_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2 FROM generate_series(1, 100) s1, "
      + "generate_series(1, 100) s2", T_NO_PKEY_NAME));
      stmt.execute(String.format("ANALYZE %s", T_NO_PKEY_NAME));

      final String query = "/*+ BitmapScan(t) */ SELECT * FROM %s AS t WHERE %s";
      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 1 AND k2 <= 1"),
        T_NO_PKEY_NAME, 100, 120, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_INDEX_SCAN).build());

      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 2 AND k2 <= 2"),
        T_NO_PKEY_NAME, 200, 220, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_INDEX_SCAN).build());

      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 5 AND k2 <= 5"),
        T_NO_PKEY_NAME, 25, 75, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_AND).build());

      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 10 AND k2 <= 10"),
        T_NO_PKEY_NAME, 100, 120, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_AND).build());

      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 20 AND k2 <= 20"),
        T_NO_PKEY_NAME, 400, 420, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_AND).build());

      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 40 AND k2 <= 40"),
        T_NO_PKEY_NAME, 1600, 1640, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_AND).build());

      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 80 AND k2 <= 80"),
        T_NO_PKEY_NAME, 8000, 8160, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_INDEX_SCAN).build());

      // If the two sets of ybctids are not similar sizes, it doesn't make sense
      // to collect both sets. Instead, we collect the smaller and filter out
      // the larger.
      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 20 AND k2 <= 40"),
        T_NO_PKEY_NAME, 2000, 2040, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_INDEX_SCAN).indexName(T_K1_INDEX_NAME).build());

      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, T_NO_PKEY_NAME, "k1 <= 20 AND k2 <= 10"),
        T_NO_PKEY_NAME, 1000, 1020, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_INDEX_SCAN).indexName(T_K2_INDEX_NAME).build());
    }
  }

  /* These scans exceed work_mem, so their YB Bitmap Table Scan just performs a
   * sequential scan. The seeks and nexts of the Table Scan should be equivalent
   * to the seeks and nexts of the Sequential Scan.
   */
  @Test
  public void testSeekNextEstimationBitmapScanExceedingWorkMem() throws Exception {
    try (Statement stmt = this.connection2.createStatement()) {
      final double estimated_seeks = 114;
      final double estimated_nexts = 160112;
      final String query = "/*+ %s(t) */ SELECT * FROM %s AS t WHERE %s >= 4 AND %s >= 4";
      testSeekAndNextEstimationSeqScanHelper(stmt,
        String.format(query, "SeqScan", T4_NAME, "k1", "k2"),
        T4_NAME, estimated_seeks, estimated_nexts, 1, 20);
      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, "BitmapScan", T4_NAME, "k1", "k2"),
        T4_NAME, estimated_seeks, estimated_nexts, 20,
        makeBitmapIndexScanChecker(T4_INDEX_NAME, 129, 115744));

      testSeekAndNextEstimationSeqScanHelper(stmt,
        String.format(query, "SeqScan", T4_NAME, "k1", "k3"),
        T4_NAME, estimated_seeks, estimated_nexts, 1, 20);
      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, "BitmapScan", T4_NAME, "k1", "k3"),
        T4_NAME, estimated_seeks, estimated_nexts, 20,
        makeBitmapIndexScanChecker(T4_INDEX_NAME, 453, 116392));

      testSeekAndNextEstimationSeqScanHelper(stmt,
        String.format(query, "SeqScan", T4_NAME, "k1", "k4"),
        T4_NAME, estimated_seeks, estimated_nexts, 1, 20);
      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, "BitmapScan", T4_NAME, "k1", "k4"),
        T4_NAME, estimated_seeks, estimated_nexts, 20,
        makeBitmapIndexScanChecker(T4_INDEX_NAME, 6913, 129000));
    }
  }

  @Test
  public void testSeekNextEstimationSeqScan() throws Exception {
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute(String.format("SET enable_indexscan=off"));
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19, 1, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19, 1, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19, 1, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 1, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 1, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 5, 160003, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 5, 160003, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 7, 160005, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, 1, 19, 1, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 1, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4, 8002, 1, 15);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 1, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4, 8002, 1, 15);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 2, 160000, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, 2, 160000, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, 2, 160000, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 1, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 1, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 1, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16)", T3_NAME, T3_NAME),
        T3_NAME, 2, 8000, 1, 15);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 4, 160002, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 32, 160029, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 114, 160112, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 5, 160003, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 114, 160112, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038, 1, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065, 1, 20);
    }
  }

  @Test
  public void testSeekNextEstimationStorageIndexFilters() throws Exception {
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute("CREATE TABLE test (k1 INT, v1 INT)");
      stmt.execute("CREATE INDEX test_index_k1 ON test (k1 ASC)");
      stmt.execute("CREATE INDEX test_index_k1_v1 ON test (k1 ASC) INCLUDE (v1)");
      stmt.execute("INSERT INTO test (SELECT s, s FROM generate_series(1, 100000) s)");
      stmt.execute("ANALYZE test");

      /* All rows matching the filter on k1 will be seeked in the base table, and the filter on v1
       * will be applied on the base table.
       */
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+IndexScan(test test_index_k1) */ SELECT * FROM test WHERE k1 > 50000 and v1 > 80000",
        "test", "test_index_k1", 50000, 50000, 0, 10, 10);

      /* The filter on v1 will be executed on the included column in test_index_k1_v1. As a result,
       * fewer seeks will be needed on the base table.
       */
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+IndexScan(test test_index_k1_v1) */ SELECT * FROM test WHERE k1 > 50000 and v1 > 80000",
        "test", "test_index_k1_v1", 10000, 50000, 0, 10, 10);
    }
  }

  @Test
  public void testSeekNextEstimationSeekForwardOptimization() throws Exception {
    try (Statement stmt = this.connection2.createStatement()) {
      /*
       * Seek Forward Optimization is a technique used in DocDB to avoid
       * expensive seeks. Before seeking for the next key, we try to find the
       * it in the subsequent few keys by performing cheaper next operations.
       * Only if the key is not found, we seek to it. The number of nexts
       * performed is determined by the max_nexts_to_avoid_seek yb-master flag.
       *
       * In the cost model, we assume that the seek forward optimization does
       * not yield any benefits. We may be able to improve this in the future.
       * Github issue #25902 tracks this enhancement.
       *
       * For each of the following queries, the cost model estimates the same
       * number of seeks and nexts, but actual seeks are much less if the
       * keys are close to one another. When the keys are far apart, the
       * estimated seeks and nexts match the actual seeks and nexts.
       */
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+IndexScan(t4)*/ SELECT * FROM t4 WHERE k2 IN (4, 5, 6, 7)",
        T4_NAME, T4_INDEX_NAME, 132, 32200, 1, 0, 20);

      testSeekAndNextEstimationIndexScanHelper(stmt,
        "/*+IndexScan(t4)*/ SELECT * FROM t4 WHERE k2 IN (4, 6, 8, 10)",
        T4_NAME, T4_INDEX_NAME, 132, 32200, 1, 0, 20);

      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+IndexScan(t4)*/ SELECT * FROM t4 WHERE k4 IN (4, 5, 6, 7)",
        T4_NAME, T4_INDEX_NAME, 40031, 80000, 1, 0, 20);

      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+IndexScan(t4)*/ SELECT * FROM t4 WHERE k4 IN (4, 6, 8, 10)",
        T4_NAME, T4_INDEX_NAME, 40031, 80000, 1, 0, 20);

      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+IndexScan(t4)*/ SELECT * FROM t4 WHERE k4 IN (4, 7, 10, 13)",
        T4_NAME, T4_INDEX_NAME, 40031, 80000, 1, 0, 20);

      testSeekAndNextEstimationIndexScanHelper(stmt,
        "/*+IndexScan(t4)*/ SELECT * FROM t4 WHERE k4 IN (4, 8, 12, 16)",
        T4_NAME, T4_INDEX_NAME, 40031, 80000, 1, 0, 20);
    }
  }

  @Test
  public void testSeekNextEstimation25862IntegerOverflow() throws Exception {
    /*
     * #25862 : Estimated seeks and nexts value can overflow in a large table
     *
     * This test case checks that estimated seeks and nexts values do not overflow
     * when the table has a large number of rows.
     */
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute("CREATE TABLE t_25862 (k1 INT, v1 INT, PRIMARY KEY (k1 ASC))");
      stmt.execute("CREATE INDEX t_25862_idx on t_25862 (v1 ASC)");
      /* Simluate a large table by setting reltuples in pg_class to 4B rows. */
      stmt.execute("SET yb_non_ddl_txn_for_sys_tables_allowed = ON");
      stmt.execute("UPDATE pg_class SET reltuples=4000000000 WHERE relname LIKE '%t_25862%'");
      stmt.execute("UPDATE pg_yb_catalog_version SET current_version=current_version+1 "
        + "WHERE db_oid=1");
      stmt.execute("SET yb_non_ddl_txn_for_sys_tables_allowed = OFF");

      stmt.execute("SET yb_enable_base_scans_cost_model = ON");

      testSeekAndNextEstimationSeqScanHelper_IgnoreActualResults(stmt,
        "/*+ SeqScan(t_25862) */ SELECT * FROM t_25862 WHERE k1 > 0",
        "t_25862", 1302084.0, 4001302082.0, 1, 2);
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+ IndexScan(t_25862 t_25862_pkey) */ SELECT * FROM t_25862 WHERE k1 > 0",
        "t_25862", "t_25862_pkey", 1302084.0, 1333333334.0, 1, 0, 2);
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+ IndexScan(t_25862 t_25862_idx) */ SELECT * FROM t_25862 WHERE v1 > 0",
        "t_25862", "t_25862_idx", 1334635417.0, 1333333334.0, 0, 1302084.0, 2);
      testSeekAndNextEstimationIndexOnlyScanHelper_IgnoreActualResults(stmt,
        "/*+ IndexOnlyScan(t_25862 t_25862_idx) */ SELECT v1 FROM t_25862 WHERE v1 > 0",
        "t_25862", "t_25862_idx", 1302084.0, 1333333334.0, 1, 0, 1);
    }
  }

  @Test
  public void test26462SeekNextEstimationForInnerTableInBNL() throws Exception {
    /*
     * #26462 : Seeks and nexts are misestimated for Batched Nested Loop Joins
     *
     * This test checks the seek and next estimations for inner tables in
     * Batched Nested Loop Join.
     */
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute("CREATE TABLE t1_26462 (k1 INT, k2 INT, PRIMARY KEY (k1 ASC))");
      stmt.execute("CREATE INDEX t1_26462_idx ON t1_26462 (k1 ASC, k2 ASC)");
      stmt.execute("CREATE TABLE t2_26462 (k1 INT, k2 INT, PRIMARY KEY (k1 ASC))");
      stmt.execute("CREATE INDEX t2_26462_idx ON t2_26462 (k1 ASC, k2 ASC)");
      stmt.execute("INSERT INTO t1_26462 (SELECT s, s FROM generate_series(1, 10000) s)");
      stmt.execute("INSERT INTO t2_26462 (SELECT s, s FROM generate_series(1, 10000) s)");
      stmt.execute("ANALYZE t1_26462, t2_26462");

      stmt.execute("SET yb_enable_base_scans_cost_model=on");

      testSeekAndNextEstimationJoinHelper_IgnoreActualResults(stmt,
        "/*+ YbBatchedNL(t1_26462 t2_26462) */ SELECT * "
        + "FROM t1_26462 JOIN t2_26462 ON t1_26462.k1 = t2_26462.k1;",
        NODE_YB_BATCHED_NESTED_LOOP,
        "t1_26462", NODE_INDEX_SCAN, 10.0, 10000.0,
        "t2_26462", NODE_INDEX_SCAN, 1024.0, 2048.0);

      testSeekAndNextEstimationJoinHelper_IgnoreActualResults(stmt,
        "/*+ YbBatchedNL(t1_26462 t2_26462) IndexScan(t1_26462 t1_26462_idx) "
        + "IndexScan(t2_26462 t2_26462_idx) */ SELECT * FROM t1_26462 JOIN t2_26462 "
        + "ON t1_26462.k1 = t2_26462.k1 AND t1_26462.k2 = t2_26462.k2;",
        NODE_YB_BATCHED_NESTED_LOOP,
        "t1_26462", NODE_INDEX_SCAN, 10010.0, 10000.0,
        "t2_26462", NODE_INDEX_SCAN, 1024.0, 2048.0);

      testSeekAndNextEstimationJoinHelper_IgnoreActualResults(stmt,
        "/*+ YbBatchedNL(t1_26462 t2_26462) IndexScan(t1_26462 t1_26462_idx) "
        + "IndexScan(t2_26462 t2_26462_idx) */ SELECT * FROM t1_26462 JOIN t2_26462 "
        + "ON ROW(t1_26462.k1, t1_26462.k2) = ROW(t2_26462.k1, t2_26462.k2);",
        NODE_YB_BATCHED_NESTED_LOOP,
        "t1_26462", NODE_INDEX_SCAN, 10010.0, 10000.0,
        "t2_26462", NODE_INDEX_SCAN, 1024.0, 2048.0);
    }
  }

  @Test
  public void test26463IntegerOverflowInSeekEstimationforBNL() throws Exception {
    /*
     * #26463 : Negative cost estimates for BNL due to integer overflow
     *
     * This test checks that seek and next estimations do not overflow when
     * joining large tables using Batched Nested Loop Join.
     */
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute("CREATE TABLE t1_26463 (k INT, PRIMARY KEY (k ASC))");
      stmt.execute("CREATE TABLE t2_26463 (k INT)");
      /* Simluate a large table by setting reltuples in pg_class */
      stmt.execute("SET yb_non_ddl_txn_for_sys_tables_allowed = ON");
      stmt.execute("UPDATE pg_class SET reltuples=1000000000 WHERE relname='t1_26463'");
      stmt.execute("UPDATE pg_class SET reltuples=100000000000 WHERE relname='t2_26463'");
      stmt.execute("UPDATE pg_yb_catalog_version SET current_version=current_version+1 "
        + "WHERE db_oid=1");
      stmt.execute("SET yb_non_ddl_txn_for_sys_tables_allowed = OFF");

      stmt.execute("SET yb_enable_base_scans_cost_model=on");

      testSeekAndNextEstimationJoinHelper_IgnoreActualResults(stmt,
        "/*+ Leading((t2_26463 t1_26463)) YbBatchedNL(t1_26463 t2_26463) */ SELECT t1_26463.k "
        + "FROM t1_26463, t2_26463 WHERE t1_26463.k = t2_26463.k;",
        NODE_YB_BATCHED_NESTED_LOOP,
        "t2_26463", NODE_SEQ_SCAN, 97656248.0, 100097654198.0,
        "t1_26463", NODE_INDEX_SCAN, 1024.0, 2048.0);
    }
  }
}
