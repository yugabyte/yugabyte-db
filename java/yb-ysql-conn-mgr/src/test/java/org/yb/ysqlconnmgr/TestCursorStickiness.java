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
import java.sql.SQLException;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestCursorStickiness extends BaseYsqlConnMgr {
  private static final String DECLARE_HOLD =
      "DECLARE %s CURSOR WITH HOLD FOR SELECT x FROM generate_series(1, 5) x";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
  }

  private int openCursors(Statement stmt) throws Exception {
    return queryInt(stmt, "SELECT count(*) FROM pg_cursors WHERE name <> ''");
  }

  private void declareHold(Statement stmt, String name) throws Exception {
    stmt.execute(String.format(DECLARE_HOLD, name));
  }

  // DECLARE WITH HOLD: a second client cannot get the only backend. CLOSE: the second client is
  // served.
  @Test
  public void testDeclareClose() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      assertQueuedClientServed();

      declareHold(stmt, "c");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("CLOSE c");
      client.assertServed();
      assertEquals(0, openCursors(stmt));
    }
  }

  // DECLARE two WITH HOLD cursors and CLOSE one: the second client stays blocked. CLOSE the other:
  // the second client is served.
  @Test
  public void testCloseOneOfTwoCursors() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      declareHold(stmt, "a");
      declareHold(stmt, "b");
      stmt.execute("CLOSE a");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("CLOSE b");
      client.assertServed();
    }
  }

  // DECLARE a cursor without HOLD in a committed transaction: the second client is served and no
  // cursor is open.
  @Test
  public void testCursorWithoutHold() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("DECLARE c CURSOR FOR SELECT 1");
      stmt.execute("COMMIT");
      assertQueuedClientServed();
      assertEquals(0, openCursors(stmt));
    }
  }

  // DECLARE WITH HOLD in a transaction that is rolled back: the second client is served and no
  // cursor is open.
  @Test
  public void testDeclareRolledBack() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      declareHold(stmt, "c");
      stmt.execute("ROLLBACK");
      assertQueuedClientServed();
      assertEquals(0, openCursors(stmt));
    }
  }

  // DECLARE WITH HOLD whose query fails when the cursor is materialized at COMMIT: COMMIT fails,
  // the second client is served and no cursor is open.
  @Test
  public void testDeclareFailsAtCommit() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute(
          "DECLARE c CURSOR WITH HOLD FOR SELECT 1 / (x - 3) FROM generate_series(1, 5) x");
      try {
        stmt.execute("COMMIT");
        fail("COMMIT should fail with division by zero");
      } catch (SQLException e) {
        assertTrue(e.getMessage(), e.getMessage().contains("division by zero"));
      }
      assertQueuedClientServed();
      assertEquals(0, openCursors(stmt));
    }
  }

  // DECLARE WITH HOLD, then CLOSE in a transaction that is rolled back: the cursor stays closed
  // and the second client is served.
  @Test
  public void testCloseRolledBack() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      declareHold(stmt, "c");
      stmt.execute("BEGIN");
      stmt.execute("CLOSE c");
      stmt.execute("ROLLBACK");
      assertQueuedClientServed();
      assertEquals(0, openCursors(stmt));
    }
  }

  // DECLARE a NO SCROLL WITH HOLD cursor, then a plpgsql FETCH PRIOR on it fails: the cursor
  // stays open and the second client stays blocked. CLOSE: the second client is served.
  @Test
  public void testFailedFetchKeepsCursor() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute(
          "DECLARE c NO SCROLL CURSOR WITH HOLD FOR SELECT x FROM generate_series(1, 5) x");
      assertEquals(1, queryInt(stmt, "FETCH c"));
      try {
        stmt.execute("DO $$ DECLARE r refcursor := 'c'; v int; " +
            "BEGIN FETCH PRIOR FROM r INTO v; END $$");
        fail("FETCH PRIOR should fail on a NO SCROLL cursor");
      } catch (SQLException e) {
        assertTrue(e.getMessage(), e.getMessage().contains("can only scan forward"));
      }
      QueuedClient client = startQueuedClient();
      client.assertBlocked();
      assertEquals(1, openCursors(stmt));

      stmt.execute("CLOSE c");
      client.assertServed();
    }
  }

  // CALL a procedure that commits inside a FOR loop over a query: the second client is served
  // and no cursor is open.
  @Test
  public void testProcedureCommitInLoop() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE OR REPLACE PROCEDURE commit_in_loop() LANGUAGE plpgsql AS $$ " +
          "DECLARE r record; BEGIN " +
          "FOR r IN SELECT x FROM generate_series(1, 3) x LOOP COMMIT; END LOOP; END $$");
      stmt.execute("CALL commit_in_loop()");
      assertQueuedClientServed();
      assertEquals(0, openCursors(stmt));
      stmt.execute("DROP PROCEDURE commit_in_loop");
    }
  }

  // DECLARE WITH HOLD in a batch that commits and runs another statement before its single
  // ReadyForQuery: the second client stays blocked until CLOSE.
  @Test
  public void testHeldCursorInBatchAcrossCommit() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN; " + String.format(DECLARE_HOLD, "c") + "; COMMIT; SELECT 1");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("CLOSE c");
      client.assertServed();
    }
  }

  // DECLARE WITH HOLD and CLOSE on one client. A second client gets the same backend, sees no
  // cursors and can declare a cursor with the same name.
  @Test
  public void testNextClientSeesNoCursors() throws Exception {
    try (Connection conn = connectToConnMgr();
         Connection nextConn = connectToConnMgr();
         Statement stmt = conn.createStatement();
         Statement nextStmt = nextConn.createStatement()) {
      declareHold(stmt, "c");
      int pid = queryInt(stmt, "SELECT pg_backend_pid()");
      stmt.execute("CLOSE c");

      assertEquals(pid, queryInt(nextStmt, "SELECT pg_backend_pid()"));
      assertEquals(0, openCursors(nextStmt));
      declareHold(nextStmt, "c");
      nextStmt.execute("CLOSE c");
    }
  }

  // Exclude cursor, then DECLARE WITH HOLD and CLOSE: the second client stays blocked until the
  // first client disconnects.
  @Test
  public void testExclusionBeforeDeclare() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "listen,cursor");
        declareHold(stmt, "c");
        stmt.execute("CLOSE c");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // DECLARE WITH HOLD, then exclude cursor at runtime and CLOSE: the second client stays blocked
  // until the first client disconnects.
  @Test
  public void testExclusionAddedWhileHeld() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        declareHold(stmt, "c");
        setReversibleStickinessExclusions(stmt, "cursor");
        stmt.execute("CLOSE c");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Exclude cursor, DECLARE WITH HOLD, then clear the list and CLOSE: the second client stays
  // blocked until the first client disconnects. A new client that declares and closes a cursor
  // does not block others.
  @Test
  public void testExclusionRemovedWhileHeld() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "cursor");
        declareHold(stmt, "c");
        setReversibleStickinessExclusions(stmt, "");
        stmt.execute("CLOSE c");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();

      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        declareHold(stmt, "c");
        stmt.execute("CLOSE c");
        assertQueuedClientServed();
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }
}
