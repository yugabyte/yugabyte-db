package com.datastax.driver.core;

import org.junit.Test;
import org.yb.cql.BaseCQLTest;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.util.List;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

// This test verifies that 'system.peers' table doesn't return an entry for the node that we're
// connected to. In addition to this, we also test that the 'system.local' table returns only the
// node that we are connected to. For this purpose, we need to look into a few internals of the
// datastax driver and hence we are in the 'com.datastax.driver.core' package.
public class TestSystemTables extends BaseCQLTest {

  @Test
  public void testSystemTables() throws Exception {
    List<InetSocketAddress> contactPoints = miniCluster.getCQLContactPoints();
    assertEquals(NUM_TABLET_SERVERS, contactPoints.size());

    // Chose one contact point to connect and remove it from the list.
    InetSocketAddress contactPointToConnect = contactPoints.get(0);
    contactPoints.remove(contactPointToConnect);

    // Initialize connection.
    Host host = new Host(contactPointToConnect, cluster.manager.convictionPolicyFactory,
      cluster.manager);
    Connection connection = cluster.manager.connectionFactory.open(host);

    // Run query and verify.
    DefaultResultSetFuture peersFuture = new DefaultResultSetFuture(null,
      cluster.manager.protocolVersion(), new Requests.Query("SELECT * FROM system.peers"));
    connection.write(peersFuture);
    List<Row> rows = peersFuture.get().all();
    assertEquals(NUM_TABLET_SERVERS - 1, rows.size());

    for (Row row : rows) {
      // Connected host should not be in result.
      InetAddress peer = row.getInet("peer");
      assertNotEquals(peer, contactPointToConnect.getAddress());

      // Other contact points should be present.
      boolean found = false;
      for (InetSocketAddress contactPoint : contactPoints) {
        if (peer.equals(contactPoint.getAddress())) {
          found = true;
          break;
        }
      }
      assertTrue(found);
    }

    // Verify the local table contains the connected host.
    DefaultResultSetFuture localFuture = new DefaultResultSetFuture(null,
      cluster.manager.protocolVersion(), new Requests.Query("SELECT * FROM system.local"));
    connection.write(localFuture);
    rows = localFuture.get().all();
    assertEquals(1, rows.size());
    assertEquals(contactPointToConnect.getAddress(), rows.get(0).getInet("broadcast_address"));
    assertEquals(contactPointToConnect.getAddress(), rows.get(0).getInet("listen_address"));
    assertEquals(contactPointToConnect.getAddress(), rows.get(0).getInet("rpc_address"));
  }
}
