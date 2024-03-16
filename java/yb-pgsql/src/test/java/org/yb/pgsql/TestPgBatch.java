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
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
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
    flagMap.put("ysql_max_read_restart_attempts", "600000");
    flagMap.put("ysql_max_write_restart_attempts", "600000");
    return flagMap;
  }

  protected void setUpTable(int numRows, IsolationLevel isolationLevel) throws Throwable {
    try (Statement s = connection.createStatement()) {
      s.execute("DROP TABLE IF EXISTS t");
      s.execute("CREATE TABLE t(k int PRIMARY KEY, v int)");
      s.execute(String.format("INSERT INTO t SELECT generate_series(1, %d), 0", numRows));
    }
  }

  protected Thread startConflictingThread(int conflictingStatement,
      final AtomicBoolean failureDetected, final CountDownLatch startSignal) {
    Thread t = new Thread(() -> {
      try (Connection c = getConnectionBuilder().connect();
          Statement s = c.createStatement()) {
        s.execute("SET yb_transaction_priority_lower_bound = 0.5");
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

  private String toSql(IsolationLevel isolationLevel) {
    switch (isolationLevel) {
    case READ_COMMITTED:
      return "READ COMMITTED";
    case REPEATABLE_READ:
      return "REPEATABLE READ";
    case SERIALIZABLE:
      return "SERIALIZABLE";
    default:
      throw new IllegalArgumentException();
    }
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
      s.execute("SET yb_transaction_priority_upper_bound=0.4");
      s.execute("BEGIN TRANSACTION ISOLATION LEVEL " + toSql(isolationLevel));
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
      // In RC in v2.14, without wait queues we do see a failure from the conflicting transaction.
      // "expired or aborted by a conflict: 40001"
      if (isolationLevel != RC) {
        assertFalse(failureDetected.get());
      }
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
      // In SR without wait queues (which are unavailable in v2.14), we do get this exception and
      // therefore don't have an assertion that this isn't SR here.
      return;
    }
    // In RC, as noted above, the other thread fails, so we expect to get here in all cases.
    // In the other isolation levels, if they get here it's because they were able to retry the
    // first statement.
    if (isolationLevel != RC) {
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
      s.execute("SET yb_transaction_priority_upper_bound=0.4");
      s.execute("BEGIN TRANSACTION ISOLATION LEVEL " + toSql(isolationLevel));
      for (int i = 1; i <= numStatements; i++) {
        s.addBatch(String.format("UPDATE t SET v=2 WHERE k=%d", i));
      }
      startSignal.countDown();
      startSignal.await();
      s.executeBatch();
      s.execute("COMMIT");

      t.join();
      // In RC in v2.14, without wait queues we do see a failure from the conflicting transaction.
      // "expired or aborted by a conflict: 40001"
      if (isolationLevel != RC) {
        assertFalse(failureDetected.get());
      }
      Set<Row> expected = new HashSet<>();
      for (int i = 1; i <= numStatements; i++) {
        expected.add(new Row(i, 2));
      }
      assertRowSet(s, "SELECT * FROM t ORDER BY k", expected);
    } catch (BatchUpdateException e) {
      LOG.info(e.toString());
      // In SR without wait queues (which are unavailable in v2.14), we do get this exception and
      // therefore don't have an assertion that this isn't SR here.
      // In v2.14, in RC, with certain long batches in certain build modes, we hit this exception
      // as well.
      return;
    }
    // In v2.14 we only get all the way here in RC, because of the error mentioned above.
    // In v2.14, in some build modes, there is also a case (conflicting statement is 256) where we
    // can retry correctly in SR due to SYNC packet handling.
    if (conflictingStatement != 256 && isolationLevel != SR) {
      assertEquals(isolationLevel, RC);
    }
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
      s.execute("SET yb_transaction_priority_upper_bound=0.4");
      s.execute("BEGIN TRANSACTION ISOLATION LEVEL " + toSql(isolationLevel));
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
    // This test currently succeeds. In v2.14 it's simply because the other thread fails.
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
      s.execute("SET yb_transaction_priority_upper_bound=0.4");
      if (explicitTransaction) {
        query = "BEGIN TRANSACTION ISOLATION LEVEL " + toSql(isolationLevel) + ";";
      } else {
        s.execute(
            "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL " + toSql(isolationLevel));
      }
      for (int i = 1; i < 5; i++) {
        query += String.format("UPDATE t SET v=2 WHERE k=%d;", i);
      }
      query += String.format("UPDATE t SET v=2 WHERE k IN (5,6,7,8);");
      for (int i = 9; i < 12; i++) {
        query += String.format("UPDATE t SET v=2 WHERE k=%d;", i);
      }
      if (explicitTransaction) {
        query += "COMMIT;";
      }
      s.execute(query);

      t.join();
      // In RC in v2.14, without wait queues we do see a failure from the conflicting transaction.
      // "expired or aborted by a conflict: 40001"
      if (isolationLevel != RC) {
        assertFalse(failureDetected.get());
      }
      Set<Row> expected = new HashSet<>();
      for (int i = 1; i < 12; i++) {
        expected.add(new Row(i, 2));
      }
      assertRowSet(s, "SELECT * FROM t ORDER BY k", expected);
    } catch (PSQLException e) {
      LOG.info(e.toString());
      // In RR using the simple query protocol, we hit "Restart isn't possible because statement
      // isn't one of SELECT/UPDATE/INSERT/DELETE" (due to the "command" being detected as "BEGIN"),
      // and expect a PSQLException.
      // In RR using the extended query protocol, it executes as above, a batch statement after the
      // first statement which can't be retried at the query layer.
      // In v2.14, for SR without wait queues, same thing.
      // In v2.14, RC doesn't reach here because the other transaction fails.
      assertNotEquals(isolationLevel, RC);
      if (isolationLevel == RR || isolationLevel == SR) {
        assertFalse(explicitTransaction == false && extendedProtocol == false);
      }
      return;
    }
    // In RR, in the simple protocol and where there is no explicit transaction, it's able to retry.
    // In v2.14, this is true of SR also.
    // In v2.14, RC makes it here even in the extendedProtocol case due to the other transaction
    // failing.
    if (isolationLevel == RR || isolationLevel == SR) {
      assertFalse(extendedProtocol);
      assertFalse(explicitTransaction);
    }
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
