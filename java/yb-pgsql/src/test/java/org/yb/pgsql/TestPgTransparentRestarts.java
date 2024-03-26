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
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Random;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeoutException;
import java.util.function.BooleanSupplier;
import java.util.stream.Collectors;

import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import static org.yb.util.BuildTypeUtil.getTimeoutMultiplier;

import org.yb.client.YBClient;
import org.yb.util.CatchingThread;
import org.yb.util.RandomUtil;
import org.yb.util.ThreadUtil;
import org.yb.util.ThrowingRunnable;
import org.yb.YBTestRunner;

import com.yugabyte.util.PSQLException;
import com.yugabyte.util.PSQLState;

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
@RunWith(value = YBTestRunner.class)
public class TestPgTransparentRestarts extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgTransparentRestarts.class);

  /**
   * Size of PG output buffer, longer stuff is flushed immediately. We're setting this to be
   * relatively low to more consistently hit read restarts on long strings.
   */
  private static final int PG_OUTPUT_BUFFER_SIZE_BYTES = 1024;

  /**
   * There are some test cases to verify that automatic query restarts are not happening
   * if the query has started to return data to the client. The tests are based on relatively high
   * probability of the fact, that the tablet that would initiate query restart is encountered
   * later in the scan, so the scan would start emitting some rows. Combined with too long output
   * row and too short output buffer cause buffer to flush, triggering the condition when the query
   * should not restart automatically.
   *
   * However, probability drastically changes when request are sent in parallel. If there is a
   * tablet that would require query restart, it responds immediately, and there is a high
   * probability that it is received first, before any rows are emitted.
   *
   * Solution is to create significantly more tablets than the parallelism level, so there is a
   * higher chance that the tablet that would initiate query restart is not in the first batch of
   * requests.
   *
   * Default parallelism is 6, gives the probability of about 75%. Number of tablets may be
   * increased or parallelism may be capped if tests that expect restart error fail too often.
   */
  private static final int NUM_TABLETS = 24;

  /**
   * Size of long strings to provoke read restart errors. This should be comparable to
   * {@link #PG_OUTPUT_BUFFER_SIZE_BYTES} so as to force buffer flushes - thus preventing YSQL from
   * doing a transparent restart.
   */
  private static final int LONG_STRING_LENGTH = PG_OUTPUT_BUFFER_SIZE_BYTES;

  /** Maximum value to insert in a table column {@code i} (minimum is 0) */
  private static final int MAX_INT_TO_INSERT = 5;

  /**
   * How long do we wait until {@link #NUM_INSERTS} {@code INSERT}s finish?
   * <p>
   * This should be way more than average local execution time of a single test, to account for slow
   * CI machines.
   */
  private static final int INSERTS_AWAIT_TIME_SEC = (int) (150 * getTimeoutMultiplier());

  /**
   * How long do we wait for {@code SELECT}s to finish after {@code INSERT}s are completed and
   * {@code SELECT} threads are interrupted?
   * <p>
   * Ideally they shouldn't take long, but we need to account for potential network and YB-side
   * slowdown. Overstepping this limit might mean a bug in the {@code SELECT} threads code.
   */
  private static final int SELECTS_AWAIT_TIME_SEC = 20;

  private static final String LOG_RESTARTS_SQL =
      "SET yb_debug_log_internal_restarts TO true";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_output_buffer_size", String.valueOf(PG_OUTPUT_BUFFER_SIZE_BYTES));
    flags.put("yb_enable_read_committed_isolation", "true");
    flags.put("wait_queue_poll_interval_ms", "5");
    return flags;
  }

  @Before
  public void setUp() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      LOG.info("Creating table test_rr");
      stmt.execute(String.format("CREATE TABLE test_rr (id SERIAL PRIMARY KEY, t TEXT, i INT) " +
                                 "SPLIT INTO %d TABLETS", NUM_TABLETS));
    }
  }

  @After
  public void tearDown() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      LOG.info("Dropping table test_rr");
      stmt.execute("DROP TABLE test_rr;");
      LOG.info("Completed drop table");
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
    ConnectionBuilder cb = getConnectionBuilder();
    // Case 1: Single statement case
    new RegularDmlStatementTester(
        cb,
        "UPDATE test_rr set i=1 where i=0",
        getShortString(),
        false /* is_deadlock_possible */
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
        getShortString(),
        false /* is_deadlock_possible */
    ).runTest();
    try (Statement s = connection.createStatement()) {
      s.execute("truncate table test_rr");
    }

    // Case 3: Prepared statement
    new PreparedDmlStatementTester(
        cb,
        "UPDATE test_rr set i=? where i=?",
        getShortString(),
        false /* is_deadlock_possible */) {

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

    try (Statement stmt = cb.connect().createStatement()) {
      stmt.execute("DROP FUNCTION func()");
    }
  }

  @Test
  public void delete() throws Exception {
    ConnectionBuilder cb = getConnectionBuilder();
    // Case 1: Single statement case
    new RegularDmlStatementTester(
        cb,
        "DELETE from test_rr where i > 0",
        getShortString(),
        // A deadlock can occur as follows: an rc/rr transaction's write might wait on a row that
        // was read and locked by a serializable txn, while the serializable transaction tries to
        // read and lock a row which has already been written by the rc/rr txn. This situation can
        // occur because the "DELETE ... i > 0" issues read rpcs to all tablets in parallel.
        true /* is_deadlock_possible */
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
        getShortString(),
        true /* is_deadlock_possible */
    ).runTest();
    try (Statement s = connection.createStatement()) {
      s.execute("truncate table test_rr");
    }

    // Case 3: Prepared statement
    new PreparedDmlStatementTester(
        cb,
        "DELETE from test_rr where i > ?",
        getShortString(),
        true /* is_deadlock_possible */) {

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
  }

  //
  // Helpers methods
  //

  /** Isolation levels supported by YSQL */
  private static final List<IsolationLevel> isolationLevels =
      Arrays.asList(IsolationLevel.SERIALIZABLE,
                    IsolationLevel.REPEATABLE_READ,
                    IsolationLevel.READ_COMMITTED);

  /** Map from every supported isolation level to zero, used as a base for counter maps. */
  private static final Map<IsolationLevel, Integer> emptyIsolationCounterMap =
      Collections.unmodifiableMap(
          isolationLevels.stream().collect(Collectors.toMap(il -> il, il -> 0)));

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
    return lcMsg.contains("could not serialize access due to concurrent update");
  }

  private static boolean isAbortError(Exception ex) {
    return ex.getMessage().toLowerCase().contains("abort");
  }

  private static boolean isDeadlockError(Exception ex) {
    if (!(ex instanceof PSQLException)) {
      return false;
    }
    String sqlState = ((PSQLException) ex).getSQLState();
    Boolean is_deadlock_error = DEADLOCK_DETECTED_PSQL_STATE.equals(sqlState);
    if (is_deadlock_error) {
      String lcMsg = ex.getMessage().toLowerCase();
      assertTrue(lcMsg.contains("deadlock detected"));
    }
    return is_deadlock_error;
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
        || SERIALIZATION_FAILURE_PSQL_STATE.equals(sqlState)
        || DEADLOCK_DETECTED_PSQL_STATE.equals(sqlState);
  }

  private static <T> T chooseForIsolation(
      IsolationLevel isolation,
      T serializableT,
      T rrT,
      T rcT) {
    switch (isolation) {
      case SERIALIZABLE:    return serializableT;
      case REPEATABLE_READ: return rrT;
      case READ_COMMITTED:  return rcT;
      default:
        throw new IllegalArgumentException("Unexpected isolation level: " + isolation);
    }
  }

  private static <T> void inc(Map<T, Integer> map, T key) {
    assertTrue("Map " + map + " does not contain key " + key, map.containsKey(key));
    map.put(key, map.get(key) + 1);
  }

  //
  // Helpers classes
  //

  /** Runnable responsible for inserts. Starts paused, call unpause() when readers are set. */
  private static class InsertRunnable implements ThrowingRunnable {
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

    public void run() throws Exception {
      Map<IsolationLevel, Integer> insertsAttempted =
          new HashMap<>(emptyIsolationCounterMap);
      Map<IsolationLevel, Integer> insertsSucceeded =
          new HashMap<>(emptyIsolationCounterMap);
      int insertsRetriesExhausted = 0;
      int insertsWithReadRestartError = 0;
      int insertsWithConflictError = 0;
      int insertsWithAbortError = 0;
      int insertsWithAbortErrorAtCommit = 0; // kAborted on explicit "commit"
      int insertsInTxnBlock = 0;
      Random rnd = RandomUtil.getRandomGenerator();
      String insertSql = "INSERT INTO test_rr (t, i) VALUES (?, ?)";
      try (Connection insertSerializableConn =
             cb.withIsolationLevel(IsolationLevel.SERIALIZABLE).connect();
           Connection insertRrConn =
             cb.withIsolationLevel(IsolationLevel.REPEATABLE_READ).connect();
           Connection insertRcConn =
             cb.withIsolationLevel(IsolationLevel.READ_COMMITTED).connect();

           PreparedStatement serializableStmt = insertSerializableConn.prepareStatement(insertSql);
           PreparedStatement rrStmt = insertRrConn.prepareStatement(insertSql);
           PreparedStatement rcStmt = insertRcConn.prepareStatement(insertSql);

           Statement auxSerializableStmt = insertSerializableConn.createStatement();
           Statement auxRrStmt = insertRrConn.createStatement();
           Statement auxRcStmt = insertRcConn.createStatement()) {
        auxSerializableStmt.execute(LOG_RESTARTS_SQL);
        auxRrStmt.execute(LOG_RESTARTS_SQL);
        auxRcStmt.execute(LOG_RESTARTS_SQL);
        serializableStmt.setString(1, stringToInsert);
        rrStmt.setString(1, stringToInsert);
        rcStmt.setString(1, stringToInsert);
        startSignal.await();

        for (int i = 0; i < numInserts; ++i) {
          boolean runInTxnBlock = rnd.nextBoolean();
          IsolationLevel isolation =
              RandomUtil.getRandomElement(isolationLevels);
          PreparedStatement stmt = chooseForIsolation(isolation,
              serializableStmt, rrStmt, rcStmt);
          Statement auxStmt = chooseForIsolation(isolation,
              auxSerializableStmt, auxRrStmt, auxRcStmt);
          try {
            inc(insertsAttempted, isolation);
            LOG.info("Starting insert num " + insertsAttempted);

            if (runInTxnBlock) {
              auxStmt.execute("START TRANSACTION");
              ++insertsInTxnBlock;
            }
            stmt.setInt(2, rnd.nextInt(MAX_INT_TO_INSERT + 1));
            stmt.executeUpdate();
            if (runInTxnBlock) {
              try {
                auxStmt.execute("COMMIT");
                inc(insertsSucceeded, isolation);
                LOG.info("Successful insert num " + insertsAttempted);
              } catch (Exception ex) {
                LOG.info("Failed insert num " + insertsAttempted);
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
                  auxStmt.execute("ROLLBACK");
                } catch (Exception ex2) {
                  fail("ROLLBACK failed " + ex2.getMessage());
                }
              }
            } else {
              LOG.info("Success insert num " + insertsAttempted);
              inc(insertsSucceeded, isolation);
            }
          } catch (Exception ex) {
            LOG.info("Failed insert num " + insertsAttempted);
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
                auxStmt.execute("ROLLBACK");
              } catch (Exception ex2) {
                fail("ROLLBACK failed " + ex2.getMessage());
              }
            }
          }
        }
      } catch (Exception ex) {
        LOG.error("INSERT thread failed", ex);
        fail("INSERT thread failed: " + ex.getMessage());
      }
      LOG.info("INSERT:\n" +
          " insertsAttempted=" + insertsAttempted + "\n" +
          " insertsSucceeded=" + insertsSucceeded + "\n" +
          " insertsRetriesExhausted=" + insertsRetriesExhausted + "\n" +
          " insertsWithReadRestartError=" + insertsWithReadRestartError + "\n" +
          " insertsWithConflictError=" + insertsWithConflictError + "\n" +
          " insertsWithAbortError=" + insertsWithAbortError + "\n" +
          " insertsWithAbortErrorAtCommit=" + insertsWithAbortErrorAtCommit + "\n" +
          " insertsInTxnBlock=" + insertsInTxnBlock);
      int totalSucceeded = insertsSucceeded.values().stream().reduce((a, b) -> a + b).get();
      assertTrue("No INSERT operations succeeded!", totalSucceeded > 0);
      assertTrue(insertsWithConflictError == 0 && insertsWithReadRestartError == 0);
    }
  }

  /**
   * Performs generic testing of concurrent INSERT with other queries by running several
   * concurrent threads.
   */
  private abstract class ConcurrentInsertQueryTester<Stmt extends AutoCloseable> {

    private int numInserts;

    private final ConnectionBuilder cb;

    private final String valueToInsert;

    public ConcurrentInsertQueryTester(ConnectionBuilder cb,
                                       String valueToInsert,
                                       int numInserts) {
      this.cb = cb;
      this.valueToInsert = valueToInsert;
      this.numInserts = numInserts;
    }

    public abstract List<ThrowingRunnable> getRunnableThreads(
        ConnectionBuilder cb, BooleanSupplier isExecutionDone);

    public void runTest() throws Exception {
      List<CatchingThread> workerThreads = new ArrayList<>();

      InsertRunnable insertRunnable = new InsertRunnable(cb, valueToInsert, numInserts);
      CatchingThread insertThread = new CatchingThread("INSERT", insertRunnable);
      insertThread.start();
      workerThreads.add(insertThread);

      BooleanSupplier areInsertsDone = () -> {
        return !insertThread.isAlive();
      };

      int i = 0;
      for (ThrowingRunnable r : getRunnableThreads(cb, areInsertsDone)) {
        CatchingThread th = new CatchingThread("Worker-" + (++i), r);
        th.start();
        workerThreads.add(th);
      }

      insertRunnable.unpause();
      try {
        LOG.info("Waiting for INSERT thread");
        insertThread.join(INSERTS_AWAIT_TIME_SEC * 1000);
        if (insertThread.isAlive()) {
          LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
          fail("Waiting for INSERT thread timed out! Try increasing waiting time?");
        }
        insertThread.rethrowIfCaught();
        try {
          LOG.info("Waiting for other threads");
          for (CatchingThread th : workerThreads) {
            th.finish(SELECTS_AWAIT_TIME_SEC * 1000);
          }
        } catch (TimeoutException ex) {
          LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
          // It's very likely that cause lies on a YB side (e.g. unexpected performance slowdown),
          // not in test.
          fail("Waiting for other threads timed out, this is unexpected!");
        }
      } finally {
        LOG.info("Shutting down executor service");
        for (CatchingThread th : workerThreads) {
          th.interrupt();
        }
        for (CatchingThread th : workerThreads) {
          th.finish(10 * 1000);
        }
        LOG.info("Threads shutdown complete");
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
      super(cb, valueToInsert, 50 /* numInserts */);
      this.expectRestartErrors = expectRestartErrors;
    }

    public abstract Stmt createStatement(Connection conn) throws Exception;

    public abstract ResultSet executeQuery(Stmt stmt) throws Exception;

    private Boolean expectReadRestartErrors(IsolationLevel isolation) {
      // We never expect SERIALIZABLE transaction to result in "restart read required" because the
      // latest committed data is read (i.e., not a consistent read snapshot of the database).
      return this.expectRestartErrors && isolation != IsolationLevel.SERIALIZABLE;
    }

    private Boolean expectConflictErrors(IsolationLevel isolation) throws Exception {
      YBClient client = miniCluster.getClient();

      String enable_wait_queues = client.getFlag(
          miniCluster.getTabletServers().keySet().iterator().next(), "enable_wait_queues");

      boolean is_wait_on_conflict_concurrency_control = (enable_wait_queues == "true");

      // We never expect REPEATABLE READ/READ COMMITTED transaction to result in "conflict" for pure
      // reads.
      //
      // In Wait-on-Conflict mode, read rpcs in serializable isolation from the ysql query layer
      // to the tserver don't return kConflict errors because the tserver internally retries the
      // conflict resolution for the read op once it wakes up from the wait queue after
      // conflicting transations have finished. This retry is done indefinitely until the read
      // op succeeds. The underlying reason this can be done is: seriailzable read rpcs use
      // kMaxHybridTimeValue for the read i.e., read and lock the latest piece of data.
      return this.expectRestartErrors && isolation == IsolationLevel.SERIALIZABLE &&
          !is_wait_on_conflict_concurrency_control;
    }

    @Override
    public List<ThrowingRunnable> getRunnableThreads(
        ConnectionBuilder cb, BooleanSupplier isExecutionDone) {
      List<ThrowingRunnable> runnables = new ArrayList<>();
      //
      // Singular SELECT statement (equal probability of being either serializable/repeatable read/
      // /read committed isolation level)
      //
      runnables.add(() -> {
        Map<IsolationLevel, Integer> selectsAttempted =
            new HashMap<>(emptyIsolationCounterMap);
        Map<IsolationLevel, Integer> selectsSucceeded =
            new HashMap<>(emptyIsolationCounterMap);
        Map<IsolationLevel, Integer> selectsRestartRequired =
            new HashMap<>(emptyIsolationCounterMap);
        Map<IsolationLevel, Integer> selectsRetriesExhausted =
            new HashMap<>(emptyIsolationCounterMap);
        Map<IsolationLevel, Integer> selectsWithAbortError =
            new HashMap<>(emptyIsolationCounterMap);
        Map<IsolationLevel, Integer> selectsWithConflictError =
            new HashMap<>(emptyIsolationCounterMap);

        boolean onlyEmptyResults = true;

        try (Connection serializableConn =
                cb.withIsolationLevel(IsolationLevel.SERIALIZABLE).connect();
             Connection rrConn =
                cb.withIsolationLevel(IsolationLevel.REPEATABLE_READ).connect();
             Connection rcConn =
                cb.withIsolationLevel(IsolationLevel.READ_COMMITTED).connect();

             Stmt serializableStmt = createStatement(serializableConn);
             Stmt rrStmt = createStatement(rrConn);
             Stmt rcStmt = createStatement(rcConn)) {
          try (Statement auxSerializableStatement = serializableConn.createStatement();
               Statement auxRrStatement = rrConn.createStatement();
               Statement auxRcStatement = rcConn.createStatement()) {
            auxSerializableStatement.execute(LOG_RESTARTS_SQL);
            auxRrStatement.execute(LOG_RESTARTS_SQL);
            auxRcStatement.execute(LOG_RESTARTS_SQL);
          }

          for (/* No setup */; !isExecutionDone.getAsBoolean(); /* NOOP */) {
            IsolationLevel isolation =
                RandomUtil.getRandomElement(isolationLevels);
            Stmt stmt =
                chooseForIsolation(isolation, serializableStmt, rrStmt, rcStmt);

            try {
              inc(selectsAttempted, isolation);
              List<Row> rows = getRowList(executeQuery(stmt));
              if (!rows.isEmpty()) {
                onlyEmptyResults = false;
              }
              inc(selectsSucceeded, isolation);
            } catch (Exception ex) {
              if (!isTxnError(ex)) {
                fail("SELECT thread failed: " + ex.getMessage());
              } else if (isRetriesExhaustedError(ex)) {
                inc(selectsRetriesExhausted, isolation);
              } else if (isRestartReadError(ex)) {
                inc(selectsRestartRequired, isolation);
              } else if (isAbortError(ex)) {
                inc(selectsWithAbortError, isolation);
              } else if (isConflictError(ex)) {
                inc(selectsWithConflictError, isolation);
              } else {
                fail("SELECT thread failed: " + ex.getMessage());
              }
            }
          }
        } catch (Exception ex) {
          LOG.error("Connection-wide exception! This shouldn't happen", ex);
          fail("Connection-wide exception! This shouldn't happen: " + ex.getMessage());
        }
        LOG.info("SELECT (single-stmt):\n" +
            " selectsAttempted=" + selectsAttempted + "\n" +
            " selectsSucceeded=" + selectsSucceeded + "\n" +
            " selectsRestartRequired=" + selectsRestartRequired + "\n" +
            " selectsRetriesExhausted=" + selectsRetriesExhausted + "\n" +
            " selectsWithAbortError=" + selectsWithAbortError + "\n" +
            " selectsWithConflictError=" + selectsWithConflictError);

        for (Entry<IsolationLevel, Integer> e : selectsRestartRequired.entrySet()) {
          IsolationLevel isolation = e.getKey();
          int numReadRestarts = e.getValue();

          if (expectReadRestartErrors(isolation)) {
            assertNotEquals("SELECT (single-stmt): expected read restarts to happen, but got " +
                "none at " + isolation + " level",
                0, numReadRestarts);
          } else {
            assertEquals("SELECT (single-stmt): unexpected " + numReadRestarts +
                " read restarts at " + isolation + " level!",
                0, numReadRestarts);
          }
        }

        for (Entry<IsolationLevel, Integer> e : selectsWithConflictError.entrySet()) {
          IsolationLevel isolation = e.getKey();
          int numConflictRestarts = e.getValue();

          if (expectConflictErrors(isolation)) {
            assertNotEquals("SELECT (single-stmt): expected conflict errors to happen, but got " +
                "none at " + isolation + " level",
                0, numConflictRestarts);
          } else {
            assertEquals("SELECT (single-stmt): unexpected " + numConflictRestarts +
                " conflict errors at " + isolation + " level!",
                0, numConflictRestarts);
          }
        }

        if (onlyEmptyResults) {
          fail("SELECT (single-stmt) thread didn't yield any meaningful result! Flawed test?");
        }
      });

      // Two SELECTs grouped in a transaction.
      for (IsolationLevel isolation : isolationLevels) {
        runnables.add(() -> {
          int txnsAttempted = 0;
          int selectsRetriesExhausted = 0;
          int selectsFirstOpRestartRequired = 0;
          int selectsSecondOpRestartRequired = 0;
          int selectsFirstOpConflictDetected = 0;
          int txnsSucceeded = 0;
          int selectsWithAbortError = 0;
          int commitOfTxnThatRequiresRestart = 0;

          try (Connection selectTxnConn = cb.withIsolationLevel(isolation).connect();
              Stmt stmt = createStatement(selectTxnConn)) {
            try (Statement auxStmt = selectTxnConn.createStatement()) {
              auxStmt.execute(LOG_RESTARTS_SQL);
            }
            selectTxnConn.setAutoCommit(false);
            for (/* No setup */; !isExecutionDone.getAsBoolean(); ++txnsAttempted) {
              int numCompletedOps = 0;
              try {
                List<Row> rows1 = getRowList(executeQuery(stmt));
                ++numCompletedOps;
                List<Row> rows2 = getRowList(executeQuery(stmt));
                ++numCompletedOps;
                try {
                  selectTxnConn.commit();
                } catch (Exception ex) {
                  // TODO(Piyush): Once #11514 is fixed, we won't have to handle this rare
                  // occurrence.
                  if (ex.getMessage().contains(
                        "Illegal state: Commit of transaction that requires restart is not " +
                        "allowed")){
                    commitOfTxnThatRequiresRestart++;
                  } else {
                    throw ex;
                  }
                }
                assertTrue("Two SELECTs done within same transaction mismatch" +
                           ", " + isolation + " transaction isolation breach!",
                           rows1.equals(rows2) || (isolation == IsolationLevel.READ_COMMITTED));
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
            LOG.error("SELECT (" + isolation + ") thread failed", ex);
            fail("SELECT in " + isolation + " thread failed: " + ex.getMessage());
          }
          LOG.info("SELECT (" + isolation + "):" +
              " txnsAttempted=" + txnsAttempted +
              " selectsRetriesExhausted=" + selectsRetriesExhausted +
              " selectsFirstOpRestartRequired=" + selectsFirstOpRestartRequired +
              " selectsSecondOpRestartRequired=" + selectsSecondOpRestartRequired +
              " selectsFirstOpConflictDetected=" + selectsFirstOpConflictDetected +
              " txnsSucceeded=" + txnsSucceeded +
              " selectsWithAbortError=" + selectsWithAbortError +
              " commitOfTxnThatRequiresRestart=" + commitOfTxnThatRequiresRestart);

          if (expectReadRestartErrors(isolation)) {
            // Read restart errors can never occur in serializable isolation.
            assertTrue(isolation != IsolationLevel.SERIALIZABLE);
            // We don't assert for selectsSecondOpRestartRequired > 0 because of the following
            // reasoning:
            //
            // Read restart errors are retried transparently only for the first statement in the
            // transaction. So, we can expect some read restart errors in the second statement.
            // However, it is highly unlikely that we see them because all tservers holding data
            // would have responded with a local limit during the first statement, hence
            // reducing the scope of ambiguity that results in a read restart in the second
            // statement. Moreover, the first statement might have faced a read restart error
            // and moved the read time ahead to reduce the scope of ambiguity even further.
            assertTrue("SELECT (" + isolation + "): Expected restarts, but " +
                " selectsFirstOpRestartRequired=" + selectsFirstOpRestartRequired,
                selectsFirstOpRestartRequired > 0);
          } else {
            assertEquals("SELECT (" + isolation + "): Unexpected restarts!",
                0, selectsFirstOpRestartRequired);

            // Assertions for the second statement vary based on the isolation level.
            switch (isolation) {
              case SERIALIZABLE:
                // Read restarts can't occur in SERIALIZABLE isolation
                assertEquals(0, selectsSecondOpRestartRequired);
                break;
              case REPEATABLE_READ:
                // Read restart errors are retried transparently only for the first statement in the
                // transaction. So, we can expect some read restart errors in the second statement.
                // However, it is highly unlikely that we see them because all tservers holding data
                // would have responded with a local limit during the first statement, hence
                // reducing the scope of ambiguity that results in a read restart in the second
                // statement. Moreover, the first statement might have faced a read restart error
                // and moved the read time ahead to reduce the scope of ambiguity even further.
                break;
              case READ_COMMITTED:
                // Read restarts retries are performed transparently for each statement in
                // READ COMMITTED isolation
                assertEquals(0, selectsSecondOpRestartRequired);
                break;
              default:
                fail("Unexpected isolation level: " + isolation);
            }
          }

          assertTrue(
            (!expectConflictErrors(isolation) && selectsFirstOpConflictDetected == 0) ||
            (expectConflictErrors(isolation) && selectsFirstOpConflictDetected > 0));

          // If we (at all) expect restart/conflict errors, then we cannot guarantee that any
          // operation would succeed.
          if (!(expectReadRestartErrors(isolation) || expectConflictErrors(isolation))) {
            assertGreaterThan("No txns in " + isolation + " succeeded, ever! Flawed test?",
                txnsSucceeded, 0);
          }
        });
      }

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
      super(cb, valueToInsert, 50 /* numInserts */);
    }

    private ThrowingRunnable getRunnableThread(
        ConnectionBuilder cb, BooleanSupplier isExecutionDone, String secondSavepointOpString) {
      return () -> {
        int selectsAttempted = 0;
        int selectsRestartRequired = 0;
        int selectsSucceeded = 0;
        try (Connection selectTxnConn =
                 cb.withIsolationLevel(IsolationLevel.REPEATABLE_READ).connect();
             Statement stmt = selectTxnConn.createStatement()) {
          selectTxnConn.setAutoCommit(false);
          for (/* No setup */; !isExecutionDone.getAsBoolean(); ++selectsAttempted) {
            try {
              stmt.execute("SAVEPOINT a");
              if (!secondSavepointOpString.isEmpty()) {
                stmt.execute(secondSavepointOpString);
              }
              stmt.executeQuery("SELECT count(*) from test_rr");
              selectTxnConn.commit();
              ++selectsSucceeded;
            } catch (Exception ex) {
              try {
                selectTxnConn.rollback();
              } catch (SQLException ex1) {
                fail("Rollback failed: " + ex1.getMessage());
              }
              if (isRestartReadError(ex)) {
                ++selectsRestartRequired;
              }
              if (!isTxnError(ex)) {
                throw ex;
              }
            }
          }
        } catch (Exception ex) {
          fail("SELECT in savepoint thread failed: " + ex.getMessage());
        }
        LOG.info(String.format(
            "SELECT in savepoint thread with second savepoint op: \"%s\": selectsSucceeded=%d" +
            " selectsAttempted=%d selectsRestartRequired=%d", secondSavepointOpString,
            selectsSucceeded, selectsAttempted, selectsRestartRequired));
        assertTrue(
            "No SELECTs after second savepoint statement: " + secondSavepointOpString
                + " resulted in 'restart read required' on second operation"
                + " - but we expected them to!"
                + " " + selectsAttempted + " attempted, " + selectsSucceeded + " succeeded",
                selectsRestartRequired > 0);
      };
    }

    @Override
    public List<ThrowingRunnable> getRunnableThreads(
        ConnectionBuilder cb, BooleanSupplier isExecutionDone) {
      List<ThrowingRunnable> runnables = new ArrayList<>();
      runnables.add(getRunnableThread(cb, isExecutionDone, ""));
      runnables.add(getRunnableThread(cb, isExecutionDone, "SAVEPOINT b"));
      runnables.add(getRunnableThread(cb, isExecutionDone, "ROLLBACK TO a"));
      runnables.add(getRunnableThread(cb, isExecutionDone, "RELEASE a"));
      return runnables;
    }
  }

  /* DmlTester tests read restarts for DML statements. */
  private abstract class DmlTester <Stmt extends AutoCloseable> extends
      ConcurrentInsertQueryTester<Stmt> {
    public abstract Stmt createStatement(Connection conn) throws Exception;
    public abstract boolean execute(Stmt stmt) throws Exception;
    private Boolean is_deadlock_possible;

    public DmlTester(ConnectionBuilder cb, String valueToInsert, Boolean is_deadlock_possible) {
      super(cb, valueToInsert, 50 /* numInserts */);
      this.is_deadlock_possible = is_deadlock_possible;
    }

    @Override
    public List<ThrowingRunnable> getRunnableThreads(
        ConnectionBuilder cb, BooleanSupplier isExecutionDone) {
      List<ThrowingRunnable> runnables = new ArrayList<>();
      for (IsolationLevel isolation : isolationLevels) {
        runnables.add(() -> {
          LOG.info("Starting DmlTester's runnable...");

          int executionsAttempted = 0;
          int executionsSucceeded = 0;
          int executionsRetriesExhausted = 0;
          int executionsRestartRequired = 0;
          int executionsConflictErrors = 0;
          int executionsAbortedByOtherTxn = 0;
          int executionsDeadlockErrors = 0;
          int executionsAbortedByOtherTxnAtCommit = 0; // kAborted on explicit "commit"
          int executionsRanInTxnBlock = 0;

          try (Connection conn = cb.withIsolationLevel(isolation).connect();
               Stmt stmt = createStatement(conn);
               Statement auxStmt = conn.createStatement()) {
            auxStmt.execute(LOG_RESTARTS_SQL);
            for (/* No setup */; !isExecutionDone.getAsBoolean(); ++executionsAttempted) {
              boolean runInTxnBlock = RandomUtil.getRandomGenerator().nextBoolean();
              executionsRanInTxnBlock += runInTxnBlock ? 1 : 0;
              try {
                if (runInTxnBlock)
                  auxStmt.execute("START TRANSACTION ISOLATION LEVEL " + isolation.sql);
                execute(stmt);
                if (runInTxnBlock) {
                  try {
                    auxStmt.execute("COMMIT");
                  } catch (Exception ex) {
                    if (!isTxnError(ex)) {
                      fail("COMMIT faced unknown error: " + ex.getMessage());
                    } else if (isRetriesExhaustedError(ex)) {
                      ++executionsRetriesExhausted;
                    } else if (isAbortError(ex)) {
                      // A txn might have already been aborted by another txn before commit
                      ++executionsAbortedByOtherTxnAtCommit;
                    } else {
                      fail("COMMIT faced unknown error: " + ex.getMessage());
                    }
                    try {
                      auxStmt.execute("rollback");
                    } catch (Exception ex2) {
                      fail("Rollback shouldn't fail: " + ex2.getMessage());
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
                  // A conflict can only occur on the tserver when the read time of the UPDATE
                  // is the same as the write time of when a row was inserted.
                  ++executionsConflictErrors;
                } else if (isDeadlockError(ex)) {
                  executionsDeadlockErrors++;
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
                "isolation=" + isolation + "\n" +
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
            assertTrue((executionsDeadlockErrors == 0) || is_deadlock_possible);
          } catch (Exception ex) {
            fail(ex.getMessage());
          }
        });
      }

      return runnables;
    }
  }

  /** RegularDmlStatementTester that uses regular Statement */
  private class RegularDmlStatementTester extends DmlTester<Statement> {
    protected String queryString;

    public RegularDmlStatementTester(
        ConnectionBuilder cb,
        String queryString,
        String valueToInsert,
        Boolean is_deadlock_possible) {
      super(cb, valueToInsert, is_deadlock_possible);
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
        String valueToInsert,
        Boolean is_deadlock_possible) {
      super(cb, valueToInsert, is_deadlock_possible);
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
