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
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

// Regression test for the ysql_pg.conf / connection manager start race (D55903, #32777).
//
// On tserver start the PostgreSQL and connection manager supervisors come up concurrently. If the
// connection manager reads ysql_pg.conf before the new PostgreSQL process rewrites it, it picks up
// a stale file left behind by a previous run. When that stale file has no max_connections line the
// old code defaulted it to 10, and CHECK_LE(ysql_conn_mgr_reserve_internal_conns=15, 10) crashed
// the tserver. The fix makes the connection manager wait for the PostgreSQL process to start (and
// rewrite ysql_pg.conf) before reading it.
//
// This test seeds exactly that stale condition on disk and restarts the cluster in place (which,
// unlike restartClusterWithFlags(), preserves the data directories), then verifies the tserver
// comes back up and the connection manager config reflects the fresh max_connections.
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrMaxConnectionsOnRestart extends BaseYsqlConnMgr {
  // Reserve more internal connections than the old stale-file fallback (10). This is what turned a
  // stale read into a tserver crash, so it is required to exercise the regression.
  private static final int RESERVE_INTERNAL_CONNS = 15;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // BaseYsqlConnMgr sets this to 0; override it so a stale (max_connections-less) read would
    // trip the CHECK the fix removes.
    builder.addCommonTServerFlag(
        "ysql_conn_mgr_reserve_internal_conns", Integer.toString(RESERVE_INTERNAL_CONNS));
  }

  private Path connMgrConfPath(MiniYBDaemon ts) {
    return Paths.get(ts.getDataDirPath(), "yb-data", "tserver", "ysql_conn_mgr.conf");
  }

  private Path ysqlPgConfPath(MiniYBDaemon ts) {
    return Paths.get(ts.getDataDirPath(), "pg_data", "ysql_pg.conf");
  }

  private boolean isMaxConnectionsAssignment(String line) {
    int indexOfComment = line.indexOf('#');
    if (indexOfComment != -1) {
      line = line.substring(0, indexOfComment);
    }
    line = line.trim();
    int eq = line.indexOf('=');
    if (eq < 0) {
      return false;
    }
    return line.substring(0, eq).trim().equals("max_connections");
  }

  // Reads "max_connections" from ysql_pg.conf, or null if there is no active line. Values may carry
  // inline comments, e.g. "max_connections = 300  # (change requires restart)".
  private Integer readPgConfMaxConnections(Path path) throws IOException {
    Integer value = null;
    for (String line : Files.readAllLines(path)) {
      if (isMaxConnectionsAssignment(line)) {
        int indexOfComment = line.indexOf('#');
        if (indexOfComment != -1) {
          line = line.substring(0, indexOfComment);
        }
        value = Integer.parseInt(line.substring(line.indexOf('=') + 1).trim());
      }
    }
    return value;
  }

  // Reads "yb_ysql_max_connections" from ysql_conn_mgr.conf ("key value" format), or null if
  // absent.
  private Integer readConnMgrMaxConnections(Path path) throws IOException {
    Integer value = null;
    for (String line : Files.readAllLines(path)) {
      int indexOfComment = line.indexOf('#');
      if (indexOfComment != -1) {
        line = line.substring(0, indexOfComment);
      }
      line = line.trim();
      if (!line.startsWith("yb_ysql_max_connections")) {
        continue;
      }
      String[] parts = line.split("\\s+");
      if (parts.length >= 2 && parts[0].equals("yb_ysql_max_connections")) {
        value = Integer.parseInt(parts[1].trim());
      }
    }
    return value;
  }

  // Rewrites ysql_pg.conf without its max_connections line, simulating a stale file from a previous
  // run that predates a max_connections being written.
  private void dropMaxConnectionsLine(Path path) throws IOException {
    List<String> kept = new ArrayList<>();
    for (String line : Files.readAllLines(path)) {
      if (!isMaxConnectionsAssignment(line)) {
        kept.add(line);
      }
    }
    Files.write(path, kept);
  }

  // Waits until every tserver accepts a connection manager connection. If a tserver process died
  // (the pre-fix behavior: conn mgr read a stale ysql_pg.conf and CHECK_LE crashed the tserver),
  // this fails fast with a clear message instead of waiting out the timeout.
  private void waitForConnMgrOnAllTservers() throws Exception {
    TestUtils.waitFor(() -> {
      for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
        assertTrue("Tserver " + ts + " process is not alive; it likely crashed reading a stale "
                + "ysql_pg.conf before PostgreSQL rewrote it (CHECK_LE on "
                + "ysql_conn_mgr_reserve_internal_conns)",
            ts.getProcess().isAlive());
      }
      ConnectionBuilder builder =
          getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR);
      builder.setMaxConnectionAttempts(1);

      try {
        for (int i = 0; i < NUM_TSERVER; i++) {
          builder.withTServer(i).connect().close();
        }
        return true;
      } catch (Exception e) {
        return false;
      }
    }, 15000 /* timeoutMs */);
  }

  @Test
  public void testConnMgrPicksUpFreshMaxConnectionsOnRestart() throws Exception {
    waitForConnMgrOnAllTservers();

    // Record the max_connections PostgreSQL wrote, and confirm the connection manager already
    // agrees with it.
    int freshMaxConnections = -1;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      Integer pgConf = readPgConfMaxConnections(ysqlPgConfPath(ts));
      Integer connMgrConf = readConnMgrMaxConnections(connMgrConfPath(ts));
      assertNotNull("max_connections should be present in ysql_pg.conf at startup", pgConf);
      assertNotNull("yb_ysql_max_connections should be present in ysql_conn_mgr.conf", connMgrConf);
      assertEquals("Connection manager should match ysql_pg.conf at startup",
          pgConf - RESERVE_INTERNAL_CONNS, connMgrConf.intValue());
      if (freshMaxConnections < 0) {
        freshMaxConnections = pgConf;
      } else {
        assertEquals(
            "All tservers should agree on max_connections", freshMaxConnections, pgConf.intValue());
      }
    }
    assertTrue(freshMaxConnections > RESERVE_INTERNAL_CONNS);

    // Seed the stale condition on every tserver: a ysql_pg.conf with no max_connections line.
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      Path pgConfPath = ysqlPgConfPath(ts);
      dropMaxConnectionsLine(pgConfPath);
      assertNull("Seeded ysql_pg.conf should have no max_connections line",
          readPgConfMaxConnections(pgConfPath));
    }

    // Restart in place. The data directories (and the stale ysql_pg.conf) are preserved, so the
    // connection manager and the new PostgreSQL process race exactly as they do after a real
    // tserver restart.
    miniCluster.restart();

    // With the fix the connection manager waits for PostgreSQL to rewrite ysql_pg.conf, so every
    // tserver comes back up (no CHECK crash) and serves connections.
    waitForConnMgrOnAllTservers();

    // A single restart is enough: PostgreSQL restored max_connections and the connection manager
    // config reflects that fresh value, not the stale (missing) one.
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      Integer pgConf = readPgConfMaxConnections(ysqlPgConfPath(ts));
      Integer connMgrConf = readConnMgrMaxConnections(connMgrConfPath(ts));
      assertNotNull("PostgreSQL should have rewritten max_connections into ysql_pg.conf", pgConf);
      assertNotNull("yb_ysql_max_connections should be present in ysql_conn_mgr.conf", connMgrConf);
      assertEquals("ysql_pg.conf max_connections should be restored to the fresh value",
          freshMaxConnections, pgConf.intValue());
      assertEquals("Connection manager should match the fresh ysql_pg.conf after restart",
          pgConf - RESERVE_INTERNAL_CONNS, connMgrConf.intValue());
    }
  }
}
