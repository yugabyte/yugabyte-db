// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.loadtester;

import com.google.common.net.HostAndPort;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.yugabyte.sample.Main;
import com.yugabyte.sample.common.CmdLineOpts;
import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common;
import org.yb.client.ChangeConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.ModifyClusterConfigReplicationFactor;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.cql.BaseCQLTest;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.Socket;
import java.net.URL;
import java.net.URLConnection;
import java.util.*;

import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertNotNull;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

/**
 * This is the base class for integration test for cluster expand/shrink with workload testing.
 * NOTE: Please add the actual @Test to the derived classes.
 */
public class TestClusterBase extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestClusterBase.class);

  protected LoadTester loadTesterRunnable = null;

  protected Thread loadTesterThread = null;

  protected String cqlContactPoints = null;

  protected YBClient client = null;

  private static final String WORKLOAD = "CassandraStockTicker";

  // Number of ops to wait for between significant events in the test.
  protected static final int NUM_OPS_INCREMENT = 1000;

  // Timeout to wait for desired number of ops.
  protected static final int WAIT_FOR_OPS_TIMEOUT_MS = 60000; // 60 seconds

  // Timeout to wait for load balancing to complete.
  protected static final int LOADBALANCE_TIMEOUT_MS = 240000; // 4 mins

  // Timeout to wait for cluster move to complete.
  protected static final int CLUSTER_MOVE_TIMEOUT_MS = 300000; // 5 mins

  // Timeout for test completion.
  protected static final int TEST_TIMEOUT_SEC = 600; // 10 mins

  // Timeout to wait for a new master to come up.
  protected static final int NEW_MASTER_TIMEOUT_MS = 10000; // 10 seconds.

  // Timeout to wait for webservers to be up.
  protected static final int WEBSERVER_TIMEOUT_MS = 10000; // 10 seconds.

  // Timeout to wait for a master leader.
  protected static final int MASTER_LEADER_TIMEOUT_MS = 10000; // 10 seconds.

  // Timeout to wait expected number of tservers to be alive.
  protected static final int EXPECTED_TSERVERS_TIMEOUT_MS = 30000; // 30 seconds.

  // The total number of ops seen so far.
  protected static long totalOps = 0;

  // Maximum number of exceptions to tolerate. We might see exceptions during a full master move,
  // since the newly elected leader might not have received heartbeats from any tservers. As a
  // result, the system.peers table would be empty and any client reading the table in that state
  // would end up removing all the hosts from its metadata causing exceptions.
  protected static final int MAX_NUM_EXCEPTIONS = 20;

  @Override
  public int getTestMethodTimeoutSec() {
    return TEST_TIMEOUT_SEC;
  }

  @Override
  protected void afterStartingMiniCluster() throws Exception {
    cqlContactPoints = miniCluster.getCQLContactPointsAsString();
    client = miniCluster.getClient();
    LOG.info("Retrieved contact points from cluster: " + cqlContactPoints);
    updateConfigReplicationFactor(NUM_MASTERS);
  }

  @Before
  public void startLoadTester() throws Exception {
    // Start the load tester.
    LOG.info("Using contact points for load tester: " + cqlContactPoints);
    loadTesterRunnable = new LoadTester(WORKLOAD, cqlContactPoints);
    loadTesterThread = new Thread(loadTesterRunnable);
    loadTesterThread.start();
    LOG.info("Loadtester start.");
  }

  @After
  public void stopLoadTester() throws Exception {
    // Stop load tester and exit.
    if (loadTesterRunnable != null) {
      loadTesterRunnable.stopLoadTester();
      loadTesterRunnable = null;
      LOG.info("Loadtester stopped.");
    }

    if (loadTesterThread != null) {
      loadTesterThread.join();
      loadTesterThread = null;
    }
  }

  @Override
  protected void afterBaseCQLTestTearDown() throws Exception {
    // We need to destroy the mini cluster after BaseCQLTest cleans up all the tables and keyspaces.
    destroyMiniCluster();
    LOG.info("Stopped minicluster.");
  }

  protected class LoadTester implements Runnable {
    private final Main testRunner;

    private volatile boolean testRunnerFailed = false;

    public LoadTester(String workload, String cqlContactPoints) throws Exception {
      String args[] = {"--workload", workload, "--nodes", cqlContactPoints,
        "--print_all_exceptions", "--num_threads_read", "1", "--num_threads_write", "1",
        "--refresh_partition_metadata_seconds",
        Integer.toString(PARTITION_POLICY_REFRESH_FREQUENCY_SECONDS)};
      CmdLineOpts configuration = CmdLineOpts.createFromArgs(args);
      testRunner = new Main(configuration);
    }

    public void stopLoadTester() {
      testRunner.stopAllThreads();
    }

    @Override
    public void run() {
      try {
        testRunner.run();
      } catch (Exception e) {
        LOG.error("Load tester exited!", e);
        testRunnerFailed = true;
      }
    }

    public boolean hasFailures() {
      return testRunner.hasFailures() || testRunnerFailed;
    }

    public boolean hasThreadFailed() {
      return testRunner.hasThreadFailed();
    }

    private int getNumExceptions() {
      return testRunner.getNumExceptions();
    }

    public void waitNumOpsAtLeast(long expectedNumOps) throws Exception {
      TestUtils.waitFor(() -> testRunner.numOps() >= expectedNumOps, WAIT_FOR_OPS_TIMEOUT_MS);
      totalOps = testRunner.numOps();
      LOG.info("Num Ops: " + totalOps + ", Expected: " + expectedNumOps);
      assertTrue(totalOps >= expectedNumOps);
    }

    public void verifyNumExceptions() {
      assertTrue(String.format("Number of exceptions: %d", getNumExceptions()),
        getNumExceptions() < MAX_NUM_EXCEPTIONS && !hasThreadFailed());
    }
  }

  private void verifyMetrics(int minOps) throws Exception {
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      // Wait for the webserver to be up.
      TestUtils.waitForServer(ts.getLocalhostIP(), ts.getCqlWebPort(), WEBSERVER_TIMEOUT_MS);
      Metrics metrics = new Metrics(ts.getLocalhostIP(), ts.getCqlWebPort(), "server");
      long numOps = metrics.getHistogram(
        "handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest").totalCount;

      LOG.info("TSERVER: " + ts.getLocalhostIP() + ", num_ops: " + numOps + ", min_ops: " + minOps);
      assertTrue(numOps >= minOps);
    }
  }

  private void verifyExpectedLiveTServers(int expected_live) throws Exception {
    // Wait for metrics to be submitted.
    Thread.sleep(2 * MiniYBCluster.CATALOG_MANAGER_BG_TASK_WAIT_MS);

    // Wait for expected number of live tservers to be present since leader elections could
    // result in the metric not being correct momentarily.
    TestUtils.waitFor(() -> {
      client.waitForMasterLeader(MASTER_LEADER_TIMEOUT_MS);

      // Retrieve web address for leader master.
      HostAndPort masterHostAndPort = client.getLeaderMasterHostAndPort();
      assertNotNull(masterHostAndPort);
      Map<HostAndPort, MiniYBDaemon> masters = miniCluster.getMasters();
      assertTrue(String.format("Couldn't find master %s in list %s", masterHostAndPort,
        masters.keySet().toString()), masters.containsKey(masterHostAndPort));
      int masterLeaderWebPort = masters.get(masterHostAndPort).getWebPort();
      Metrics metrics = new Metrics(masterHostAndPort.getHostText(), masterLeaderWebPort,
        "cluster");
      int live_tservers = metrics.getCounter("num_tablet_servers_live").value;
      return live_tservers == expected_live;
    }, EXPECTED_TSERVERS_TIMEOUT_MS);
  }

  private void createAndAddNewMasters(int numMasters) throws Exception {
    for (int i = 0; i < numMasters; i++) {
      // Add new master.
      HostAndPort masterRpcHostPort = miniCluster.startShellMaster();

      // Wait for new master to be online.
      assertTrue(client.waitForMaster(masterRpcHostPort, NEW_MASTER_TIMEOUT_MS));

      LOG.info("New master online: " + masterRpcHostPort.toString());

      // Add new master to the config.
      ChangeConfigResponse response = client.changeMasterConfig(masterRpcHostPort.getHostText(),
        masterRpcHostPort.getPort(), true);
      assertFalse("ChangeConfig has error: " + response.errorMessage(), response.hasError());

      LOG.info("Added new master to config: " + masterRpcHostPort.toString());

      // Wait for hearbeat interval to ensure tservers pick up the new masters.
      Thread.sleep(2 * MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS);

      LOG.info("Done waiting for new leader");

      // Verify no load tester errors.
      loadTesterRunnable.verifyNumExceptions();
    }
  }

  public void performFullMasterMove() throws Exception {
    // Create a copy to store original list.
    Map<HostAndPort, MiniYBDaemon> originalMasters = new HashMap<>(miniCluster.getMasters());
    for (HostAndPort originalMaster : originalMasters.keySet()) {
      // Add new master.
      HostAndPort masterRpcHostPort = miniCluster.startShellMaster();

      // Wait for new master to be online.
      assertTrue(client.waitForMaster(masterRpcHostPort, NEW_MASTER_TIMEOUT_MS));

      LOG.info("New master online: " + masterRpcHostPort.toString());

      // Add new master to the config.
      ChangeConfigResponse response = client.changeMasterConfig(masterRpcHostPort.getHostText(),
        masterRpcHostPort.getPort(), true);
      assertFalse("ChangeConfig has error: " + response.errorMessage(), response.hasError());

      LOG.info("Added new master to config: " + masterRpcHostPort.toString());

      // Wait for hearbeat interval to ensure tservers pick up the new masters.
      Thread.sleep(2 * MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS);

      // Remove old master.
      response = client.changeMasterConfig(originalMaster.getHostText(), originalMaster.getPort(),
                                           false);
      assertFalse("ChangeConfig has error: " + response.errorMessage(), response.hasError());

      LOG.info("Removed old master from config: " + originalMaster.toString());

      // Wait for the new leader to be online.
      client.waitForMasterLeader(NEW_MASTER_TIMEOUT_MS);
      LOG.info("Done waiting for new leader");

      // Kill the old master.
      miniCluster.killMasterOnHostPort(originalMaster);

      LOG.info("Killed old master: " + originalMaster.toString());

      // Verify no load tester errors.
      loadTesterRunnable.verifyNumExceptions();
    }
  }

  protected void addNewTServers(int numTservers) throws Exception {
    // Now double the number of tservers to expand the cluster and verify load spreads.
    for (int i = 0; i < numTservers; i++) {
      miniCluster.startTServer(null);
    }

    // Wait for the CQL client to discover the new nodes.
    Thread.sleep(2 * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);
  }

  protected void verifyStateAfterTServerAddition() throws Exception {
    // Wait for some ops across the entire cluster.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);

    // Verify no failures in load tester.
    loadTesterRunnable.verifyNumExceptions();

    // Verify metrics for all tservers.
    verifyMetrics(0);

    // Verify live tservers.
    verifyExpectedLiveTServers(2 * NUM_TABLET_SERVERS);
  }

  private void removeTServers(Map<HostAndPort, MiniYBDaemon> originalTServers) throws Exception {
    // Retrieve existing config, set blacklist and reconfigure cluster.
    List<Common.HostPortPB> blacklisted_hosts = new ArrayList<>();
    for (Map.Entry<HostAndPort, MiniYBDaemon> ts : originalTServers.entrySet()) {
      Common.HostPortPB hostPortPB = Common.HostPortPB.newBuilder()
        .setHost(ts.getKey().getHostText())
        .setPort(ts.getKey().getPort())
        .build();
      blacklisted_hosts.add(hostPortPB);
    }

    ModifyMasterClusterConfigBlacklist operation =
      new ModifyMasterClusterConfigBlacklist(client, blacklisted_hosts, true);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      fail(e.getMessage());
    }

    // Wait for the move to complete.
    TestUtils.waitFor(() -> {
      verifyExpectedLiveTServers(2 * NUM_TABLET_SERVERS);
      final double move_completion = client.getLoadMoveCompletion().getPercentCompleted();
      LOG.info("Move completion percent: " + move_completion);
      return move_completion >= 100;
    }, CLUSTER_MOVE_TIMEOUT_MS);

    assertEquals(100, (int) client.getLoadMoveCompletion().getPercentCompleted());

    // Wait for the partition aware policy to refresh, to remove old tservers before we kill them.
    Thread.sleep(2 * PARTITION_POLICY_REFRESH_FREQUENCY_SECONDS * 1000);

    // Kill the old tablet servers.
    // TODO: Check that the blacklisted tablet servers have no tablets assigned.
    for (HostAndPort rpcPort : originalTServers.keySet()) {
      miniCluster.killTabletServerOnHostPort(rpcPort);
    }

    // Wait for heartbeats to expire.
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_TIMEOUT_MS * 2);

    // Verify live tservers.
    verifyExpectedLiveTServers(NUM_TABLET_SERVERS);
  }

  protected void verifyClusterHealth() throws Exception {
    verifyClusterHealth(NUM_TABLET_SERVERS);
  }

  private void verifyClusterHealth(int numTabletServers) throws Exception {
    // Wait for some ops.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);

    // Wait for some more ops and verify no exceptions.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);
    loadTesterRunnable.verifyNumExceptions();

    // Verify metrics for all tservers.
    verifyMetrics(NUM_OPS_INCREMENT / numTabletServers);

    // Wait for heartbeats to expire.
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_TIMEOUT_MS * 2);

    // Verify live tservers.
    verifyExpectedLiveTServers(numTabletServers);

    // Verify no failures in the load tester.
    loadTesterRunnable.verifyNumExceptions();
  }

  protected void performTServerExpandShrink(boolean fullMove) throws Exception {
    // Create a copy to store original tserver list.
    Map<HostAndPort, MiniYBDaemon> originalTServers = new HashMap<>(miniCluster.getTabletServers());
    assertEquals(NUM_TABLET_SERVERS, originalTServers.size());

    addNewTServers(originalTServers.size());

    // In the full move case, we don't wait for load balancing.
    if (!fullMove) {
      // Wait for the load to be balanced across the cluster.
      assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, NUM_TABLET_SERVERS * 2));

      // Wait for the partition aware policy to refresh.
      Thread.sleep(2 * PARTITION_POLICY_REFRESH_FREQUENCY_SECONDS * 1000);
    }

    verifyStateAfterTServerAddition();

    LOG.info("Cluster Expand Done!");

    removeTServers(originalTServers);

    LOG.info("Cluster Shrink Done!");
  }

  protected void updateConfigReplicationFactor(int replFactor) throws Exception {
    ModifyClusterConfigReplicationFactor modifyRF =
        new ModifyClusterConfigReplicationFactor(client, replFactor);
    modifyRF.doCall();
  }

  /**
   * Change the cluster from one RF to another.
   * We spawn delta number of masters and tservers and update the cluster config.
   */
  public void performRFChange(int fromRF, int toRF) throws Exception {
    if (toRF <= fromRF) {
      LOG.info("Skipping RF reduction for now.");
      return;
    }
    int numNewServers = toRF - fromRF;
    // Wait for load tester to generate traffic.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);
    LOG.info("WaitOps Done.");

    // Increase the number of masters
    createAndAddNewMasters(numNewServers);
    LOG.info("Add Masters Done.");

    // Now perform a tserver expand.
    addNewTServers(numNewServers);
    LOG.info("Add Tservers Done.");

    // Update the RF of the cluster so that load tester adds peers to existing tablets.
    updateConfigReplicationFactor(toRF);
    LOG.info("Replication Factor Change Done.");

    // Wait for load to balance across the target number of tservers.
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, toRF));

    // Wait for the partition aware policy to refresh.
    Thread.sleep(2 * PARTITION_POLICY_REFRESH_FREQUENCY_SECONDS * 1000);
    LOG.info("Load Balance Done.");

    verifyClusterHealth(toRF);
    LOG.info("Sanity Check Done.");

    GetMasterClusterConfigResponse resp = client.getMasterClusterConfig();
    assertEquals(toRF, resp.getConfig().getReplicationInfo().getLiveReplicas().getNumReplicas());

    // TODO: Add per tablet replica count check.
  }
}
