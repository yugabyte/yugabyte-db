package org.yb.pgsql;

import static org.yb.pgsql.AutoCommit.ENABLED;
import static org.yb.pgsql.AutoCommit.DISABLED;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_GATHER;
import static org.yb.pgsql.IsolationLevel.READ_COMMITTED;
import static org.yb.pgsql.IsolationLevel.REPEATABLE_READ;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;
import org.yb.util.BuildTypeUtil;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

@RunWith(value=YBTestRunner.class)
public class TestPgParallelReadIsolation extends BasePgSQLTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgParallelReadIsolation.class);
  private static final String COLOCATED_DB = "codb";
  private static final String MAIN_TABLE = "foo";
  private static final int NUM_ROWS = 100000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("yb_enable_read_committed_isolation", "true");
    flagMap.put("enable_object_locking_for_table_locks", "true");
    flagMap.put("ysql_yb_ddl_transaction_block_enabled", "true");
    flagMap.put("allowed_preview_flags_csv", "ysql_enable_concurrent_ddl");
    flagMap.put("ysql_enable_concurrent_ddl", "true");
    return flagMap;
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate(String.format("CREATE DATABASE %s WITH colocation = true", COLOCATED_DB));
    }

    Connection dbconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
    try (Statement stmt = dbconn.createStatement()) {

      stmt.execute(String.format(
        "CREATE TABLE %s (k int PRIMARY KEY, v text) with (colocation=true)", MAIN_TABLE));

      // Populate the table
      stmt.execute(String.format(
        "INSERT INTO %s (SELECT i, 'Value ' || i::text FROM generate_series(1, %d) i)",
        MAIN_TABLE, NUM_ROWS));

      stmt.execute(String.format("ANALYZE %s", MAIN_TABLE));
    }
    dbconn.close();
  }

  @After
  public void cleanup() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.executeUpdate(String.format("DROP DATABASE %s", COLOCATED_DB));
    }
    connection.close();
  }

  private void forceParallel(Statement stmt) throws SQLException {
    stmt.executeUpdate("SET yb_enable_base_scans_cost_model TO true");
    stmt.executeUpdate("SET yb_parallel_range_rows TO 1");
    stmt.executeUpdate("SET parallel_setup_cost TO 0");
    stmt.executeUpdate("SET parallel_tuple_cost TO 0");
  }

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false /* nullify */);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false /* nullify */);
  }

  private void checkParallelRowCount(Statement stmt, String table, int num_rows) throws Exception {
    Checker checker = makeTopLevelBuilder()
        .plan(makePlanBuilder()
            .nodeType(NODE_GATHER)
            .actualRows(Checkers.equal(num_rows))
            .workersPlanned(Checkers.equal(2))
            .workersLaunched(Checkers.equal(2))
            .plans(makePlanBuilder()
                .nodeType(NODE_SEQ_SCAN)
                .relationName(table)
                .alias(table)
                .build())
            .build())
        .build();
    ExplainAnalyzeUtils.testExplainNoTiming(
        stmt, String.format("SELECT * FROM %s", table), checker);
  }

  private void checkRegularRowCount(Statement stmt, String table, int num_rows) throws Exception {
    Checker checker = makeTopLevelBuilder()
        .plan(makePlanBuilder()
            .nodeType(NODE_SEQ_SCAN)
            .relationName(table)
            .alias(table)
            .actualRows(Checkers.equal(num_rows))
            .build())
        .build();

    ExplainAnalyzeUtils.testExplainNoTiming(
        stmt, String.format("SELECT * FROM %s", table), checker);
  }

  @Test
  public void testRegularRead() throws Exception {
    try (Connection dbconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
         Statement stmt = dbconn.createStatement()) {
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS);
    }
  }

  @Test
  public void testParallelRead() throws Exception {
    try (Connection dbconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
         Statement stmt = dbconn.createStatement()) {
      forceParallel(stmt);
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS);
    }
  }

  @Test
  public void testRegularReadCommitted() throws Exception {
    try (Connection dbconn = getConnectionBuilder()
             .withDatabase(COLOCATED_DB)
             .withAutoCommit(DISABLED)
             .withIsolationLevel(READ_COMMITTED)
             .connect();
         Statement stmt = dbconn.createStatement();
         Connection otherconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
         Statement otherstmt = otherconn.createStatement();) {
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS);
      otherstmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 1, NUM_ROWS + 1));
      // Expect concurrent changes to be visible
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      stmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 2, NUM_ROWS + 2));
      // Expect own changes to be also visible
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS + 2);
      otherstmt.execute(String.format("DELETE FROM %s WHERE k = %d", MAIN_TABLE, NUM_ROWS + 1));
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      dbconn.rollback();
    }
  }

  @Test
  public void testParallelReadCommitted() throws Exception {
    try (Connection dbconn = getConnectionBuilder()
             .withDatabase(COLOCATED_DB)
             .withAutoCommit(DISABLED)
             .withIsolationLevel(READ_COMMITTED)
             .connect();
         Statement stmt = dbconn.createStatement();
         Connection otherconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
         Statement otherstmt = otherconn.createStatement();) {
      forceParallel(stmt);
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS);
      otherstmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 1, NUM_ROWS + 1));
      // Expect concurrent changes to be visible
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      stmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 2, NUM_ROWS + 2));
      // Expect own changes to be also visible
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS + 2);
      otherstmt.execute(String.format("DELETE FROM %s WHERE k = %d", MAIN_TABLE, NUM_ROWS + 1));
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      dbconn.rollback();
    }
  }

  @Test
  public void testRegularRepeatableRead() throws Exception {
    try (Connection dbconn = getConnectionBuilder()
             .withDatabase(COLOCATED_DB)
             .withAutoCommit(DISABLED)
             .withIsolationLevel(REPEATABLE_READ)
             .connect();
         Statement stmt = dbconn.createStatement();
         Connection otherconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
         Statement otherstmt = otherconn.createStatement();) {
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS);
      // Wait for a second so other transaction's write time differs.
      // Having a statement already committed current transaction won't be able to retry.
      Thread.sleep(1000);
      otherstmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 1, NUM_ROWS + 1));
      // Expect concurrent changes not to be visible
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS);
      stmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 2, NUM_ROWS + 2));
      // Expect own changes to be visible
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      otherstmt.execute(String.format("DELETE FROM %s WHERE k = %d", MAIN_TABLE, NUM_ROWS + 1));
      checkRegularRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      dbconn.rollback();
    }
  }

  @Test
  public void testParallelRepeatableRead() throws Exception {
    try (Connection dbconn = getConnectionBuilder()
             .withDatabase(COLOCATED_DB)
             .withAutoCommit(DISABLED)
             .withIsolationLevel(REPEATABLE_READ)
             .connect();
         Statement stmt = dbconn.createStatement();
         Connection otherconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
         Statement otherstmt = otherconn.createStatement();) {
      forceParallel(stmt);
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS);
      // Wait for a second so other transaction's write time differs.
      // Having a statement already committed current transaction won't be able to retry.
      Thread.sleep(1000);
      otherstmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 1, NUM_ROWS + 1));
      // Expect concurrent changes not to be visible
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS);
      stmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 2, NUM_ROWS + 2));
      // Expect own changes to be visible
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      otherstmt.execute(String.format("DELETE FROM %s WHERE k = %d", MAIN_TABLE, NUM_ROWS + 1));
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      dbconn.rollback();
    }
  }

  @Test
  public void testFreshUserSnapshotInConcurrentDDLMode() throws Exception {
    try (Connection dbconn = getConnectionBuilder()
             .withDatabase(COLOCATED_DB)
             .withAutoCommit(ENABLED)
             .withIsolationLevel(READ_COMMITTED)
             .withPreferQueryMode("simple")
             .connect();
         Statement stmt = dbconn.createStatement();
         Connection otherconn = getConnectionBuilder().withDatabase(COLOCATED_DB).connect();
         Statement otherstmt = otherconn.createStatement();) {
      forceParallel(stmt);

      // Warm up the catalog cache in non-legacy (concurrent DDL) mode so the
      // subsequent BEGIN + SELECT has minimal catalog snapshot churn.
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS);

      stmt.execute("BEGIN");
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS);
      otherstmt.execute(String.format(
          "INSERT INTO %s VALUES (%d, %d)", MAIN_TABLE, NUM_ROWS + 1, NUM_ROWS + 1));

      // Expect concurrent changes to be visible.
      //
      // Currently, parallel workers in ysql_enable_concurrent_ddl mode use the legacy mode for
      // catalog operations. This is true for both the setup phase and the non-setup phase (i.e.,
      // IsInParallelMode() becomes true).
      //
      // (1) In the setup phase, we need to use the legacy mode because the transaction serial
      // numbers are 1/ 3 and the actual transaction serial number is not yet restored. Using
      // these serial numbers will abort the existing transaction silently.
      //
      // (2) In the non-setup phase, we need to use the legacy mode because of the following
      // reason:
      //
      //   New catalog snapshots in the non-setup phase of workers create use newer read time
      //   serial numbers. This causes the leader to later pick the same read time serial number
      //   for new statements' transaction snapshot -- leading to a stale read.
      //
      //   To workaround this issue, we currently force the legacy mode for catalog operations
      //   for parallel workers in the non-setup phase.
      //
      //   TOOD(#31025): Using unique read time serial numbers across parallel workers will fix
      //   this issue.
      //
      // Note that we use simple query mode here because in extended query mode, the stale read
      // is not seen -- this is because in extended query mode, the next statement picks a
      // transaction snapshot a few times for parse and bind before finally picking a read time
      // serial number for the actual read. So, it ends up not conflicting with the catalog snapshot
      // read time serial number picked by the worker of the previous statement.
      checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS + 1);
      stmt.execute("ROLLBACK");
    }
  }

  @Test
  public void testParallelReadWithConcurrentAlters() throws Exception {
    final int NUM_ITERATIONS =
        (BuildTypeUtil.isASAN() || BuildTypeUtil.isTSAN()) ? 20 : 100;
    final AtomicBoolean stop = new AtomicBoolean(false);
    final AtomicReference<Exception> readerError = new AtomicReference<>();
    final AtomicReference<Exception> ddlError = new AtomicReference<>();
    final CountDownLatch readerStarted = new CountDownLatch(1);

    Thread readerThread = new Thread(() -> {
      try (Connection conn = getConnectionBuilder()
               .withDatabase(COLOCATED_DB)
               .withAutoCommit(ENABLED)
               .withIsolationLevel(READ_COMMITTED)
               .connect();
           Statement stmt = conn.createStatement()) {
        forceParallel(stmt);
        readerStarted.countDown();

        while (!stop.get()) {
          checkParallelRowCount(stmt, MAIN_TABLE, NUM_ROWS);
        }
      } catch (Exception e) {
        readerError.set(e);
      }
    });

    Thread ddlThread = new Thread(() -> {
      try {
        readerStarted.await();
        try (Connection conn = getConnectionBuilder()
                 .withDatabase(COLOCATED_DB)
                 .withAutoCommit(ENABLED)
                 .connect();
             Statement stmt = conn.createStatement()) {
          for (int i = 0; i < NUM_ITERATIONS && readerError.get() == null; i++) {
            String colName = "ddl_col_" + i;
            stmt.execute(String.format(
                "ALTER TABLE %s ADD COLUMN %s INT DEFAULT %d",
                MAIN_TABLE, colName, i));
            LOG.info("Added column {}", colName);

            stmt.execute(String.format(
                "ALTER TABLE %s DROP COLUMN %s", MAIN_TABLE, colName));
            LOG.info("Dropped column {}", colName);
          }
        }
      } catch (Exception e) {
        ddlError.set(e);
      } finally {
        stop.set(true);
      }
    });

    readerThread.start();
    ddlThread.start();

    ddlThread.join(120_000);
    readerThread.join(10_000);

    if (ddlError.get() != null) {
      throw new AssertionError("DDL thread failed", ddlError.get());
    }
    if (readerError.get() != null) {
      throw new AssertionError(
          "Parallel reader failed during concurrent DDL",
          readerError.get());
    }
  }
}
