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
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.gson.JsonObject;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Collections;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestInactiveRouteCleanup extends BaseYsqlConnMgr {
  private static final String DEFAULT_DB = "yugabyte";
  private static final String DEFAULT_USER = "yugabyte";
  private static final int DDL_TSERVER_IDX = 0;
  private static final int CONN_MGR_TSERVER_IDX = 1;
  private static final int NUM_IDLE_CLIENTS = 5;
  private static final int NUM_RETRIES = 10;
  private static final long WAIT_SECS = 30;
  private static final String CONNECTION_FAILURE_SQLSTATE = "08006";
  private static final int SERVER_IDLE_TIME_SECS = 2;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
        String.valueOf(STATS_UPDATE_INTERVAL));
    builder.addCommonTServerFlag("ysql_conn_mgr_idle_time",
        String.valueOf(SERVER_IDLE_TIME_SECS));
    builder.addCommonTServerFlag("ysql_conn_mgr_jitter_time", "0");
  }

  private void executeDdlDirect(String sql) throws Exception {
    try (Connection conn = getConnectionBuilder().withTServer(DDL_TSERVER_IDX).connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute(sql);
    }
  }

  private ConnectionBuilder connMgrBuilder(String db, String user) {
    return getConnectionBuilder()
        .withTServer(CONN_MGR_TSERVER_IDX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withDatabase(db)
        .withUser(user);
  }

  private Connection connectConnMgr(String db, String user) throws Exception {
    return connMgrBuilder(db, user).connect();
  }

  // A fresh backend on the connection manager's node has to see the DDL, otherwise the backend
  // spawned by the trigger query could succeed and the route would never be marked inactive.
  private void waitForDdlToBeVisible(String db, String user, String expectedErr)
      throws Exception {
    for (int retry = 1; retry <= NUM_RETRIES; ++retry) {
      try (Connection conn = connectConnMgr(db, user)) {
        LOG.info("Backend for " + db + "." + user + " still starts, retrying");
      } catch (SQLException e) {
        if (e.getMessage().contains(expectedErr))
          return;
        LOG.info("Unexpected error while waiting for the DDL to be visible", e);
      }
      Thread.sleep(retry * 1000);
    }
    fail("DDL on " + db + "." + user + " never became visible");
  }

  private static void assertConnectionClosedByConnMgr(Connection conn) {
    try (Statement stmt = conn.createStatement()) {
      stmt.executeQuery("SELECT 1");
      fail("Expected the connection manager to have closed this client");
    } catch (SQLException e) {
      LOG.info("Got expected exception", e);
      assertEquals("Expected a closed connection, got: " + e.getMessage(),
          CONNECTION_FAILURE_SQLSTATE, e.getSQLState());
    }
  }

  private void waitForNoIdleServers(String db, String user, int expectedActive)
      throws Exception {
    long deadlineMs = System.currentTimeMillis() + WAIT_SECS * 1000;
    while (System.currentTimeMillis() < deadlineMs) {
      JsonObject pool = getPool(db, user, CONN_MGR_TSERVER_IDX);
      if (pool != null &&
          pool.get("active_physical_connections").getAsInt() == expectedActive &&
          pool.get("idle_physical_connections").getAsInt() == 0)
        return;
      Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
    }
    fail("Route " + db + "." + user + " still has idle servers");
  }

  private void waitForQueuedClients(String db, String user, int expectedQueued)
      throws Exception {
    long deadlineMs = System.currentTimeMillis() + WAIT_SECS * 1000;
    while (System.currentTimeMillis() < deadlineMs) {
      JsonObject pool = getPool(db, user, CONN_MGR_TSERVER_IDX);
      if (pool != null &&
          pool.get("queued_logical_connections").getAsInt() == expectedQueued)
        return;
      Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
    }
    fail("Route " + db + "." + user + " never had " + expectedQueued + " queued clients");
  }

  private void waitForPoolToBeDropped(String db, String user) throws Exception {
    long deadlineMs = System.currentTimeMillis() + WAIT_SECS * 1000;
    while (System.currentTimeMillis() < deadlineMs) {
      if (getPool(db, user, CONN_MGR_TSERVER_IDX) == null)
        return;
      Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
    }
    fail("Inactive route " + db + "." + user + " was never dropped");
  }

  // Helper method which executes a DDL and checks that idle clients are closed. Following steps
  // are performed:
  // - Open some idle clients on the route. They never send a query.
  // - Make sure the route has no idle backend. With holdTxn, one client keeps a backend busy in
  //   an open transaction. Without it, all backends are left to expire.
  // - Run the DDL, then send a query on the trigger client. It needs a new backend, which fails
  //   because of the DDL. This marks the route inactive.
  // - With holdTxn, the client in the transaction fails on its next statement.
  // - Check that the route GC closes the idle clients and then drops the route.
  // - Check that the connection manager did not crash.
  private void testIdleClientsClosed(String db, String user, String ddl, String expectedErr,
                                     boolean holdTxn) throws Exception {
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, CONN_MGR_TSERVER_IDX);
    String killLogRegex = "sent kill op to \\d+ pending clients and woke \\d+ queued clients"
        + " on inactive route " + db + "\\." + user;
    String connMgrHost = getPgHost(CONN_MGR_TSERVER_IDX);

    Connection[] idleConns = new Connection[NUM_IDLE_CLIENTS];
    try (Connection sentinelConn = connectConnMgr(DEFAULT_DB, DEFAULT_USER);
         Connection txnConn = connectWithRetries(connMgrBuilder(db, user), NUM_RETRIES);
         Connection triggerConn = connectWithRetries(connMgrBuilder(db, user), NUM_RETRIES);
         Statement sentinelStmt = sentinelConn.createStatement();
         Statement txnStmt = txnConn.createStatement();
         Statement triggerStmt = triggerConn.createStatement()) {
      for (int i = 0; i < NUM_IDLE_CLIENTS; ++i)
        idleConns[i] = connectWithRetries(connMgrBuilder(db, user), NUM_RETRIES);
      sentinelStmt.execute("SELECT 1");
      int odysseyPid = getOdysseyPidForHost(connMgrHost);

      if (holdTxn) {
        txnStmt.execute("BEGIN");
        txnStmt.execute("SELECT 1");
      }
      waitForNoIdleServers(db, user, holdTxn ? 1 : 0);

      executeDdlDirect(ddl);
      waitForDdlToBeVisible(db, user, expectedErr);
      tailer.skipToEnd();

      try {
        triggerStmt.execute("SELECT 1");
        fail("Expected a new backend for " + db + "." + user + " to fail");
      } catch (SQLException e) {
        LOG.info("Got expected exception", e);
        assertTrue("Unexpected error: " + e.getMessage(),
            e.getMessage().contains(expectedErr));
      }

      if (holdTxn) {
        try {
          txnStmt.execute("COMMIT");
          fail("Expected the backend on the inactive route to be closed");
        } catch (SQLException e) {
          LOG.info("Got expected exception", e);
        }
      }

      assertNotNull("Idle clients on the inactive route were not closed",
          tailer.waitForLogRegex(killLogRegex, WAIT_SECS, TimeUnit.SECONDS));
      waitForPoolToBeDropped(db, user);
      for (Connection conn : idleConns)
        assertConnectionClosedByConnMgr(conn);
      if (!holdTxn)
        assertConnectionClosedByConnMgr(txnConn);

      sentinelStmt.execute("SELECT 1");
      assertEquals("Connection manager restarted", odysseyPid,
          getOdysseyPidForHost(connMgrHost));
    } finally {
      for (Connection conn : idleConns) {
        if (conn != null)
          conn.close();
      }
    }
  }

  @Test
  public void testIdleClientsClosedOnNoLogin() throws Exception {
    final String role = "inactive_route_nologin";
    executeDdlDirect("CREATE ROLE " + role + " LOGIN");
    testIdleClientsClosed(DEFAULT_DB, role, "ALTER ROLE " + role + " NOLOGIN",
        "not permitted to log in", true /* holdTxn */);

    executeDdlDirect("ALTER ROLE " + role + " LOGIN");
    try (Connection conn = connectWithRetries(connMgrBuilder(DEFAULT_DB, role), NUM_RETRIES);
         Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT current_user")) {
      assertTrue(rs.next());
      assertEquals(role, rs.getString(1));
    }
  }

  @Test
  public void testIdleClientsClosedOnDropRole() throws Exception {
    final String role = "inactive_route_drop_role";
    executeDdlDirect("CREATE ROLE " + role + " LOGIN");
    testIdleClientsClosed(DEFAULT_DB, role, "DROP ROLE " + role, "does not exist",
        false /* holdTxn */);
  }

  @Test
  public void testIdleClientsClosedOnDropDatabase() throws Exception {
    final String db = "inactive_route_drop_db";
    executeDdlDirect("CREATE DATABASE " + db);
    testIdleClientsClosed(db, DEFAULT_USER, "DROP DATABASE " + db, "does not exist",
        false /* holdTxn */);
  }

  // Checks that a client queued for a backend on an inactive route is closed. Following steps are
  // performed with SINGLE_BACKEND_FLAGS, so that each route has a single backend:
  // - txnConn keeps the route's only backend busy in an open transaction, and queuedConn sends a
  //   query which gets queued.
  // - Run the DDL, then send a query on triggerConn, which is on another route of the same role.
  //   Its new backend fails because of the DDL, which marks all routes of the role inactive.
  // - Check that queuedConn is closed while txnConn still holds the backend. Closing that backend
  //   wakes up queued clients on its own, so it would hide a queued client that is never woken.
  // - Check that the route is dropped once txnConn fails, and that the connection manager did not
  //   crash.
  @Test
  public void testQueuedClientClosedOnNoLogin() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), SINGLE_BACKEND_FLAGS);
    markClusterNeedsRecreation();
    final String role = "inactive_route_queued_nologin";
    final String triggerDb = "inactive_route_queued_nologin_db";
    final String expectedErr = "not permitted to log in";
    executeDdlDirect("CREATE ROLE " + role + " LOGIN");
    executeDdlDirect("CREATE DATABASE " + triggerDb);

    String connMgrHost = getPgHost(CONN_MGR_TSERVER_IDX);
    ExecutorService executor = Executors.newSingleThreadExecutor();
    try (Connection sentinelConn = connectConnMgr(DEFAULT_DB, DEFAULT_USER);
         Connection txnConn = connectWithRetries(connMgrBuilder(DEFAULT_DB, role), NUM_RETRIES);
         Connection queuedConn = connectWithRetries(connMgrBuilder(DEFAULT_DB, role), NUM_RETRIES);
         Connection triggerConn = connectWithRetries(connMgrBuilder(triggerDb, role), NUM_RETRIES);
         Statement sentinelStmt = sentinelConn.createStatement();
         Statement txnStmt = txnConn.createStatement();
         Statement queuedStmt = queuedConn.createStatement();
         Statement triggerStmt = triggerConn.createStatement()) {
      sentinelStmt.execute("SELECT 1");
      int odysseyPid = getOdysseyPidForHost(connMgrHost);

      txnStmt.execute("BEGIN");
      txnStmt.execute("SELECT 1");
      Future<Boolean> queuedQuery = executor.submit(() -> queuedStmt.execute("SELECT 1"));
      waitForQueuedClients(DEFAULT_DB, role, 1);
      waitForNoIdleServers(triggerDb, role, 0);

      executeDdlDirect("ALTER ROLE " + role + " NOLOGIN");
      waitForDdlToBeVisible(triggerDb, role, expectedErr);

      try {
        triggerStmt.execute("SELECT 1");
        fail("Expected a new backend for " + triggerDb + "." + role + " to fail");
      } catch (SQLException e) {
        LOG.info("Got expected exception", e);
        assertTrue("Unexpected error: " + e.getMessage(),
            e.getMessage().contains(expectedErr));
      }

      try {
        queuedQuery.get(WAIT_SECS, TimeUnit.SECONDS);
        fail("Expected the queued client to be closed");
      } catch (TimeoutException e) {
        queuedConn.abort(Runnable::run);
        fail("Queued client on the inactive route was not closed");
      } catch (ExecutionException e) {
        LOG.info("Got expected exception", e.getCause());
        assertTrue("Unexpected exception: " + e.getCause(),
            e.getCause() instanceof SQLException);
        assertEquals("Expected a closed connection, got: " + e.getCause().getMessage(),
            CONNECTION_FAILURE_SQLSTATE, ((SQLException) e.getCause()).getSQLState());
      }

      try {
        txnStmt.execute("COMMIT");
        fail("Expected the backend on the inactive route to be closed");
      } catch (SQLException e) {
        LOG.info("Got expected exception", e);
      }
      waitForPoolToBeDropped(DEFAULT_DB, role);

      sentinelStmt.execute("SELECT 1");
      assertEquals("Connection manager restarted", odysseyPid,
          getOdysseyPidForHost(connMgrHost));
    } finally {
      executor.shutdownNow();
    }
  }
}
