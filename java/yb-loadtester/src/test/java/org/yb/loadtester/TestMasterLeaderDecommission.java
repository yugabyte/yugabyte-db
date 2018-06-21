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

import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBCluster;

import java.util.*;

/**
 * This is an integration test that ensures we can do a master decommission.
 */
public class TestMasterLeaderDecommission extends TestClusterBase {
  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 20 minutes.
  public void testMasterLeaderDecommission() throws Exception {
    // Wait for load tester to generate traffic.
    loadTesterRunnable.waitNumOpsIncrement(NUM_OPS_INCREMENT);
    // Disable heartbeats for all tservers.
    for (HostAndPort hp : miniCluster.getTabletServers().keySet()) {
      boolean status = client.setFlag(hp, "tserver_disable_heartbeat_test_only", "true");
      assert(status);
    }

    // Disable becoming leader in 2 master followers.
    HostAndPort leaderMasterHp = client.getLeaderMasterHostAndPort();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      if (!hp.equals(leaderMasterHp)) {
        boolean status = client.setFlag(hp, "do_not_start_election_test_only", "true");
        assert(status);
      }
    }

    // Add a new master to the config.
    createAndAddNewMasters(1);

    // Decomission master leader, after this, the added master should become leader.
    removeMaster(leaderMasterHp);

    // Enable heartbeats for all tservers.
    for (HostAndPort hp : miniCluster.getTabletServers().keySet()) {
      boolean status = client.setFlag(hp, "tserver_disable_heartbeat_test_only", "false");
      assert(status);
    }

    // Wait for tservers to find and heartbeat to new master.
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_TIMEOUT_MS * 2);

    for (HostAndPort hp : miniCluster.getTabletServers().keySet()) {
      String masters = client.getMasterAddresses(hp);
      // Assert each tserver knows about the full list of all 4 masters.
      assert(masters.split(",").length == 4);
    }

    // Wait for some ops and verify no failures in load tester.
    loadTesterRunnable.waitNumOpsIncrement(NUM_OPS_INCREMENT);
    loadTesterRunnable.verifyNumExceptions();

    verifyClusterHealth();
  }
}
