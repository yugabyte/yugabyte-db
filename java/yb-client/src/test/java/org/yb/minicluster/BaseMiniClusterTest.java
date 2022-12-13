/**
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 */
package org.yb.minicluster;

import static org.yb.AssertionWrappers.fail;

import java.net.MalformedURLException;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Scanner;
import java.util.Set;
import java.util.TreeMap;
import java.util.function.Consumer;

import org.junit.AfterClass;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.BaseYBTest;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.MasterDdlOuterClass;
import org.yb.util.BuildTypeUtil;
import org.yb.util.Timeouts;

import com.google.common.net.HostAndPort;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

/**
 * A base class for tests using a MiniCluster.
 */
public class BaseMiniClusterTest extends BaseYBTest {

  private static final Logger LOG = LoggerFactory.getLogger(BaseMiniClusterTest.class);

  // TODO: review the usage of the constants below.
  protected static final int NUM_MASTERS = 3;
  protected static final int NUM_TABLET_SERVERS = 3;

  protected static final int STANDARD_DEVIATION_FACTOR = 2;
  protected static final int DEFAULT_TIMEOUT_MS =
          (int) Timeouts.adjustTimeoutSecForBuildType(50000);

  /** Default timeout used when calling YB Java client's async API. */
  protected static final int DEFAULT_SLEEP = (int) Timeouts.adjustTimeoutSecForBuildType(50000);

  /**
   * A mini-cluster shared between invocations of multiple test methods.
   */
  protected static MiniYBCluster miniCluster;

  protected boolean useIpWithCertificate = MiniYBClusterParameters.DEFAULT_USE_IP_WITH_CERTIFICATE;

  protected String certFile = null;

  // The client cert files for mTLS.
  protected String clientCertFile = null;
  protected String clientKeyFile = null;

  /** Default bind address (Used only for mTLS verification). */
  protected String clientHost = null;
  protected int clientPort = 0;

  /** Comma separate describing the master addresses and ports. */
  protected static String masterAddresses;
  protected static List<HostAndPort> masterHostPorts;

  /**
   * Whether the fresh cluster should be created for the next test, so that custom
   * {@code createMiniCluster} won't affect other tests.
   */
  private static boolean clusterNeedsRecreation = false;

  protected int getReplicationFactor() {
    return -1;
  }

  protected int getInitialNumMasters() {
    return -1;
  }

  protected int getInitialNumTServers() {
    return -1;
  }

  /** Subclasses can override this to set the number of shards per tablet server. */
  protected int getNumShardsPerTServer() {
    return MiniYBClusterParameters.DEFAULT_NUM_SHARDS_PER_TSERVER;
  }

  /** This allows subclasses to optionally skip the usage of a mini-cluster in a test. */
  protected boolean isMiniClusterEnabled() {
    return true;
  }

  /** Mark cluster as custom, that needs to be recreated fresh for the next test. */
  protected static void markClusterNeedsRecreation() {
    clusterNeedsRecreation = true;
  }

  protected static boolean isClusterNeedsRecreation() {
    return clusterNeedsRecreation;
  }

  /** To customize, override and use {@code super} call. */
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = new TreeMap<>();

    // Default master flag to make sure we don't wait to trigger new LB tasks upon master leader
    // failover.
    flagMap.put("load_balancer_initial_delay_secs", "0");

    flagMap.put("durable_wal_write", "false");

    // Limit number of transaction table tablets to help avoid timeouts.
    flagMap.put("transaction_table_num_tablets", Integer.toString(NUM_TABLET_SERVERS));

    // For sanitizer builds, it is easy to overload the master, leading to quorum changes.
    // This could end up breaking ever trivial DDLs like creating an initial table in the cluster.
    if (BuildTypeUtil.isSanitizerBuild()) {
      flagMap.put("leader_failure_max_missed_heartbeat_periods", "10");
    }

    return flagMap;
  }

  /** To customize, override and use {@code super} call. */
  protected Map<String, String> getTServerFlags() {
    return new TreeMap<>();
  }

  /** Reset per-test settings to their default values, can be overridden to customize values. */
  protected void resetSettings() {}

  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {}

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
    resetSettings();
    if (!isMiniClusterEnabled()) {
      return;
    }
    TestUtils.clearReservedPorts();
    if (clusterNeedsRecreation) {
      destroyMiniCluster();
      clusterNeedsRecreation = false;
    }
    if (miniCluster == null) {
      createMiniCluster();
    } else if (shouldRestartMiniClusterBetweenTests()) {
      LOG.info("Restarting the MiniCluster");
      miniCluster.restart();
    }
  }

  protected boolean shouldRestartMiniClusterBetweenTests() {
    return false;
  }

  /**
   * A helper function to wait for existing tservers to heartbeat to master leader.
   * @return true if the number of tablet servers found is as expected
   */
  public boolean waitForTServersAtMasterLeader() throws Exception {
    if (!isMiniClusterEnabled()) {
      return true;
    }
    return miniCluster.waitForTabletServers(miniCluster.getTabletServers().size());
  }

  protected final void createMiniCluster() throws Exception {
    createMiniCluster(Collections.emptyMap(), Collections.emptyMap());
  }

  /**
   * Creates a new cluster with additional flags.
   * <p>
   * Flags will override initial ones on name clash.
   */
  protected final void createMiniCluster(
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags) throws Exception {
    createMiniCluster(-1, -1, additionalMasterFlags, additionalTserverFlags);
  }

  /**
   * Creates a new cluster with additional flags and environment vars.
   * <p>
   * Flags will override initial ones on name clash.
   */
  protected final void createMiniCluster(
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags,
      Map<String, String> additionalEnvironmentVars) throws Exception {
    createMiniCluster(-1, -1, additionalMasterFlags, additionalTserverFlags, null,
        additionalEnvironmentVars);
  }

  protected final void createMiniCluster(
      Consumer<MiniYBClusterBuilder> customize) throws Exception {
    createMiniCluster(-1, -1, customize);
  }

  /** Creates a new cluster with the requested number of masters and tservers. */
  protected final void createMiniCluster(int numMasters, int numTservers) throws Exception {
    createMiniCluster(numMasters, numTservers, Collections.emptyMap(), Collections.emptyMap());
  }

  /**
   * Creates a new cluster with the requested number of masters and tservers (or -1 for default)
   * with additional flags.
   * <p>
   * Flags will override initial ones on name clash.
   */
  protected final void createMiniCluster(
      int numMasters,
      int numTservers,
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags) throws Exception {
    createMiniCluster(numMasters, numTservers, additionalMasterFlags, additionalTserverFlags, null,
        Collections.emptyMap());
  }

  protected final void createMiniCluster(
      int numMasters,
      int numTservers,
      Consumer<MiniYBClusterBuilder> customize) throws Exception {
    createMiniCluster(numMasters, numTservers, Collections.emptyMap(), Collections.emptyMap(),
        customize, Collections.emptyMap());
  }

  protected void createMiniCluster(
      int numMasters,
      int numTservers,
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags,
      Consumer<MiniYBClusterBuilder> customize,
      Map<String, String> additionalEnvironmentVars
      ) throws Exception {
    if (!isMiniClusterEnabled()) {
      return;
    }

    final int replicationFactor = getReplicationFactor();

    numMasters = TestUtils.getFirstPositiveNumber(numMasters,
        getInitialNumMasters(), replicationFactor,
        MiniYBClusterParameters.DEFAULT_NUM_MASTERS);

    numTservers = TestUtils.getFirstPositiveNumber(numTservers,
        getInitialNumTServers(), replicationFactor,
        MiniYBClusterParameters.DEFAULT_NUM_TSERVERS);

    LOG.info("BaseMiniClusterTest.createMiniCluster is running");
    MiniYBClusterBuilder clusterBuilder = new MiniYBClusterBuilder()
                      .numMasters(numMasters)
                      .numTservers(numTservers)
                      .defaultTimeoutMs(DEFAULT_SLEEP)
                      .testClassName(getClass().getName())
                      .masterFlags(getMasterFlags())
                      .addMasterFlags(additionalMasterFlags)
                      .commonTServerFlags(getTServerFlags())
                      .addCommonTServerFlags(additionalTserverFlags)
                      .numShardsPerTServer(getNumShardsPerTServer())
                      .useIpWithCertificate(useIpWithCertificate)
                      .replicationFactor(replicationFactor)
                      .sslCertFile(certFile)
                      .sslClientCertFiles(clientCertFile, clientKeyFile)
                      .bindHostAddress(clientHost, clientPort);

    customizeMiniClusterBuilder(clusterBuilder);
    if (customize != null) {
      customize.accept(clusterBuilder);
    }

    clusterBuilder.addEnvironmentVariables(additionalEnvironmentVars);

    miniCluster = clusterBuilder.build();
    masterAddresses = miniCluster.getMasterAddresses();
    masterHostPorts = miniCluster.getMasterHostPorts();

    LOG.info("Started cluster with {} masters and {} tservers. " +
             "Waiting for all tablet servers to heartbeat to masters...",
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

  // Get metrics of all tservers.
  protected Map<MiniYBDaemon, Metrics> getAllMetrics() throws Exception {
    Map<MiniYBDaemon, Metrics> initialMetrics = new HashMap<>();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      Metrics metrics = new Metrics(ts.getLocalhostIP(),
          ts.getCqlWebPort(),
          "server");
      initialMetrics.put(ts, metrics);
    }
    return initialMetrics;
  }

  protected IOMetrics createIOMetrics(MiniYBDaemon ts) throws Exception {
    return new IOMetrics(new Metrics(ts.getLocalhostIP(), ts.getWebPort(), "server"));
  }

  // Get IO metrics of all tservers.
  protected Map<MiniYBDaemon, IOMetrics> getTSMetrics() throws Exception {
    Map<MiniYBDaemon, IOMetrics> initialMetrics = new HashMap<>();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      initialMetrics.put(ts, createIOMetrics(ts));
    }
    return initialMetrics;
  }

  // Get combined IO metrics of all tservers since a certain point.
  protected IOMetrics getCombinedMetrics(Map<MiniYBDaemon, IOMetrics> initialMetrics)
      throws Exception {
    IOMetrics totalMetrics = new IOMetrics();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = createIOMetrics(ts).subtract(initialMetrics.get(ts));
      LOG.info("Metrics of " + ts.toString() + ": " + metrics.toString());
      totalMetrics.add(metrics);
    }
    LOG.info("Total metrics: " + totalMetrics.toString());
    return totalMetrics;
  }

  private Set<String> getTabletIds(String tableUUID)  throws Exception {
    return miniCluster.getClient().getTabletUUIDs(
        miniCluster.getClient().openTableByUUID(tableUUID));
  }

  /**
   * Get the list of all YBTable satisfying the input table name filter as a substring.
   * @param tableNameFilter table name filter as a name substring
   * @return a list of YBTable satisfying the name filter
   */
  protected List<YBTable> getTablesFromName(final String tableNameFilter) throws Exception {
    final YBClient client = miniCluster.getClient();
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tables =
      client.getTablesList(tableNameFilter).getTableInfoList();
    List<YBTable> tablesList = new ArrayList<>();
    for (MasterDdlOuterClass.ListTablesResponsePB.TableInfo table : tables) {
      tablesList.add(client.openTableByUUID(table.getId().toStringUtf8()));
    }
    return tablesList;
  }

  protected int getTableCounterMetricByTableUUID(String tableUUID,
                                                 String metricName) throws Exception {
    int value = 0;
    Set<String> tabletIds = getTabletIds(tableUUID);
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      try {
        URL url = new URL(String.format("http://%s:%d/metrics",
            ts.getLocalhostIP(),
            ts.getWebPort()));
        Scanner scanner = new Scanner(url.openConnection().getInputStream());
        JsonParser parser = new JsonParser();
        JsonElement tree = parser.parse(scanner.useDelimiter("\\A").next());
        for (JsonElement elem : tree.getAsJsonArray()) {
          JsonObject obj = elem.getAsJsonObject();
          if (obj.get("type").getAsString().equals("table") &&
              tableUUID.equals(obj.get("id").getAsString())) {
            Metrics.Counter cntr = new Metrics(obj).getCounter(metricName);
            // If the counter isn't exported by the table's metric entity, we may still find it as
            // the summation of the corresponding tablet's metric values.
            if (cntr != null) {
              return cntr.value;
            }
          } else if (obj.get("type").getAsString().equals("tablet") &&
              tabletIds.contains(obj.get("id").getAsString())) {
            value += new Metrics(obj).getCounter(metricName).value;
          }
        }
      } catch (MalformedURLException e) {
        throw new InternalError(e.getMessage());
      }
    }
    return value;
  }

  protected RocksDBMetrics getRocksDBMetricByTableUUID(String tableUUID) throws Exception {
    Set<String> tabletIds = getTabletIds(tableUUID);
    RocksDBMetrics metrics = new RocksDBMetrics();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      try {
        URL url = new URL(String.format("http://%s:%d/metrics",
            ts.getLocalhostIP(),
            ts.getWebPort()));
        Scanner scanner = new Scanner(url.openConnection().getInputStream());
        JsonParser parser = new JsonParser();
        JsonElement tree = parser.parse(scanner.useDelimiter("\\A").next());
        for (JsonElement elem : tree.getAsJsonArray()) {
          JsonObject obj = elem.getAsJsonObject();
          if (obj.get("type").getAsString().equals("tablet") &&
              tabletIds.contains(obj.get("id").getAsString())) {
            metrics.add(new RocksDBMetrics(new Metrics(obj)));
          }
        }
      } catch (MalformedURLException e) {
        throw new InternalError(e.getMessage());
      }
    }
    return metrics;
  }


  @AfterClass
  public static void tearDownAfterClass() throws Exception {
    LOG.info("BaseMiniClusterTest.tearDownAfterClass is running");
    destroyMiniCluster();
    LOG.info("BaseMiniClusterTest.tearDownAfterClass completed");
  }

}
