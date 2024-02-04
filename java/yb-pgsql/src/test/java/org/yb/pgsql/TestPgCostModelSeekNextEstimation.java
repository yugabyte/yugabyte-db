package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplainDebug;

import java.sql.Connection;
import java.sql.Statement;

import java.util.Arrays;
import java.util.List;
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
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.util.json.ObjectChecker;
import org.yb.util.json.ObjectCheckerBuilder;
import org.yb.util.json.ValueChecker;

@RunWith(value=YBTestRunner.class)
public class TestPgCostModelSeekNextEstimation extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgExplainAnalyze.class);
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

  private void testSeekAndNextEstimationIndexScanHelper(Statement stmt, String query,
                                                        String table_name, String index_name,
                                                        double expected_seeks,
                                                        double expected_nexts) throws Exception {
    double expected_seeks_lower_bound = expected_seeks * SEEK_LOWER_BOUND_FACTOR
        - SEEK_FAULT_TOLERANCE_OFFSET;
    double expected_seeks_upper_bound = expected_seeks * SEEK_UPPER_BOUND_FACTOR
        + SEEK_FAULT_TOLERANCE_OFFSET;
    double expected_nexts_lower_bound = expected_nexts * NEXT_LOWER_BOUND_FACTOR
        - NEXT_FAULT_TOLERANCE_OFFSET;
    double expected_nexts_upper_bound = expected_nexts * NEXT_UPPER_BOUND_FACTOR
        + NEXT_FAULT_TOLERANCE_OFFSET;
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_INDEX_SCAN)
                  .relationName(table_name)
                  .indexName(index_name)
                  .estimatedSeeks(Checkers.closed(expected_seeks_lower_bound,
                                                  expected_seeks_upper_bound))
                  .estimatedNexts(Checkers.closed(expected_nexts_lower_bound,
                                                  expected_nexts_upper_bound))
                  .metric(METRIC_NUM_DB_SEEK,
                          Checkers.closed(expected_seeks_lower_bound,
                                          expected_seeks_upper_bound))
                  .metric(METRIC_NUM_DB_NEXT,
                          Checkers.closed(expected_nexts_lower_bound,
                                          expected_nexts_upper_bound))
                  .build())
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query);
      LOG.info(e.toString());
      throw e;
    }
  }

  private void testSeekAndNextEstimationSeqScanHelper(Statement stmt, String query,
                                                      String table_name, double expected_seeks,
                                                      double expected_nexts) throws Exception {
    double expected_seeks_lower_bound = expected_seeks * SEEK_LOWER_BOUND_FACTOR
        - SEEK_FAULT_TOLERANCE_OFFSET;
    double expected_seeks_upper_bound = expected_seeks * SEEK_UPPER_BOUND_FACTOR
        + SEEK_FAULT_TOLERANCE_OFFSET;
    double expected_nexts_lower_bound = expected_nexts * NEXT_LOWER_BOUND_FACTOR
        - NEXT_FAULT_TOLERANCE_OFFSET;
    double expected_nexts_upper_bound = expected_nexts * NEXT_UPPER_BOUND_FACTOR
        + NEXT_FAULT_TOLERANCE_OFFSET;
    try {
      testExplainDebug(stmt, query,
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_SEQ_SCAN)
                  .relationName(table_name)
                  .estimatedSeeks(Checkers.closed(expected_seeks_lower_bound,
                                                  expected_seeks_upper_bound))
                  .estimatedNexts(Checkers.closed(expected_nexts_lower_bound,
                                                  expected_nexts_upper_bound))
                  .metric(METRIC_NUM_DB_SEEK,
                          Checkers.closed(expected_seeks_lower_bound,
                                          expected_seeks_upper_bound))
                  .metric(METRIC_NUM_DB_NEXT,
                          Checkers.closed(expected_nexts_lower_bound,
                                          expected_nexts_upper_bound))
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
        T1_NAME, T1_INDEX_NAME, 2, 4);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 3, 7);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 4, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 4, 86);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 101, 280);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 6007, 16808);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 6505, 17804);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 22, 6436);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, T1_INDEX_NAME, 1, 10);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 1, 200);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 4, 4000);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 79, 80000);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 41, 280);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 804, 5600);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 16079, 112000);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 879, 81600);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 879, 81600);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 440, 40800);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 5, 1606);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 5, 1606);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 2002, 4000);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 5, 8);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 10, 18);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, T2_INDEX_NAME, 50, 98);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16)", T3_NAME, T3_NAME),
        T3_NAME, T3_INDEX_NAME, 301, 600);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 4844, 9680);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 35, 32037);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 129, 115744);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 76, 68084);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 59, 40077);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 100, 68132);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 22, 6436);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 453, 116392);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 440, 40839);
      testSeekAndNextEstimationIndexScanHelper(stmt, String.format("/*+IndexScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, T4_INDEX_NAME, 747, 69426);
    }
  }

  @Test
  public void testSeekNextEstimationSeqScan() throws Exception {
    try (Statement stmt = this.connection2.createStatement()) {
      stmt.execute(String.format("SET enable_indexscan=off"));
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T1_NAME, T1_NAME),
        T1_NAME, 1, 19);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12) AND k4 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 157, 160155);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 IN (4, 8, 12, 16) AND k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 157, 160155);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) AND k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 157, 160155);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T1_NAME, T1_NAME),
        T1_NAME, 1, 19);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T2_NAME, T2_NAME),
        T2_NAME, 1, 399);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4, 8002);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 14", T2_NAME, T2_NAME),
        T2_NAME, 1, 399);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T3_NAME, T3_NAME),
        T3_NAME, 4, 8002);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k4 >= 4 and k4 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 79, 160077);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k3 >= 4 and k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 = 4 and k2 IN (4, 8, 12, 16)", T4_NAME, T4_NAME),
        T4_NAME, 8, 160006);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (4, 8, 12, 16) and k2 = 4", T4_NAME, T4_NAME),
        T4_NAME, 8, 160006);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k3 IN (4, 8, 12, 16) and k4 = 4", T4_NAME, T4_NAME),
        T4_NAME, 9, 160007);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 5 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 6 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 14 and k2 IN (4, 8, 12, 16)", T2_NAME, T2_NAME),
        T2_NAME, 1, 399);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 and k1 < 7 and k3 IN (4, 8, 12, 16)", T3_NAME, T3_NAME),
        T3_NAME, 2, 8000);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k2 >= 4 and k2 < 7 and k4 IN (4, 8, 12)", T4_NAME, T4_NAME),
        T4_NAME, 24, 160022);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 157, 160155);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 114, 160112);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k2 >= 4 AND k2 < 14", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 IN (1, 4, 7, 10) AND k2 IN (1, 4, 7, 10)", T4_NAME, T4_NAME),
        T4_NAME, 157, 160155);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4", T4_NAME, T4_NAME),
        T4_NAME, 114, 160112);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k1 < 14 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 40, 160038);
      testSeekAndNextEstimationSeqScanHelper(stmt, String.format("/*+SeqScan(%s)*/ SELECT * "
        + "FROM %s WHERE k1 >= 4 AND k3 >= 4 AND k3 < 14", T4_NAME, T4_NAME),
        T4_NAME, 67, 160065);
    }
  }
}
