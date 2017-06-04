/**
 * Copyright (c) YugaByte, Inc.
 */
package org.yb.minicluster;

import com.google.common.net.HostAndPort;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.apache.commons.io.IOUtils;
import org.junit.AfterClass;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.BaseYBTest;
import org.yb.client.TestUtils;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.URL;
import java.net.URLConnection;
import java.util.List;

import static org.junit.Assert.fail;

/**
 * A base class for tests using a MiniCluster.
 */
public class BaseMiniClusterTest extends BaseYBTest {

  private static final Logger LOG = LoggerFactory.getLogger(BaseMiniClusterTest.class);

  protected static final String NUM_MASTERS_PROP = "NUM_MASTERS";
  protected static final int NUM_TABLET_SERVERS = 3;
  protected static final int DEFAULT_NUM_MASTERS = 3;

  // Number of masters that will be started for this test if we're starting
  // a cluster.
  protected static final int NUM_MASTERS =
      Integer.getInteger(NUM_MASTERS_PROP, DEFAULT_NUM_MASTERS);

  /** This is used as the default timeout when calling YB Java client's async API. */
  protected static final int DEFAULT_SLEEP = 50000;

  /** A mini-cluster shared between invocations of multiple test methods. */
  protected static MiniYBCluster miniCluster;

  protected static List<String> masterArgs = null;
  protected static List<String> tserverArgs = null;

  // Comma separate describing the master addresses and ports.
  protected static String masterAddresses;
  protected static List<HostAndPort> masterHostPorts;

  /**
   * This makes sure that the mini cluster is up and running before each test. A test might opt to
   * leave the mini cluster running, and it will be reused by next tests, or it might shut down the
   * mini cluster by calling {@link BaseMiniClusterTest#destroyMiniCluster()}, and a new cluster
   * will be created for the next test.
   *
   * Even though {@link BaseMiniClusterTest#miniCluster} is a static variable, this logic is
   * implemented using {@link Before} and not {@link org.junit.BeforeClass}, because we need to know
   * the test class name so we can pass it as a command line parameter to master / tserver daemons
   * so we can better identify stuck processes.
   */
  @Before
  public void setUpBefore() throws Exception {
    TestUtils.clearReservedPorts();
    if (miniCluster != null) {
      return;
    }
    createMiniCluster(NUM_MASTERS, NUM_TABLET_SERVERS);
  }

  // Helper function to wait for existing tservers to heartbeat to master leader.
  public boolean waitForTServersAtMasterLeader() throws Exception {
    return miniCluster.waitForTabletServers(miniCluster.getTabletServers().size());
  }

  /**
   * Creates a new cluster with the requested number of masters and tservers.
   */
  public void createMiniCluster(int numMasters, int numTservers) throws Exception {
    miniCluster = new MiniYBClusterBuilder()
                      .numMasters(numMasters)
                      .numTservers(numTservers)
                      .defaultTimeoutMs(DEFAULT_SLEEP)
                      .testClassName(getClass().getName())
                      .masterArgs(masterArgs)
                      .tserverArgs(tserverArgs)
                      .build();
    masterAddresses = miniCluster.getMasterAddresses();
    masterHostPorts = miniCluster.getMasterHostPorts();

    LOG.info("Started cluster with {} masters and {} tservers. " +
             "Waiting for all tablet servers to hearbeat to masters...",
             numMasters, numTservers);
    if (!miniCluster.waitForTabletServers(numTservers)) {
      fail("Couldn't get " + numTservers + " tablet servers running, aborting.");
    }

    afterStartingMiniCluster();
  }

  /**
   * This is called every time right after starting a mini cluster.
   */
  protected void afterStartingMiniCluster() throws Exception {
  }

  protected static void destroyMiniCluster() throws Exception {
    if (miniCluster != null) {
      LOG.info("Destroying mini cluster");
      miniCluster.shutdown();
      miniCluster = null;
    }
  }

  @AfterClass
  public static void tearDownAfterClass() throws Exception {
    LOG.info("BaseMiniClusterTest.tearDownAfterClass is running");
    destroyMiniCluster();
  }

  public long getTServerMetric(String host, int port, String metricName) throws Exception {
    // This is what a sample json looks likes:
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
    JsonElement jsonTree = getMetricsJson(host, port);
    for (JsonElement metricsArray : jsonTree.getAsJsonArray()) {
      for (JsonElement jsonElement : metricsArray.getAsJsonObject().getAsJsonArray("metrics")) {
        JsonObject jsonMetric = jsonElement.getAsJsonObject();
        String jsonMetricName = jsonMetric.get("name").getAsString();
        if (jsonMetricName.equals(metricName)) {
          return jsonMetric.get("total_count").getAsLong();
        }
      }
    }
    throw new Exception("Couldn't find metric: " + metricName + ", json: " + jsonTree.toString());
  }

  public JsonElement getMetricsJson(String host, int port) throws Exception {
    // Retrieve metrics json.
    URL metrics = new URL(String.format("http://%s:%d/metrics", host, port));
    URLConnection yc = metrics.openConnection();

    BufferedReader in = new BufferedReader(new InputStreamReader(yc.getInputStream()));
    String json = null;
    try {
      json = IOUtils.toString(in);
    } finally {
      in.close();
    }
    JsonParser parser = new JsonParser();
    return parser.parse(json);
  }
}
