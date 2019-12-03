// Copyright (c) YugaByte, Inc.
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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Random;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.postgresql.util.PSQLException;
import org.postgresql.util.PSQLState;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * Checks transparent read restarts behaviour when using YSQL API.
 * <p>
 * All tests here behave in the same fashion - we're trying different operations (vary between
 * tests) while concurrently running INSERTs on the same table
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgReadRestarts extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgReadRestarts.class);

  /**
   * Size (in bytes) of PG output buffer, longer stuff is flushed immediately.
   * This is controlled by ysql_output_buffer_size gflag, but we're testing the default value.
   */
  private static int PG_OUTPUT_BUFFER_SIZE = 262144; // 256 KiB

  /** How many inserts we attempt to do? */
  private static final int NUM_INSERTS = 1000;

  /** Maximum value to insert in a table column {@code i} (minimum is 0) */
  private static final int MAX_INT_TO_INSERT = 5;

  /**
   * How long do we wait until NUM_INSERTS inserts finish?
   * <p>
   * This should be about way more than average local execution time of a single test, to account
   * for slow CI machines.
   */
  private static final int INSERTS_AWAIT_TIME_SEC = 150;

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      LOG.info("Creating table test_rr");
      stmt.execute("CREATE TABLE test_rr (id SERIAL PRIMARY KEY, t TEXT, i INT)");
    }
  }

  @After
  public void tearDown() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      LOG.info("Dropping table test_rr");
      stmt.execute("DROP TABLE test_rr;");
    }
  }

  /**
   * Doing SELECT COUNT(*) operation and expect restarts to happen transparently
   */
  @Test
  public void selectCount() throws Exception {
    new RegularStatementTester(
        "SELECT COUNT(*) FROM test_rr",
        getShortString(),
        false /* expectNonTxnRestartErrors */,
        false /* expectSnapshotRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectCountPrepared() throws Exception {
    new PreparedStatementTester(
        "SELECT COUNT(*) FROM test_rr",
        getShortString(),
        false /* expectNonTxnRestartErrors */,
        false /* expectSnapshotRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses parameterized PreparedStatements with bindvars.
   */
  @Test
  public void selectCountPreparedParameterized() throws Exception {
    new PreparedStatementTester(
        "SELECT COUNT(*) FROM test_rr WHERE i > ?",
        getShortString(),
        false /* expectNonTxnRestartErrors */,
        false /* expectSnapshotRestartErrors */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        PreparedStatement pstmt = super.createStatement(conn);
        pstmt.setInt(1, 0);
        return pstmt;
      }
    }.runTest();
  }

  /**
   * Doing SELECT * operation on short strings and expect restarts to happen transparently.
   * <p>
   * We expect data retrieved to fit into PG output buffer, thus making transparent read restarts
   * possible.
   */
  @Test
  public void selectStarShort() throws Exception {
    new RegularStatementTester(
        "SELECT * FROM test_rr LIMIT 10",
        getShortString(),
        false /* expectNonTxnRestartErrors */,
        false /* expectSnapshotRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectStarShortPrepared() throws Exception {
    new PreparedStatementTester(
        "SELECT * FROM test_rr LIMIT 10",
        getShortString(),
        false /* expectNonTxnRestartErrors */,
        false /* expectSnapshotRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses parameterized PreparedStatements with bindvars.
   */
  @Test
  public void selectStarShortPreparedParameterized() throws Exception {
    new PreparedStatementTester(
        "SELECT * FROM test_rr WHERE i > ? LIMIT 10",
        getShortString(),
        false /* expectNonTxnRestartErrors */,
        false /* expectSnapshotRestartErrors */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        PreparedStatement pstmt = super.createStatement(conn);
        pstmt.setInt(1, 0);
        return pstmt;
      }
    }.runTest();
  }

  /**
   * Doing SELECT * operation on long strings and expect restarts to NEVER happen.
   * <p>
   * We expect data retrieved to be longer than what PG output buffer could handle, thus making
   * transparent read restarts impossible.
   */
  @Test
  public void selectStarLong() throws Exception {
    new RegularStatementTester(
        "SELECT * FROM test_rr LIMIT 10",
        getLongString(),
        true /* expectNonTxnRestartErrors */,
        true /* expectSnapshotRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectStarLongPrepared() throws Exception {
    new PreparedStatementTester(
        "SELECT * FROM test_rr LIMIT 10",
        getLongString(),
        true /* expectNonTxnRestartErrors */,
        true /* expectSnapshotRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses parameterized PreparedStatements with bindvars.
   */
  @Test
  public void selectStarLongPreparedParameterized() throws Exception {
    new PreparedStatementTester(
        "SELECT * FROM test_rr WHERE i > ? LIMIT 10",
        getLongString(),
        true /* expectNonTxnRestartErrors */,
        true /* expectSnapshotRestartErrors */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        PreparedStatement pstmt = super.createStatement(conn);
        pstmt.setInt(1, 0);
        return pstmt;
      }
    }.runTest();
  }

  //
  // Helpers methods
  //

  /** Some short string to be inserted, order of magnitude shorter than PG output buffer */
  private static String getShortString() {
    return "s";
  }

  /** Some string that is longer than PG output buffer */
  private static String getLongString() {
    StringBuffer sb = new StringBuffer();
    // Making string one char longer than buffer size, hence the "<=" condition
    for (int i = 0; i <= PG_OUTPUT_BUFFER_SIZE; ++i) {
      sb.append("a");
    }
    return sb.toString();
  }

  /** Whether this exception represents expected transaction concurrency related error */
  private static boolean isRestartReadError(Exception ex) {
    String lcMsg = ex.getMessage().toLowerCase();
    return lcMsg.contains("restart read")
        || lcMsg.contains("read restart");
  }

  /** Whether this exception represents expected transaction concurrency related error */
  private static boolean isTxnError(Exception ex) {
    if (!(ex instanceof PSQLException)) {
      return false;
    }
    String sqlState = ((PSQLException) ex).getSQLState();
    // Using Yoda conditions to avoid NPE in the theoretical case of sqlState being null.
    return PSQLState.IN_FAILED_SQL_TRANSACTION.getState().equals(sqlState)
        || SNAPSHOT_TOO_OLD_PSQL_STATE.equals(sqlState)
        || SERIALIZATION_FAILURE_PSQL_STATE.equals(sqlState);
  }

  //
  // Helpers classes
  //

  /** Runnable responsible for inserts. Starts paused, call unpause() when readers are set. */
  private static class InsertRunnable implements Runnable {
    private CountDownLatch startSignal = new CountDownLatch(1);

    private String stringToInsert;

    public InsertRunnable(String stringToInsert) {
      this.stringToInsert = stringToInsert;
    }

    public void unpause() {
      startSignal.countDown();
    }

    public void run() {
      int insertsSucceeded = 0;
      Random rnd = new Random();
      try (Connection insertConn = newConnectionBuilder().connect();
          PreparedStatement stmt = insertConn
              .prepareStatement("INSERT INTO test_rr (t, i) VALUES (?, ?)")) {
        stmt.setString(1, stringToInsert);
        startSignal.await();
        for (int i = 0; i < NUM_INSERTS; ++i) {
          if (Thread.interrupted()) return; // Skips all post-loop checks
          try {
            stmt.setInt(2, rnd.nextInt(MAX_INT_TO_INSERT + 1));
            stmt.executeUpdate();
            ++insertsSucceeded;
          } catch (Exception ex) {
            if (!isTxnError(ex)) {
              throw ex;
            }
          }
        }
      } catch (Exception ex) {
        LOG.error("INSERT thread failed", ex);
        fail("INSERT thread failed: " + ex.getMessage());
      }
      LOG.info("Number of successful INSERT operations: " + insertsSucceeded);
      assertTrue("No INSERT operations succeeded!", insertsSucceeded > 0);
    }
  }

  /**
   * Performs generic testing of concurrent SELECT/INSERT by running several concurrent threads:
   *
   * <ul>
   * <li>INSERT into table
   * <li>Singular SELECT
   * <li>Transaction with two SELECT whose result should match
   * <ul>
   * <li>(one thread per isolation level)
   * </ul>
   * </ul>
   *
   * Caller must specify both the means of creating/executing a query, as well as whether it's
   * expected to get read restart errors while running each of these threads.
   *
   * For the transactional SELECTs, we're only checking for restart read error on first operation.
   * If it happens in the second, that's always valid.
   */
  private abstract class ConcurrentInsertSelectTester<Stmt extends AutoCloseable> {

    /** Number of threads in a fixed thread pool */
    private static final int NUM_THREADS = 4;

    private final String valueToInsert;
    private final boolean expectNonTxnRestartErrors;
    private final boolean expectSnapshotRestartErrors;
    /** We never expect SERIALIZABLE transaction to result in "restart read" */
    private final boolean expectSerializableRestartErrors = false;

    public ConcurrentInsertSelectTester(
        String valueToInsert,
        boolean expectNonTxnRestartErrors,
        boolean expectSnapshotRestartErrors) {
      this.valueToInsert = valueToInsert;
      this.expectNonTxnRestartErrors = expectNonTxnRestartErrors;
      this.expectSnapshotRestartErrors = expectSnapshotRestartErrors;
    }

    public abstract Stmt createStatement(Connection conn) throws Exception;

    public abstract ResultSet executeQuery(Stmt stmt) throws Exception;

    public void runTest() throws Exception {
      ExecutorService es = Executors.newFixedThreadPool(NUM_THREADS);
      List<Future<?>> futures = new ArrayList<>();

      InsertRunnable insertRunnable = new InsertRunnable(valueToInsert);
      Future<?> insertFuture = es.submit(insertRunnable);
      futures.add(insertFuture);

      /*
       * Singular SELECT
       */
      futures.add(es.submit(() -> {
        int selectsAttempted = 0;
        int selectsRestartRequired = 0;
        boolean onlyEmptyResults = true;
        for (/* No setup */; !insertFuture.isDone(); ++selectsAttempted) {
          if (Thread.interrupted()) return; // Skips all post-loop checks
          try (Stmt stmt = createStatement(connection)) {
            List<Row> rows = getRowList(executeQuery(stmt));
            if (!rows.isEmpty()) {
              onlyEmptyResults = false;
            }
          } catch (Exception ex) {
            if (isRestartReadError(ex)) {
              ++selectsRestartRequired;
            } else {
              LOG.error("SELECT thread failed", ex);
              fail("SELECT thread failed: " + ex.getMessage());
            }
          }
        }
        if (onlyEmptyResults) {
          fail("SELECT thread didn't yield any meaningful result! Flawed test?");
        }
        if (expectNonTxnRestartErrors) {
          assertTrue(
              "No SELECTs resulted in 'restart read required' - but we expected them to!",
              selectsRestartRequired > 0);
        } else {
          assertTrue(
              selectsRestartRequired + " of " + selectsAttempted
                  + " SELECTs resulted in 'restart read required', likely a regression!",
              selectsRestartRequired == 0);
        }
      }));

      Map<IsolationLevel, Boolean> isoLevelsWithRestartsExpected = new LinkedHashMap<>();
      isoLevelsWithRestartsExpected.put(
          IsolationLevel.REPEATABLE_READ, expectSnapshotRestartErrors);
      isoLevelsWithRestartsExpected.put(
          IsolationLevel.SERIALIZABLE, expectSerializableRestartErrors);

      /*
       * Two SELECTs grouped in a transaction. Their result should match.
       */
      for (Entry<IsolationLevel, Boolean> isoEntry : isoLevelsWithRestartsExpected.entrySet()) {
        futures.add(es.submit(() -> {
          IsolationLevel isolation = isoEntry.getKey();
          boolean expectRestart = isoEntry.getValue();
          int selectsAttempted = 0;
          int selectsFirstOpRestartRequired = 0;
          int selectsSucceeded = 0;
          try (Connection selectTxnConn = newConnectionBuilder()
              .setIsolationLevel(isolation)
              .connect()) {
            selectTxnConn.setAutoCommit(false);
            for (/* No setup */; !insertFuture.isDone(); ++selectsAttempted) {
              if (Thread.interrupted()) return; // Skips all post-loop checks
              int numCompletedOps = 0;
              try (Stmt stmt = createStatement(selectTxnConn)) {
                List<Row> rows1 = getRowList(executeQuery(stmt));
                ++numCompletedOps;
                List<Row> rows2 = getRowList(executeQuery(stmt));
                ++numCompletedOps;
                selectTxnConn.commit();
                assertEquals("Two SELECT done within same transaction mismatch" +
                    ", " + isolation + " transaction isolation breach!", rows1, rows2);
                ++selectsSucceeded;
              } catch (Exception ex) {
                try {
                  selectTxnConn.rollback();
                } catch (SQLException ex1) {
                  LOG.error("Rollback failed", ex1);
                  fail("Rollback failed: " + ex1.getMessage());
                }
                if (isRestartReadError(ex) && numCompletedOps == 0) {
                  ++selectsFirstOpRestartRequired;
                }
                if (!isTxnError(ex)) {
                  throw ex;
                }
              }
            }
          } catch (Exception ex) {
            LOG.error("SELECT in " + isolation + " thread failed", ex);
            fail("SELECT in " + isolation + " thread failed: " + ex.getMessage());
          }
          LOG.info("SELECT in " + isolation + ": " + selectsSucceeded + " of "
              + selectsAttempted + " succeeded");
          assertTrue("No SELECT operations in " + isolation
              + " succeeded, ever! Flawed test?", selectsSucceeded > 0);
          if (expectRestart) {
            assertTrue(
                "No SELECTs in " + isolation
                    + " resulted in 'restart read required' on first operation"
                    + " - but we expected them to!",
                selectsFirstOpRestartRequired > 0);
          } else {
            assertTrue(
                selectsFirstOpRestartRequired + " of " + selectsAttempted
                    + " SELECTs in " + isolation
                    + " resulted in 'restart read required' on first operation!",
                selectsFirstOpRestartRequired == 0);
          }
        }));
      }

      insertRunnable.unpause();
      try {
        LOG.info("Waiting for INSERT thread");
        insertFuture.get(INSERTS_AWAIT_TIME_SEC, TimeUnit.SECONDS);
        LOG.info("Waiting for SELECT threads");
        for (Future<?> future : futures) {
          future.get(10, TimeUnit.SECONDS);
        }
      } catch (TimeoutException ex) {
        fail("Test timed out! Try increasing waiting time?");
      } finally {
        LOG.info("Shutting down executor service");
        es.shutdownNow(); // This should interrupt all submitted threads
        es.awaitTermination(10, TimeUnit.SECONDS);
        LOG.info("Executor shutdown complete");
      }
    }
  }

  /** ConcurrentInsertSelectTester that uses regular Statement */
  private class RegularStatementTester extends ConcurrentInsertSelectTester<Statement> {
    protected String queryString;

    public RegularStatementTester(
        String queryString,
        String valueToInsert,
        boolean expectNonTxnRestartErrors,
        boolean expectSnapshotRestartErrors) {
      super(valueToInsert, expectNonTxnRestartErrors, expectSnapshotRestartErrors);
      this.queryString = queryString;
    }

    @Override
    public Statement createStatement(Connection conn) throws Exception {
      return conn.createStatement();
    }

    @Override
    public ResultSet executeQuery(Statement stmt) throws Exception {
      return stmt.executeQuery(queryString);
    }
  }

  /** ConcurrentInsertSelectTester that uses regular PreparedStatement */
  private class PreparedStatementTester extends ConcurrentInsertSelectTester<PreparedStatement> {
    protected String queryString;

    public PreparedStatementTester(
        String queryString,
        String valueToInsert,
        boolean expectNonTxnRestartErrors,
        boolean expectSnapshotRestartErrors) {
      super(valueToInsert, expectNonTxnRestartErrors, expectSnapshotRestartErrors);
      this.queryString = queryString;
    }

    @Override
    public PreparedStatement createStatement(Connection conn) throws Exception {
      return conn.prepareStatement(queryString);
    }

    @Override
    public ResultSet executeQuery(PreparedStatement stmt) throws Exception {
      return stmt.executeQuery();
    }
  }
}
