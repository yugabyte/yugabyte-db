// Copyright (c) YugabyteDB, Inc.
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
package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import com.yugabyte.PGConnection;
import com.yugabyte.PGNotification;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

/**
 * Tests for LISTEN/NOTIFY functionality including error handling during
 * background worker startup and basic notification delivery.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgListenNotify extends BasePgListenNotifyTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgListenNotify.class);

  private static final String CHANNEL = "test_channel";
  private static final String PAYLOAD = "test_payload";

  /**
   * LISTEN should fail when cdc_max_virtual_wal_per_tserver is 0 (no virtual
   * WAL can be created for the notifications poller). After resetting the flag,
   * LISTEN and NOTIFY should work normally.
   */
  @Test
  public void testListenFailsDueToVirtualWalLimit() throws Exception {
    setVirtualWalLimit("0");
    try {
      try (Connection conn = getConnectionBuilder().connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
        fail("LISTEN should have failed with cdc_max_virtual_wal_per_tserver=0");
      }
    } catch (SQLException e) {
      LOG.info("Expected LISTEN failure: {}", e.getMessage());
      assertTrue("Error should mention initialization failure",
          e.getMessage().contains("failed to initialize"));
    }

    setVirtualWalLimit("5");
    verifyListenNotifyWorks();
  }

  /**
   * LISTEN should fail when the replication slot cannot be created because the
   * max_replication_slots limit on the master is already reached. After
   * increasing the limit, LISTEN and NOTIFY should work normally.
   */
  @Test
  public void testListenFailsDueToReplicationSlotLimit() throws Exception {
    setMaxReplicationSlots("1");

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SELECT pg_create_logical_replication_slot('blocker_slot', 'pgoutput')");
    }

    try {
      try (Connection conn = getConnectionBuilder().connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
        fail("LISTEN should have failed due to replication slot limit");
      }
    } catch (SQLException e) {
      LOG.info("Expected LISTEN failure: {}", e.getMessage());
      assertTrue("Error should mention replication slots",
          e.getMessage().contains("all replication slots are in use"));
    }

    setMaxReplicationSlots("5");
    verifyListenNotifyWorks();
  }

  /**
   * Verifies that LISTEN succeeds, and that a NOTIFY sent from another
   * connection is delivered to the listener.
   */
  private void verifyListenNotifyWorks() throws Exception {
    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      PGConnection pgListener = listenerConn.unwrap(PGConnection.class);

      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", '" + PAYLOAD + "'");
      }

      /*
       * In YB, notifications are delivered asynchronously via the poller.
       * Poll for up to 30 seconds.
       */
      PGNotification[] notifications = null;
      for (int i = 0; i < 300; i++) {
        try (Statement stmt = listenerConn.createStatement()) {
          stmt.execute("SELECT 1");
        }
        notifications = pgListener.getNotifications();
        if (notifications != null && notifications.length > 0)
          break;
        Thread.sleep(100);
      }

      assertNotNull("Should have received a notification", notifications);
      assertTrue("Should have received at least one notification", notifications.length > 0);
      assertEquals(CHANNEL, notifications[0].getName());
      assertEquals(PAYLOAD, notifications[0].getParameter());
    }
  }

  private void setVirtualWalLimit(String value) throws Exception {
    Set<HostAndPort> tservers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tserver : tservers) {
      miniCluster.getClient().setFlag(tserver, "cdc_max_virtual_wal_per_tserver", value);
    }
  }

  private void setMaxReplicationSlots(String value) throws Exception {
    for (HostAndPort master : miniCluster.getMasters().keySet()) {
      assertTrue("Failed to set max_replication_slots",
          miniCluster.getClient().setFlag(master, "max_replication_slots", value,
                                          /* force = */ true));
    }
  }
}
