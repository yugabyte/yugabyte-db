package com.datastax.driver.core;

import org.junit.Before;
import org.junit.Test;
import org.yb.client.MiniYBCluster;
import org.yb.cql.TestBase;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class TestNodeListRefresh extends TestBase {
  private static final Logger LOG = LoggerFactory.getLogger(TestNodeListRefresh.class);

  private MyControlConnection myControlConnection;

  private class MyControlConnection extends ControlConnection {
    public MyControlConnection(Cluster cluster) {
      super(cluster.manager);
      numRefresh = 0;
    }

    @Override
    void refreshNodeListAndTokenMap() {
      numRefresh++;
      super.refreshNodeListAndTokenMap();
    }

    public int numRefresh;
  }

  @Override
  public void SetUpBefore() throws Exception {
    SocketOptions socketOptions = new SocketOptions();
    socketOptions.setReadTimeoutMillis(0);
    cluster = Cluster.builder()
      .addContactPointsWithPorts(miniCluster.getCQLContactPoints())
      .withSocketOptions(socketOptions)
      .build();
    LOG.info("Connected to cluster: " + cluster.getMetadata().getClusterName());

    // Replace the control connection with our own.
    myControlConnection = new MyControlConnection(cluster);
    cluster.manager.controlConnection = myControlConnection;
    session = cluster.connect();
  }

  @Test
  public void testNodeListRefresh() throws Exception {
    // Verify that the system still keeps running after multiple node list refresh requests.
    int iterations = 5;
    for (int i = 0; i < iterations; i++) {
      assertEquals(1, session.execute("SELECT * FROM system.peers;").all().size());
      Thread.sleep(MiniYBCluster.CQL_NODE_LIST_REFRESH * 1000);
    }

    // Have atleast 'iterations - 1' number of refreshes, since event queueing and delivery
    // might take up some amount of time.
    LOG.info("Num node list refresh events: " + myControlConnection.numRefresh);
    assertTrue(myControlConnection.numRefresh >= iterations - 1);
  }
}
