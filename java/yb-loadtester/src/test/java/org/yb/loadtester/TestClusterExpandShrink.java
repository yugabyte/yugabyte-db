// Copyright (c) YugaByte, Inc.
package org.yb.loadtester;

import com.google.common.net.HostAndPort;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.yugabyte.sample.Main;
import com.yugabyte.sample.common.CmdLineOpts;
import org.junit.After;
import org.junit.Ignore;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common;
import org.yb.client.MiniYBCluster;
import org.yb.client.ModifyMasterClusterConfigBlacklist;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.cql.BaseCQLTest;
import org.yb.minicluster.MiniYBDaemon;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.URL;
import java.net.URLConnection;
import java.util.*;

import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

/**
 * This is an integration test that ensures we can expand, shrink and fully move a YB cluster
 * without any significant impact to a running load test.
 */
public class TestClusterExpandShrink extends BaseCQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestClusterExpandShrink.class);

  private static LoadTester loadTesterRunnable = null;

  private static Thread loadTesterThread = null;

  private static String cqlContactPoints = null;

  private static final String WORKLOAD = "CassandraStockTicker";

  // Number of ops to wait for between significant events in the test.
  private static final int NUM_OPS_INCREMENT = 10000;

  // Timeout to wait for desired number of ops.
  private static final int WAIT_FOR_OPS_TIMEOUT_MS = 30000; // 30 seconds

  // Timeout to wait for load balancing to complete.
  private static final int LOADBALANCE_TIMEOUT_MS = 30000; // 30 seconds

  // Timeout to wait for cluster move to complete.
  private static final int CLUSTER_MOVE_TIMEOUT_MS = 300000; // 5 mins

  @Override
  protected void afterStartingMiniCluster() {
    cqlContactPoints = miniCluster.getCQLContactPointsAsString();
  }

  @After
  public void TearDownAfter() throws Exception {
    // Stop load tester and exit.
    if (loadTesterRunnable != null) {
      loadTesterRunnable.stopLoadTester();
    }

    if (loadTesterThread != null) {
      loadTesterThread.join();
    }
  }

  public class LoadTester implements Runnable {

    private final Main testRunner;

    private volatile boolean testRunnerFailed = false;

    public LoadTester(String workload, String cqlContactPoints) throws Exception {
      String args[] = {"--verbose", "--workload", workload, "--nodes", cqlContactPoints};
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

    public int getNumExceptions() {
      return testRunner.getNumExceptions();
    }

    public int waitNumOpsAtLeast(int expectedNumOps) throws Exception {
      Integer numOps = 0;
      TestUtils.waitFor(() -> testRunner.numOps() >= expectedNumOps, WAIT_FOR_OPS_TIMEOUT_MS);
      LOG.info("Num Ops: " + numOps + ", Expected: " + expectedNumOps);
      assertTrue(numOps >= expectedNumOps);
      return numOps;
    }
  }

  private JsonElement getMetricsJson(String host, int port) throws Exception {
    // Retrieve metrics json.
    URL metrics = new URL(String.format("http://%s:%d/metrics", host, port));
    URLConnection yc = metrics.openConnection();

    BufferedReader in = new BufferedReader(new InputStreamReader(yc.getInputStream()));
    String json = "";
    try {
      String inputLine;
      while ((inputLine = in.readLine()) != null) {
        json += inputLine;
      }
    } finally {
      in.close();
    }
    JsonParser parser = new JsonParser();
    return parser.parse(json);
  }

  private void verifyMetrics(int minOps) throws Exception {
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {

      // This is what the json looks likes:
      //[
      //  {
      //    "type": "server",
      //    "id": "yb.cqlserver",
      //    "attributes": {},
      //    "metrics": [
      //      {
      //        "name": "handler_latency_yb_cqlserver_SQLProcessor_SelectStmt",
      //        "total_count": 0,
      //        ...
      //      },
      //      {
      //        "name": "handler_latency_yb_cqlserver_SQLProcessor_InsertStmt",
      //        "total_count": 0,
      //        ...
      //      }
      //    ]
      //  }
      //]
      // Now parse the json.
      JsonElement jsonTree = getMetricsJson(ts.getLocalhostIP(), ts.getCqlWebPort());
      JsonObject jsonObject = jsonTree.getAsJsonArray().get(0).getAsJsonObject();
      int numOps = 0;
      for (JsonElement jsonElement : jsonObject.getAsJsonArray("metrics")) {
        JsonObject jsonMetric = jsonElement.getAsJsonObject();
        String metric_name = jsonMetric.get("name").getAsString();
        if (metric_name.equals("handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest")) {
          numOps += jsonMetric.get("total_count").getAsInt();
        }
      }
      LOG.info("TSERVER: " + ts.getLocalhostIP() + ", num_ops: " + numOps + ", min_ops: " + minOps);
      assertTrue(numOps >= minOps);
    }
  }

  private void verifyExpectedLiveTServers(int expected_live) throws Exception {
    YBClient client = miniCluster.getClient();
    HostAndPort masterHostAndPort = client.getLeaderMasterHostAndPort();
    int masterLeaderWebPort =
      miniCluster.getMasters().get(masterHostAndPort.getPort()).getWebPort();
    JsonElement jsonTree = getMetricsJson(masterHostAndPort.getHostText(), masterLeaderWebPort);
    for (JsonElement element : jsonTree.getAsJsonArray()) {
      JsonObject object = element.getAsJsonObject();
      if (object.get("type").getAsString().equals("cluster")) {
        for (JsonElement metric : object.getAsJsonArray("metrics")) {
          JsonObject metric_object = metric.getAsJsonObject();
          if (metric_object.get("name").getAsString().equals("num_tablet_servers_live")) {
            assertEquals(expected_live, metric_object.get("value").getAsInt());
            LOG.info("Found live tservers: " + expected_live);
            return;
          }
        }
      }
    }
    fail("Didn't find live tserver metric");
  }

  // Enable test once we fix ENG-1472.
  @Ignore
  @Test(timeout = 600000) // 10 minutes.
  public void testClusterExpandShrink() throws Exception {
    // Start the load tester.
    loadTesterRunnable = new LoadTester(WORKLOAD, cqlContactPoints);
    loadTesterThread = new Thread(loadTesterRunnable);
    loadTesterThread.start();

    // Wait for load tester to generate traffic.
    int totalOps = loadTesterRunnable.waitNumOpsAtLeast(NUM_OPS_INCREMENT);

    // Create a copy to store original list.
    Map<HostAndPort, MiniYBDaemon> originalTServers = new HashMap<>(miniCluster.getTabletServers());
    assertEquals(NUM_TABLET_SERVERS, originalTServers.size());

    // Now double the number of tservers to expand the cluster and verify load spreads.
    for (int i = 0; i < NUM_TABLET_SERVERS; i++) {
      miniCluster.startTServer(null);
    }

    YBClient client = miniCluster.getClient();

    // Wait for the load to be balanced across the cluster.
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, NUM_TABLET_SERVERS * 2));

    // Wait for some ops across the entire cluster.
    totalOps = loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);

    // Verify no failures in load tester.
    assertFalse(loadTesterRunnable.hasFailures());

    // Verify metrics for all tservers.
    verifyMetrics(0);

    // Verify live tservers.
    verifyExpectedLiveTServers(2 * NUM_TABLET_SERVERS);

    LOG.info("Cluster Expand Done!");

    // Retrieve existing config, set blacklist and reconfigure cluster.
    List<Common.HostPortPB> blacklisted_hosts = new ArrayList<>();
    for (Map.Entry<HostAndPort, MiniYBDaemon> ts : originalTServers.entrySet()) {
      Common.HostPortPB hostPortPB = Common.HostPortPB.newBuilder()
        .setHost(ts.getKey().getHostText())
        .setPort(ts.getKey().getPort())
        .build();
      blacklisted_hosts.add(hostPortPB);
    }

    // TODO: We should ensure we can move all masters as well.
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

    // Kill the old tablet servers.
    // TODO: Check that the blacklisted tablet servers have no tablets assigned.
    for (HostAndPort rpcPort : originalTServers.keySet()) {
      miniCluster.killTabletServerOnHostPort(rpcPort);
    }

    // Wait for heartbeats to expire.
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_TIMEOUT_MS * 2);

    // Verify live tservers.
    verifyExpectedLiveTServers(NUM_TABLET_SERVERS);

    LOG.info("Cluster Shrink Done!");

    // Wait for some ops.
    totalOps = loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);

    // Wait for some more ops and verify no exceptions.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);
    assertEquals(0, loadTesterRunnable.getNumExceptions());

    // Verify metrics for all tservers.
    verifyMetrics(NUM_OPS_INCREMENT / NUM_TABLET_SERVERS);

    // Wait for heartbeats to expire.
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_TIMEOUT_MS * 2);

    // Verify live tservers.
    verifyExpectedLiveTServers(NUM_TABLET_SERVERS);

    // Verify no failures in the load tester.
    assertFalse(loadTesterRunnable.hasFailures());
  }
}
