package org.yb.pgsql;

import static org.junit.Assume.assumeTrue;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_BITMAP_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_BITMAP_OR;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
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

  private Connection connection2;

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
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

  private void testSeekAndNextEstimationIndexScanHelper(
      Statement stmt, String query,
      String table_name, String index_name,
      double expected_seeks,
      double expected_nexts,
      Integer expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_INDEX_SCAN)
                  .relationName(table_name)
                  .indexName(index_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNexts(expectedNextsRange(expected_nexts))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
                  .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
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
      .estimatedNexts(expectedNextsRange(expected_nexts))
      .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
      .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
      .build();
  }

  private ObjectChecker makeBitmapIndexScanChecker_IgnoreActualResults(
      String index_name,
      double expected_seeks,
      double expected_nexts) {
    return makePlanBuilder()
      .nodeType(NODE_BITMAP_INDEX_SCAN)
      .indexName(index_name)
      .estimatedSeeks(expectedSeeksRange(expected_seeks))
      .estimatedNexts(expectedNextsRange(expected_nexts))
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
                  .estimatedNexts(expectedNextsRange(expected_nexts))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
                  .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
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
                  .estimatedNexts(expectedNextsRange(expected_nexts))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
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
      double expected_seeks,
      double expected_nexts,
      Integer expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_INDEX_SCAN)
                  .relationName(table_name)
                  .indexName(index_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNexts(expectedNextsRange(expected_nexts))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
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
      long expected_docdb_result_width) throws Exception {
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_SEQ_SCAN)
                  .relationName(table_name)
                  .estimatedSeeks(expectedSeeksRange(expected_seeks))
                  .estimatedNexts(expectedNextsRange(expected_nexts))
                  .estimatedDocdbResultWidth(Checkers.equal(expected_docdb_result_width))
                  .metric(METRIC_NUM_DB_SEEK, expectedSeeksRange(expected_seeks))
                  .metric(METRIC_NUM_DB_NEXT, expectedNextsRange(expected_nexts))
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

    this.connection2 = getConnectionBuilder().withDatabase(DATABASE_NAME).connect();

    try (Statement stmt = connection2.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, PRIMARY KEY (k1 ASC))", T1_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1 FROM generate_series(1, 20) s1",
                                 T1_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, PRIMARY KEY (k1 ASC, k2 "
                                 + "ASC))", T2_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2 FROM generate_series(1, 20) s1, "
                                 + "generate_series(1, 20) s2", T2_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, k3 INT,"
                                 + "PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC))", T3_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2, s3 FROM "
        + "generate_series(1, 20) s1, generate_series(1, 20) s2, generate_series(1, 20) s3"
                                 , T3_NAME));
      stmt.execute(String.format("CREATE TABLE %s (k1 INT, k2 INT, k3 INT, k4 INT, "
        + "PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC, k4 ASC))", T4_NAME));
      stmt.execute(String.format("INSERT INTO %s SELECT s1, s2, s3, s4 FROM "
        + "generate_series(1, 20) s1, generate_series(1, 20) s2, generate_series(1, 20) s3, "
        + "generate_series(1, 20) s4", T4_NAME));
      stmt.execute("ANALYZE");
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
    try (Statement stmt = this.connection2.createStatement()) {
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 2, 4, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 3, 7, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 4, 10, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 4, 86, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 101, 280, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 6007, 16808, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 6505, 17804, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 22, 6436, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 1, 10, 5);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 1, 200, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 5, 4000, 15);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 79, 80000, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 41, 280, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 804, 5600, 15);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 16079, 112000, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 879, 81600, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 120, 80000, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 440, 40800, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 5, 1606, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 5, 1606, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 2002, 4000, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 5, 8, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 10, 18, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 50, 98, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16)", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 301, 600, 15);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 4844, 9680, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 35, 32037, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 129, 115744, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 76, 68084, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 59, 40077, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 100, 68132, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 22, 6436, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 453, 116392, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 440, 40839, 20);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 747, 69426, 20);
    }
  }

  @Test
  public void testSeekNextEstimationBitmapScan() throws Exception {
    assumeTrue("BitmapScan has much fewer nexts in fastdebug (#22052)", TestUtils.isReleaseBuild());
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute("SET work_mem TO '1GB'"); /* avoid getting close to work_mem */
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
        T2_NAME, 80, 240, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 4, 86));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 80, 240, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 101, 280));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 4830, 14490, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 6007, 16808));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 4800, 14400, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 6505, 17804));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 6400, 19200, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 22, 6436));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, 10, 30, 5, makeBitmapIndexScanChecker(T1_INDEX_NAME, 1, 10));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, 200, 600, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 1, 200));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4000, 12000, 15, makeBitmapIndexScanChecker(T3_INDEX_NAME, 4, 4000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 240000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 79, 80000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, 200, 600, 10, makeBitmapIndexScanChecker(T2_INDEX_NAME, 41, 280));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4000, 12000, 15, makeBitmapIndexScanChecker(T3_INDEX_NAME, 804, 5600));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 240000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 16079, 112000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 240000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 879, 81600));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 80000, 240000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 120, 80000));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40000, 120000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 440, 40800));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 1600, 4800, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 5, 1606));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, 1600, 4800, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 5, 1606));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, 1600, 4800, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 2002, 4000));
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
        T3_NAME, 240, 720, 15, makeBitmapIndexScanChecker(T3_INDEX_NAME, 301, 600));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 3600, 10800, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 4844, 9680));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 32000, 96000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 35, 32037));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 68000, 204000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 76, 68084));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40000, 120000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 59, 40077));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 68000, 204000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 100, 68132));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 6400, 19200, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 22, 6436));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40000, 120000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 440, 40839));
      testSeekAndNextEstimationBitmapScanHelper(stmt, String.format("/*+BitmapScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 68000, 204000, 20, makeBitmapIndexScanChecker(T4_INDEX_NAME, 747, 69426));
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
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 4, 60),
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 80, 240)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T2_NAME, "k1 < 2 OR k2 < 4"),
        T2_NAME, 70, 230, 10,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 2, 20),
          makeBitmapIndexScanChecker_IgnoreActualResults(T2_INDEX_NAME, 20, 100)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 IN (4, 8, 12) OR k2 IN (4, 8, 12)"),
        T4_NAME, 44400, 133200, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 10, 24000),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 109, 24000)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 IN (4, 8, 12) OR k3 IN (4, 8, 12)"),
        T4_NAME, 44400, 133200, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 10, 24000),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 1600, 24000)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 IN (4, 8, 12) OR k4 IN (4, 8, 12)"),
        T4_NAME, 44400, 133200, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 10, 24000),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 32000, 88250)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 < 2 OR k2 < 4"),
        T4_NAME, 30800, 92400, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 2, 8000),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 20, 24000)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 < 2 OR k3 < 4"),
        T4_NAME, 30800, 92400, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 2, 8000),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 400, 24000)).build());

      testSeekAndNextEstimationBitmapScanHelper_IgnoreActualResults(stmt,
        String.format(query, T4_NAME, "k1 < 2 OR k4 < 4"),
        T4_NAME, 30800, 92400, 20,
        makePlanBuilder().nodeType(NODE_BITMAP_OR).plans(
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 2, 8000),
          makeBitmapIndexScanChecker_IgnoreActualResults(T4_INDEX_NAME, 8000, 40000)).build());
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
        T4_NAME, estimated_seeks, estimated_nexts, 20);
      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, "BitmapScan", T4_NAME, "k1", "k2"),
        T4_NAME, estimated_seeks, estimated_nexts, 20,
        makeBitmapIndexScanChecker(T4_INDEX_NAME, 129, 115744));

      testSeekAndNextEstimationSeqScanHelper(stmt,
        String.format(query, "SeqScan", T4_NAME, "k1", "k3"),
        T4_NAME, estimated_seeks, estimated_nexts, 20);
      testSeekAndNextEstimationBitmapScanHelper(stmt,
        String.format(query, "BitmapScan", T4_NAME, "k1", "k3"),
        T4_NAME, estimated_seeks, estimated_nexts, 20,
        makeBitmapIndexScanChecker(T4_INDEX_NAME, 453, 116392));

      testSeekAndNextEstimationSeqScanHelper(stmt,
        String.format(query, "SeqScan", T4_NAME, "k1", "k4"),
        T4_NAME, estimated_seeks, estimated_nexts, 20);
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
        T1_NAME, 1, 19, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 5, 160003, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 5, 160003, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 7, 160005, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, 1, 19, 5);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4, 8002, 15);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4, 8002, 15);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 2, 160000, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, 2, 160000, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, 2, 160000, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399, 10);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16)", T3_NAME, T3_NAME),
        T3_NAME, 2, 8000, 15);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 4, 160002, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 32, 160029, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 114, 160112, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 5, 160003, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 114, 160112, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038, 20);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065, 20);
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
        "test", "test_index_k1", 50000, 50000, 10);

      /* The filter on v1 will be executed on the included column in test_index_k1_v1. As a result,
       * fewer seeks will be needed on the base table.
       */
      testSeekAndNextEstimationIndexScanHelper_IgnoreActualResults(stmt,
        "/*+IndexScan(test test_index_k1_v1) */ SELECT * FROM test WHERE k1 > 50000 and v1 > 80000",
        "test", "test_index_k1_v1", 10000, 50000, 10);
    }
  }
}
