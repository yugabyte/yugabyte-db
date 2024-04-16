package org.yb.pgsql;

import static org.yb.pgsql.AutoCommit.DISABLED;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_GATHER;
import static org.yb.pgsql.IsolationLevel.READ_COMMITTED;
import static org.yb.pgsql.IsolationLevel.REPEATABLE_READ;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

@RunWith(value=YBTestRunner.class)
public class TestPgParallelReadIsolation extends BasePgSQLTest {
  private static final String COLOCATED_DB = "codb";
  private static final String MAIN_TABLE = "foo";
  private static final int NUM_ROWS = 100000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("yb_enable_read_committed_isolation", "true");
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
}
