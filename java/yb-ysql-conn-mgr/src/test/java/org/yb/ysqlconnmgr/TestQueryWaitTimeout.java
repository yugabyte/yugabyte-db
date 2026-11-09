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

import static org.yb.AssertionWrappers.*;

import com.google.common.net.HostAndPort;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
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
public class TestQueryWaitTimeout extends BaseYsqlConnMgr {

  private static final int TSERVER_INDEX = 0;
  private static final int NUM_CONNECTIONS = 10;
  private static final int DEPRECATED_TIMEOUT_MS = 1000;
  private static final int RUNTIME_TIMEOUT_MS = 6000;
  private static final int IGNORED_TIMEOUT_MS = 60000;
  // A connect that does not time out would hang while the pool stays saturated.
  private static final long CONNECT_GUARD_SECS = 90;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_use_auth_backend", "true");
    builder.addCommonTServerFlag("ysql_max_connections", Integer.toString(NUM_CONNECTIONS));
  }

  private ConnectionBuilder connBuilder() {
    return getConnectionBuilder()
        .withTServer(TSERVER_INDEX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser("yugabyte")
        .withPassword("yugabyte");
  }

  private Connection connect() throws Exception {
    return connBuilder().connect();
  }

  // Holds a transactional backend in every slot (see TestAuthPoolTimeout).
  private Connection[] saturatePool() throws Exception {
    Connection[] connections = new Connection[NUM_CONNECTIONS];
    for (int i = 0; i < NUM_CONNECTIONS; i++) {
      connections[i] = connect();
    }
    for (Connection conn : connections) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("BEGIN");
      }
    }
    return connections;
  }

  private void releasePool(Connection[] connections) throws SQLException {
    for (Connection conn : connections) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("COMMIT");
      }
      conn.close();
    }
  }

  // Returns how long a connect took to fail; fails the test if it succeeds or never returns.
  private long timeFailedConnectMs() throws Exception {
    ExecutorService executor = Executors.newSingleThreadExecutor();
    try {
      long start = System.nanoTime();
      Future<?> attempt = executor.submit(() -> {
        // ConnectionBuilder retries a failed connect, which would multiply the wait.
        ConnectionBuilder builder = connBuilder();
        builder.setMaxConnectionAttempts(1);
        try (Connection conn = builder.connect()) {
          fail("Expected the connection to time out waiting for a server connection");
        } catch (SQLException e) {
          LOG.info("Connection failed as expected: " + e.getMessage());
        }
        return null;
      });
      try {
        attempt.get(CONNECT_GUARD_SECS, TimeUnit.SECONDS);
      } catch (TimeoutException e) {
        attempt.cancel(true);
        fail("Connection waited over " + CONNECT_GUARD_SECS + "s for a server connection");
      }
      return TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
    } finally {
      executor.shutdownNow();
    }
  }

  @Test
  public void testRuntimeChange() throws Exception {
    markClusterNeedsRecreation();
    HostAndPort tserver = miniCluster.getTabletServers().keySet().stream()
        .filter(hp -> hp.getHost().equals(getPgHost(TSERVER_INDEX))).findFirst()
        .orElseThrow(() -> new IllegalStateException("No tserver at index " + TSERVER_INDEX));
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_INDEX);
    setServerFlag(tserver, "ysql_conn_mgr_query_wait_timeout_ms",
        Integer.toString(RUNTIME_TIMEOUT_MS));
    // setServerFlag returns once the tserver has rewritten the config and signalled odyssey;
    // the new value applies once odyssey has reloaded it.
    assertNotNull("Expected odyssey to reload its config",
        tailer.waitForLogRegex("reconfigure rules", 10, TimeUnit.SECONDS));

    Connection[] connections = saturatePool();
    try {
      long elapsedMs = timeFailedConnectMs();
      LOG.info("Connect after the runtime change failed after " + elapsedMs + " ms");
      assertGreaterThanOrEqualTo(
          "Connect should wait the runtime-set ysql_conn_mgr_query_wait_timeout_ms",
          elapsedMs, (long) RUNTIME_TIMEOUT_MS);
    } finally {
      releasePool(connections);
    }
  }

  @Test
  public void testDeprecatedFlagTakesPrecedence() throws Exception {
    markClusterNeedsRecreation();
    restartClusterWithAdditionalFlags(Collections.emptyMap(), new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_pool_timeout", Integer.toString(DEPRECATED_TIMEOUT_MS));
        put("ysql_conn_mgr_query_wait_timeout_ms", Integer.toString(IGNORED_TIMEOUT_MS));
      }
    });

    Connection[] connections = saturatePool();
    try {
      long elapsedMs = timeFailedConnectMs();
      LOG.info("Connect with both flags set failed after " + elapsedMs + " ms");
      assertLessThan(
          "ysql_conn_mgr_pool_timeout should take precedence over "
              + "ysql_conn_mgr_query_wait_timeout_ms",
          elapsedMs, (long) IGNORED_TIMEOUT_MS / 2);
    } finally {
      releasePool(connections);
    }
  }
}
