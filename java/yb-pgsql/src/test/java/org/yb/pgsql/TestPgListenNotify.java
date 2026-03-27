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
import static org.yb.AssertionWrappers.assertFalse;
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
import org.yb.util.ProcessUtil;
import org.yb.util.Timeouts;

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
   * Validates that LISTEN/NOTIFY continues to work after the only listening backend is killed
   * with SIGKILL.
   *
   * Scenario:
   *   1. Session 1 starts listening on channel 'c', then is killed via SIGKILL.
   *   2. Session 2 starts listening on channel 'c'.
   *   3. Session 3 sends NOTIFY on channel 'c' with a payload.
   *   4. Session 2 receives the notification with the correct payload.
   */
  @Test
  public void testListenNotifyAfterSoloListenerCrash() throws Exception {
    final String channel = "c";
    final String payload = "some payload";

    // Session 1: start listening, then crash it.
    Connection conn1 = getConnectionBuilder().withTServer(0).connect();
    try (Statement stmt1 = conn1.createStatement()) {
      stmt1.execute("LISTEN " + channel);
    }
    int pid1 = getPgBackendPid(conn1);
    LOG.info("Session 1 backend PID: " + pid1);
    ProcessUtil.signalProcess(pid1, "KILL");
    LOG.info("Sent SIGKILL to session 1 (pid " + pid1 + ")");

    // Give the system a moment to clean up the crashed backend.
    Thread.sleep(Timeouts.adjustTimeoutSecForBuildType(2000));

    // Session 2: a new listener that should work despite the previous crash.
    try (Connection conn2 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt2 = conn2.createStatement()) {
      stmt2.execute("LISTEN " + channel);
      LOG.info("Session 2 is now listening on channel '" + channel + "'");

      // Session 3: send a notification.
      try (Connection conn3 = getConnectionBuilder().connect();
           Statement stmt3 = conn3.createStatement()) {
        stmt3.execute("NOTIFY " + channel + ", '" + payload + "'");
        LOG.info("Session 3 sent NOTIFY on channel '" + channel + "'");
      }

      // Poll session 2 for notifications.
      waitForNotification(conn2, channel, payload);
      LOG.info("Session 2 received notification on channel '" + channel + "'");
    }
  }

  /**
   * Validates that LISTEN/NOTIFY continues to work for a surviving listener after another
   * listening backend on the same node is killed with SIGKILL.
   *
   * Scenario:
   *   1. Session 1 starts listening on channel 'c'.
   *   2. Session 2 starts listening on channel 'c' (same node), then is killed via SIGKILL.
   *   3. Session 3 starts listening on channel 'c' (same node).
   *   3. Session 4 sends NOTIFY on channel 'c' with a payload.
   *   4. Session 1 & 3 receives the notification with the correct payload.
   */
  @Test
  public void testListenNotifyAfterPeerListenerCrash() throws Exception {
    final String channel = "c";
    final String payload = "some payload";

    // Session 1: start listening (this session survives).
    try (Connection conn1 = getConnectionBuilder().withTServer(0).connect();
         Statement stmt1 = conn1.createStatement()) {
      stmt1.execute("LISTEN " + channel);
      LOG.info("Session 1 is now listening on channel '" + channel + "'");

      // Session 2: start listening on the same node, then crash it.
      Connection conn2 = getConnectionBuilder().withTServer(0).connect();
      try (Statement stmt2 = conn2.createStatement()) {
        stmt2.execute("LISTEN " + channel);
      }
      int pid2 = getPgBackendPid(conn2);
      LOG.info("Session 2 backend PID: " + pid2);
      ProcessUtil.signalProcess(pid2, "KILL");
      LOG.info("Sent SIGKILL to session 2 (pid " + pid2 + ")");

      // Give the system a moment to clean up the crashed backend.
      Thread.sleep(Timeouts.adjustTimeoutSecForBuildType(2000));

      // Session 3: start new listening session
      try (Connection conn3 = getConnectionBuilder().withTServer(0).connect();
           Statement stmt3 = conn3.createStatement()) {
        stmt3.execute("LISTEN " + channel);

        // Session 4: send a notification.
        try (Connection conn4 = getConnectionBuilder().connect();
            Statement stmt4 = conn4.createStatement()) {
          stmt4.execute("NOTIFY " + channel + ", '" + payload + "'");
          LOG.info("Session 3 sent NOTIFY on channel '" + channel + "'");
        }

        // Poll session 1 & 3 for notifications.
        waitForNotification(conn1, channel, payload);
        waitForNotification(conn3, channel, payload);
      }
    }
  }

  /**
   * Polls the given connection for notifications by executing "SELECT 1" and
   * checking for notifications after each attempt. Asserts that a notification
   * with the expected channel and payload is received within the timeout.
   */
  private void waitForNotification(Connection connection, String expectedChannel,
      String expectedPayload) throws Exception {
    PGNotification[] notifications = null;
    PGConnection pgConecction = connection.unwrap(PGConnection.class);
    try (Statement stmt = connection.createStatement()) {
      for (int attempt = 0; attempt < 30; attempt++) {
        // The JDBC driver fetches notifications as a side-effect of executing a query.
        stmt.execute("SELECT 1");
        notifications = pgConecction.getNotifications();
        if (notifications != null && notifications.length > 0) {
          break;
        }
        Thread.sleep(500);
      }
    }
    assertNotNull("Expected to receive a notification", notifications);
    assertTrue("Expected at least one notification", notifications.length > 0);
    assertEquals(expectedChannel, notifications[0].getName());
    assertEquals(expectedPayload, notifications[0].getParameter());
  }

  /**
   * Verifies that LISTEN succeeds, and that a NOTIFY sent from another
   * connection is delivered to the listener.
   */
  private void verifyListenNotifyWorks() throws Exception {
    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", '" + PAYLOAD + "'");
      }

      waitForNotification(listenerConn, CHANNEL, PAYLOAD);
    }
  }

  /**
   * Verifies that changing the empty-poll sleep duration at runtime
   * actually affects notification delivery latency.
   */
  @Test
  public void testNotificationPollSleepDuration() throws Exception {
    final String channel = "c";

    try {
      try (Connection listener = getConnectionBuilder().withTServer(0).connect();
           Statement listenerStmt = listener.createStatement()) {
        listenerStmt.execute("LISTEN " + channel);

        assertOneRow(listenerStmt, "SHOW yb_notifications_poll_sleep_duration_empty_ms", "100ms");
        setNotificationsPollSleepDurationEmpty("30000");
        Thread.sleep(5000);

        try (Connection notifier = getConnectionBuilder().connect();
             Statement notifierStmt = notifier.createStatement()) {
          notifierStmt.execute("NOTIFY " + channel + ", 'slow'");
        }

        Thread.sleep(15000);

        listenerStmt.execute("SELECT 1");
        PGConnection pgConn = listener.unwrap(PGConnection.class);
        PGNotification[] notifs = pgConn.getNotifications();
        assertFalse("Notification should not arrive within 15s with 30s poll interval",
            notifs != null && notifs.length > 0);
      }

      try (Connection listener = getConnectionBuilder().connect();
           Statement listenerStmt = listener.createStatement()) {
        listenerStmt.execute("LISTEN " + channel);

        assertOneRow(listenerStmt, "SHOW yb_notifications_poll_sleep_duration_empty_ms", "30s");
        setNotificationsPollSleepDurationEmpty("100");
        Thread.sleep(5000);

        try (Connection notifier = getConnectionBuilder().connect();
             Statement notifierStmt = notifier.createStatement()) {
          notifierStmt.execute("NOTIFY " + channel + ", 'restored'");
        }
        waitForNotification(listener, channel, "restored");
      }
    } finally {
      setNotificationsPollSleepDurationEmpty("100");
    }
  }

  private void setNotificationsPollSleepDurationEmpty(String valueMs) throws Exception {
    Set<HostAndPort> tservers = miniCluster.getTabletServers().keySet();
    for (HostAndPort tserver : tservers) {
      setServerFlag(tserver,
          "ysql_yb_notifications_poll_sleep_duration_empty_ms", valueMs);
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
