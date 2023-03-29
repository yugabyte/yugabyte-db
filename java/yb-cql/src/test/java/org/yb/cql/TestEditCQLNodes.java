//
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
package org.yb.cql;

import com.datastax.driver.core.Host;
import com.google.common.net.HostAndPort;

import java.util.Collections;
import java.util.HashMap;

import org.junit.Test;
import org.yb.minicluster.MiniYBCluster;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestEditCQLNodes extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestEditCQLNodes.class);

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.tserverHeartbeatTimeoutMs(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000 / 2);
  }

  private void internalTestEditCQLNodes(boolean limitToSubscribedConns) throws Exception {
    assertEquals(miniCluster.getCQLContactPoints().size(),
      cluster.getMetadata().getAllHosts().size());
    assertEquals(NUM_TABLET_SERVERS, cluster.getMetadata().getAllHosts().size());

    // Add a new node.
    miniCluster.startTServer(new HashMap<String, String>() {{
        put("cql_limit_nodelist_refresh_to_subscribed_conns",
            String.valueOf(limitToSubscribedConns));
      }});

    // Wait for node list refresh.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 2 * 1000);

    // Verify we have an extra node.
    assertEquals(miniCluster.getCQLContactPoints().size(),
      cluster.getMetadata().getAllHosts().size());
    assertEquals(NUM_TABLET_SERVERS + 1, cluster.getMetadata().getAllHosts().size());

    for (Host host : cluster.getMetadata().getAllHosts()) {
      assertTrue(host.isUp());
    }

    // Remove a node.
    HostAndPort tserver = miniCluster.getTabletServers().keySet().iterator().next();
    miniCluster.killTabletServerOnHostPort(tserver);

    // Wait for node list refresh.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 2 * 1000);

    // Verify we have one less node. Note that we don't assert here since it is not guaranteed here
    // that we will have one less node on the driver side. This can happen if the node that was
    // removed was the one with which the driver had the control connection with. In that case, the
    // driver waits a little longer (default 16 sec) before establishing a new control connection.
    if (miniCluster.getCQLContactPoints().size() == cluster.getMetadata().getAllHosts().size()) {
      assertEquals(NUM_TABLET_SERVERS, cluster.getMetadata().getAllHosts().size());
      return;
    }

    // Wait a little more in case the removed node was the one with which the driver had the
    // control connection.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 4 * 1000);

    // Verify we have one less node.
    assertEquals(miniCluster.getCQLContactPoints().size(),
      cluster.getMetadata().getAllHosts().size());
    assertEquals(NUM_TABLET_SERVERS, cluster.getMetadata().getAllHosts().size());
  }

  @Test
  public void testEditCQLNodes() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());
    internalTestEditCQLNodes(/*limitToSubscribedConns=*/ true);
  }

  @Test
  public void testEditCQLNodesSendToAllConnectionsGFlag() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());
    destroyMiniCluster();
    // Testing cql_limit_nodelist_refresh_to_subscribed_conns flag disabled. It is enabled by
    // default.
    createMiniCluster(
        Collections.emptyMap(),
        Collections.singletonMap("cql_limit_nodelist_refresh_to_subscribed_conns", "false"));
    setUpCqlClient();

    internalTestEditCQLNodes(/*limitToSubscribedConns=*/ false);
  }
}
