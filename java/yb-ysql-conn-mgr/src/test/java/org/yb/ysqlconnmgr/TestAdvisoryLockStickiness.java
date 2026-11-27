// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Collections;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestAdvisoryLockStickiness extends BaseYsqlConnMgr {
  private static final String LOCK_COUNT_GUC = "ysql_conn_mgr_sticky_lock_count";
  private static final String PERMANENT_STICKY_GUC = "yb_conn_mgr_advisory_lock_permanent_sticky";
  private static final String MARKER_GUC = "log_statement";
  private static final String MARKER_FLAG = "ysql_" + MARKER_GUC;

  private void assertLockCount(Statement stmt, int expected) throws Exception {
    assertTrue("Unexpected " + LOCK_COUNT_GUC,
        verifySessionParameterValue(stmt, LOCK_COUNT_GUC, Integer.toString(expected)));
  }

  private boolean queryBool(Statement stmt, String query) throws Exception {
    try (ResultSet rs = stmt.executeQuery(query)) {
      assertTrue(rs.next());
      return rs.getBoolean(1);
    }
  }

  // Take a lock: the connection becomes sticky and another connection cannot take it. Unlock:
  // the connection becomes unsticky and the other connection can take the lock.
  @Test
  public void testLockUnlock() throws Exception {
    try (Connection conn = connectToConnMgr();
         Connection otherConn = connectToConnMgr();
         Statement stmt = conn.createStatement();
         Statement otherStmt = otherConn.createStatement()) {
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);

      stmt.execute("SELECT pg_advisory_lock(1)");
      assertLockCount(stmt, 1);
      assertConnectionStickyState(stmt, true);
      assertEquals(false, queryBool(otherStmt, "SELECT pg_try_advisory_lock(1)"));

      assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);

      assertEquals(true, queryBool(otherStmt, "SELECT pg_try_advisory_lock(1)"));
      assertEquals(true, queryBool(otherStmt, "SELECT pg_advisory_unlock(1)"));
    }
  }

  // Take the same lock twice and a shared lock once. The connection stays sticky until the last
  // unlock, then becomes unsticky.
  @Test
  public void testNestedLocks() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT pg_advisory_lock(1)");
      stmt.execute("SELECT pg_advisory_lock(1)");
      stmt.execute("SELECT pg_advisory_lock_shared(2)");
      assertLockCount(stmt, 3);

      assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
      assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock_shared(2)"));
      assertLockCount(stmt, 1);
      assertConnectionStickyState(stmt, true);

      assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);
    }
  }

  // Try a lock another connection holds: the try fails, the count stays 0 and the connection
  // stays unsticky.
  @Test
  public void testFailedTryLock() throws Exception {
    try (Connection holderConn = connectToConnMgr();
         Connection conn = connectToConnMgr();
         Statement holderStmt = holderConn.createStatement();
         Statement stmt = conn.createStatement()) {
      holderStmt.execute("SELECT pg_advisory_lock(1)");

      assertEquals(false, queryBool(stmt, "SELECT pg_try_advisory_lock(1)"));
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);

      assertEquals(true, queryBool(holderStmt, "SELECT pg_advisory_unlock(1)"));
    }
  }

  // With wait queues off, take a lock another connection holds until it fails with a conflict
  // error: the count stays 0 and the connection stays unsticky.
  @Test
  public void testConflictRetriesNotCounted() throws Exception {
    markClusterNeedsRecreation();
    // Disable wait queues so that the second connection will get a conflict error instead of
    // waiting for the lock.
    restartClusterWithAdditionalFlags(Collections.emptyMap(),
        Collections.singletonMap("enable_wait_queues", "false"));

    // Retry 20 times since transaction priorities are random, the second connection may
    // not always get a conflict error.
    for (int key = 1; key <= 20; key++) {
      try (Connection holderConn = connectToConnMgr();
           Connection conn = connectToConnMgr();
           Statement holderStmt = holderConn.createStatement();
           Statement stmt = conn.createStatement()) {
        holderStmt.execute("SELECT pg_advisory_lock(" + key + ")");
        stmt.execute("SET yb_max_query_layer_retries = 3");
        try {
          stmt.execute("SELECT pg_advisory_lock(" + key + ")");
        } catch (SQLException e) {
          assertEquals("40001", e.getSQLState());
          assertLockCount(stmt, 0);
          assertConnectionStickyState(stmt, false);
          assertEquals(true,
              queryBool(holderStmt, "SELECT pg_advisory_unlock(" + key + ")"));
          return;
        }
        assertLockCount(stmt, 1);
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(" + key + ")"));
      }
    }
    fail("pg_advisory_lock never lost a conflict");
  }

  // Unlock locks that are not held: the unlocks return false and the count is unchanged.
  // Unlocking the held lock makes the connection unsticky.
  @Test
  public void testUnlockNotHeld() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT pg_advisory_lock(1)");
      assertEquals(false, queryBool(stmt, "SELECT pg_advisory_unlock(2)"));
      assertEquals(false, queryBool(stmt, "SELECT pg_advisory_unlock_shared(1)"));
      assertLockCount(stmt, 1);
      assertConnectionStickyState(stmt, true);

      assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
      assertEquals(false, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);
    }
  }

  // Take three locks, then call pg_advisory_unlock_all(): the count drops to 0 and the
  // connection becomes unsticky.
  @Test
  public void testUnlockAll() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT pg_advisory_lock(1)");
      stmt.execute("SELECT pg_advisory_lock(1, 2)");
      stmt.execute("SELECT pg_advisory_lock_shared(3)");
      assertLockCount(stmt, 3);

      stmt.execute("SELECT pg_advisory_unlock_all()");
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);
    }
  }

  // Take a lock in a transaction and roll back: the lock is kept and the connection stays sticky.
  // Unlock in a rolled-back transaction: the connection becomes unsticky.
  @Test
  public void testLockSurvivesRollback() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("SELECT pg_advisory_lock(1)");
      stmt.execute("ROLLBACK");
      assertLockCount(stmt, 1);
      assertConnectionStickyState(stmt, true);

      stmt.execute("BEGIN");
      assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
      stmt.execute("ROLLBACK");
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);
    }
  }

  // Take transaction-level locks and commit: the count stays 0 and the connection stays
  // unsticky.
  @Test
  public void testXactLocksNotSticky() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("SELECT pg_advisory_xact_lock(1)");
      stmt.execute("SELECT pg_try_advisory_xact_lock_shared(2)");
      stmt.execute("COMMIT");
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, false);
    }
  }

  // Start the cluster with advisory_lock excluded. Lock and unlock: the connection stays sticky.
  @Test
  public void testExclusionAtStartup() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithAdditionalFlags(Collections.emptyMap(),
        Collections.singletonMap(EXCLUSIONS_FLAG, "advisory_lock"));
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT pg_advisory_lock(1)");
      assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
      assertLockCount(stmt, 0);
      assertConnectionStickyState(stmt, true);
    }
  }

  // Take a lock, then exclude advisory_lock at runtime and unlock: the connection stays sticky.
  // A new connection that locks and unlocks also stays sticky.
  @Test
  public void testExclusionAddedWhileLockHeld() throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        stmt.execute("SELECT pg_advisory_lock(1)");
        assertLockCount(stmt, 1);
        assertConnectionStickyState(stmt, true);

        setReversibleStickinessExclusions(stmt, "advisory_lock");
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }

      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        stmt.execute("SELECT pg_advisory_lock(2)");
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(2)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Take a lock, then exclude advisory_lock and run a statement on the same connection. Clear the
  // list and unlock: the connection stays sticky.
  @Test
  public void testExclusionAddedAndRemovedWhileLockHeld() throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        stmt.execute("SELECT pg_advisory_lock(1)");
        assertLockCount(stmt, 1);

        setReversibleStickinessExclusions(stmt, "advisory_lock");
        setReversibleStickinessExclusions(stmt, "");
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Take a lock, then exclude advisory_lock, waiting for the reload on a second connection. The
  // next statement on the first connection is the unlock: the connection stays sticky.
  @Test
  public void testExclusionAddedBeforeUnlock() throws Exception {
    testExclusionAddedBeforeRelease("SELECT pg_advisory_unlock(1)");
  }

  // Take a lock, then exclude advisory_lock, waiting for the reload on a second connection. The
  // next statement on the first connection is pg_advisory_unlock_all(): the connection stays
  // sticky.
  @Test
  public void testExclusionAddedBeforeUnlockAll() throws Exception {
    testExclusionAddedBeforeRelease("SELECT pg_advisory_unlock_all()");
  }

  private void testExclusionAddedBeforeRelease(String release) throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Connection otherConn = connectToConnMgr();
           Statement stmt = conn.createStatement();
           Statement otherStmt = otherConn.createStatement()) {
        stmt.execute("SELECT pg_advisory_lock(1)");
        assertLockCount(stmt, 1);

        setReversibleStickinessExclusions(otherStmt, "advisory_lock");
        stmt.execute(release);
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Exclude advisory_lock, then lock and unlock inside one transaction and commit: the connection
  // stays sticky.
  @Test
  public void testExclusionLockUnlockInTransaction() throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "advisory_lock");
        stmt.execute("BEGIN");
        stmt.execute("SELECT pg_advisory_lock(1)");
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
        stmt.execute("COMMIT");
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Exclude advisory_lock, lock, then clear the list and unlock: the connection stays sticky.
  // A new connection that locks and unlocks becomes unsticky.
  @Test
  public void testExclusionRemovedWhileLockHeld() throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "advisory_lock");
        stmt.execute("SELECT pg_advisory_lock(1)");
        assertLockCount(stmt, 1);
        assertConnectionStickyState(stmt, true);

        setReversibleStickinessExclusions(stmt, "");
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }

      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        stmt.execute("SELECT pg_advisory_lock(2)");
        assertConnectionStickyState(stmt, true);
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(2)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, false);
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Inside one transaction, exclude advisory_lock, then set the list to an unknown token along
  // with a change to log_statement. Once log_statement changes, the list still holds
  // advisory_lock. Lock, commit and unlock: the connection stays sticky.
  @Test
  public void testInvalidExclusionRejected() throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        stmt.execute("BEGIN");
        setReversibleStickinessExclusions(stmt, "advisory_lock");

        try {
          setServerFlagOnAll(EXCLUSIONS_FLAG, "advisory_lock,bogus");
          fail("Expected set_flag to reject an unknown sticky source");
        } catch (java.io.IOException e) {
        }
        setServerFlagOnAll(MARKER_FLAG, "ddl");
        TestUtils.waitFor(() -> verifySessionParameterValue(stmt, MARKER_GUC, "ddl"), 30000);
        assertTrue(verifySessionParameterValue(stmt, PERMANENT_STICKY_GUC, "on"));

        stmt.execute("SELECT pg_advisory_lock(1)");
        stmt.execute("COMMIT");
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }
    } finally {
      setServerFlagOnAll(MARKER_FLAG, "none");
      clearReversibleStickinessExclusions();
    }
  }

  // Set the list to " ADVISORY_LOCK , advisory_lock ". Lock and unlock: the connection stays
  // sticky. Then set the list to empty: a new connection that locks and unlocks becomes unsticky.
  @Test
  public void testExclusionParsing() throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, " ADVISORY_LOCK , advisory_lock ");
        stmt.execute("SELECT pg_advisory_lock(1)");
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, true);
      }

      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "");
        stmt.execute("SELECT pg_advisory_lock(1)");
        assertConnectionStickyState(stmt, true);
        assertEquals(true, queryBool(stmt, "SELECT pg_advisory_unlock(1)"));
        assertLockCount(stmt, 0);
        assertConnectionStickyState(stmt, false);
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }
}
