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
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBCluster;

import static junit.framework.TestCase.assertTrue;
import static junit.framework.TestCase.assertFalse;

/**
 * This is an integration test that ensures we can fully move a YB cluster
 * without any significant impact to a running load test.
 */

@RunWith(value=YBTestRunner.class)
public class TestClusterIsLoadBalancerIdle extends TestClusterBase {
  @Override
  protected int getReplicationFactor() {
    return 3;
  }

  @Override
  protected int getInitialNumMasters() {
    return 3;
  }

  @Override
  protected int getInitialNumTServers() {
    return 3;
  }

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 20 minutes.
  public void testClusterIsLoadBalancerIdle() throws Exception {
    // Setup test tables.
    int num_tables = 7;
    for (int i = 0; i < num_tables; i++) {
      setupTable("test_table_" + i, 2);
    }

    // Wait for the partition metadata to refresh.
    Thread.sleep(num_tables * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    // Wait for load to balance across the three tservers.
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, NUM_TABLET_SERVERS));

    // Load should be balanced and load balancer should be idle.
    verifyClusterHealth(NUM_TABLET_SERVERS);
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, NUM_TABLET_SERVERS));
    assertTrue(client.waitForLoadBalancerIdle(LOADBALANCE_TIMEOUT_MS));

    // Add a new tserver.
    addNewTServers(1);

    // Wait for the partition metadata to refresh.
    Thread.sleep(num_tables * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    verifyClusterHealth(NUM_TABLET_SERVERS + 1);

    // Load should be balanced and load balancer should be idle.
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, NUM_TABLET_SERVERS + 1));
    assertTrue(client.waitForLoadBalancerIdle(LOADBALANCE_TIMEOUT_MS));
  }
}
