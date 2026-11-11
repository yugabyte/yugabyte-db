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
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Properties;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestPrepareStickiness extends BaseYsqlConnMgr {
  private static final String PREPARE = "PREPARE %s AS SELECT 1";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
  }

  private int sqlPreparedStatements(Statement stmt) throws Exception {
    return queryInt(stmt, "SELECT count(*) FROM pg_prepared_statements WHERE from_sql");
  }

  private void prepare(Statement stmt, String name) throws Exception {
    stmt.execute(String.format(PREPARE, name));
  }

  // PREPARE: a second client cannot get the only backend. DEALLOCATE: the second client is
  // served.
  @Test
  public void testPrepareDeallocate() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      assertQueuedClientServed();

      prepare(stmt, "p");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("DEALLOCATE p");
      client.assertServed();
      assertEquals(0, sqlPreparedStatements(stmt));
    }
  }

  // PREPARE two statements, then DEALLOCATE ALL: the second client is served.
  @Test
  public void testDeallocateAll() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      prepare(stmt, "a");
      prepare(stmt, "b");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("DEALLOCATE ALL");
      client.assertServed();
      assertEquals(0, sqlPreparedStatements(stmt));
    }
  }

  // PREPARE two statements and DEALLOCATE one: the second client stays blocked. DEALLOCATE the
  // other: the second client is served.
  @Test
  public void testDeallocateOneOfTwo() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      prepare(stmt, "a");
      prepare(stmt, "b");
      stmt.execute("DEALLOCATE a");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("DEALLOCATE PREPARE b");
      client.assertServed();
    }
  }

  // PREPARE in a transaction that is rolled back: the statement survives and the second client
  // stays blocked. DEALLOCATE: the second client is served.
  @Test
  public void testPrepareRolledBack() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      prepare(stmt, "p");
      stmt.execute("ROLLBACK");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();
      assertEquals(1, sqlPreparedStatements(stmt));

      stmt.execute("DEALLOCATE p");
      client.assertServed();
    }
  }

  // BEGIN, PREPARE, then disconnect without ending the transaction. The next client gets a new
  // backend and sees no prepared statement.
  @Test
  public void testPrepareThenDisconnectInTransaction() throws Exception {
    int pid;
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      pid = queryInt(stmt, "SELECT pg_backend_pid()");
      stmt.execute("BEGIN");
      prepare(stmt, "p");
    }
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      assertNotEquals(pid, queryInt(stmt, "SELECT pg_backend_pid()"));
      assertEquals(0, sqlPreparedStatements(stmt));
      prepare(stmt, "p");
      stmt.execute("DEALLOCATE p");
    }
  }

  // PREPARE of a query on a missing table fails: the second client is served. PREPARE p twice:
  // the second PREPARE fails, the second client stays blocked, and one DEALLOCATE serves it.
  @Test
  public void testPrepareFails() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      try {
        stmt.execute("PREPARE bad AS SELECT * FROM no_such_table");
        fail("PREPARE should fail");
      } catch (SQLException e) {
        assertTrue(e.getMessage(), e.getMessage().contains("does not exist"));
      }
      assertQueuedClientServed();
      assertEquals(0, sqlPreparedStatements(stmt));

      prepare(stmt, "p");
      try {
        prepare(stmt, "p");
        fail("Second PREPARE should fail");
      } catch (SQLException e) {
        assertTrue(e.getMessage(), e.getMessage().contains("already exists"));
      }
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("DEALLOCATE p");
      client.assertServed();
    }
  }

  // PREPARE, then DEALLOCATE in a transaction that is rolled back: the statement stays dropped
  // and the second client is served.
  @Test
  public void testDeallocateRolledBack() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      prepare(stmt, "p");
      stmt.execute("BEGIN");
      stmt.execute("DEALLOCATE p");
      stmt.execute("ROLLBACK");
      assertQueuedClientServed();
      assertEquals(0, sqlPreparedStatements(stmt));
    }
  }

  // Run a JDBC prepared statement with prepareThreshold=1, so the driver uses a named Parse: the
  // second client is served. PREPARE then DEALLOCATE ALL on the same connection: the second
  // client stays blocked in between and is served after.
  @Test
  public void testProtocolPreparedStatementNotSticky() throws Exception {
    Properties props = new Properties();
    props.setProperty("prepareThreshold", "1");
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect(props);
         PreparedStatement pstmt = conn.prepareStatement("SELECT ?::int");
         Statement stmt = conn.createStatement()) {
      pstmt.setInt(1, 1);
      pstmt.execute();
      pstmt.execute();
      assertQueuedClientServed();

      prepare(stmt, "p");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("DEALLOCATE ALL");
      client.assertServed();
      pstmt.execute();
    }
  }

  // PREPARE and DEALLOCATE on one client. A second client gets the same backend, sees no
  // prepared statements and can prepare a statement with the same name.
  @Test
  public void testNextClientSeesNoPreparedStatements() throws Exception {
    try (Connection conn = connectToConnMgr();
         Connection nextConn = connectToConnMgr();
         Statement stmt = conn.createStatement();
         Statement nextStmt = nextConn.createStatement()) {
      prepare(stmt, "p");
      int pid = queryInt(stmt, "SELECT pg_backend_pid()");
      stmt.execute("DEALLOCATE p");

      assertEquals(pid, queryInt(nextStmt, "SELECT pg_backend_pid()"));
      assertEquals(0, sqlPreparedStatements(nextStmt));
      prepare(nextStmt, "p");
      nextStmt.execute("DEALLOCATE p");
    }
  }

  // Exclude prepare, then PREPARE and DEALLOCATE: the second client stays blocked until the
  // first client disconnects.
  @Test
  public void testExclusionBeforePrepare() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "replication_origin,prepare");
        prepare(stmt, "p");
        stmt.execute("DEALLOCATE p");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // PREPARE, then exclude prepare at runtime and DEALLOCATE: the second client stays blocked
  // until the first client disconnects.
  @Test
  public void testExclusionAddedWhileHeld() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        prepare(stmt, "p");
        setReversibleStickinessExclusions(stmt, "prepare");
        stmt.execute("DEALLOCATE p");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Exclude prepare, PREPARE, then clear the list and DEALLOCATE: the second client stays blocked
  // until the first client disconnects. A new client that prepares and deallocates does not
  // block others.
  @Test
  public void testExclusionRemovedWhileHeld() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "prepare");
        prepare(stmt, "p");
        setReversibleStickinessExclusions(stmt, "");
        stmt.execute("DEALLOCATE p");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();

      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        prepare(stmt, "p");
        stmt.execute("DEALLOCATE p");
        assertQueuedClientServed();
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }
}
