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
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestQueryWaitTimeout extends BaseYsqlConnMgr {

  private static final int TSERVER_INDEX = 0;
  private static final int NUM_CONNECTIONS = 10;
  private static final int DEPRECATED_TIMEOUT_MS = 1000;
  private static final int RUNTIME_TIMEOUT_MS = 6000;
  // A connect that does not time out would hang forever while the pool stays saturated.
  private static final long CONNECT_GUARD_SECS = 60;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    // Only the deprecated name is set, so it must be the one in effect at startup.
    builder.addCommonTServerFlag("ysql_conn_mgr_pool_timeout",
        Integer.toString(DEPRECATED_TIMEOUT_MS));
    builder.addCommonTServerFlag("ysql_conn_mgr_use_auth_backend", "true");
    builder.addCommonTServerFlag("ysql_max_connections", Integer.toString(NUM_CONNECTIONS));
  }

  private Connection connect() throws SQLException {
    return getConnectionBuilder()
        .withTServer(TSERVER_INDEX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser("yugabyte")
        .withPassword("yugabyte")
        .connect();
  }

  // Returns how long a connect took to fail; fails the test if it succeeds or never returns.
  private long timeFailedConnectMs(ExecutorService executor) throws Exception {
    long start = System.nanoTime();
    Future<?> attempt = executor.submit(() -> {
      try (Connection conn = connect()) {
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
  }

  @Test
  public void testDeprecatedFlagAndRuntimeChange() throws Exception {
    // Create the logical connections first and then open a transaction on each, so that every
    // backend slot is held by a transactional backend (see TestAuthPoolTimeout).
    Connection[] connections = new Connection[NUM_CONNECTIONS];
    for (int i = 0; i < NUM_CONNECTIONS; i++) {
      connections[i] = connect();
    }
    for (Connection conn : connections) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("BEGIN");
      }
    }

    ExecutorService executor = Executors.newSingleThreadExecutor();
    try {
      long elapsedMs = timeFailedConnectMs(executor);
      LOG.info("Connect with the deprecated flag failed after " + elapsedMs + " ms");

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

      elapsedMs = timeFailedConnectMs(executor);
      LOG.info("Connect after the runtime change failed after " + elapsedMs + " ms");
      assertGreaterThanOrEqualTo(
          "Connect should wait the runtime-set ysql_conn_mgr_query_wait_timeout_ms",
          elapsedMs, (long) RUNTIME_TIMEOUT_MS);
    } finally {
      executor.shutdownNow();
      for (Connection conn : connections) {
        try (Statement stmt = conn.createStatement()) {
          stmt.execute("COMMIT");
        }
        conn.close();
      }
    }
  }
}
