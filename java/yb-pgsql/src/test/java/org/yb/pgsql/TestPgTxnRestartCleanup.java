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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;

/**
 * Tests to validate state that a query-layer transaction restart must clean up or preserve.
 *
 * Each test forces kConflict deterministically: with wait queues off, a higher-priority blocker
 * holds an uncommitted update of t.k = 1, so every attempt of reading the row by a victim
 * transaction under REPEATABLE READ isolation fails (rather than waits) until the blocker commits.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgTxnRestartCleanup extends BasePgSQLTest {
  private static final String PORTAL_REFUSAL =
      "a portal or cursor opened by an earlier statement would not survive the transaction restart";

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("enable_wait_queues", "false");
    appendToYsqlPgConf(flags, "yb_debug_log_internal_restarts=true");
    return flags;
  }

  private void createConflictTable() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)");
      stmt.execute("INSERT INTO t VALUES (1, 1)");
    }
  }

  /**
   * Opens a high priority transaction that holds an uncommitted update of t.k = 1 at a higher
   * priority than any victim.
   */
  private Connection beginBlocker() throws Exception {
    Connection blocker = getConnectionBuilder().connect();
    try (Statement stmt = blocker.createStatement()) {
      stmt.execute("SET yb_transaction_priority_lower_bound = 0.6");
      blocker.setAutoCommit(false);
      // An explicit row lock puts the blocker in the high-priority class, which a victim using
      // FOR UPDATE is in too. The bounds then order the two within it.
      stmt.execute("SELECT 1 FROM t WHERE k = 1 FOR UPDATE");
      stmt.execute("UPDATE t SET v = 2 WHERE k = 1");
    }
    return blocker;
  }

  /** The low-priority side of the conflict, retrying until the blocker commits. */
  private Connection connectVictim() throws Exception {
    Connection victim = getConnectionBuilder()
        .withIsolationLevel(IsolationLevel.REPEATABLE_READ)
        .connect();
    // Set these before autocommit goes off, so they don't open the transaction under test.
    try (Statement stmt = victim.createStatement()) {
      stmt.execute("SET yb_transaction_priority_upper_bound = 0.4");
      // The base class lowers this to 2; give the blocker time to commit.
      stmt.execute("SET yb_max_query_layer_retries = 60");
    }
    victim.setAutoCommit(false);
    return victim;
  }

  /**
   * Waits until the victim's statement has run into the blocker.
   */
  private void waitForConflict(int victimPid, String queryPrefix, Future<?> result)
      throws Exception {
    try (PreparedStatement activity = connection.prepareStatement(
        "SELECT 1 FROM pg_stat_activity WHERE pid = ? AND state = 'active' AND query LIKE ?")) {
      activity.setInt(1, victimPid);
      activity.setString(2, queryPrefix + "%");
      TestUtils.waitFor(() -> {
        try (ResultSet rs = activity.executeQuery()) {
          return result.isDone() || rs.next();
        }
      }, 30_000);
    }

    /* Wait for a second to give it time to conflict and retry. */
    Thread.sleep(1000);
  }

  /**
   * Test to validate that a cursor DECLAREd earlier in the pipeline is still open when a later
   * statement conflicts. Restarting would drop the cursor, so the retry is refused.
   */
  @Test
  public void testPipelinedCursorRefusesRestart() throws Exception {
    createConflictTable();
    ExecutorService executor = Executors.newSingleThreadExecutor();
    try (Connection blocker = beginBlocker();
         Connection victim = connectVictim();
         Statement stmt = victim.createStatement()) {
      // Not from the victim thread: the connection is busy once the batch is running.
      int victimPid = getPgBackendPid(victim);

      // pgjdbc sends BEGIN and the whole batch before a single Sync packet, so nothing reaches the
      // client before the UPDATE fails and the transaction is otherwise restartable.
      stmt.addBatch("DECLARE c CURSOR FOR SELECT k FROM t");
      stmt.addBatch("UPDATE t SET v = v + 10 WHERE k = 1");
      Future<SQLException> result = executor.submit(() -> {
        try {
          stmt.executeBatch();
          return null;
        } catch (SQLException e) {
          return e;
        }
      });

      waitForConflict(victimPid, "UPDATE", result);
      blocker.commit();

      SQLException e = result.get(60, TimeUnit.SECONDS);
      assertNotNull("batch should fail instead of restarting", e);
      // pgjdbc chains the server error behind its BatchUpdateException.
      StringBuilder messages = new StringBuilder();
      for (SQLException x = e; x != null; x = x.getNextException()) {
        messages.append(x.getMessage()).append('\n');
      }
      assertTrue("unexpected error: " + messages, messages.toString().contains(PORTAL_REFUSAL));

      victim.rollback();
      try (ResultSet rs = stmt.executeQuery("SELECT v FROM t WHERE k = 1")) {
        assertTrue(rs.next());
        assertEquals(2, rs.getInt(1));
      }
      victim.commit();
    } finally {
      executor.shutdownNow();
    }
  }

  /**
   * Test to validate that a finished portal does not block transaction restart.
   */
  @Test
  public void testFinishedPortalAllowsRestart() throws Exception {
    createConflictTable();
    ExecutorService executor = Executors.newSingleThreadExecutor();
    try (Connection blocker = beginBlocker();
         Connection victim = connectVictim();
         Statement stmt = victim.createStatement()) {
      int victimPid = getPgBackendPid(victim);

      // A fetch size with autocommit off makes pgjdbc run the query on a named portal.
      stmt.setFetchSize(10);
      Future<Integer> result = executor.submit(() -> {
        try (ResultSet rs = stmt.executeQuery("SELECT v FROM t WHERE k = 1 FOR UPDATE")) {
          assertTrue(rs.next());
          return rs.getInt(1);
        }
      });

      waitForConflict(victimPid, "SELECT", result);
      blocker.commit();

      // The blocker's committed value: the restart took a new snapshot.
      assertEquals(Integer.valueOf(2), result.get(60, TimeUnit.SECONDS));
      victim.commit();
    } finally {
      executor.shutdownNow();
    }
  }
}
