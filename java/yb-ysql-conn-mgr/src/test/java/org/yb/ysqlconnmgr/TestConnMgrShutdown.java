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

import java.nio.file.Files;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.BuildTypeUtil;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrShutdown extends BaseYsqlConnMgr {
  private static final long SHUTDOWN_DELAY_MS = BuildTypeUtil.adjustTimeout(10000);
  private static final int WORKER_EXIT_WAIT_MS = 3000;
  private static final long EXIT_WAIT_MS = SHUTDOWN_DELAY_MS + 30000;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("TEST_ysql_conn_mgr_shutdown_delay_ms",
        String.valueOf(SHUTDOWN_DELAY_MS));
  }

  private Connection connectToConnMgr() throws Exception {
    return getConnectionBuilder()
        .withTServer(TSERVER_IDX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
  }

  private void runSelectOne() throws Exception {
    try (Connection conn = connectToConnMgr();
        Statement stmt = conn.createStatement();
        ResultSet rs = stmt.executeQuery("SELECT 1")) {
      assertTrue(rs.next());
      assertEquals(1, rs.getInt(1));
    }
  }

  private static boolean isProcessRunning(int pid) throws Exception {
    String stat;
    try {
      stat = new String(Files.readAllBytes(Paths.get("/proc/" + pid + "/stat")));
    } catch (java.nio.file.NoSuchFileException e) {
      return false;
    }
    String state = stat.substring(stat.lastIndexOf(')') + 2).split(" ")[0];
    return !state.equals("Z") && !state.equals("X");
  }

  private static class ConnMgrExitListener implements LogErrorListener {
    private final CompletableFuture<String> exitLine = new CompletableFuture<>();

    @Override
    public void handleLine(String line) {
      if (line.contains("Ysql Connection Manager exited normally")
          || line.contains("Ysql Connection Manager server returned with code")) {
        exitLine.complete(line);
      }
    }

    @Override
    public void reportErrorsAtEnd() {
    }

    String waitForExit(long timeoutMs) throws Exception {
      return exitLine.get(timeoutMs, TimeUnit.MILLISECONDS);
    }
  }

  private MiniYBDaemon getTServerDaemon(String host) {
    return miniCluster.getTabletServers().values().stream()
        .filter(daemon -> host.equals(daemon.getLocalhostIP())).findFirst()
        .orElseThrow(() -> new IllegalStateException("No tserver found for host " + host));
  }

  @Test
  public void testConnectAfterShutdownSignal() throws Exception {
    final String host = getPgHost(TSERVER_IDX);
    final int odysseyPid = getOdysseyPidForHost(host);

    runSelectOne();

    ConnMgrExitListener exitListener = new ConnMgrExitListener();
    getTServerDaemon(host).getLogPrinter().addErrorListener(exitListener);

    Process kill = new ProcessBuilder("kill", "-INT", String.valueOf(odysseyPid)).start();
    assertEquals(0, kill.waitFor());

    // Due to #22345, ConnMgr is not able to restart on it's own so this cluster is
    // not usable after the shutdown signal. Mark it as needing recreation so that
    // other tests don't try to use it.
    markClusterNeedsRecreation();

    Thread.sleep(WORKER_EXIT_WAIT_MS);

    try {
      runSelectOne();
    } catch (Exception e) {
      LOG.error("Connection during conn mgr shutdown failed", e);
      fail("Connection during conn mgr shutdown failed: " + e
          + "; conn mgr running: " + isProcessRunning(odysseyPid));
    }
    assertTrue("Conn mgr exited before the shutdown delay elapsed",
        isProcessRunning(odysseyPid));

    String exitLine = exitListener.waitForExit(EXIT_WAIT_MS);
    assertNotNull(exitLine);
    assertTrue("Conn mgr did not exit cleanly: " + exitLine,
        exitLine.contains("exited normally"));
  }
}
