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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBCluster;

import static junit.framework.TestCase.assertTrue;

/**
 * This is an integration test that is specific to RF=1 case, so that we can default to
 * creating and deleting RF=1 cluster in the setup phase.
 */

@RunWith(value=YBTestRunner.class)
public class TestRF1Cluster extends TestClusterBase {
  private static final Logger LOG = LoggerFactory.getLogger(TestRF1Cluster.class);

  @Override
  public void setUpBefore() throws Exception {
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
    loadTesterRunnable.waitNumOpsIncrement(NUM_OPS_INCREMENT);
    LOG.info("WaitOps Done.");

    // Now perform a tserver expand.
    addNewTServers(2);
    LOG.info("Add Tservers Done.");

    // Wait for load to balance across the three tservers.
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, 3));

    // Wait for the partition metadata to refresh.
    Thread.sleep(2 * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);
    LOG.info("Load Balance Done.");
  }
}
