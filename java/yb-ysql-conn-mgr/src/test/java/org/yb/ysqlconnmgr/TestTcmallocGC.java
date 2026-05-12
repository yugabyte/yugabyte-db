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
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;

import java.sql.*;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.BuildTypeUtil;
import java.util.Collections;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestTcmallocGC extends BaseYsqlConnMgr {

  private static final int TEST_GC_INTERVAL_SECS = 2;
  private static final int NUM_CONNECTIONS = 500;
  private static final int TSERVER_INDEX = 1;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_tcmalloc_gc_interval",
      String.valueOf(TEST_GC_INTERVAL_SECS));
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings", "log_debug, log_query");
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
    assumeFalse("RSS-based memory assertions are unreliable under ASAN builds",
        BuildTypeUtil.isASAN());

    final int tserverIndex = 1;
    final String tserverHost = getPgHost(tserverIndex);
    final int odysseyPid = getOdysseyPidForHost(tserverHost);
    long odysseyRSSStart = getRssForPid(odysseyPid);
    long odysseyRSSPeak = createLoadToRecordPeakRss(odysseyPid);
    assertTrue(String.format("Odyssey RSS should have increased from %d" +
        "KB to %d KB", odysseyRSSStart, odysseyRSSPeak),
        odysseyRSSStart < odysseyRSSPeak);

    Thread.sleep(500);
    long odysseyRSSEnd = getRssForPid(odysseyPid);

    // Sanitizer builds (ASAN/TSAN) do not use tcmalloc. So therefore skip
    // the log-based assertion as conn mgr does gc for YB_GOOGLE_TCMALLOC
    // based allocations only.
    if (!BuildTypeUtil.isSanitizerBuild()) {
      ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, tserverIndex);
      tailer.skipToEnd();

      String released = tailer.waitForLogRegex(
          "released pageheap free memory to OS", (2), TimeUnit.SECONDS);
      assertNotNull(
          "Expected cron to log 'released pageheap free memory to OS' ",
          released);
    }

    assertTrue(String.format("Odyssey RSS should have released from %d KB to %d KB",
        odysseyRSSPeak, odysseyRSSEnd), odysseyRSSPeak > odysseyRSSEnd);
  }

  // Validates ysql_conn_mgr_tcmalloc_gc_interval can be set correctly at runtime
  // and is effective in releasing memory back to the OS.
  @Test
  public void testRuntimeFlagChange () throws Exception {
    assumeFalse("tcmalloc is not used for sanitizer (tsan/asan) builds and" +
        " conn mgr does gc for google tcmalloc only.",
        BuildTypeUtil.isSanitizerBuild());

    restartClusterWithAdditionalFlags(Collections.emptyMap(),
      Collections.singletonMap("ysql_conn_mgr_tcmalloc_gc_interval", "0"));

    final int tserverIndex = 1;
    final String tserverHost = getPgHost(tserverIndex);
    final HostAndPort tserver = miniCluster.getTabletServers().keySet().stream()
        .filter(hp -> hp.getHost().equals(tserverHost)).findFirst()
        .orElseThrow(() -> new IllegalStateException(
            "No tserver found for host " + tserverHost));
    final int odysseyPid = getOdysseyPidForHost(tserverHost);
    long odysseyRSSStart = getRssForPid(odysseyPid);

    long odysseyRSSPeak = createLoadToRecordPeakRss(odysseyPid);

    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, tserverIndex);
    tailer.skipToEnd();

    // Small sleep to ensure RSS gets reduced.
    Thread.sleep(500);
    long odysseyRSSEnd1 = getRssForPid(odysseyPid);
    assertTrue(String.format("Odyssey RSS should have decreased from %d KB to %d KB",
        odysseyRSSPeak, odysseyRSSEnd1), odysseyRSSPeak > odysseyRSSEnd1);

    String released = tailer.waitForLogRegex(
        "released pageheap free memory to OS", (2), TimeUnit.SECONDS);
    assertNull("Expected cron to not log 'released pageheap free memory to OS' "
          + "within after draining connections", released);

    setServerFlag(tserver, "ysql_conn_mgr_tcmalloc_gc_interval",
        String.valueOf(TEST_GC_INTERVAL_SECS));

    released = tailer.waitForLogRegex(
        "released pageheap free memory to OS", (2), TimeUnit.SECONDS);
    assertNotNull(
        "Expected cron to log 'released pageheap free memory to OS' ",
        released);

    long odysseyRSSEnd2 = getRssForPid(odysseyPid);

    assertTrue(String.format("Odyssey RSS should have released from %d KB to %d KB",
        odysseyRSSEnd1, odysseyRSSEnd2), odysseyRSSEnd1 > odysseyRSSEnd2);
  }
}
