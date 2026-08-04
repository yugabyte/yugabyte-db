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

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Properties;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

import com.google.gson.JsonObject;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestBackendSyncReset extends BaseYsqlConnMgr {
  static final String USER = "yugabyte";
  static final String PASS = "yugabyte";
  static final String DB   = "yugabyte";
  static final int TIMEOUT = 1_000;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_wait_timeout_ms", Integer.toString(TIMEOUT));
        put("ysql_conn_mgr_log_settings", "log_debug,log_query");
      }
    };

    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  private int getIdleBackends(String username, String dbname) throws Exception {
    JsonObject pool = getPool(dbname, username);
    assertNotNull(
        "Txn pool should not be empty/non-existent for " + dbname + "," + username + " route",
        pool);
    return pool.get("idle_physical_connections").getAsInt();
  }

  // This test verifies that backends are not left in an inconsistent state in case a client
  // disconnects when a long running query is in progress while employing the Extended Query
  // Protocol.
  // Specifically, if a client sends a sync packet, a bug in conn mgr would allow the backend to
  // pass through the reset phase without actually being reset. While this is bad enough on its own,
  // conn mgr would further effectively buffer the RFQ (and result, if any) from the previous query.
  // This buffered output would then be forwarded to the next client that is assigned to that
  // physical backend, causing protocol violations and possible information leaks.
  //
  // This test fires a "long running" query (here, pg_sleep) via the Ext. Quer. Prot. and then kills
  // the connection. The test then verifies that the backend is correctly reset and reused for the
  // next incoming client.
  @Test
  public void TestAbortDuringExtendedQuery() throws Exception {
    ExecutorService exec = Executors.newCachedThreadPool();
    CountDownLatch started = new CountDownLatch(1);

    Properties props = new Properties();
    props.setProperty("prepareThreshold", "1");
    props.setProperty("user", USER);
    props.setProperty("password", PASS);

    final int[] backendPid = new int[] { -1 };


    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withDatabase(DB)
             .connect(props)) {

      // Capture backend PID before abort
      try (Statement s = conn.createStatement();
           ResultSet rs = s.executeQuery("SELECT pg_backend_pid()")) {
        assertTrue(rs.next());
        backendPid[0] = rs.getInt(1);
      }

      PreparedStatement stmt = conn.prepareStatement("SELECT pg_sleep(5)");

      Future<?> execFuture = exec.submit(() -> {
        try {
          started.countDown();
          stmt.execute();
          fail("Query unexpectedly completed after abort");
        } catch (SQLException e) {
          // Expected: connection abort interrupts execution
        }
      });

      // Abort thread
      Future<?> abortFuture = exec.submit(() -> {
        try {
          started.await();
          Thread.sleep(20);

          ExecutorService abortExec = Executors.newSingleThreadExecutor(r -> {
            Thread t = new Thread(r);
            t.setDaemon(true);
            return t;
          });

          conn.abort(abortExec);
          abortExec.shutdown();
        } catch (Exception e) {
          fail("Abort failed: " + e.getMessage());
        }
      });

      execFuture.get();
      abortFuture.get();
    } finally {
      exec.shutdownNow();
    }

    Thread.sleep(Math.max(2 * TIMEOUT, 2 * STATS_UPDATE_INTERVAL));

    assertEquals("Txn backend should not have been closed during reset phase", 1,
        getIdleBackends(USER, "yugabyte"));

    // Verify that a new connection works fine
    // An error is likely to occur here if the backend was not correctly synced/reset after the
    // abort above.
    try (Connection conn2 = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withDatabase(DB)
             .connect(props);
        Statement stmt2 = conn2.createStatement()) {
      int secondPid;
      ResultSet rs = stmt2.executeQuery("SELECT pg_backend_pid()");
      assertTrue(rs.next());
      secondPid = rs.getInt(1);

      assertEquals("Backend should be reused after aborted extended-protocol query",
          backendPid[0], secondPid);
    }

    Thread.sleep(2 * STATS_UPDATE_INTERVAL);

    assertEquals("No extra txn backends should have been made for a new connection", 1,
        getIdleBackends(USER, "yugabyte"));
  }
}
