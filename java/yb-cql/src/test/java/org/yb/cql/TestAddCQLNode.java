package org.yb.cql;

import com.datastax.driver.core.Host;
import org.junit.Test;
import org.yb.client.MiniYBCluster;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestAddCQLNode extends BaseCQLTest {

  @Test
  public void testAddCQLNode() throws Exception {
    assertEquals(miniCluster.getCQLContactPoints().size(),
      cluster.getMetadata().getAllHosts().size());
    assertEquals(NUM_TABLET_SERVERS, cluster.getMetadata().getAllHosts().size());
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
  }

}
