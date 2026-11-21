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
import java.util.ArrayList;
import java.util.List;
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
  private static final int MAX_CONNECTIONS = 10;
  private static final int DEPRECATED_TIMEOUT_MS = 1000;
  private static final int RUNTIME_TIMEOUT_MS = 6000;
  // A query that does not time out would hang forever while the pool stays saturated.
  private static final long QUERY_GUARD_SECS = 60;
  // What a client gets when its wait for a server connection times out under multi-route pooling.
  private static final String WAIT_TIMEOUT_ERROR = "too many active clients across all pools";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    // Only the deprecated name is set, so it must be the one in effect at startup.
    builder.addCommonTServerFlag("ysql_conn_mgr_pool_timeout",
        Integer.toString(DEPRECATED_TIMEOUT_MS));
    builder.addCommonTServerFlag("ysql_max_connections", Integer.toString(MAX_CONNECTIONS));
  }

  private Connection connect() throws Exception {
    return getConnectionBuilder()
        .withTServer(TSERVER_INDEX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser("yugabyte")
        .withPassword("yugabyte")
        .connect();
  }

  // Runs the statements on conn. Returns -1 if they succeed, or how long they took to fail if
  // the client timed out waiting for a server connection.
  private long runOrTimeWaitMs(ExecutorService executor, Connection conn, String... statements)
      throws Exception {
    Future<Long> attempt = executor.submit(() -> {
      long start = System.nanoTime();
      try (Statement stmt = conn.createStatement()) {
        for (String sql : statements) {
          stmt.execute(sql);
        }
        return -1L;
      } catch (SQLException e) {
        assertTrue("Unexpected error: " + e.getMessage(),
            e.getMessage().contains(WAIT_TIMEOUT_ERROR));
        return TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
      }
    });
    try {
      return attempt.get(QUERY_GUARD_SECS, TimeUnit.SECONDS);
    } catch (TimeoutException e) {
      attempt.cancel(true);
      fail("Query waited over " + QUERY_GUARD_SECS + "s for a server connection");
      return -1;
    }
  }

  // All logical connections are made before the pool is saturated, since a new one would wait
  // for an auth backend. A timeout disconnects the client, so each timed wait uses its own
  // connection.
  @Test
  public void testDeprecatedFlagAndRuntimeChange() throws Exception {
    List<Connection> conns = new ArrayList<>();
    for (int i = 0; i < MAX_CONNECTIONS + 2; i++) {
      conns.add(connect());
    }

    ExecutorService executor = Executors.newSingleThreadExecutor();
    List<Connection> holders = new ArrayList<>();
    try {
      // Hold a server connection in an open transaction per client until one times out. Fewer
      // than MAX_CONNECTIONS fit, since some slots are reserved for the control pool.
      long elapsedMs = -1;
      int next = 0;
      while (elapsedMs < 0) {
        assertLessThan("Expected the pool to saturate", next, MAX_CONNECTIONS + 1);
        Connection conn = conns.get(next++);
        elapsedMs = runOrTimeWaitMs(executor, conn, "BEGIN", "SELECT 1");
        if (elapsedMs < 0) {
          holders.add(conn);
        }
      }
      LOG.info("With the deprecated flag, " + holders.size() + " transactions held the pool and "
          + "the next one timed out after " + elapsedMs + " ms");

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

      elapsedMs = runOrTimeWaitMs(executor, conns.get(next), "SELECT 1");
      LOG.info("After the runtime change, the query timed out after " + elapsedMs + " ms");
      assertGreaterThanOrEqualTo(
          "Query should wait the runtime-set ysql_conn_mgr_query_wait_timeout_ms",
          elapsedMs, (long) RUNTIME_TIMEOUT_MS);
    } finally {
      executor.shutdownNow();
      for (Connection conn : holders) {
        try (Statement stmt = conn.createStatement()) {
          stmt.execute("COMMIT");
        }
      }
      for (Connection conn : conns) {
        conn.close();
      }
    }
  }
}
