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
import java.util.Arrays;
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
import com.yugabyte.util.PSQLException;
import com.yugabyte.util.PSQLState;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.ThreadUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * Checks transparent retries in case of kReadRestart/kConflict errors when using YSQL API.
 * <p>
 * The following cases are being tested by running in parallel with a thread that INSERTs new
 * rows -
 * <ol>
 * <li> Queries that only read data using SELECT in various forms.
 * <li> DML statements as standalone or within a SELECT func().
 * </ol>
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgTransparentRestarts extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgTransparentRestarts.class);

  /**
   * Size of PG output buffer, longer stuff is flushed immediately. We're setting this to be
   * relatively low to more consistently hit read restarts on long strings.
   */
  private static final int PG_OUTPUT_BUFFER_SIZE_BYTES = 1024;

  /**
   * Size of long strings to provoke read restart errors. This should be significantly larger than
   * {@link #PG_OUTPUT_BUFFER_SIZE_BYTES} to force buffer flushes - thus preventing YSQL from doing
   * a transparent restart.
   */
  private static final int LONG_STRING_LENGTH = PG_OUTPUT_BUFFER_SIZE_BYTES * 100;

  /** Maximum value to insert in a table column {@code i} (minimum is 0) */
  private static final int MAX_INT_TO_INSERT = 5;

  /**
   * How long do we wait until {@link #NUM_INSERTS} {@code INSERT}s finish?
   * <p>
   * This should be way more than average local execution time of a single test, to account for slow
   * CI machines.
   */
  private static final int INSERTS_AWAIT_TIME_SEC = 300;

  /**
   * How long do we wait for {@code SELECT}s to finish after {@code INSERT}s are completed and
   * {@code SELECT} threads are interrupted?
   * <p>
   * Ideally they shouldn't take long, but we need to account for potential network and YB-side
   * slowdown. Overstepping this limit might mean a bug in the {@code SELECT} threads code.
   */
  private static final int SELECTS_AWAIT_TIME_SEC = 20;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_output_buffer_size", String.valueOf(PG_OUTPUT_BUFFER_SIZE_BYTES));
    flags.put("ysql_sleep_before_retry_on_txn_conflict", "true");
    flags.put("ysql_max_write_restart_attempts", "5");
    flags.put("yb_enable_read_committed_isolation", "true");
    return flags;
  }

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
        getConnectionBuilder(),
        "SELECT COUNT(*) FROM test_rr",
        getShortString(),
        false /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectCountPrepared() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder(),
        "SELECT COUNT(*) FROM test_rr",
        getShortString(),
        false /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses parameterized PreparedStatements with bindvars.
   */
  @Test
  public void selectCountPreparedParameterized() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder(),
        "SELECT COUNT(*) FROM test_rr WHERE i >= ?",
        getShortString(),
        false /* expectRestartErrors */) {

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
        getConnectionBuilder(),
        "SELECT * FROM test_rr LIMIT 10",
        getShortString(),
        false /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Doing SELECT * operation on short strings with savepoints enabled and created and expect
   * restarts to to *not* happen transparently.
   * <p>
   * todo
   */
  @Test
  public void selectStarShortWithSavepoints() throws Exception {
    new SavepointStatementTester(
        getConnectionBuilder(),
        getShortString()
    ).runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectStarShortPrepared() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder(),
        "SELECT * FROM test_rr LIMIT 10",
        getShortString(),
        false /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses parameterized PreparedStatements with bindvars.
   */
  @Test
  public void selectStarShortPreparedParameterized() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder(),
        "SELECT * FROM test_rr WHERE i >= ? LIMIT 10",
        getShortString(),
        false /* expectRestartErrors */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        PreparedStatement pstmt = super.createStatement(conn);
        pstmt.setInt(1, 0);
        return pstmt;
      }
    }.runTest();
  }

  /**
   * Same as the previous test but uses an array bindvar (via {@code UNNEST} function).
   * <p>
   * This is a separate case because postgres code binds an array as a pointer to a location within
   * a portal memory context.
   */
  @Test
  public void selectShortPreparedParameterizedArray() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder(),
        "SELECT unnest(?::int[]), * FROM test_rr LIMIT 10",
        getShortString(),
        false /* expectRestartErrors */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        PreparedStatement pstmt = super.createStatement(conn);
        pstmt.setArray(1, conn.createArrayOf("int", new Object[] { 1 }));
        return pstmt;
      }
    }.runTest();
  }

  /**
   * Same as {@link #selectStarShort()} but uses YSQL connections in "simple" mode.
   */
  @Test
  public void selectStarShort_simpleQueryMode() throws Exception {
    new RegularStatementTester(
        getConnectionBuilder().withPreferQueryMode("simple"),
        "SELECT * FROM test_rr LIMIT 10",
        getShortString(),
        false /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectStarShortPrepared_simpleQueryMode() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder().withPreferQueryMode("simple"),
        "SELECT * FROM test_rr LIMIT 10",
        getShortString(),
        false /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses parameterized PreparedStatements with bindvars.
   */
  @Test
  public void selectStarShortPreparedParameterized_simpleQueryMode() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder().withPreferQueryMode("simple"),
        "SELECT * FROM test_rr WHERE i >= ? LIMIT 10",
        getShortString(),
        false /* expectRestartErrors */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        PreparedStatement pstmt = super.createStatement(conn);
        pstmt.setInt(1, 0);
        return pstmt;
      }
    }.runTest();
  }

  /**
   * Same as the previous test but relies on raw PREPARE/EXECUTE rather than JDBC-backed prepared
   * statement.
   */
  @Test
  public void selectStarShortExecute_simpleQueryMode() throws Exception {
    new RegularStatementTester(
        getConnectionBuilder().withPreferQueryMode("simple"),
        "EXECUTE select_stmt(0)",
        getShortString(),
        false /* expectRestartErrors */) {

      @Override
      public Statement createStatement(Connection conn) throws Exception {
        Statement stmt = super.createStatement(conn);
        stmt.execute("PREPARE select_stmt (int) AS SELECT * FROM test_rr WHERE i >= $1 LIMIT 10");
        return stmt;
      };
    }.runTest();
  }

  /**
   * Doing SELECT * operation on long strings, we MIGHT get read restart errors.
   * <p>
   * We expect data retrieved to be longer than what PG output buffer could handle, thus making
   * transparent read restarts impossible.
   */
  @Test
  public void selectStarLong() throws Exception {
    new RegularStatementTester(
        getConnectionBuilder(),
        "SELECT * FROM test_rr",
        getLongString(),
        true /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses PreparedStatements (with no parameters)
   */
  @Test
  public void selectStarLongPrepared() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder(),
        "SELECT * FROM test_rr",
        getLongString(),
        true /* expectRestartErrors */
    ).runTest();
  }

  /**
   * Same as the previous test but uses parameterized PreparedStatements with bindvars.
   */
  @Test
  public void selectStarLongPreparedParameterized() throws Exception {
    new PreparedStatementTester(
        getConnectionBuilder(),
        "SELECT * FROM test_rr WHERE i >= ?",
        getLongString(),
        true /* expectRestartErrors */) {

      @Override
      public PreparedStatement createStatement(Connection conn) throws Exception {
        PreparedStatement pstmt = super.createStatement(conn);
        pstmt.setInt(1, 0);
        return pstmt;
      }
    }.runTest();
  }

  /**
   * The following two methods attempt to test retries on kReadRestart for all below combinations -
   *    1. Type of statement - UPDATE/DELETE.
   *      We already have a parallel INSERT thread running. So don't need a separate insert() test.
   *    2. DML as a single statement or within a SELECT func() or within a txn block?
   *    3. Prepared or not
   *    4. Simple or extended query mode
   */
  @Test
  public void update() throws Exception {
    // Simple or extended query mode
    for (ConnectionBuilder cb: Arrays.asList(
      getConnectionBuilder(),
      getConnectionBuilder().withPreferQueryMode("simple"))) {
      // Case 1: Single statement case
      new RegularDmlStatementTester(
          cb,
          "UPDATE test_rr set i=1 where i=0",
          getShortString()
      ).runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      // Case 2: In SELECT func()
      try (Statement stmt = cb.connect().createStatement()) {
        stmt.execute("CREATE FUNCTION func() RETURNS void AS " +
                     "$$ UPDATE test_rr set i=1 where i=0 $$ LANGUAGE SQL");
      }
      new RegularDmlStatementTester(
          cb,
          "SELECT func()",
          getShortString()
      ).runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      // Case 3: Prepared statement
      new PreparedDmlStatementTester(
          cb,
          "UPDATE test_rr set i=? where i=?",
          getShortString()) {

        @Override
        public PreparedStatement createStatement(Connection conn) throws Exception {
          PreparedStatement pstmt = super.createStatement(conn);
          pstmt.setInt(1, 1);
          pstmt.setInt(2, 0);
          return pstmt;
        }
      }.runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      // Case 4: In SELECT func() with Prepared statement
      new PreparedDmlStatementTester(
          cb,
          "SELECT func()",
          getShortString()
      ).runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      try (Statement stmt = cb.connect().createStatement()) {
        stmt.execute("DROP FUNCTION func()");
      }
    }
  }

  @Test
  public void delete() throws Exception {
    // Simple or extended query mode
    for (ConnectionBuilder cb: Arrays.asList(
        getConnectionBuilder(),
        getConnectionBuilder().withPreferQueryMode("simple"))) {
      // Case 1: Single statement case
      new RegularDmlStatementTester(
          cb,
          "DELETE from test_rr where i > 0",
          getShortString()
      ).runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      // Case 2: In SELECT func()
      try (Statement stmt = cb.connect().createStatement()) {
        stmt.execute("CREATE FUNCTION func() RETURNS void AS " +
                     "$$ DELETE from test_rr where i > 0 $$ LANGUAGE SQL");
      }
      new RegularDmlStatementTester(
          cb,
          "SELECT func()",
          getShortString()
      ).runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      // Case 3: Prepared statement
      new PreparedDmlStatementTester(
          cb,
          "DELETE from test_rr where i > ?",
          getShortString()) {

        @Override
        public PreparedStatement createStatement(Connection conn) throws Exception {
          PreparedStatement pstmt = super.createStatement(conn);
          pstmt.setInt(1, 0);
          return pstmt;
        }
      }.runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      // Case 4: In SELECT func() with Prepared statement
      new PreparedDmlStatementTester(
          cb,
          "SELECT func()",
          getShortString()
      ).runTest();
      try (Statement s = connection.createStatement()) {
        s.execute("truncate table test_rr");
      }

      try (Statement stmt = cb.connect().createStatement()) {
        stmt.execute("DROP FUNCTION func()");
      }
    }
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
    for (int i = 0; i < LONG_STRING_LENGTH; ++i) {
      sb.append("a");
    }
    return sb.toString();
  }

  private static boolean isRestartReadError(Exception ex) {
    String lcMsg = ex.getMessage().toLowerCase();
    return lcMsg.contains("restart read") || lcMsg.contains("read restart");
  }

  // TODO(Piyush): Find a more robust way to check for kConflict/kAbort/kReadRestart
  private static boolean isConflictError(Exception ex) {
    String lcMsg = ex.getMessage().toLowerCase();
    // kAborted messages also have the conflict word sometimes.
    return lcMsg.contains("conflict") && !lcMsg.contains("abort");
  }

  private static boolean isAbortError(Exception ex) {
    return ex.getMessage().toLowerCase().contains("abort");
  }

  private static boolean isRetriesExhaustedError(Exception ex) {
    return ex.getMessage().contains("All transparent retries exhausted");
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

  String IsolationLevelStr(IsolationLevel level) {
    if (level == IsolationLevel.REPEATABLE_READ)
      return "repeatable read";
    else if (level == IsolationLevel.SERIALIZABLE)
      return "serializable";
    else if (level == IsolationLevel.READ_COMMITTED)
      return "read committed";

    assertTrue("Isolation level not supported yet", false);
    return "";
  }

  /** Runnable responsible for inserts. Starts paused, call unpause() when readers are set. */
  private static class InsertRunnable implements Runnable {
    private CountDownLatch startSignal = new CountDownLatch(1);

    /**
     * Connection builder that should be used for creating new connections.
     * <p>
     * <b>WARNING:</b> Builder is mutable! Make sure to copy it before changing its settings.
     */
    private ConnectionBuilder cb;

    private String stringToInsert;
    private int numInserts;

    public InsertRunnable(ConnectionBuilder cb, String stringToInsert, int numInserts) {
      this.cb = cb;
      this.stringToInsert = stringToInsert;
      this.numInserts = numInserts;
    }

    public void unpause() {
      startSignal.countDown();
    }

    public void run() {
      int insertsAttempted = 0;
      int insertsSucceeded = 0;
      int insertsRetriesExhausted = 0;
      int insertsWithReadRestartError = 0;
      int insertsWithConflictError = 0;
      int insertsWithAbortError = 0;
      int insertsWithAbortErrorAtCommit = 0; // kAborted on explicit "commit"
      int insertsWithSnapshotIsolation = 0;
      int insertsWithSerializable = 0;
      int insertsWithReadCommitted = 0;
      int insertsInTxnBlock = 0;
      Random rnd = new Random();
      try (Connection insertSnapshotIsolationConn =
             cb.withIsolationLevel(IsolationLevel.REPEATABLE_READ).connect();
           Connection insertSerializableConn =
             cb.withIsolationLevel(IsolationLevel.SERIALIZABLE).connect();
           Connection insertReadCommittedConn =
             cb.withIsolationLevel(IsolationLevel.READ_COMMITTED).connect();
           PreparedStatement snapshotIsolationStmt = insertSnapshotIsolationConn
               .prepareStatement("INSERT INTO test_rr (t, i) VALUES (?, ?)");
           PreparedStatement serializableStmt = insertSerializableConn
               .prepareStatement("INSERT INTO test_rr (t, i) VALUES (?, ?)");
           PreparedStatement readCommittedStmt = insertReadCommittedConn
               .prepareStatement("INSERT INTO test_rr (t, i) VALUES (?, ?)");
           Statement auxSnapshotIsolationStmt = insertSnapshotIsolationConn.createStatement();
           Statement auxSerializableStmt = insertSerializableConn.createStatement();
           Statement auxReadCommittedStmt = insertReadCommittedConn.createStatement()) {
        auxSnapshotIsolationStmt.execute("set yb_debug_log_internal_restarts=true");
        auxSerializableStmt.execute("set yb_debug_log_internal_restarts=true");
        auxReadCommittedStmt.execute("set yb_debug_log_internal_restarts=true");
        snapshotIsolationStmt.setString(1, stringToInsert);
        serializableStmt.setString(1, stringToInsert);
        readCommittedStmt.setString(1, stringToInsert);
        startSignal.await();
        for (int i = 0; i < numInserts; ++i) {
          boolean runInTxnBlock = rnd.nextBoolean();
          IsolationLevel isolation = rnd.nextDouble() <= 0.33 ? IsolationLevel.REPEATABLE_READ
            : (rnd.nextDouble() <= 0.5 ? IsolationLevel.SERIALIZABLE
                                        : IsolationLevel.READ_COMMITTED);
          PreparedStatement stmt =
            isolation == IsolationLevel.REPEATABLE_READ ? snapshotIsolationStmt :
              (isolation == IsolationLevel.SERIALIZABLE ? serializableStmt : readCommittedStmt);
          Statement auxStmt =
            isolation == IsolationLevel.REPEATABLE_READ ? auxSnapshotIsolationStmt :
              (isolation == IsolationLevel.SERIALIZABLE ? auxSerializableStmt
                                                        : auxReadCommittedStmt);
          if (Thread.interrupted()) return; // Skips all post-loop checks
          try {
            ++insertsAttempted;
            if (isolation == IsolationLevel.REPEATABLE_READ)
              ++insertsWithSnapshotIsolation;
            else if (isolation == IsolationLevel.SERIALIZABLE)
              ++insertsWithSerializable;
            else
              ++insertsWithReadCommitted;

            if (runInTxnBlock) {
              auxStmt.execute("start transaction");
              ++insertsInTxnBlock;
            }
            stmt.setInt(2, rnd.nextInt(MAX_INT_TO_INSERT + 1));
            stmt.executeUpdate();
            if (runInTxnBlock) {
              try {
                auxStmt.execute("commit");
                ++insertsSucceeded;
              } catch (Exception ex) {
                if (!isTxnError(ex)) {
                  fail("Unexpected error in INSERT: isolation=" + isolation + " runInTxnBlock=" +
                       runInTxnBlock + " " + ex.getMessage());
                } else if (isRetriesExhaustedError(ex)) {
                  ++insertsRetriesExhausted;
                } else if (isConflictError(ex)) {
                  ++insertsWithConflictError;
                } else if (isRestartReadError(ex)) {
                  ++insertsWithReadRestartError;
                }  else if (isAbortError(ex)) {
                  ++insertsWithAbortErrorAtCommit;
                } else {
                  fail("Unexpected error in INSERT: isolation=" + isolation + " runInTxnBlock=" +
                       runInTxnBlock + " " + ex.getMessage());
                }
                try {
                  auxStmt.execute("rollback");
                } catch (Exception ex2) {
                  fail("rollback failed " + ex2.getMessage());
                }
              }
            } else {
              ++insertsSucceeded;
            }
          } catch (Exception ex) {
            if (!isTxnError(ex)) {
              fail("Unexpected error in INSERT: isolation=" + isolation + " runInTxnBlock=" +
                   runInTxnBlock + " " + ex.getMessage());
            } else if (isRetriesExhaustedError(ex)) {
              ++insertsRetriesExhausted;
            } else if (isConflictError(ex)) {
              ++insertsWithConflictError;
            } else if (isRestartReadError(ex)) {
              ++insertsWithReadRestartError;
            }  else if (isAbortError(ex)) {
              ++insertsWithAbortError;
            } else {
              fail("Unexpected error in INSERT: isolation=" + isolation + " runInTxnBlock=" +
                   runInTxnBlock + " " + ex.getMessage());
            }
            if (runInTxnBlock) {
              try {
                auxStmt.execute("rollback");
              } catch (Exception ex2) {
                fail("rollback failed " + ex2.getMessage());
              }
            }
          }
        }
      } catch (Exception ex) {
        LOG.error("INSERT thread failed", ex);
        fail("INSERT thread failed: " + ex.getMessage());
      }
      LOG.info(
          "insertsAttempted=" + insertsAttempted + "\n" +
          " insertsSucceeded=" + insertsSucceeded + "\n" +
          " insertsRetriesExhausted=" + insertsRetriesExhausted + "\n" +
          " insertsWithReadRestartError=" + insertsWithReadRestartError + "\n" +
          " insertsWithConflictError=" + insertsWithConflictError + "\n" +
          " insertsWithAbortError=" + insertsWithAbortError + "\n" +
          " insertsWithAbortErrorAtCommit=" + insertsWithAbortErrorAtCommit + "\n" +
          " insertsWithSnapshotIsolation=" + insertsWithSnapshotIsolation + "\n" +
          " insertsWithSerializable=" + insertsWithSerializable + "\n" +
          " insertsWithReadCommitted=" + insertsWithReadCommitted + "\n" +
          " insertsInTxnBlock=" + insertsInTxnBlock);
      assertTrue("No INSERT operations succeeded!", insertsSucceeded > 0);
      assertTrue(insertsWithConflictError == 0 && insertsWithReadRestartError == 0);
    }
  }

  /**
   * Performs generic testing of concurrent INSERT with other queries by running several
   * concurrent threads.
   */
  private abstract class ConcurrentInsertQueryTester<Stmt extends AutoCloseable> {

    /** Number of threads in a fixed thread pool */
    private static final int NUM_THREADS = 5;
    private int numInserts;

    private final ConnectionBuilder cb;

    private final String valueToInsert;

    public ConcurrentInsertQueryTester(ConnectionBuilder cb, String valueToInsert,
                                       int numInserts) {
      this.cb = cb;
      this.valueToInsert = valueToInsert;
      this.numInserts = numInserts;
    }

    public abstract List<Runnable> getRunnableThreads(ConnectionBuilder cb, Future<?> execution);

    public void runTest() throws Exception {
      ExecutorService es = Executors.newFixedThreadPool(NUM_THREADS);
      List<Future<?>> futures = new ArrayList<>();

      InsertRunnable insertRunnable = new InsertRunnable(cb, valueToInsert, numInserts);
      Future<?> insertFuture = es.submit(insertRunnable);
      futures.add(insertFuture);

      for (Runnable r : getRunnableThreads(cb, insertFuture)) {
        futures.add(es.submit(r));
      }

      insertRunnable.unpause();
      try {
        try {
          LOG.info("Waiting for INSERT thread");
          insertFuture.get(INSERTS_AWAIT_TIME_SEC, TimeUnit.SECONDS);
        } catch (TimeoutException ex) {
          LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
          fail("Test timed out! Try increasing waiting time?");
        }
        try {
          LOG.info("Waiting for other threads");
          for (Future<?> future : futures) {
            future.get(SELECTS_AWAIT_TIME_SEC, TimeUnit.SECONDS);
          }
        } catch (TimeoutException ex) {
          LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
          // It's very likely that cause lies on a YB side (e.g. unexpected performance slowdown),
          // not in test.
          fail("Waiting for other threads timed out, this is unexpected!");
        }
      } finally {
        LOG.info("Shutting down executor service");
        es.shutdownNow(); // This should interrupt all submitted threads
        if (es.awaitTermination(10, TimeUnit.SECONDS)) {
          LOG.info("Executor shutdown complete");
        } else {
          LOG.info("Executor shutdown failed (timed out)");
        }
      }
    }
  }

  /**
   * Performs generic testing of concurrent SELECT/INSERT by running several concurrent threads:
   *
   * <ul>
   * <li>INSERT into table
   * <li>Singular SELECT
   * <li>Transaction with two SELECTs
   * <ul>
   * <li>(one thread per isolation level)
   * </ul>
   * </ul>
   *
   * Caller must specify both the means of creating/executing a query, as well as whether it's
   * expected to get read restart errors while running each of these threads.
   *
   * For the transactional SELECTs, we're only checking for restart read error on first operation.
   * If it happens in the second, that's always valid (except for READ COMMITTED transactions since
   * retries are handled on a per statement level in this isolation).
   */
  private abstract class ConcurrentInsertSelectTester<Stmt extends AutoCloseable>
      extends ConcurrentInsertQueryTester<Stmt>{
    /** Whether we expect read restart/conflict errors to happen */
    private final boolean expectRestartErrors;

    public ConcurrentInsertSelectTester(
        ConnectionBuilder cb,
        String valueToInsert,
        boolean expectRestartErrors) {
      super(cb, valueToInsert, 500 /* numInserts */);
      this.expectRestartErrors = expectRestartErrors;
    }

    public abstract Stmt createStatement(Connection conn) throws Exception;

    public abstract ResultSet executeQuery(Stmt stmt) throws Exception;

    @Override
    public List<Runnable> getRunnableThreads(ConnectionBuilder cb, Future<?> execution) {
      List<Runnable> runnables = new ArrayList<>();
      //
      // Singular SELECT statement (1/3 probability of being either snapshot/ serializable/ read
      // committed isolation level)
      //
      runnables.add(() -> {
        int selectsAttempted = 0;
        int selectsRetriesExhausted = 0;
        int selectsRestartRequired = 0;
        int selectsWithAbortError = 0;
        int selectsWithConflictError = 0;
        int selectsSucceeded = 0;
        int selectsWithSnapshotIsolation = 0;
        int selectsWithSerializable = 0;

        boolean onlyEmptyResults = true;
        try (Connection snapshotIsolationConn =
                cb.withIsolationLevel(IsolationLevel.REPEATABLE_READ).connect();
             Stmt snapshotIsolationStmt = createStatement(snapshotIsolationConn);
             Connection serializableConn =
                cb.withIsolationLevel(IsolationLevel.REPEATABLE_READ).connect();
             Stmt serializableStmt = createStatement(serializableConn);
             Connection readCommittedConn =
                cb.withIsolationLevel(IsolationLevel.READ_COMMITTED).connect();
             Stmt readCommittedStmt = createStatement(readCommittedConn);) {
          try (Statement auxSnapshotIsolationStatement = snapshotIsolationConn.createStatement()) {
            auxSnapshotIsolationStatement.execute("set yb_debug_log_internal_restarts=true");
          }
          try (Statement auxSerializableStatement = serializableConn.createStatement()) {
            auxSerializableStatement.execute("set yb_debug_log_internal_restarts=true");
          }
          try (Statement auxReadCommittedStatement = readCommittedConn.createStatement()) {
            auxReadCommittedStatement.execute("set yb_debug_log_internal_restarts=true");
          }
          Random rnd = new Random();

          for (/* No setup */; !execution.isDone(); ++selectsAttempted) {
            if (Thread.interrupted()) return; // Skips all post-loop checks
            IsolationLevel isolation = rnd.nextDouble() <= 0.33 ? IsolationLevel.REPEATABLE_READ
              : (rnd.nextDouble() <= 0.5 ? IsolationLevel.SERIALIZABLE
                                          : IsolationLevel.READ_COMMITTED);
            Stmt stmt = isolation == IsolationLevel.REPEATABLE_READ ? snapshotIsolationStmt
                : (isolation == IsolationLevel.SERIALIZABLE ? serializableStmt
                                                            : readCommittedStmt);
            try {
              List<Row> rows = getRowList(executeQuery(stmt));
              if (!rows.isEmpty()) {
                onlyEmptyResults = false;
              }
              ++selectsSucceeded;
            } catch (Exception ex) {
              if (!isTxnError(ex)) {
                fail("SELECT thread failed: " + ex.getMessage());
              } else if (isRetriesExhaustedError(ex)) {
                ++selectsRetriesExhausted;
              } else if (isRestartReadError(ex)) {
                ++selectsRestartRequired;
              } else if (isAbortError(ex)) {
                ++selectsWithAbortError;
              } else if (isConflictError(ex)) {
                ++selectsWithConflictError;
              } else {
                fail("SELECT thread failed: " + ex.getMessage());
              }
            }
          }
        } catch (Exception ex) {
          LOG.error("Connection-wide exception! This shouldn't happen", ex);
          fail("Connection-wide exception! This shouldn't happen: " + ex.getMessage());
        }
        LOG.info("SELECT (non-txn): " +
            " selectsAttempted=" + selectsAttempted +
            " selectsRetriesExhausted=" + selectsRetriesExhausted +
            " selectsRestartRequired=" + selectsRestartRequired +
            " selectsWithAbortError=" + selectsWithAbortError +
            " selectsWithConflictError=" + selectsWithConflictError +
            " selectsSucceeded=" + selectsSucceeded +
            " selectsWithSnapshotIsolation=" + selectsWithSnapshotIsolation +
            " selectsWithSerializable=" + selectsWithSerializable);

        assertTrue(expectRestartErrors || selectsRestartRequired == 0);
        assertTrue(expectRestartErrors || selectsWithConflictError == 0);

        if (onlyEmptyResults) {
          fail("SELECT (non-txn) thread didn't yield any meaningful result! Flawed test?");
        }
      });

      List<IsolationLevel> isoLevels = Arrays.asList(IsolationLevel.REPEATABLE_READ,
                                                     IsolationLevel.SERIALIZABLE,
                                                     IsolationLevel.READ_COMMITTED);

      //
      // Two SELECTs grouped in a transaction. Their result should match for REPEATABLE READ
      // and SERIALIZABLE level. For READ COMMITTED, it is likely they won't match due to the
      // parallel inserts. And so, for READ COMMITTED ensure that there is atleast one instance
      // where the results don't match.
      //
      isoLevels.forEach((isolation) -> {
        runnables.add(() -> {
          int txnsAttempted = 0;
          int selectsRetriesExhausted = 0;
          int selectsFirstOpRestartRequired = 0;
          int selectsSecondOpRestartRequired = 0;
          int selectsFirstOpConflictDetected = 0;
          int txnsSucceeded = 0;
          int selectsWithAbortError = 0;
          boolean resultsAlwaysMatched = true;

          // We never expect SNAPSHOT ISOLATION/ READ COMMITTED transaction to result in "conflict"
          // We never expect SERIALIZABLE transaction to result in "restart read required"
          boolean expectReadRestartErrors = this.expectRestartErrors &&
                                            (isolation == IsolationLevel.REPEATABLE_READ ||
                                            isolation == IsolationLevel.READ_COMMITTED);
          boolean expectConflictErrors = this.expectRestartErrors &&
                                         isolation == IsolationLevel.SERIALIZABLE;

          try (Connection selectTxnConn = cb.withIsolationLevel(isolation).connect();
              Stmt stmt = createStatement(selectTxnConn)) {
            try (Statement auxStmt = selectTxnConn.createStatement()) {
              auxStmt.execute("set yb_debug_log_internal_restarts=true");
            }
            selectTxnConn.setAutoCommit(false);
            for (/* No setup */; !execution.isDone(); ++txnsAttempted) {
              if (Thread.interrupted()) return; // Skips all post-loop checks
              int numCompletedOps = 0;
              try {
                List<Row> rows1 = getRowList(executeQuery(stmt));
                ++numCompletedOps;
                if (Thread.interrupted()) return; // Skips all post-loop checks
                List<Row> rows2 = getRowList(executeQuery(stmt));
                ++numCompletedOps;
                selectTxnConn.commit();
                assertTrue("Two SELECTs done within same transaction mismatch" +
                           ", " + isolation + " transaction isolation breach!",
                           rows1.equals(rows2) || (isolation == IsolationLevel.READ_COMMITTED));
                resultsAlwaysMatched = resultsAlwaysMatched && rows1.equals(rows2);
                ++txnsSucceeded;
              } catch (Exception ex) {
                if (!isTxnError(ex)) {
                  throw ex;
                }
                if (isRetriesExhaustedError(ex)) {
                  ++selectsRetriesExhausted;
                } else if (isRestartReadError(ex)) {
                  if (numCompletedOps == 0) ++selectsFirstOpRestartRequired;
                  if (numCompletedOps == 1) ++selectsSecondOpRestartRequired;
                } else if (isConflictError(ex)) {
                  if (numCompletedOps == 0) ++selectsFirstOpConflictDetected;
                } else if (isAbortError(ex)) {
                  ++selectsWithAbortError;
                }
                try {
                  selectTxnConn.rollback();
                } catch (SQLException ex1) {
                  LOG.error("Rollback failed", ex1);
                  fail("Rollback failed: " + ex1.getMessage());
                }
              }
            }
          } catch (Exception ex) {
            LOG.error("SELECT in " + isolation + " thread failed", ex);
            fail("SELECT in " + isolation + " thread failed: " + ex.getMessage());
          }
          LOG.info("SELECT in " + isolation + ": " +
              " txnsAttempted=" + txnsAttempted +
              " selectsRetriesExhausted=" + selectsRetriesExhausted +
              " selectsFirstOpRestartRequired=" + selectsFirstOpRestartRequired +
              " selectsSecondOpRestartRequired=" + selectsSecondOpRestartRequired +
              " selectsFirstOpConflictDetected=" + selectsFirstOpConflictDetected +
              " txnsSucceeded=" + txnsSucceeded +
              " selectsWithAbortError=" + selectsWithAbortError);

          if (expectReadRestartErrors) {
            assertTrue(selectsFirstOpRestartRequired > 0 && selectsSecondOpRestartRequired > 0);
          }
          else {
            assertTrue(selectsFirstOpRestartRequired == 0);
            if (isolation == IsolationLevel.REPEATABLE_READ) {
              // Read restart errors are retried transparently only for the first statement in the
              // transaction
              assertTrue(selectsSecondOpRestartRequired > 0);
            } else if (isolation == IsolationLevel.READ_COMMITTED) {
              // Read restarts retries are performed transparently for each statement in
              // READ COMMITTED isolation
              assertTrue(selectsSecondOpRestartRequired == 0);
            } else if (isolation == IsolationLevel.SERIALIZABLE) {
              // Read restarts can't occur in SERIALIZABLE isolation
              assertTrue(selectsSecondOpRestartRequired == 0);
            } else {
              assertTrue(false); // Shouldn't reach here.
            }
          }

          assertTrue(
            (!expectConflictErrors && selectsFirstOpConflictDetected == 0) ||
            (expectConflictErrors && selectsFirstOpConflictDetected > 0));

          // If we (at all) expect restart/conflict errors, then we cannot guarantee that any
          // operation would succeed.
          if (!(expectReadRestartErrors || expectConflictErrors)) {
            assertTrue("No txns in " + isolation + " succeeded, ever! Flawed test?",
                       txnsSucceeded > 0);
          }
          assertTrue("It can't be the case that results were always same in both SELECTs at " +
                     "READ COMMITTED isolation level",
                     (isolation != IsolationLevel.READ_COMMITTED && resultsAlwaysMatched) ||
                     (isolation == IsolationLevel.READ_COMMITTED && !resultsAlwaysMatched));
        });
      });

      return runnables;
    }
  }

  /** ConcurrentInsertSelectTester that uses regular Statement */
  private class RegularStatementTester extends ConcurrentInsertSelectTester<Statement> {
    protected String queryString;

    public RegularStatementTester(
        ConnectionBuilder cb,
        String queryString,
        String valueToInsert,
        boolean expectRestartErrors) {
      super(cb, valueToInsert, expectRestartErrors);
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
        ConnectionBuilder cb,
        String queryString,
        String valueToInsert,
        boolean expectRestartErrors) {
      super(cb, valueToInsert, expectRestartErrors);
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

  /** SavepointStatementTester that uses regular Statement and savepoints in various places */
  private class SavepointStatementTester extends ConcurrentInsertQueryTester<Statement> {
    public SavepointStatementTester(
        ConnectionBuilder cb,
        String valueToInsert) {
      super(cb, valueToInsert, 1000 /* numInserts */);
    }

    private Runnable getRunnableThread(
        ConnectionBuilder cb, Future<?> execution, String secondSavepointOpString) {
      return () -> {
        int selectsAttempted = 0;
        int selectsPostSecondSavepointOpRestartRequired = 0;
        int selectsSucceeded = 0;
        try (Connection selectTxnConn = cb.withIsolationLevel(
              IsolationLevel.REPEATABLE_READ).connect();
              Statement stmt = selectTxnConn.createStatement()) {
          selectTxnConn.setAutoCommit(false);
          for (/* No setup */; !execution.isDone(); ++selectsAttempted) {
            if (Thread.interrupted()) return; // Skips all post-loop checks
            int numCompletedOps = 0;
            try {
              stmt.execute("SAVEPOINT a");
              List<Row> rows1 = getRowList(stmt.executeQuery("SELECT * from test_rr LIMIT 1"));
              ++numCompletedOps;
              if (Thread.interrupted()) return; // Skips all post-loop checks

              stmt.execute(secondSavepointOpString);
              List<Row> rows2 = getRowList(stmt.executeQuery("SELECT * from test_rr LIMIT 1"));
              ++numCompletedOps;
              if (Thread.interrupted()) return; // Skips all post-loop checks

              selectTxnConn.commit();
              assertEquals("Two SELECT done within same transaction mismatch", rows1, rows2);
              ++selectsSucceeded;
            } catch (Exception ex) {
              try {
                selectTxnConn.rollback();
              } catch (SQLException ex1) {
                fail("Rollback failed: " + ex1.getMessage());
              }
              if (isRestartReadError(ex)) {
                if (numCompletedOps == 1) {
                  ++selectsPostSecondSavepointOpRestartRequired;
                }
              }
              if (!isTxnError(ex)) {
                throw ex;
              }
            }
          }
        } catch (Exception ex) {
          fail("SELECT in savepoint thread failed: " + ex.getMessage());
        }
        LOG.info(
            "SELECT in savepoint thread with second savepoint op \"" + secondSavepointOpString
            + "\": " + selectsSucceeded + " of " + selectsAttempted + " succeeded");
        assertTrue(
            "No SELECTs after second savepoint statement: " + secondSavepointOpString
                + " resulted in 'restart read required' on second operation"
                + " - but we expected them to!"
                + " " + selectsAttempted + " attempted, " + selectsSucceeded + " succeeded",
                selectsPostSecondSavepointOpRestartRequired > 0);
      };
    }

    @Override
    public List<Runnable> getRunnableThreads(ConnectionBuilder cb, Future<?> execution) {
      List<Runnable> runnables = new ArrayList<>();
      runnables.add(getRunnableThread(cb, execution, "SAVEPOINT b"));
      runnables.add(getRunnableThread(cb, execution, "ROLLBACK TO a"));
      runnables.add(getRunnableThread(cb, execution, "RELEASE a"));
      return runnables;
    }
  }

  /* DmlTester tests read restarts for DML statements. */
  private abstract class DmlTester <Stmt extends AutoCloseable> extends
      ConcurrentInsertQueryTester<Stmt> {
    public abstract Stmt createStatement(Connection conn) throws Exception;
    public abstract boolean execute(Stmt stmt) throws Exception;

    public DmlTester(ConnectionBuilder cb, String valueToInsert) {
      super(cb, valueToInsert, 10 /* numInserts */);
    }

    @Override
    public List<Runnable> getRunnableThreads(ConnectionBuilder cb, Future<?> execution) {
      List<IsolationLevel> isoLevels = Arrays.asList(IsolationLevel.REPEATABLE_READ,
                                                     IsolationLevel.SERIALIZABLE,
                                                     IsolationLevel.READ_COMMITTED);
      List<Runnable> runnables = new ArrayList<>();
      isoLevels.forEach((isolation) -> {
        runnables.add(() -> {
          LOG.info("Starting DmlTester's runnable...");

          int executionsAttempted = 0;
          int executionsSucceeded = 0;
          int executionsRetriesExhausted = 0;
          int executionsRestartRequired = 0;
          int executionsConflictErrors = 0;
          int executionsAbortedByOtherTxn = 0;
          int executionsAbortedByOtherTxnAtCommit = 0; // kAborted on explicit "commit"
          int executionsRanInTxnBlock = 0;

          try (Connection conn = cb.withIsolationLevel(isolation).connect();
               Stmt stmt = createStatement(conn);
               Statement auxStmt = conn.createStatement()) {
            auxStmt.execute("set yb_debug_log_internal_restarts=true");
            for (/* No setup */; !execution.isDone(); ++executionsAttempted) {
              if (Thread.interrupted()) return; // Skips all post-loop checks
              Random rnd = new Random();
              boolean runInTxnBlock = rnd.nextDouble() <= 0.5;
              executionsRanInTxnBlock += runInTxnBlock ? 1 : 0;
              try {
                if (runInTxnBlock)
                  auxStmt.execute("start transaction isolation level " +
                    IsolationLevelStr(isolation));
                execute(stmt);
                if (runInTxnBlock) {
                  try {
                    auxStmt.execute("commit");
                  } catch (Exception ex) {
                    if (!isTxnError(ex)) {
                      fail("commit faced unknown error: " + ex.getMessage());
                    } else if (isRetriesExhaustedError(ex)) {
                      ++executionsRetriesExhausted;
                    } else if (isAbortError(ex)) {
                      // A txn might have already been aborted by another txn before commit
                      ++executionsAbortedByOtherTxnAtCommit;
                    } else {
                      fail("commit faced unknown error: " + ex.getMessage());
                    }
                    try {
                      auxStmt.execute("rollback");
                    } catch (Exception ex2) {
                      fail("rollback shouldn't fail: " + ex2.getMessage());
                    }
                  }
                }
                ++executionsSucceeded;
              } catch (Exception ex) {
                if (!isTxnError(ex)) {
                  fail("faced unknown error: " + ex.getMessage());
                } else if (isRetriesExhaustedError(ex)) {
                  ++executionsRetriesExhausted;
                } else if (isRestartReadError(ex)) {
                  ++executionsRestartRequired;
                } else if (isAbortError(ex)) {
                  ++executionsAbortedByOtherTxn;
                } else if (isConflictError(ex)) {
                  // A conflict can only occur when the read time of the UPDATE
                  // is the same as the write time of when a row was inserted.
                  ++executionsConflictErrors;
                } else {
                  fail("failed with ex for runInTxnBlock=" + runInTxnBlock + ": " +
                       ex.getMessage());
                }
                if (runInTxnBlock) {
                  try {
                    auxStmt.execute("rollback");
                  } catch (Exception ex2) {
                    fail("rollback shouldn't fail: " + ex2.getMessage());
                  }
                }
              }
            }
            LOG.info(
                "isolation=" + IsolationLevelStr(isolation) + "\n" +
                " executionsAttempted=" + executionsAttempted + "\n" +
                " executionsRanInTxnBlock=" + executionsRanInTxnBlock + "\n" +
                " executionsSucceeded=" + executionsSucceeded + "\n" +
                " executionsRetriesExhausted=" + executionsRetriesExhausted + "\n" +
                " executionsRestartRequired=" + executionsRestartRequired + "\n" +
                " executionsConflictErrors=" + executionsConflictErrors + "\n" +
                " executionsAbortedByOtherTxn=" + executionsAbortedByOtherTxn + "\n" +
                " executionsAbortedByOtherTxnAtCommit=" +
                    executionsAbortedByOtherTxnAtCommit);
            assertTrue(executionsRestartRequired == 0);
            assertTrue(executionsConflictErrors == 0);
          } catch (Exception ex) {
            fail(ex.getMessage());
          }
        });
      });

      return runnables;
    }
  }

  /** RegularDmlStatementTester that uses regular Statement */
  private class RegularDmlStatementTester extends DmlTester<Statement> {
    protected String queryString;

    public RegularDmlStatementTester(
        ConnectionBuilder cb,
        String queryString,
        String valueToInsert) {
      super(cb, valueToInsert);
      this.queryString = queryString;
    }

    @Override
    public Statement createStatement(Connection conn) throws Exception {
      return conn.createStatement();
    }

    @Override
    public boolean execute(Statement stmt) throws Exception {
      return stmt.execute(queryString);
    }
  }

  /** PreparedDmlStatementTester that uses PreparedStatement */
  private class PreparedDmlStatementTester extends DmlTester<PreparedStatement> {
    protected String queryString;

    public PreparedDmlStatementTester(
        ConnectionBuilder cb,
        String queryString,
        String valueToInsert) {
      super(cb, valueToInsert);
      this.queryString = queryString;
    }

    @Override
    public PreparedStatement createStatement(Connection conn) throws Exception {
      return conn.prepareStatement(queryString);
    }

    @Override
    public boolean execute(PreparedStatement stmt) throws Exception {
      return stmt.execute();
    }
  }
}
