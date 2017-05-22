package org.yb.cql;

import com.datastax.driver.core.Host;
import com.google.common.net.HostAndPort;
import org.junit.Test;
import org.yb.minicluster.MiniYBCluster;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestEditCQLNodes extends BaseCQLTest {

  @Test
  public void testEditCQLNodes() throws Exception {
    assertEquals(miniCluster.getCQLContactPoints().size(),
      cluster.getMetadata().getAllHosts().size());
    assertEquals(NUM_TABLET_SERVERS, cluster.getMetadata().getAllHosts().size());

    // Add a new node.
    miniCluster.startTServer(null);

    // Wait for node list refresh.
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH * 2 * 1000);

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
    Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH * 2 * 1000);

    // Verify we have one less node.
    assertEquals(miniCluster.getCQLContactPoints().size(),
      cluster.getMetadata().getAllHosts().size());
  }

}
