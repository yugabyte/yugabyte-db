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
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBCluster;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestDroppedObjectRoutes extends BaseYsqlConnMgr {
  private static final int DROP_TSERVER_IDX = 0;
  private static final int SESSION_TSERVER_IDX = 1;

  private static final String[] USER_ERRORS = {
    "DO $$ BEGIN RAISE EXCEPTION 'x' USING HINT = "
        + "'Database may have been dropped and recreated'; END $$",
    "DO $$ BEGIN RAISE EXCEPTION 'catalog version for database 1 was not found.' USING "
        + "ERRCODE = 'database_dropped', HINT = 'Database may have been dropped and recreated. "
        + "Ensure your client connection pool or metadata cache is refreshed to pick up the new "
        + "database OID.'; END $$",
    "DO $$ BEGIN RAISE EXCEPTION 'invalid role OID: 1' USING ERRCODE = 'undefined_object'; "
        + "END $$",
    "SELECT 'invalid role OID'::int",
  };

  private Connection directConn() throws Exception {
    return directConn(DROP_TSERVER_IDX);
  }

  private Connection directConn(int tserverIdx) throws Exception {
    return getConnectionBuilder()
        .withTServer(tserverIdx)
        .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
        .connect();
  }

  private void runDirect(String... queries) throws Exception {
    try (Connection conn = directConn(); Statement stmt = conn.createStatement()) {
      for (String query : queries)
        stmt.execute(query);
    }
  }

  private Connection connMgrConn(String database, String user) throws Exception {
    return getConnectionBuilder()
        .withDatabase(database)
        .withUser(user)
        .withTServer(SESSION_TSERVER_IDX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
  }

  private static void waitForCatalogChange() throws InterruptedException {
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 4);
  }

  private static void execute(Connection conn, String query) throws SQLException {
    try (Statement stmt = conn.createStatement()) {
      stmt.execute(query);
    }
  }

  private static void assertHasRoleDroppedFatal(SQLException e, String role) {
    String expected = "role \"" + role + "\" does not exist";
    for (SQLException cur = e; cur != null; cur = cur.getNextException()) {
      if ("28000".equals(cur.getSQLState()) && cur.getMessage().contains(expected))
        return;
    }
    LOG.error("Unexpected error", e);
    fail("Expected FATAL 28000 '" + expected + "', got: " + e.getSQLState() + " "
        + e.getMessage());
  }

  private static void assertClosedWithRoleDroppedFatal(Connection conn, String role)
      throws SQLException {
    try {
      execute(conn, "SELECT 1");
      fail("Expected the session to be disconnected");
    } catch (SQLException e) {
      assertHasRoleDroppedFatal(e, role);
    }
    assertTrue("Expected the session to be closed", conn.isClosed());
  }

  private static void assertClosedWithDbDroppedFatal(Connection conn, String db)
      throws SQLException {
    String expected = "database \"" + db + "\" does not exist";
    try {
      execute(conn, "SELECT 1");
      fail("Expected the session to be disconnected");
    } catch (SQLException e) {
      assertEquals("Unexpected error: " + e.getMessage(), "3D000", e.getSQLState());
      assertTrue("Unexpected error: " + e.getMessage(), e.getMessage().contains(expected));
    }
    assertTrue("Expected the session to be closed", conn.isClosed());
  }

  // A session raises errors whose text looks like a dropped database or role.
  // Its other session and a session of another role both keep their open
  // transactions and commit them; the raising session also stays usable.
  @Test
  public void testUserErrorsKeepOtherSessions() throws Exception {
    runDirect(
        "CREATE ROLE ycm_raiser LOGIN",
        "CREATE ROLE ycm_other LOGIN",
        "CREATE TABLE ycm_t (id int PRIMARY KEY)",
        "GRANT ALL ON ycm_t TO ycm_raiser, ycm_other");

    try (Connection raiser = connMgrConn("yugabyte", "ycm_raiser");
         Connection raiserPeer = connMgrConn("yugabyte", "ycm_raiser");
         Connection other = connMgrConn("yugabyte", "ycm_other")) {
      raiserPeer.setAutoCommit(false);
      other.setAutoCommit(false);
      execute(raiserPeer, "INSERT INTO ycm_t VALUES (1)");
      execute(other, "INSERT INTO ycm_t VALUES (2)");

      for (String query : USER_ERRORS) {
        try {
          execute(raiser, query);
          fail("Expected an error from: " + query);
        } catch (SQLException e) {
          LOG.info("Got expected error", e);
        }
        execute(raiser, "SELECT 1");
      }

      raiserPeer.commit();
      other.commit();
    }

    try (Connection conn = directConn();
         Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT count(*) FROM ycm_t")) {
      assertTrue(rs.next());
      assertEquals(2, rs.getInt(1));
    }
  }

  // Two sessions of a role, then DROP ROLE on another node.
  // current_user fails on the first session and both sessions get disconnected
  // with FATAL 28000 role "..." does not exist.
  @Test
  public void testDroppedRoleClosesItsSessions() throws Exception {
    runDirect("CREATE ROLE ycm_dropped_role LOGIN");

    try (Connection conn1 = connMgrConn("yugabyte", "ycm_dropped_role");
         Connection conn2 = connMgrConn("yugabyte", "ycm_dropped_role")) {
      execute(conn1, "SELECT 1");
      execute(conn2, "SELECT 1");

      runDirect("DROP ROLE ycm_dropped_role");
      waitForCatalogChange();

      try {
        execute(conn1, "SELECT current_user");
        fail("Expected current_user to fail after DROP ROLE");
      } catch (SQLException e) {
        assertTrue("Unexpected error: " + e.getMessage(),
            e.getMessage().contains("invalid role OID"));
        assertHasRoleDroppedFatal(e, "ycm_dropped_role");
      }
      assertTrue("Expected the session to be closed", conn1.isClosed());
      assertClosedWithRoleDroppedFatal(conn2, "ycm_dropped_role");
    }
  }

  // Same as above, but the failing current_user is caught in a PL/pgSQL block.
  // Both sessions still get disconnected.
  @Test
  public void testDroppedRoleCaughtErrorClosesItsSessions() throws Exception {
    runDirect("CREATE ROLE ycm_dropped_role_caught LOGIN");

    try (Connection conn1 = connMgrConn("yugabyte", "ycm_dropped_role_caught");
         Connection conn2 = connMgrConn("yugabyte", "ycm_dropped_role_caught")) {
      execute(conn1, "SELECT 1");
      execute(conn2, "SELECT 1");

      runDirect("DROP ROLE ycm_dropped_role_caught");
      waitForCatalogChange();

      try {
        execute(conn1, "DO $$ BEGIN PERFORM current_user; "
            + "EXCEPTION WHEN undefined_object THEN NULL; END $$");
        fail("Expected the session to be disconnected");
      } catch (SQLException e) {
        assertHasRoleDroppedFatal(e, "ycm_dropped_role_caught");
      }
      assertTrue("Expected the session to be closed", conn1.isClosed());
      assertClosedWithRoleDroppedFatal(conn2, "ycm_dropped_role_caught");
    }
  }

  // Sessions of a database, then their pooled backends are terminated and the
  // database is dropped on another node. Queries on the trigger sessions make
  // ConnMgr spawn a backend, which fails with 3D000 and invalidates the
  // database's routes; a trigger may first hit a terminated backend instead.
  // The remaining session then gets ConnMgr's own FATAL 3D000.
  @Test
  public void testDroppedDbClosesItsSessions() throws Exception {
    final int numTriggers = 4;
    runDirect("CREATE DATABASE ycm_dropped_db");
    waitForCatalogChange();

    List<Connection> triggers = new ArrayList<>();
    try (Connection target = connMgrConn("ycm_dropped_db", "yugabyte")) {
      for (int i = 0; i < numTriggers; i++)
        triggers.add(connMgrConn("ycm_dropped_db", "yugabyte"));
      execute(target, "SELECT 1");
      for (Connection conn : triggers)
        execute(conn, "SELECT 1");

      try (Connection conn = directConn(SESSION_TSERVER_IDX)) {
        execute(conn, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
            + "WHERE datname = 'ycm_dropped_db'");
      }
      runDirect("DROP DATABASE ycm_dropped_db");
      waitForCatalogChange();

      boolean invalidated = false;
      for (Connection conn : triggers) {
        try {
          execute(conn, "SELECT 1");
          fail("Expected the session to fail after DROP DATABASE");
        } catch (SQLException e) {
          LOG.info("Trigger session got error", e);
          if ("3D000".equals(e.getSQLState())) {
            invalidated = true;
            break;
          }
        }
      }
      assertTrue("No trigger session got 3D000", invalidated);
      assertClosedWithDbDroppedFatal(target, "ycm_dropped_db");
    } finally {
      for (Connection conn : triggers)
        conn.close();
    }
  }

  // A role is made NOLOGIN and its pooled backends are terminated. Spawning a
  // new backend fails with 28000 "not permitted to log in", which invalidates
  // the role's routes, but the role still exists, so the remaining session
  // must not be told that it does not exist.
  @Test
  public void testNologinRoleIsNotReportedDropped() throws Exception {
    final int numTriggers = 4;
    runDirect("CREATE ROLE ycm_nologin_role LOGIN");
    waitForCatalogChange();

    List<Connection> triggers = new ArrayList<>();
    try (Connection target = connMgrConn("yugabyte", "ycm_nologin_role")) {
      for (int i = 0; i < numTriggers; i++)
        triggers.add(connMgrConn("yugabyte", "ycm_nologin_role"));
      execute(target, "SELECT 1");
      for (Connection conn : triggers)
        execute(conn, "SELECT 1");

      runDirect("ALTER ROLE ycm_nologin_role NOLOGIN");
      waitForCatalogChange();
      try (Connection conn = directConn(SESSION_TSERVER_IDX)) {
        execute(conn, "SELECT pg_terminate_backend(pid) FROM pg_stat_activity "
            + "WHERE usename = 'ycm_nologin_role'");
      }

      boolean invalidated = false;
      for (Connection conn : triggers) {
        try {
          execute(conn, "SELECT 1");
          fail("Expected the session to fail after ALTER ROLE NOLOGIN");
        } catch (SQLException e) {
          LOG.info("Trigger session got error", e);
          if ("28000".equals(e.getSQLState())) {
            invalidated = true;
            break;
          }
        }
      }
      assertTrue("No trigger session got 28000", invalidated);

      try {
        execute(target, "SELECT 1");
        fail("Expected the session to be disconnected");
      } catch (SQLException e) {
        LOG.info("Target session got error", e);
        assertTrue("Unexpected error: " + e.getSQLState() + " " + e.getMessage(),
            e.getSQLState() != null && e.getSQLState().startsWith("08"));
        assertTrue("Unexpected error: " + e.getMessage(),
            !e.getMessage().contains("does not exist"));
      }
    } finally {
      for (Connection conn : triggers)
        conn.close();
    }
  }
}
