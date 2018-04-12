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
 *
 */
package org.yb.minicluster;

import com.google.common.net.HostAndPort;
import org.junit.AfterClass;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.BaseYBTest;
import org.yb.client.TestUtils;

import java.util.Collections;
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
  protected static final int STANDARD_DEVIATION_FACTOR = 2;
  protected static final int DEFAULT_TIMEOUT_MS = 50000;

  // Number of masters that will be started for this test if we're starting
  // a cluster.
  protected static final int NUM_MASTERS =
      Integer.getInteger(NUM_MASTERS_PROP, DEFAULT_NUM_MASTERS);

  /**
   * This is used as the default timeout when calling YB Java client's async API.
   */
  protected static final int DEFAULT_SLEEP = 50000;

  /**
   * A mini-cluster shared between invocations of multiple test methods.
   */
  protected static MiniYBCluster miniCluster;

  protected static List<String> masterArgs = null;
  protected static List<String> tserverArgs = null;

  // Comma separate describing the master addresses and ports.
  protected static String masterAddresses;
  protected static List<HostAndPort> masterHostPorts;

  // Subclasses can override this to set the number of shards per tablet server.
  protected int overridableNumShardsPerTServer() {
    return MiniYBCluster.DEFAULT_NUM_SHARDS_PER_TSERVER;
  }

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
    if (miniCluster == null) {
      createMiniCluster();
    }
  }

  // Helper function to wait for existing tservers to heartbeat to master leader.
  public boolean waitForTServersAtMasterLeader() throws Exception {
    return miniCluster.waitForTabletServers(miniCluster.getTabletServers().size());
  }

  /**
   * Override this method to create a custom minicluster for your test.
   */
  protected void createMiniCluster() throws Exception {
    createMiniCluster(NUM_MASTERS, NUM_TABLET_SERVERS);
  }

  /**
   * Creates a new cluster with the requested number of masters and tservers.
   */
  public void createMiniCluster(int numMasters, int numTservers) throws Exception {
    createMiniCluster(numMasters, Collections.nCopies(numTservers, tserverArgs));
  }

  public void createMiniCluster(int numMasters, List<List<String>> tserverArgs)
      throws Exception {
    LOG.info("BaseMiniClusterTest.createMiniCluster is running");
    int numTservers = tserverArgs.size();
    miniCluster = new MiniYBClusterBuilder()
                      .numMasters(numMasters)
                      .numTservers(numTservers)
                      .defaultTimeoutMs(DEFAULT_SLEEP)
                      .testClassName(getClass().getName())
                      .masterArgs(masterArgs)
                      .tserverArgs(tserverArgs)
                      .numShardsPerTServer(overridableNumShardsPerTServer())
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

  public void createMiniCluster(int numMasters, List<String> masterArgs,
                                List<List<String>> tserverArgs)
      throws Exception {
    LOG.info("BaseMiniClusterTest.createMiniCluster is running");
    int numTservers = tserverArgs.size();
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
}
