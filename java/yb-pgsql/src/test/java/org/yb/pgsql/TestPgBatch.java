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
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.yugabyte.util.PSQLException;
import java.sql.BatchUpdateException;
import java.sql.Connection;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgBatch extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgBatch.class);

  protected static IsolationLevel RC = IsolationLevel.READ_COMMITTED;
  protected static IsolationLevel RR = IsolationLevel.REPEATABLE_READ;
  protected static IsolationLevel SR = IsolationLevel.SERIALIZABLE;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // TODO: Remove this override when read committed isolation is enabled by default.
    flagMap.put("yb_enable_read_committed_isolation", "true");
    // TODO: Remove this override when wait queues are enabled by default.
    flagMap.put("enable_wait_queues", "true");
    // Easier debugging.
    flagMap.put("ysql_log_statement", "all");
    return flagMap;
  }

  protected void setUpTable(int numRows, IsolationLevel isolationLevel) throws Throwable {
    try (Statement s = connection.createStatement()) {
      s.execute("DROP TABLE IF EXISTS t");
      waitForTServerHeartbeatIfConnMgrEnabled();
      s.execute("CREATE TABLE t(k int PRIMARY KEY, v int)");
      s.execute(String.format("INSERT INTO t SELECT generate_series(1, %d), 0", numRows));
    }
  }

  protected Thread startConflictingThread(int conflictingStatement,
      final AtomicBoolean failureDetected, final CountDownLatch startSignal) {
    Thread t = new Thread(() -> {
      try (Connection c = getConnectionBuilder().connect();
          Statement s = c.createStatement()) {
        s.execute("BEGIN");
        s.execute(String.format("UPDATE t SET v=1 WHERE k=%d", conflictingStatement));
        startSignal.countDown();
        startSignal.await();
        Thread.sleep(3000);
        s.execute("COMMIT");
      } catch (Exception e) {
        LOG.error("Unexpected exception", e);
        failureDetected.set(true);
      }
    });
    t.start();
    return t;
  }

  /**
   * Statements in this method start from 1.
   */
  public void testTransparentRestartHelper(int conflictingStatement,
      IsolationLevel isolationLevel) throws Throwable {
    setUpTable(11, isolationLevel);
    final AtomicBoolean failureDetected = new AtomicBoolean(false);
    final CountDownLatch startSignal = new CountDownLatch(2);
    Thread t = startConflictingThread(conflictingStatement, failureDetected, startSignal);
    try (Connection c = getConnectionBuilder().connect();
        Statement s = c.createStatement()) {
      s.execute("BEGIN TRANSACTION ISOLATION LEVEL " + isolationLevel.sql);
      for (int i = 1; i < 5; i++) {
        s.addBatch(String.format("UPDATE t SET v=2 WHERE k=%d", i));
      }
      // In RC, a single row UPDATE will choose a read time in docdb rather than in the query layer
      // and hence retries will be performed in docdb, without the transaction conflict error
      // surfacing to the query layer. To force a transaction conflict error to be returned to the
      // query layer for retry, we use this construction (k IN <set>), which issues parallel rpcs to
      // docdbs on multiple tservers and causes the query layer to choose the read time instead of
      // docdb.
      s.addBatch("UPDATE t SET v=2 WHERE k IN (5,6,7,8)");
      for (int i = 9; i < 12; i++) {
        s.addBatch(String.format("UPDATE t SET v=2 WHERE k=%d", i));
      }
      startSignal.countDown();
      startSignal.await();
      s.executeBatch();
      s.execute("COMMIT");

      t.join();
      assertFalse(failureDetected.get());
      Set<Row> expected = new HashSet<>();
      for (int i = 1; i < 12; i++) {
        expected.add(new Row(i, 2));
      }
      assertRowSet(s, "SELECT * FROM t ORDER BY k", expected);
    } catch (BatchUpdateException e) {
      LOG.info(e.toString());
      // Because we can't replay the transaction from the middle of a batch, if it isn't the first
      // statement, then we expect a BatchUpdateException.
      assertGreaterThan(conflictingStatement, 1);
      // In SR, the read time is always managed by docdb. Therefore executeBatch() blocks until the
      // conflicting transaction completes, and is able to proceed with the transaction after that.
      // Therefore, if we get a BatchUpdateException, the code is incorrect.
      assertNotEquals(isolationLevel, SR);
      return;
    }
    // In RC and RR, we skip the BatchUpdateException only in the case where the conflicting
    // statement was the first statement, and therefore can be retried without losing data.
    // In SR, per the comment in the catch block just above, this is the expected path in all cases.
    if (isolationLevel != SR) {
      assertEquals(conflictingStatement, 1);
    }
  }

  @Test
  public void testTransparentRestart() throws Throwable {
    // Params: conflicting statement, isolation level
    testTransparentRestartHelper(5, RC);
    testTransparentRestartHelper(5, RR);
    testTransparentRestartHelper(5, SR);
  }

  @Test
  public void testSchemaMismatchRetry() throws Throwable {
    setUpTable(2, RR);
    try (Connection c1 = getConnectionBuilder().connect();
        Connection c2 = getConnectionBuilder().connect();
        Statement s1 = c1.createStatement();
        Statement s2 = c2.createStatement()) {
      // Run UPDATE statement for the sole purpose of a caching catalog version.
      s1.execute("UPDATE t SET v=2 WHERE k=0");
      // Add more than one statement to the batch to ensure that
      // YB treats this as batched execution mode.
      for (int i = 1; i <= 2; i++) {
        s1.addBatch(String.format("UPDATE t SET v=2 WHERE k=%d", i));
      }
      // Causes a schema version mismatch error on the next UPDATE statement.
      // Execute ALTER in a different session c2 so as not to invalidate
      // the catalog cache of c1 until the next heartbeat with the master.
      s2.execute("ALTER TABLE t ALTER COLUMN v SET NOT NULL");
      try {
        // This uses the cached catalog version but the schema is changed
        // by the ALTER TABLE statement above. This should cause a schema
        // mismatch error. The schema mismatch error is not retried internally
        // in batched execution mode.
        s1.executeBatch();
        // Should not reach here since we do not support retries in batched
        // execution mode for schema mismatch errors.
        fail("Internal retries are not supported in batched execution mode");
      } catch (BatchUpdateException e) {
        LOG.info(e.toString());
      }
    }
  }

  @Test
  public void testTransparentRestartFirstStatement() throws Throwable {
    // Params: conflicting statement, isolation level
    testTransparentRestartHelper(1, RC);
    testTransparentRestartHelper(1, RR);
    testTransparentRestartHelper(1, SR);
  }

  /**
   * Requires numStatements >= conflictingStatement
   */
  protected void testSyncInLongBatchHelper(
      int numStatements, int conflictingStatement, IsolationLevel isolationLevel) throws Throwable {
    setUpTable(numStatements, isolationLevel);
    final AtomicBoolean failureDetected = new AtomicBoolean(false);
    final CountDownLatch startSignal = new CountDownLatch(2);
    Thread t = startConflictingThread(conflictingStatement, failureDetected, startSignal);
    try (Connection c = getConnectionBuilder().connect();
        Statement s = c.createStatement()) {
      s.execute("BEGIN TRANSACTION ISOLATION LEVEL " + isolationLevel.sql);
      for (int i = 1; i <= numStatements; i++) {
        s.addBatch(String.format("UPDATE t SET v=2 WHERE k=%d", i));
      }
      startSignal.countDown();
      startSignal.await();
      s.executeBatch();
      s.execute("COMMIT");

      t.join();
      assertFalse(failureDetected.get());
      Set<Row> expected = new HashSet<>();
      for (int i = 1; i <= numStatements; i++) {
        expected.add(new Row(i, 2));
      }
      assertRowSet(s, "SELECT * FROM t ORDER BY k", expected);
    } catch (BatchUpdateException e) {
      LOG.info(e.toString());
      // In isolation level READ COMMITTED, update queries with a single key are assigned read times
      // on docdb, and docdb can transparently advance them in case of conflict.
      // In isolation level SERIALIZABLE, since read times are managed on docdb, the statement for
      // the conflicting row blocks until the conflicting transaction completes, and the transaction
      // is able to proceed after that.
      // Therefore, we only expect a BatchUpdateException in the REPEATABLE READ isolation level,
      // where there is a single read time for the entire transaction and thus the transaction
      // cannot be re-applied.
      assertEquals(isolationLevel, RR);
      return;
    }
    // We skip the BatchUpdateException with single-row update statements in isolation levels other
    // than REPEATABLE READ.
    assertNotEquals(isolationLevel, RR);
  }

  /**
   * In very long batches, a SYNC message is sent as part of the protocol after message #255
   * (counting messages from 1). Our batch detection detects a non-batch by peeking for a SYNC
   * message after an EXECUTE message. If there are exactly 256 messages, then the last message will
   * be treated as a single statement, potentially causing correctness errors. We test around the
   * boundary.
   */
  @Test
  public void testSyncInLongBatch() throws Throwable {
    // Params: num statements, conflicting statement, isolation level
    testSyncInLongBatchHelper(255, 255, RC);
    testSyncInLongBatchHelper(255, 255, RR);
    testSyncInLongBatchHelper(255, 255, SR);
    testSyncInLongBatchHelper(256, 256, RC);
    testSyncInLongBatchHelper(256, 256, RR);
    testSyncInLongBatchHelper(256, 256, SR);
    testSyncInLongBatchHelper(257, 257, RC);
    testSyncInLongBatchHelper(257, 257, RR);
    testSyncInLongBatchHelper(257, 257, SR);
    testSyncInLongBatchHelper(258, 258, RC);
    testSyncInLongBatchHelper(258, 258, RR);
    testSyncInLongBatchHelper(258, 258, SR);

    testSyncInLongBatchHelper(255, 254, RC);
    testSyncInLongBatchHelper(255, 254, RR);
    testSyncInLongBatchHelper(255, 254, SR);
    testSyncInLongBatchHelper(256, 255, RC);
    testSyncInLongBatchHelper(256, 255, RR);
    testSyncInLongBatchHelper(256, 255, SR);
    testSyncInLongBatchHelper(257, 256, RC);
    testSyncInLongBatchHelper(257, 256, RR);
    testSyncInLongBatchHelper(257, 256, SR);
    testSyncInLongBatchHelper(258, 257, RC);
    testSyncInLongBatchHelper(258, 257, RR);
    testSyncInLongBatchHelper(258, 257, SR);
  }

  /**
   * Tests a very long batch in RC, where the 256th statement (currently detected as a single
   * statement by the batch detection heuristic) is "k IN (256, 257)", thus forcing a read time to
   * be assigned by the query layer, and an error to be returned.
   */
  @Test
  public void testSyncInLongBatchRcCornerCase() throws Throwable {
    int numRows = 257;
    int conflictingRow = numRows - 1;
    IsolationLevel isolationLevel = RC;
    setUpTable(numRows, isolationLevel);
    final AtomicBoolean failureDetected = new AtomicBoolean(false);
    final CountDownLatch startSignal = new CountDownLatch(2);
    Thread t = startConflictingThread(conflictingRow, failureDetected, startSignal);
    try (Connection c = getConnectionBuilder().connect();
        Statement s = c.createStatement()) {
      s.execute("BEGIN TRANSACTION ISOLATION LEVEL " + isolationLevel.sql);
      for (int i = 1; i < conflictingRow; i++) {
        s.addBatch(String.format("UPDATE t SET v=2 WHERE k=%d", i));
      }
      s.addBatch(
          String.format("UPDATE t SET v=2 WHERE k IN (%d,%d)", conflictingRow, conflictingRow + 1));

      startSignal.countDown();
      startSignal.await();
      s.executeBatch();
      s.execute("COMMIT");

      t.join();
      assertFalse(failureDetected.get());
      Set<Row> expected = new HashSet<>();
      for (int i = 1; i <= numRows; i++) {
        expected.add(new Row(i, 2));
      }
      assertRowSet(s, "SELECT * FROM t ORDER BY k", expected);
    } catch (BatchUpdateException e) {
      // In READ COMMITTED, when the multi-row update statement comes after the mid-batch SYNC
      // message, we do not get a BatchUpdateException.
      LOG.info(e.toString());
      assertFalse(true);  // Shouldn't get here.
    }
    // This test currently succeeds, which is ok after validating vs. the expected rows.
  }

  protected void testMultipleStatementsPerQueryHelper(boolean extendedProtocol,
      boolean explicitTransaction, IsolationLevel isolationLevel) throws Throwable {
    setUpTable(11, isolationLevel);
    final AtomicBoolean failureDetected = new AtomicBoolean(false);
    final CountDownLatch startSignal = new CountDownLatch(2);
    Thread t = startConflictingThread(5, failureDetected, startSignal);
    String preferQueryMode = "simple";
    if (extendedProtocol) {
      preferQueryMode = "extended";
    }
    try (Connection c = getConnectionBuilder().withPreferQueryMode(preferQueryMode).connect();
        Statement s = c.createStatement()) {
      startSignal.countDown();
      startSignal.await();
      String query = new String();
      if (explicitTransaction) {
        s.execute("BEGIN TRANSACTION ISOLATION LEVEL " + isolationLevel.sql);
      } else {
        s.execute(
            "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL " + isolationLevel.sql);
      }
      for (int i = 1; i < 5; i++) {
        query += String.format("UPDATE t SET v=2 WHERE k=%d;", i);
      }
      query += String.format("UPDATE t SET v=2 WHERE k IN (5,6,7,8);");
      for (int i = 9; i < 12; i++) {
        query += String.format("UPDATE t SET v=2 WHERE k=%d;", i);
      }
      s.execute(query);
      if (explicitTransaction) {
        s.execute("COMMIT");
      }

      t.join();
      assertFalse(failureDetected.get());
      Set<Row> expected = new HashSet<>();
      for (int i = 1; i < 12; i++) {
        expected.add(new Row(i, 2));
      }
      assertRowSet(s, "SELECT * FROM t ORDER BY k", expected);
      LOG.info("Parameters: extendedProtocol: " + extendedProtocol +
          ", explicitTransaction: " + explicitTransaction +
          ", isolationLevel: " + isolationLevel + " success!");
    } catch (PSQLException e) {
      LOG.info("Parameters: extendedProtocol: " + extendedProtocol +
          ", explicitTransaction: " + explicitTransaction +
          ", isolationLevel: " + isolationLevel + " error: " + e);

      // In RR and RC:
      // (1) If using the extended query protocol, it executes the multi-statement query as a batch
      // statement for which non-first statements which can't be retried at the query layer.
      // (2) If using the simple query protocol, retries are blocked due to the error mentioned
      // below.
      assertNotEquals(isolationLevel, SR);
      if (extendedProtocol) {
        assertTrue(e.toString().contains(
            "could not serialize access due to concurrent update (query layer retries aren't " +
            "supported when executing non-first statement in batch, will be unable to replay " +
            "earlier commands)"));
      } else {
        assertTrue(e.toString().contains(
            "could not serialize access due to concurrent update (query layer retries aren't " +
            "supported for multi-statement queries issued via the simple query protocol, upvote " +
            "github issue #21833 if you want this)"));
      }
      return;
    }
    // In SR, the query pauses on the docdb side until the conflicting transaction commits. Then the
    // transaction continues and succeeds.
    assertEquals(isolationLevel, SR);
  }

  @Test
  public void testMultipleStatementsPerQuery() throws Throwable {
    // Params: use extended protocol, use an explicit transaction, isolation level
    testMultipleStatementsPerQueryHelper(false, true, RC);
    testMultipleStatementsPerQueryHelper(false, true, RR);
    testMultipleStatementsPerQueryHelper(false, true, SR);

    testMultipleStatementsPerQueryHelper(true, true, RC);
    testMultipleStatementsPerQueryHelper(true, true, RR);
    testMultipleStatementsPerQueryHelper(true, true, SR);


    testMultipleStatementsPerQueryHelper(false, false, RC);
    testMultipleStatementsPerQueryHelper(false, false, RR);
    testMultipleStatementsPerQueryHelper(false, false, SR);

    testMultipleStatementsPerQueryHelper(true, false, RC);
    testMultipleStatementsPerQueryHelper(true, false, RR);
    testMultipleStatementsPerQueryHelper(true, false, SR);
  }
}
