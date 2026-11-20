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
    return getConnectionBuilder()
        .withTServer(DROP_TSERVER_IDX)
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

  private static void assertConnectionLost(Connection conn) {
    try {
      execute(conn, "SELECT 1");
      fail("Expected the session to be disconnected");
    } catch (SQLException e) {
      assertTrue("Unexpected error: " + e.getSQLState() + " " + e.getMessage(),
          e.getSQLState() != null && e.getSQLState().startsWith("08"));
    }
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
  // current_user fails on the first session and both sessions get disconnected.
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
      }
      assertConnectionLost(conn1);
      assertConnectionLost(conn2);
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
      } catch (SQLException e) {
        LOG.info("Got error", e);
      }
      assertConnectionLost(conn1);
      assertConnectionLost(conn2);
    }
  }
}
