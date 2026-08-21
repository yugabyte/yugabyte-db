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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.BuildTypeUtil;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.YBTestRunner;

import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import static org.yb.AssertionWrappers.*;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_AGGREGATE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_GATHER;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;

@RunWith(value=YBTestRunner.class)
public class TestPgFollowerReads extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgFollowerReads.class);
  private static int kMaxClockSkewMs = 100;
  private static int kRaftHeartbeatIntervalMs = 500;
  private static final int NUM_ACCOUNTS = 100;
  private static final int INITIAL_BALANCE = 1000;
  private static final long EXPECTED_TOTAL = (long) NUM_ACCOUNTS * INITIAL_BALANCE;

  /**
   * @return flags shared between tablet server and initdb
   */
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("max_clock_skew_usec", "" + kMaxClockSkewMs * 1000);
    flagMap.put("raft_heartbeat_interval_ms", "" + kRaftHeartbeatIntervalMs);
    flagMap.put("yb_enable_read_committed_isolation", "true");
    return flagMap;
  }

  private Long getCountForTable(String metricName, String tableName) throws Exception {
    return getTserverMetricCountForTable(metricName, tableName);
  }

  // Validates the fix for #31166: before it, each parallel-scan worker picked
  // its own follower read time, so a single SUM(balance) could read different
  // tablets at different snapshots. A transfer that commits between two
  // workers' read times is then seen by only one -- e.g. the debit is
  // counted but its matching credit is missed -- so the total is off by
  // that amount.
  @Test
  public void testBankInvariantWithParallelFollowerReads() throws Exception {
    runBankInvariantTest("accounts_parallel", true /* useParallel */);
  }

  // Non-parallel counterpart.
  @Test
  public void testBankInvariantWithFollowerReads() throws Exception {
    runBankInvariantTest("accounts", false /* useParallel */);
  }

  private void runBankInvariantTest(String table, boolean useParallel) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE " + table +
                   " (id INT PRIMARY KEY, balance BIGINT NOT NULL) SPLIT INTO 12 TABLETS");
      stmt.execute("INSERT INTO " + table +
                   " SELECT g, " + INITIAL_BALANCE + " FROM generate_series(1, " +
                   NUM_ACCOUNTS + ") g");
      stmt.execute("ANALYZE " + table);
    }

    Thread.sleep(5000);

    runBankInvariant(table, useParallel);
  }

  private static void setFollowerReadConfig(Statement stmt) throws Exception {
    stmt.execute("SET yb_read_from_followers = true");
    stmt.execute("SET yb_follower_read_staleness_ms = 4000");
    stmt.execute("SET default_transaction_read_only = true");
  }

  private static void setParallelQueryConfig(Statement stmt) throws Exception {
    stmt.execute("SET yb_enable_cbo = ON");
    stmt.execute("SET yb_parallel_range_rows TO 1");
    stmt.execute("SET yb_test_force_parallel = force");
  }

  private static void setNonParallelQueryConfig(Statement stmt) throws Exception {
    stmt.execute("SET yb_enable_parallel_scan_colocated = off");
    stmt.execute("SET yb_enable_parallel_scan_hash_sharded = off");
    stmt.execute("SET yb_enable_parallel_scan_range_sharded = off");
    stmt.execute("SET yb_enable_parallel_scan_system = off");
  }

  private static void setQueryExecutionConfig(Statement stmt, boolean useParallel)
      throws Exception {
    setFollowerReadConfig(stmt);
    if (useParallel) {
      setParallelQueryConfig(stmt);
    } else {
      setNonParallelQueryConfig(stmt);
    }
  }

  private static final String BANK_SUM_STMT = "bank_sum";

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false /* nullify */);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false /* nullify */);
  }

  private void checkPlan(Statement stmt, boolean useParallel) throws Exception {
    PlanCheckerBuilder child = makePlanBuilder();
    if (useParallel) {
      child.nodeType(NODE_GATHER).workersPlanned(Checkers.greaterOrEqual(1));
    } else {
      child.nodeType(NODE_SEQ_SCAN);
    }
    Checker checker = makeTopLevelBuilder()
        .plan(makePlanBuilder()
            .nodeType(NODE_AGGREGATE)
            .plans(child.build())
            .build())
        .build();
    ExplainAnalyzeUtils.testExplainNoTiming(stmt, "EXECUTE " + BANK_SUM_STMT, checker);
  }

  private void runBankInvariant(String table, boolean useParallel) throws Exception {
    final String[] ISOLATIONS = {"read committed", "repeatable read", "serializable"};
    AtomicBoolean stop = new AtomicBoolean(false);
    AtomicInteger transfers = new AtomicInteger(0);
    AtomicInteger reads = new AtomicInteger(0);
    AtomicInteger inconsistencies = new AtomicInteger(0);

    Thread writerThread = new Thread(() -> {
      Random rnd = new Random();
      try (Connection wConn = getConnectionBuilder().connect();
           Statement wStmt = wConn.createStatement()) {
        while (!stop.get()) {
          int from = rnd.nextInt(NUM_ACCOUNTS) + 1;
          int to = rnd.nextInt(NUM_ACCOUNTS - 1) + 1;
          if (to >= from) to++;
          int amount = rnd.nextInt(10) + 1;
          try {
            wStmt.execute("BEGIN");
            wStmt.execute("UPDATE " + table + " SET balance = balance - " + amount +
                          " WHERE id = " + from);
            wStmt.execute("UPDATE " + table + " SET balance = balance + " + amount +
                          " WHERE id = " + to);
            wStmt.execute("COMMIT");
            transfers.incrementAndGet();
          } catch (Exception e) {
            try { wStmt.execute("ROLLBACK"); } catch (Exception ignored) {}
          }
        }
      } catch (Exception e) {
        LOG.error("Writer thread failed", e);
      }
    });

    try (Connection rConn = getConnectionBuilder().connect();
         Statement rStmt = rConn.createStatement()) {
      setQueryExecutionConfig(rStmt, useParallel);
      rStmt.execute("PREPARE " + BANK_SUM_STMT + " AS SELECT SUM(balance) FROM " + table);
      checkPlan(rStmt, useParallel);

      Thread readerThread = new Thread(() -> {
        Random rnd = new Random();
        try {
          while (!stop.get()) {
            String isolation = ISOLATIONS[rnd.nextInt(ISOLATIONS.length)];
            try {
              rStmt.execute("SET default_transaction_isolation = '" + isolation + "'");
              ResultSet rs = rStmt.executeQuery("EXECUTE " + BANK_SUM_STMT);
              rs.next();
              long total = rs.getLong(1);
              reads.incrementAndGet();
              if (total != EXPECTED_TOTAL) {
                inconsistencies.incrementAndGet();
                LOG.error("INCONSISTENCY [" + isolation + "]: expected total=" + EXPECTED_TOTAL +
                          " but got " + total + " (diff=" + (total - EXPECTED_TOTAL) +
                          ") after " + transfers.get() + " transfers");
              }
            } catch (Exception e) {
              // Transient errors are OK
            }
          }
        } catch (Exception e) {
          LOG.error("Reader thread failed", e);
        }
      });

      writerThread.start();
      readerThread.start();

      Thread.sleep(20000);
      stop.set(true);
      writerThread.join(10000);
      readerThread.join(10000);
    }

    LOG.info("[useParallel=" + useParallel + "] Transfers: " + transfers.get() +
             ", reads: " + reads.get() + ", inconsistencies: " + inconsistencies.get());

    assertEquals("Bank invariant violated! Follower reads produced inconsistent results.",
                 0, inconsistencies.get());
  }

  @Test
  public void testSetIsolationLevelsWithReadFromFollowersSessionVariable() throws Exception {
    try (Statement statement = connection.createStatement()) {
      // Enabling follower reads should fail if staleness is less than 2 * max_clock_skew.
      statement.execute("SET yb_read_from_followers = false");
      runInvalidQuery(statement, "SET yb_follower_read_staleness_ms = " + (2 * kMaxClockSkewMs - 1),
                      "ERROR: cannot enable yb_read_from_followers with a staleness of less than "
                      + "2 * (max_clock_skew");
      runInvalidQuery(statement, "SET yb_follower_read_staleness_ms = " + kMaxClockSkewMs / 2,
                      "ERROR: cannot enable yb_read_from_followers with a staleness of less than "
                      + "2 * (max_clock_skew");
      runInvalidQuery(statement, "SET yb_follower_read_staleness_ms = " + 0,
                      "ERROR: cannot enable yb_read_from_followers with a staleness of less than "
                      + "2 * (max_clock_skew");
      statement.execute("SET yb_follower_read_staleness_ms = " + (2 * kMaxClockSkewMs + 1));

      statement.execute("SET yb_read_from_followers = true");
      statement.execute("SET yb_follower_read_staleness_ms = " + 2 * kMaxClockSkewMs);
      runInvalidQuery(statement, "SET yb_follower_read_staleness_ms = " + (2 * kMaxClockSkewMs - 1),
                      "ERROR: cannot enable yb_read_from_followers with a staleness of less than "
                      + "2 * (max_clock_skew");
      runInvalidQuery(statement, "SET yb_follower_read_staleness_ms = " + kMaxClockSkewMs / 2,
                      "ERROR: cannot enable yb_read_from_followers with a staleness of less than "
                      + "2 * (max_clock_skew");
      runInvalidQuery(statement, "SET yb_follower_read_staleness_ms = 0",
                      "ERROR: cannot enable yb_read_from_followers with a staleness of less than "
                      + "2 * (max_clock_skew");
      statement.execute("SET yb_follower_read_staleness_ms = " + (2 * kMaxClockSkewMs + 1));

      // Test enabling follower reads with various isolation levels.
      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");

      // READ UNCOMMITTED with yb_read_from_followers enabled -> ok.
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ UNCOMMITTED");
      statement.execute("SET yb_read_from_followers = true");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");

      // READ COMMITTED with yb_read_from_followers enabled -> ok.
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED");
      statement.execute("SET yb_read_from_followers = true");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");

      // REPEATABLE READ with yb_read_from_followers enabled
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      statement.execute("SET yb_read_from_followers = true");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");

      // SERIALIZABLE with yb_read_from_followers enabled
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE");
      statement.execute("SET yb_read_from_followers = true");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");

      // Reset the isolation level to the lowest possible.
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ UNCOMMITTED");
      statement.execute("SET yb_read_from_followers = true");

      // yb_read_from_followers enabled with READ UNCOMMITTED -> ok.
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ UNCOMMITTED");

      // yb_read_from_followers enabled with READ COMMITTED -> ok.
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ COMMITTED");

      // yb_read_from_followers enabled with REPEATABLE READ
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL REPEATABLE READ");

      // yb_read_from_followers enabled with SERIALIZABLE
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE");

      // Reset the isolation level to the lowest possible.
      statement.execute(
          "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL READ UNCOMMITTED");

      // yb_read_from_followers enabled with START TRANSACTION ISOLATION LEVEL READ UNCOMMITTED
      // -> ok.
      statement.execute("SET yb_read_from_followers = true");
      statement.execute("START TRANSACTION ISOLATION LEVEL READ UNCOMMITTED");
      statement.execute("ABORT");

      // yb_read_from_followers enabled with START TRANSACTION ISOLATION LEVEL READ COMMITTED
      // -> ok.
      statement.execute("START TRANSACTION ISOLATION LEVEL READ COMMITTED");
      statement.execute("ABORT");


      // yb_read_from_followers enabled with START TRANSACTION ISOLATION LEVEL REPEATABLE READ
      statement.execute("START TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      statement.execute("ABORT");

      // yb_read_from_followers enabled with START TRANSACTION ISOLATION LEVEL SERIALIZABLE
      statement.execute("START TRANSACTION ISOLATION LEVEL SERIALIZABLE");
      statement.execute("ABORT");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");

      // START TRANSACTION ISOLATION LEVEL READ UNCOMMITTED with yb_read_from_followers enabled
      // -> ok.
      statement.execute("START TRANSACTION ISOLATION LEVEL READ UNCOMMITTED");
      statement.execute("SET yb_read_from_followers = true");
      statement.execute("ABORT");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");

      // START TRANSACTION ISOLATION LEVEL READ COMMITTED with yb_read_from_followers enabled
      // -> ok.
      statement.execute("START TRANSACTION ISOLATION LEVEL READ COMMITTED");
      statement.execute("SET yb_read_from_followers = true");
      statement.execute("ABORT");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");
      // START TRANSACTION ISOLATION LEVEL REPEATABLE READ with yb_read_from_followers enabled
      statement.execute("START TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      statement.execute("SET yb_read_from_followers = true");
      statement.execute("ABORT");

      // Reset session variable.
      statement.execute("SET yb_read_from_followers = false");
      // START TRANSACTION ISOLATION LEVEL SERIALIZABLE with yb_read_from_followers enabled
      statement.execute("START TRANSACTION ISOLATION LEVEL SERIALIZABLE");
      statement.execute("SET yb_read_from_followers = true");
      statement.execute("ABORT");
    }
  }

  @Test
  public void testConsistentPrefixForIndexes() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE consistentprefix(k int primary key, v int)");
      statement.execute("CREATE INDEX idx on consistentprefix(v)");
      LOG.info("Start writing");
      statement.execute(String.format("INSERT INTO consistentprefix(k, v) VALUES(%d, %d)", 1, 1));
      LOG.info("Done writing");

      final long kFollowerReadStalenessMs = BuildTypeUtil.adjustTimeout(1200);
      statement.execute("SET yb_read_from_followers = true;");
      statement.execute("SET yb_follower_read_staleness_ms = " + kFollowerReadStalenessMs);
      LOG.info("Using staleness of " + kFollowerReadStalenessMs + " ms.");
      // Sleep for the updates to be visible during follower reads.
      Thread.sleep(kFollowerReadStalenessMs);

      statement.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY");
      assertOneRow(statement, "SELECT * FROM consistentprefix where v = 1", 1, 1);
      statement.execute("COMMIT");
      // The read will first read the ybctid from the index table, then use it to do a lookup
      // on the indexed table.
      long count_reqs = getCountForTable("consistent_prefix_read_requests", "consistentprefix");
      assertEquals(count_reqs, 1);
      count_reqs = getCountForTable("consistent_prefix_read_requests", "idx");
      assertEquals(count_reqs, 1);

      long count_rows = getCountForTable("pgsql_consistent_prefix_read_rows", "consistentprefix");
      assertEquals(count_rows, 1);
      count_rows = getCountForTable("pgsql_consistent_prefix_read_rows", "idx");
      assertEquals(count_rows, 1);
    }
  }

  @Test
  public void testFollowerReadsRedirected() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE t(a int primary key) SPLIT INTO 9 TABLETS");
      LOG.info("Start writing");
      final int kRows = 100;
      statement.execute(String.format("INSERT INTO t SELECT generate_series(1, %d)", kRows));
      LOG.info("Done writing");

      final long kFollowerReadStalenessLargeMs = 30000;

      statement.execute("SET yb_debug_log_docdb_requests = true;");
      statement.execute("SET yb_read_from_followers = true;");
      statement.execute("SET default_transaction_read_only = true;");

      statement.execute("SET yb_follower_read_staleness_ms = " + kFollowerReadStalenessLargeMs);
      final int kNumLoops = 100;
      final int kJitterMs = 3 * kRaftHeartbeatIntervalMs / kNumLoops;
      long count_reqs0 = getCountForTable("consistent_prefix_read_requests", "t");
      for (int i = 0; i < kNumLoops; i++) {
        statement.executeQuery(String.format("SELECT * from t where a = %d", 1 + (i % kRows)));
        Thread.sleep(kJitterMs);
      }
      long count_reqs1 = getCountForTable("consistent_prefix_read_requests", "t");
      LOG.info("Reading " + kNumLoops + " rows with large staleness. Had "
               + (count_reqs1 - count_reqs0) + " requests.");
      assertEquals(count_reqs1 - count_reqs0, kNumLoops);

      final int kFollowerReadStalenessSmallMs = 300;
      assertLessThan(kFollowerReadStalenessSmallMs, kRaftHeartbeatIntervalMs);
      assertGreaterThan(kFollowerReadStalenessSmallMs, 2 * kMaxClockSkewMs);
      statement.execute("SET yb_follower_read_staleness_ms = " + kFollowerReadStalenessSmallMs);
      for (int i = 0; i < kNumLoops; i++) {
        statement.executeQuery(String.format("SELECT * from t where a = %d", 1 + (i % kRows)));
        Thread.sleep(kJitterMs);
      }
      long count_reqs2 = getCountForTable("consistent_prefix_read_requests", "t");
      LOG.info("Reading " + kNumLoops + " rows with small staleness. Had "
               + (count_reqs1 - count_reqs0) + " requests.");
      assertGreaterThan(count_reqs2 - count_reqs1, (long)kNumLoops);

      // required to clean up the table.
      statement.execute("SET default_transaction_read_only = false;");
    }
  }

  @Test
  public void testFollowerReadsOnReadCommitted() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test(k int primary key, v int)");
      statement.execute("SET yb_read_from_followers = true;");
      statement.execute("BEGIN TRANSACTION READ ONLY ISOLATION LEVEL READ COMMITTED;");
      final int kNumSelects = 1000;
      LOG.info("Start selects");
      final long begin = System.currentTimeMillis();
      for (int i = 0; i < kNumSelects; i++) {
        statement.execute(String.format("SELECT * FROM test where k = %d", i));
      }
      LOG.info("Done selects");
      long end = System.currentTimeMillis();
      long kExpectedLatencyWithoutFollowerReadsMs = kNumSelects * (kRaftHeartbeatIntervalMs / 2);
      int kFudgeFactor = 2;
      LOG.info("Running " + kNumSelects + " took " + (end - begin) + " ms.");
      assertLessThan(end - begin, kExpectedLatencyWithoutFollowerReadsMs / kFudgeFactor);
    }
  }

  public void doSelect(boolean use_ordered_by, boolean get_count, Statement statement,
                       boolean enable_follower_read, List<Row> rows_list,
                       long expected_num_tablet_requests) throws Exception {
    int row_count = rows_list.size();
    LOG.info("Reading rows with enable_follower_read=" + enable_follower_read);
    long old_count_reqs = getCountForTable("consistent_prefix_read_requests", "consistentprefix");
    long old_count_rows = getCountForTable("pgsql_consistent_prefix_read_rows", "consistentprefix");
    statement.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ " +
        (enable_follower_read ? "READ ONLY" : ""));
    if (get_count) {
      assertOneRow(statement, "SELECT count(*) FROM consistentprefix", row_count);
    } else if (use_ordered_by) {
      assertRowList(statement, "SELECT * FROM consistentprefix ORDER BY k", rows_list);
    } else {
      assertRowSet(statement, "SELECT * FROM consistentprefix k", new HashSet(rows_list));
    }
    statement.execute("COMMIT");
    long count_reqs = getCountForTable("consistent_prefix_read_requests", "consistentprefix");
    assertEquals(count_reqs - old_count_reqs,
                 !enable_follower_read ? 0 : expected_num_tablet_requests);
    long count_rows = getCountForTable("pgsql_consistent_prefix_read_rows", "consistentprefix");
    assertEquals(count_rows - old_count_rows,
                 !enable_follower_read || row_count == 0
                     ? 0
                     : (get_count ? expected_num_tablet_requests : row_count));

  }

  public void testConsistentPrefix(int kNumRows, boolean use_ordered_by, boolean get_count)
      throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE consistentprefix(k int primary key)");

      final int kNumRowsDeleted = 10;
      ArrayList<Row> all_rows = new ArrayList<Row>();
      ArrayList<Row> unchanged_rows = new ArrayList<Row>();
      LOG.info("Start writing");
      long startWriteMs = System.currentTimeMillis();
      for (int i = 0; i < kNumRows; i++) {
        statement.execute(String.format("INSERT INTO consistentprefix(k) VALUES(%d)", i));
        all_rows.add(new Row(i));
        if (i >= kNumRowsDeleted) {
          unchanged_rows.add(new Row(i));
        }
      }
      LOG.info("Done writing");
      long doneWriteMs = System.currentTimeMillis();

      final int kNumTablets = 3;
      final int kNumRowsPerTablet = (int)Math.ceil(kNumRows / (1.0 * kNumTablets));
      final int kNumTabletRequests = kNumTablets * (int)Math.ceil(kNumRowsPerTablet / 1024.0);
      final long kOpDurationMs = BuildTypeUtil.adjustTimeout(2500);

      Thread.sleep(kOpDurationMs);

      statement.execute("SET yb_read_from_followers = true;");

      // Set staleness so that the read happens before the initial writes have started.
      long staleness_ms = System.currentTimeMillis() + kOpDurationMs - startWriteMs;
      statement.execute("SET yb_follower_read_staleness_ms = " + staleness_ms);
      LOG.info("Using staleness of " + staleness_ms + " ms.");
      doSelect(use_ordered_by, get_count, statement, true, Collections.emptyList(),
               kNumTablets);


      // Set staleness so that the read happens after the initial writes are done.
      staleness_ms = System.currentTimeMillis() - doneWriteMs;
      statement.execute("SET yb_follower_read_staleness_ms = " + staleness_ms);
      LOG.info("Using staleness of " + staleness_ms + " ms.");
      doSelect(use_ordered_by, get_count, statement, true, all_rows, kNumTabletRequests);

      Connection write_connection = getConnectionBuilder().connect();
      ArrayList<Statement> write_txns = new ArrayList<Statement>();
      LOG.info("Start delete");
      long startDeleteMs = System.currentTimeMillis();
      for (int i = 0; i < kNumRowsDeleted; i++) {
        write_txns.add(write_connection.createStatement());
        Statement write_txn = write_txns.get(i);
        write_txn.execute("START TRANSACTION");
        write_txn.execute("DELETE FROM consistentprefix where k = " + i);
      }
      long writtenDeleteMs = System.currentTimeMillis();
      Thread.sleep(kOpDurationMs);

      doSelect(use_ordered_by, get_count, statement, false, all_rows, 0);

      // Set staleness so the read happens after the initial writes are done. Before deletes start.
      staleness_ms = System.currentTimeMillis() - (doneWriteMs + startDeleteMs) / 2;
      statement.execute("SET yb_follower_read_staleness_ms = " + staleness_ms);
      LOG.info("Using staleness of " + staleness_ms + " ms.");
      doSelect(use_ordered_by, get_count, statement, true, all_rows, kNumTabletRequests);

      long startCommitMs = System.currentTimeMillis();
      for (int i = 0; i < kNumRowsDeleted; i++) {
        Statement write_txn = write_txns.get(i);
        write_txn.execute("COMMIT");
      }
      long committedDeleteMs = System.currentTimeMillis();
      LOG.info("Done delete");
      Thread.sleep(kOpDurationMs);

      doSelect(use_ordered_by, get_count, statement, false, unchanged_rows, 0);

      // Set staleness so that the read happens before deletes are committed.
      staleness_ms = System.currentTimeMillis() - (writtenDeleteMs + startCommitMs) / 2;
      statement.execute("SET yb_follower_read_staleness_ms = " + staleness_ms);
      LOG.info("Using staleness of " + staleness_ms + " ms.");
      doSelect(use_ordered_by, get_count, statement, true, all_rows, kNumTabletRequests);

      // Set staleness so that the read happens after deletes are committed.
      staleness_ms = System.currentTimeMillis() - (committedDeleteMs + kOpDurationMs / 2);
      statement.execute("SET yb_follower_read_staleness_ms = " + staleness_ms);
      LOG.info("Using staleness of " + staleness_ms + " ms.");
      doSelect(use_ordered_by, get_count, statement, true, unchanged_rows, kNumTabletRequests);
    }
  }

  @Test
  public void testCountConsistentPrefix() throws Exception {
    testConsistentPrefix(100, /* use_ordered_by */ false, /* get_count */ true);
  }

  @Test
  public void testOrderedSelectConsistentPrefix() throws Exception {
    testConsistentPrefix(5000, /* use_ordered_by */ true, /* get_count */ false);
  }

  @Test
  public void testSelectConsistentPrefix() throws Exception {
    testConsistentPrefix(7000, /* use_ordered_by */ false, /* get_count */ false);
  }

  // The test checks that follower reads are not used if sys catalog reads are to be performed.
  @Test
  public void testPgSysCatalogNoFollowerReads() throws Exception {
    // Make a new connection. The current connection was used to execute some test setup
    // code including a DDL statement: GRANT ALL ON SCHEMA public TO public
    // When we use invalidation messages we use a SQL function to increment the catalog
    // version and insert the messages into the pg_yb_invalidation_messages table.
    // Calling this SQL function caused more master catalog table reads which resulted
    // in a larger catalog cache in the current session. In particular, it already included
    // those catalog cache entries that would have triggered master RPCs when executing the
    // normal query of the test. As a result, no more master RPCs were triggered. Using
    // a new connection avoids this problem.
    connection = getConnectionBuilder().connect();
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET yb_follower_read_staleness_ms = " + (2 * kMaxClockSkewMs + 1));
      stmt.execute("SET yb_read_from_followers = true");
      stmt.execute("BEGIN TRANSACTION READ ONLY");
      long startReadRPCCount = getMasterReadRPCCount();
      stmt.execute("SELECT EXTRACT(month FROM NOW())");
      long endReadRPCCount = getMasterReadRPCCount();
      stmt.execute("COMMIT");
      assertGreaterThan(endReadRPCCount, startReadRPCCount);
      long sysCatalogFolowerReads = getMetricCountForTable(
          getMasterMetricSources(), "consistent_prefix_read_requests", "sys.catalog");
      assertEquals(0L, sysCatalogFolowerReads);
    }
  }

  private long getMasterReadRPCCount() throws Exception {
    return getReadRPCMetric(getMasterMetricSources()).count;
  }
}
