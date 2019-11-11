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

  /** How many inserts we attempt to do? */
  private static final int NUM_INSERTS = 1000;

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
    String queryString = "SELECT COUNT(*) FROM test_rr";
    new ConcurrentInsertSelectTester<Statement>(
        getShortString(),
        false /* expectNonTxnRestarts */ ,
        false /* expectSnapshotRestarts */ ,
        false /* expectSerializableRestart */) {

      @Override
      public Statement createStatement(Connection conn) throws Exception {
        return conn.createStatement();
      }

      @Override
      public ResultSet executeQuery(Statement stmt) throws Exception {
        return stmt.executeQuery(queryString);
      }
    }.runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectCountPrepared() throws Exception {
    String queryString = "SELECT COUNT(*) FROM test_rr";

    new ConcurrentInsertSelectTester<PreparedStatement>(
        getShortString(),
        false /* expectNonTxnRestarts */ ,
        false /* expectSnapshotRestarts */ ,
        false /* expectSerializableRestart */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        return conn.prepareStatement(queryString);
      }

      @Override
      public ResultSet executeQuery(PreparedStatement stmt) throws Exception {
        return stmt.executeQuery();
      }
    }.runTest();
  }

  //
  // Helpers methods
  //

  /** Some short string to be inserted, order of magnitude shorter than PG network buffer */
  private static String getShortString() {
    return "Some arbitrary string";
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
            stmt.setInt(2, rnd.nextInt());
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
    private final boolean expectNonTxnRestarts;
    private final boolean expectSnapshotRestarts;
    private final boolean expectSerializableRestart;

    public ConcurrentInsertSelectTester(
        String valueToInsert,
        boolean expectNonTxnRestarts,
        boolean expectSnapshotRestarts,
        boolean expectSerializableRestart) {
      this.valueToInsert = valueToInsert;
      this.expectNonTxnRestarts = expectNonTxnRestarts;
      this.expectSnapshotRestarts = expectSnapshotRestarts;
      this.expectSerializableRestart = expectSerializableRestart;
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
        if (!expectNonTxnRestarts) {
          assertEquals(
              selectsRestartRequired + " of " + selectsAttempted
                  + " SELECTs resulted in 'restart read required', likely a regression!",
              selectsRestartRequired, 0);
        }
      }));

      Map<IsolationLevel, Boolean> isoLevelsWithRestartsExpected = new LinkedHashMap<>();
      isoLevelsWithRestartsExpected.put(IsolationLevel.REPEATABLE_READ, expectSnapshotRestarts);
      isoLevelsWithRestartsExpected.put(IsolationLevel.SERIALIZABLE, expectSerializableRestart);

      /*
       * Two SELECTs grouped in a transaction. Their result should match.
       */
      for (Entry<IsolationLevel, Boolean> isoEntry : isoLevelsWithRestartsExpected.entrySet()) {
        futures.add(es.submit(() -> {
          IsolationLevel isolation = isoEntry.getKey();
          boolean expectRestart = isoEntry.getValue();
          int selectsAttempted = 0;
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
                if (isRestartReadError(ex)
                    && numCompletedOps == 0
                    && !expectRestart) {
                  fail("SELECT in " + isolation
                      + ": got 'restart read required' on first operation!");
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
}
