// Copyright (c) YugaByte, Inc.
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
 * This is an integration test that is specific to RF=1 case, so that we can default to
 * creating and deleting RF=1 cluster in the setup phase.
 */
public class TestRF1Cluster extends TestClusterBase {
  private static final Logger LOG = LoggerFactory.getLogger(TestRF1Cluster.class);

  @Override
  public void setUpBefore() throws Exception {
    // This should ideally be consistent with kDefaultNumShardsPerTServer in client.cc.
    MiniYBCluster.setNumShardsPerTserver(TestUtils.isTSAN() ? 2 : 8);
    createMiniCluster(1, 1);
    updateConfigReplicationFactor(1);
  }

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 10 minutes.
  public void testRF1toRF3() throws Exception {
    performRFChange(1, 3);
  }

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 10 minutes.
  public void testRF1LoadBalance() throws Exception {
    // Wait for load tester to generate traffic.
    loadTesterRunnable.waitNumOpsAtLeast(totalOps + NUM_OPS_INCREMENT);
    LOG.info("WaitOps Done.");

    // Now perform a tserver expand.
    addNewTServers(2);
    LOG.info("Add Tservers Done.");

    // Wait for load to balance across the three tservers.
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, 3));

    // Wait for the partition aware policy to refresh.
    Thread.sleep(2 * PARTITION_POLICY_REFRESH_FREQUENCY_SECONDS * 1000);
    LOG.info("Load Balance Done.");
  }
}
