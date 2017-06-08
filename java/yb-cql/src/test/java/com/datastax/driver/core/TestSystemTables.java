package com.datastax.driver.core;

import org.junit.Test;
import org.yb.cql.BaseCQLTest;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

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

    // Chose one contact point to connect.
    for (InetSocketAddress contactPointToConnect : contactPoints) {

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

      // Build set of all contact points.
      Set<InetAddress> contactPointsSet = new HashSet<>();
      for (InetSocketAddress contactPoint : contactPoints) {
        contactPointsSet.add(contactPoint.getAddress());
      }

      for (Row row : rows) {
        // Connected host should not be in result.
        InetAddress peer = row.getInet("peer");
        assertNotEquals(peer, contactPointToConnect.getAddress());

        // Other contact points should be present.
        assertTrue(contactPointsSet.remove(peer));
      }

      // Only connected host should be left in the set.
      assertEquals(1, contactPointsSet.size());
      assertTrue(contactPointsSet.contains(contactPointToConnect.getAddress()));

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
}
