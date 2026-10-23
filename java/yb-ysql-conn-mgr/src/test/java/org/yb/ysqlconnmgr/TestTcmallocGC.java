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

import static org.junit.Assume.assumeFalse;
import static org.junit.Assume.assumeTrue;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import com.google.gson.JsonObject;

import java.sql.*;
import java.util.ArrayList;
import java.util.List;
import java.util.Scanner;
import java.util.concurrent.TimeUnit;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.RequiresLinux;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.BuildTypeUtil;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestTcmallocGC extends BaseYsqlConnMgr {

  private static final int TEST_GC_INTERVAL_SECS = 2;
  private static final int NUM_CONNECTIONS = 500;
  private static final int TSERVER_INDEX = 1;
  private static final int DRAIN_TIMEOUT_MS = 30000;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_tcmalloc_gc_interval",
      String.valueOf(TEST_GC_INTERVAL_SECS));
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings", "log_debug, log_query");
  }

  // Logical connections (active + queued + waiting) on the pool the burst uses, as reported by the
  // connection manager serving TSERVER_INDEX. Zero if that pool does not exist yet.
  private int getBurstPoolLogicalConns() throws Exception {
    JsonObject pool = getPool("yugabyte", "yugabyte", TSERVER_INDEX);
    if (pool == null) {
      return 0;
    }
    return pool.get("active_logical_connections").getAsInt()
        + pool.get("queued_logical_connections").getAsInt()
        + pool.get("waiting_logical_connections").getAsInt();
  }

  // Waits for the connection manager to finish tearing down the burst's clients, i.e. for the pool
  // to be back to the logical connection count it reported before the burst. The stats behind
  // /connections are refreshed on the cron tick, so this lags the client-side close by up to
  // ysql_conn_mgr_stats_interval.
  private void waitForBurstToDrain(int logicalConnsBefore) throws Exception {
    final long deadline = System.currentTimeMillis() + DRAIN_TIMEOUT_MS;
    int logicalConns = getBurstPoolLogicalConns();
    while (logicalConns > logicalConnsBefore && System.currentTimeMillis() < deadline) {
      Thread.sleep(200);
      logicalConns = getBurstPoolLogicalConns();
    }
    assertTrue(String.format(
        "Pool still reports %d logical connections after %d ms, was %d before the burst",
        logicalConns, DRAIN_TIMEOUT_MS, logicalConnsBefore),
        logicalConns <= logicalConnsBefore);
  }

  private static long getSystemPageSize() throws Exception {
    Process process = Runtime.getRuntime().exec("getconf PAGESIZE");
    try (Scanner scanner = new Scanner(process.getInputStream())) {
      return scanner.nextLong();
    }
  }

  private long createLoadToRecordPeakRss(int odysseyPid) throws Exception {
    try (Connection conn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withTServer(TSERVER_INDEX)
        .withUser("yugabyte")
        .withPassword("yugabyte")
        .connect()) {
      conn.createStatement().execute("CREATE TABLE IF NOT EXISTS gc_test_base (id int, data text)");
      conn.close();
    }
    List<Connection> connections = new ArrayList<>();
    for (int i = 0; i < NUM_CONNECTIONS; i++) {
      try {
        connections.add(getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withTServer(TSERVER_INDEX)
            .withUser("yugabyte")
            .withPassword("yugabyte")
            .connect());
        PreparedStatement pstmt = connections.get(i).prepareStatement(
          "SELECT * FROM gc_test_base");
        pstmt.execute();
        pstmt.close();
      } catch (Exception e) {
        LOG.error("Test failed with exception: ", e);
        fail("TestTcmallocGC failed: " + e.getMessage());
      }
    }

    long odysseyRSSPeak = getRssForPid(odysseyPid);

    for (Connection conn : connections) {
      try {
        conn.close();
      } catch (Exception e) {
        LOG.error("Test failed with exception: ", e);
        fail("TestTcmallocGC failed: " + e.getMessage());
      }
    }
    return odysseyRSSPeak;
  }

  // Verifies that the YSQL Connection Manager process releases memory
  // back to the OS after a burst of client connections is closed after
  // ysql_conn_mgr_tcmalloc_gc_interval seconds.
  @Test
  public void TestTcmallocGC() throws Exception {
    assumeFalse("tcmalloc is not used for sanitizer (tsan/asan) builds and conn mgr "
        + "does gc for google tcmalloc only.",
        BuildTypeUtil.isSanitizerBuild());

    // Must stay the node the burst connects to, i.e. the one getBurstPoolLogicalConns reads.
    final int tserverIndex = TSERVER_INDEX;
    final String tserverHost = getPgHost(tserverIndex);
    final HostAndPort tserver = miniCluster.getTabletServers().keySet().stream()
        .filter(hp -> hp.getHost().equals(tserverHost)).findFirst()
        .orElseThrow(() -> new IllegalStateException(
            "No tserver found for host " + tserverHost));
    final int odysseyPid = getOdysseyPidForHost(tserverHost);

    // Keep the GC off for the whole burst so nothing is released while the
    // connections drain.
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, tserverIndex);
    setServerFlag(tserver, "ysql_conn_mgr_tcmalloc_gc_interval", "0");

    // setServerFlag returns once the tserver has rewritten odyssey's config file and signalled it,
    // but the new interval only takes effect when odyssey finishes reloading. Sync on the reload
    // and clear the buffer behind it: every line the assertNull below scans is then from a cron
    // that already has the interval at 0, and it covers the burst and the drain, not just the
    // window that assertion waits on.
    assertNotNull("Expected odyssey to log a config reload after disabling the GC",
        tailer.waitForLogRegex("reconfigure rules", 10, TimeUnit.SECONDS));
    tailer.skipToEnd();

    waitForConnectionStats(tserverIndex);
    int logicalConnsBefore = getBurstPoolLogicalConns();

    long odysseyRSSStart = getRssForPid(odysseyPid);
    long odysseyRSSPeak = createLoadToRecordPeakRss(odysseyPid);
    assertTrue(String.format("Odyssey RSS should have increased from %d" +
        "KB to %d KB", odysseyRSSStart, odysseyRSSPeak),
        odysseyRSSStart < odysseyRSSPeak);

    waitForBurstToDrain(logicalConnsBefore);
    // RSS once the burst has drained, with the freed pages still held by
    // tcmalloc because the GC is disabled.
    long odysseyRSSBeforeGc = getRssForPid(odysseyPid);

    assertNull("Expected cron to not log 'released pageheap free memory to OS' while "
        + "ysql_conn_mgr_tcmalloc_gc_interval is 0",
        tailer.waitForLogRegex("released pageheap free memory to OS",
            2 * TEST_GC_INTERVAL_SECS, TimeUnit.SECONDS));

    // Re-enabling releases everything accumulated above in one shot.
    setServerFlag(tserver, "ysql_conn_mgr_tcmalloc_gc_interval",
        String.valueOf(TEST_GC_INTERVAL_SECS));
    String released = tailer.waitForLogRegex(
        "released pageheap free memory to OS", (10), TimeUnit.SECONDS);
    assertNotNull(
        "Expected cron to log 'released pageheap free memory to OS' ",
        released);

    // tcmalloc can unmap only whole system pages. With 64 KiB pages (aarch64 AlmaLinux 8) the
    // burst's freed 8 KiB tcmalloc pages rarely cover one, so the release lowers RSS by too little
    // to tell apart from RSS growing in 64 KiB steps over the same window.
    final long pageSize = getSystemPageSize();
    assumeTrue("Skipping the RSS check with " + pageSize + " byte pages", pageSize <= 4096);

    // Sampled after the release, so it reflects what the GC gave back.
    long odysseyRSSEnd = getRssForPid(odysseyPid);
    for (int i = 0; i < 20 && odysseyRSSEnd >= odysseyRSSBeforeGc; ++i) {
      Thread.sleep(100);
      odysseyRSSEnd = getRssForPid(odysseyPid);
    }
    assertTrue(String.format("Odyssey RSS should have released from %d KB to %d KB",
        odysseyRSSBeforeGc, odysseyRSSEnd), odysseyRSSBeforeGc > odysseyRSSEnd);
  }
}
