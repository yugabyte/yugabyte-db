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
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

import com.google.gson.JsonObject;
import com.yugabyte.core.BaseConnection;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestCancelQuery extends BaseYsqlConnMgr {
  private static final String QUERY_CANCELED = "57014";
  private static final String IN_FAILED_SQL_TRANSACTION = "25P02";
  private static final long WAIT_TIMEOUT_MS = 30000;
  // Outlasts WAIT_TIMEOUT_MS, so a long sleep only finishes in time if the cancel lands.
  private static final int LONG_SLEEP_S = 60;
  private static final int SHORT_SLEEP_S = 5;

  private final ExecutorService executor = Executors.newCachedThreadPool();

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_max_conns_per_db", "2");
    builder.addCommonTServerFlag("ysql_conn_mgr_enable_multi_route_pool", "false");
  }

  @After
  public void shutdownExecutor() {
    executor.shutdownNow();
  }

  private static class RunningQuery {
    final Future<?> result;
    final int backendPid;

    RunningQuery(Future<?> result, int backendPid) {
      this.result = result;
      this.backendPid = backendPid;
    }
  }

  private Connection connMgrConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
  }

  private Connection postgresConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
        .connect();
  }

  private static int backendPid(Connection conn) throws Exception {
    try (Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT pg_backend_pid()")) {
      rs.next();
      return rs.getInt(1);
    }
  }

  private static void assertSelectOne(Connection conn) throws Exception {
    try (Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT 1")) {
      assertTrue(rs.next());
      assertEquals(1, rs.getInt(1));
    }
  }

  private static void assertSqlState(Throwable t, String expectedSqlState) {
    assertTrue("Expected SQLException, got " + t, t instanceof SQLException);
    assertEquals(expectedSqlState, ((SQLException) t).getSQLState());
  }

  private static void assertCanceled(Future<?> result) throws Exception {
    try {
      result.get(WAIT_TIMEOUT_MS, TimeUnit.MILLISECONDS);
      fail("Query was expected to be canceled");
    } catch (ExecutionException e) {
      assertSqlState(e.getCause(), QUERY_CANCELED);
    }
  }

  private static void assertCompleted(Future<?> result) throws Exception {
    result.get(WAIT_TIMEOUT_MS, TimeUnit.MILLISECONDS);
  }

  // Starts pg_sleep on stmt and returns once a backend is executing it. The pid is read through a
  // direct postgres connection, since conn mgr hands clients its own key instead of the real pid.
  private RunningQuery startSleep(Statement stmt, String marker, int seconds) throws Exception {
    Future<?> result = executor.submit(() -> {
      stmt.execute("SELECT pg_sleep(" + seconds + ") /* " + marker + " */");
      return null;
    });

    AtomicInteger pid = new AtomicInteger(-1);
    try (Connection observer = postgresConn();
         Statement observerStmt = observer.createStatement()) {
      TestUtils.waitFor(() -> {
        if (result.isDone()) {
          result.get();
          fail("Query '" + marker + "' finished before it was seen running");
        }
        try (ResultSet rs = observerStmt.executeQuery(
            "SELECT pid FROM pg_stat_activity WHERE state = 'active'" +
            " AND pid <> pg_backend_pid() AND query LIKE '%" + marker + "%'")) {
          if (!rs.next()) {
            return false;
          }
          pid.set(rs.getInt(1));
          return true;
        }
      }, WAIT_TIMEOUT_MS);
    }
    return new RunningQuery(result, pid.get());
  }

  // Statement.cancel() sends a CancelRequest carrying the key conn mgr handed the client; conn mgr
  // must map it to the backend the client is attached to, and the connection stays usable.
  @Test
  public void testStatementCancel() throws Exception {
    try (Connection conn = connMgrConn();
         Statement stmt = conn.createStatement()) {
      RunningQuery query = startSleep(stmt, "stmt_cancel", LONG_SLEEP_S);
      stmt.cancel();
      assertCanceled(query.result);
      assertSelectOne(conn);
    }
  }

  // The driver-side query timeout is implemented with a CancelRequest.
  @Test
  public void testQueryTimeout() throws Exception {
    try (Connection conn = connMgrConn();
         Statement stmt = conn.createStatement()) {
      stmt.setQueryTimeout(1);
      try {
        stmt.execute("SELECT pg_sleep(" + LONG_SLEEP_S + ")");
        fail("Query was expected to time out");
      } catch (SQLException e) {
        assertSqlState(e, QUERY_CANCELED);
      }
      stmt.setQueryTimeout(0);
      assertSelectOne(conn);
    }
  }

  // With two clients running on separate backends, canceling one must not touch the other.
  @Test
  public void testCancelOnlyAffectsTargetClient() throws Exception {
    try (Connection target = connMgrConn();
         Connection bystander = connMgrConn();
         Statement targetStmt = target.createStatement();
         Statement bystanderStmt = bystander.createStatement()) {
      RunningQuery bystanderQuery = startSleep(bystanderStmt, "bystander", SHORT_SLEEP_S);
      RunningQuery targetQuery = startSleep(targetStmt, "target", LONG_SLEEP_S);
      assertNotEquals(bystanderQuery.backendPid, targetQuery.backendPid);

      targetStmt.cancel();
      assertCanceled(targetQuery.result);
      assertCompleted(bystanderQuery.result);
    }
  }

  // A cancel from a client that has detached must not reach the next client attached to the
  // backend it last used. holder pins one of the two backends in an open transaction so that
  // active is forced onto the backend idle just released.
  @Test
  public void testCancelFromDetachedClientIsIgnored() throws Exception {
    try (Connection holder = connMgrConn();
         Connection idle = connMgrConn();
         Connection active = connMgrConn();
         Statement activeStmt = active.createStatement()) {
      holder.setAutoCommit(false);
      int holderPid = backendPid(holder);
      int idlePid = backendPid(idle);
      assertNotEquals(holderPid, idlePid);

      RunningQuery query = startSleep(activeStmt, "reused_backend", SHORT_SLEEP_S);
      assertEquals(idlePid, query.backendPid);

      idle.unwrap(BaseConnection.class).cancelQuery();
      assertCompleted(query.result);
      holder.commit();
    }
  }

  // A sticky client keeps its backend, so session state must survive the cancel.
  @Test
  public void testCancelStickyClient() throws Exception {
    try (Connection conn = connMgrConn();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE TEMP TABLE cancel_sticky (k INT)");
      stmt.execute("INSERT INTO cancel_sticky VALUES (1)");
      int pidBefore = backendPid(conn);

      RunningQuery query = startSleep(stmt, "sticky_cancel", LONG_SLEEP_S);
      assertEquals(pidBefore, query.backendPid);
      stmt.cancel();
      assertCanceled(query.result);

      assertEquals(pidBefore, backendPid(conn));
      try (ResultSet rs = stmt.executeQuery("SELECT count(*) FROM cancel_sticky")) {
        rs.next();
        assertEquals(1, rs.getInt(1));
      }
    }
  }

  // Canceling inside an explicit transaction aborts it; the client stays attached until ROLLBACK
  // and is usable afterwards.
  @Test
  public void testCancelInTransaction() throws Exception {
    try (Connection conn = connMgrConn();
         Statement stmt = conn.createStatement()) {
      conn.setAutoCommit(false);
      int txnPid = backendPid(conn);

      RunningQuery query = startSleep(stmt, "txn_cancel", LONG_SLEEP_S);
      assertEquals(txnPid, query.backendPid);
      stmt.cancel();
      assertCanceled(query.result);

      try {
        stmt.execute("SELECT 1");
        fail("Statement in an aborted transaction was expected to fail");
      } catch (SQLException e) {
        assertSqlState(e, IN_FAILED_SQL_TRANSACTION);
      }
      conn.rollback();
      conn.setAutoCommit(true);
      assertSelectOne(conn);
    }
  }

  // The cancel key belongs to the client, not to a backend: after the client moves to a different
  // backend, its cancel must reach the new one. holder pins the backend the client used first.
  @Test
  public void testCancelAfterClientMovesBackend() throws Exception {
    try (Connection conn = connMgrConn();
         Connection holder = connMgrConn();
         Connection otherHolder = connMgrConn();
         Statement stmt = conn.createStatement()) {
      int firstPid = backendPid(conn);

      holder.setAutoCommit(false);
      if (backendPid(holder) != firstPid) {
        otherHolder.setAutoCommit(false);
        assertEquals(firstPid, backendPid(otherHolder));
        holder.commit();
      }

      RunningQuery query = startSleep(stmt, "moved_backend", LONG_SLEEP_S);
      assertNotEquals(firstPid, query.backendPid);
      stmt.cancel();
      assertCanceled(query.result);
      assertSelectOne(conn);
    }
  }

  // A client queued for a backend has no server for its cancel to reach, so the cancel is dropped
  // and the query runs once a backend frees up. Postgres would cancel it instead.
  @Test
  public void testCancelWhileQueuedIsDropped() throws Exception {
    try (Connection holder1 = connMgrConn();
         Connection holder2 = connMgrConn();
         Connection queued = connMgrConn();
         Statement queuedStmt = queued.createStatement()) {
      holder1.setAutoCommit(false);
      holder2.setAutoCommit(false);
      assertNotEquals(backendPid(holder1), backendPid(holder2));

      Future<?> result = executor.submit(() -> {
        queuedStmt.execute("SELECT pg_sleep(" + SHORT_SLEEP_S + ")");
        return null;
      });
      TestUtils.waitFor(() -> {
        JsonObject pool = getPool("yugabyte", "yugabyte");
        return pool != null && pool.get("queued_logical_connections").getAsInt() == 1;
      }, WAIT_TIMEOUT_MS);

      queuedStmt.cancel();
      holder1.commit();
      assertCompleted(result);
      holder2.commit();
    }
  }
}
