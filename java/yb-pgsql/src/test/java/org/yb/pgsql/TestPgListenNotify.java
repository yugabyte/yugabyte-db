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
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import com.yugabyte.PGNotification;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonNet;
import org.yb.CommonNet.CloudInfoPB;
import org.yb.CommonTypes;
import org.yb.YBTestRunner;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.ModifyClusterConfigLiveReplicas;
import org.yb.client.SnapshotInfo;
import org.yb.master.CatalogEntityInfo;
import org.yb.client.ModifyClusterConfigReadReplicas;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.util.ProcessUtil;
import org.yb.util.Timeouts;
import org.yb.util.YBBackupUtil;

/**
 * Tests for LISTEN/NOTIFY functionality including error handling during
 * background worker startup and basic notification delivery.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgListenNotify extends BasePgListenNotifyTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgListenNotify.class);

  private static final String CHANNEL = "test_channel";
  private static final String PAYLOAD = "test_payload";
  private static final String LARGE_PAYLOAD;
  static {
    char[] chars = new char[7000];
    Arrays.fill(chars, 'x');
    LARGE_PAYLOAD = new String(chars);
  }

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
   * Validates that the notifications poller correctly persists its streaming position
   * (restart_lsn) to the cdc_state table after processing each transaction. When the
   * poller is killed and restarts, it should resume from the persisted position and NOT
   * re-deliver notifications that were already consumed.
   *
   * Scenario:
   *   1. Listener starts LISTEN on a channel.
   *   2. Three notifications are sent and received by the listener.
   *   3. The notifications poller is killed with SIGTERM.
   *   4. The poller restarts automatically (bgw_restart_time = 1s).
   *   5. Verify no old notifications are re-delivered to the listener.
   *   6. Send a new notification and verify it is delivered.
   */
  @Test
  public void testPollerRestartDoesNotRedeliverNotifications() throws Exception {
    final String channel = "poller_restart";

    try (Connection listenerConn = getConnectionBuilder().withTServer(0).connect();
         Statement listenerStmt = listenerConn.createStatement()) {
      listenerStmt.execute("LISTEN " + channel);

      try (Connection notifierConn = getConnectionBuilder().connect();
           Statement notifierStmt = notifierConn.createStatement()) {
        notifierStmt.execute("NOTIFY " + channel + ", 'msg1'");
        notifierStmt.execute("NOTIFY " + channel + ", 'msg2'");
        notifierStmt.execute("NOTIFY " + channel + ", 'msg3'");
      }

      List<PGNotification> received = waitForNotification(listenerConn, channel, "msg3");
      assertEquals("Should receive all 3 notifications", 3, received.size());
      LOG.info("Received initial notifications: " + received);

      // Allow the poller to finish LSN persistence for the last transaction.
      // YBCCalculatePersistAndGetRestartLSN runs after adding to the queue,
      // so there's a small window between delivery and LSN flush.
      Thread.sleep(Timeouts.adjustTimeoutSecForBuildType(2000));

      int pollerPid = getPollerPid(listenerStmt);
      LOG.info("Notifications poller PID: " + pollerPid);

      // SIGTERM causes FATAL -> proc_exit(1). The postmaster treats exit code 1
      // as a non-crash exit, so only the bgworker is restarted (not all backends).
      ProcessUtil.signalProcess(pollerPid, "TERM");
      LOG.info("Sent SIGTERM to notifications poller (pid " + pollerPid + ")");

      int newPollerPid = waitForPollerRestart(listenerStmt, pollerPid);
      LOG.info("Notifications poller restarted with PID: " + newPollerPid);

      // Give the restarted poller time to re-stream. If LSN persistence is broken,
      // old notifications would be re-delivered during this window.
      waitAndAssertNoNotifications(listenerConn,
          "Old notifications should not be re-delivered after poller restart");

      // Verify that the restarted poller delivers new notifications.
      try (Connection notifierConn = getConnectionBuilder().connect();
           Statement notifierStmt = notifierConn.createStatement()) {
        notifierStmt.execute("NOTIFY " + channel + ", 'after_restart'");
      }

      waitForNotification(listenerConn, channel, "after_restart");
      LOG.info("New notification received after poller restart");
    }
  }

  private int getPollerPid(Statement stmt) throws Exception {
    Row row = getSingleRow(stmt,
        "SELECT pid FROM pg_stat_activity WHERE backend_type = 'notifications poller'");
    return row.getInt(0);
  }

  /**
   * Polls pg_stat_activity until the notifications poller appears with a PID
   * different from {@code oldPid}, indicating it has restarted.
   */
  private int waitForPollerRestart(Statement stmt, int oldPid) throws Exception {
    for (int attempt = 0; attempt < 60; attempt++) {
      try {
        List<Row> rows = getRowList(stmt,
            "SELECT pid FROM pg_stat_activity WHERE backend_type = 'notifications poller'");
        if (!rows.isEmpty()) {
          int pid = rows.get(0).getInt(0);
          if (pid != oldPid)
            return pid;
        }
      } catch (Exception e) {
        // Poller might not be visible yet during restart.
      }
      Thread.sleep(500);
    }
    fail("Notifications poller did not restart within 30 seconds");
    return -1;
  }

  /**
   * Verifies that a connection receives its own notifications (self-notify).
   */
  @Test
  public void testSelfNotify() throws Exception {
    try (Connection conn = getConnectionBuilder().connect()) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
        stmt.execute("NOTIFY " + CHANNEL + ", 'self_notify'");
      }
      waitForNotification(conn, CHANNEL, "self_notify");
    }
  }

  /**
   * Verifies that LISTEN and NOTIFY issued within the same transaction
   * result in the notification being delivered after COMMIT, regardless
   * of the order of LISTEN and NOTIFY within the transaction.
   */
  @Test
  public void testListenAndNotifyInSameTransaction() throws Exception {
    // Case 1: LISTEN before NOTIFY.
    try (Connection conn = getConnectionBuilder().connect()) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("LISTEN " + CHANNEL);
        stmt.execute("NOTIFY " + CHANNEL + ", 'listen_first'");
        stmt.execute("COMMIT");
      }
      waitForNotification(conn, CHANNEL, "listen_first");
    }

    // Case 2: NOTIFY before LISTEN.
    try (Connection conn = getConnectionBuilder().connect()) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("NOTIFY " + CHANNEL + ", 'notify_first'");
        stmt.execute("LISTEN " + CHANNEL);
        stmt.execute("COMMIT");
      }
      waitForNotification(conn, CHANNEL, "notify_first");
    }
  }

  /**
   * Verifies LISTEN behavior inside subtransactions:
   *   - LISTEN inside a committed subtransaction (RELEASE SAVEPOINT) takes effect
   *     and the connection receives notifications after COMMIT.
   *   - LISTEN inside a rolled-back subtransaction (ROLLBACK TO SAVEPOINT) is
   *     cancelled and the connection does NOT receive notifications after COMMIT.
   */
  @Test
  public void testListenInSubtransaction() throws Exception {
    // Case 1: LISTEN in a committed subtransaction -- should receive notifications.
    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("SAVEPOINT sp1");
        stmt.execute("LISTEN " + CHANNEL);
        stmt.execute("RELEASE SAVEPOINT sp1");
        stmt.execute("COMMIT");
      }
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'subtxn_listen_commit'");
      }
      waitForNotification(listenerConn, CHANNEL, "subtxn_listen_commit");
    }

    // Case 2: LISTEN in a rolled-back subtransaction -- should NOT receive notifications.
    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("SAVEPOINT sp2");
        stmt.execute("LISTEN " + CHANNEL);
        stmt.execute("ROLLBACK TO SAVEPOINT sp2");
        stmt.execute("COMMIT");
      }
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'subtxn_listen_abort'");
      }

      waitAndAssertNoNotifications(listenerConn,
          "Should not receive notifications when LISTEN was in rolled-back subtransaction");
    }
  }

  /**
   * Verifies NOTIFY behavior inside subtransactions:
   *   - A notification sent inside a committed subtransaction (RELEASE SAVEPOINT)
   *     is delivered to the listener.
   *   - A notification sent inside a rolled-back subtransaction (ROLLBACK TO SAVEPOINT)
   *     is NOT delivered to the listener.
   */
  @Test
  public void testNotifyInSubtransaction() throws Exception {
    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      // Subtransaction that commits: notification should be delivered.
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("SAVEPOINT sp1");
        stmt.execute("NOTIFY " + CHANNEL + ", 'subtxn_commit'");
        stmt.execute("RELEASE SAVEPOINT sp1");
        stmt.execute("COMMIT");
      }
      waitForNotification(listenerConn, CHANNEL, "subtxn_commit");

      // Subtransaction that aborts: notification should NOT be delivered.
      // A sentinel notification is sent after the rollback to confirm the delivery path.
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("BEGIN");
        stmt.execute("SAVEPOINT sp2");
        stmt.execute("NOTIFY " + CHANNEL + ", 'subtxn_abort'");
        stmt.execute("ROLLBACK TO SAVEPOINT sp2");
        stmt.execute("NOTIFY " + CHANNEL + ", 'sentinel'");
        stmt.execute("COMMIT");
      }

      List<PGNotification> received =
          waitForNotification(listenerConn, CHANNEL, "sentinel");
      for (PGNotification n : received) {
        assertTrue("Should not receive notification from rolled-back subtransaction, got: "
            + n.getParameter(), !n.getParameter().equals("subtxn_abort"));
      }
    }
  }

  /**
   * Sends a large number of notifications in a single transaction and verifies
   * that all are delivered in order to the listener.
   */
  @Test
  public void testLargeNumberOfNotifiesInTransaction() throws Exception {
    final int numNotifications = 500;

    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("BEGIN");
        for (int i = 0; i < numNotifications; i++) {
          stmt.addBatch("NOTIFY " + CHANNEL + ", 'msg_" + i + "'");
        }
        stmt.executeBatch();
        stmt.execute("COMMIT");
      }

      List<PGNotification> allNotifs = waitForNotification(
          listenerConn, CHANNEL, "msg_" + (numNotifications - 1));
      assertEquals("Expected all notifications to be delivered",
          numNotifications, allNotifs.size());
      for (int i = 0; i < numNotifications; i++) {
        assertEquals(CHANNEL, allNotifs.get(i).getName());
        assertEquals("msg_" + i, allNotifs.get(i).getParameter());
      }
    }
  }

  /**
   * Verifies that a committed notification is still delivered to an active
   * listener even after the notifier's backend is killed with SIGKILL.
   */
  @Test
  public void testNotifyCommitAndNotifierCrash() throws Exception {
    try (Connection listenerConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      Connection notifierConn = getConnectionBuilder().connect();
      int notifierPid = getPgBackendPid(notifierConn);
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'survive_crash'");
      }
      ProcessUtil.signalProcess(notifierPid, "KILL");
      LOG.info("Crashed notifier backend PID: {}", notifierPid);

      waitForNotification(listenerConn, CHANNEL, "survive_crash");
    }
  }

  /**
   * Backs up the source database, sends notifications on it, restores to a new database,
   * then verifies that LISTEN/NOTIFY works on the restored database with no spurious
   * notifications from the source.
   */
  @Test
  public void testListenNotifyAfterBackupRestore() throws Exception {
    YBBackupUtil.setTSAddresses(miniCluster.getTabletServers());
    YBBackupUtil.setMasterAddresses(masterAddresses);
    YBBackupUtil.setPostgresContactPoint(miniCluster.getPostgresContactPoints().get(0));
    YBBackupUtil.maybeStartYbControllers(miniCluster);

    final String restoredDb = "restored_db";

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)");
      stmt.execute("INSERT INTO t VALUES (1, 'hello'), (2, 'world')");
    }

    // Send notifications on the source database before backup.
    try (Connection notifierConn = getConnectionBuilder().connect();
         Statement stmt = notifierConn.createStatement()) {
      stmt.execute("NOTIFY " + CHANNEL + ", 'before_backup'");
    }

    String backupDir = YBBackupUtil.getTempBackupDir();
    String output = YBBackupUtil.runYbBackupCreate("--backup_location", backupDir,
        "--keyspace", "ysql.yugabyte");
    if (!TestUtils.useYbController()) {
      backupDir = new JSONObject(output).getString("snapshot_url");
    }

    // Send more notifications after backup.
    try (Connection notifierConn = getConnectionBuilder().connect();
         Statement stmt = notifierConn.createStatement()) {
      stmt.execute("NOTIFY " + CHANNEL + ", 'after_backup'");
    }

    YBBackupUtil.runYbBackupRestore(backupDir, "--keyspace", "ysql." + restoredDb);

    // Verify data was restored.
    try (Connection restoredConn = getConnectionBuilder().withDatabase(restoredDb).connect();
         Statement stmt = restoredConn.createStatement()) {
      assertQuery(stmt, "SELECT * FROM t ORDER BY id",
          new Row(1, "hello"), new Row(2, "world"));
    }

    // LISTEN on the restored database -- no stale notifications should arrive.
    try (Connection listenerConn = getConnectionBuilder().withDatabase(restoredDb).connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      waitAndAssertNoNotifications(listenerConn,
          "Expected no spurious notifications after restore");

      // Send a new NOTIFY on the restored database and verify delivery.
      try (Connection notifierConn = getConnectionBuilder().withDatabase(restoredDb).connect();
           Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'after_restore'");
      }

      waitForNotification(listenerConn, CHANNEL, "after_restore");
    }

    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE " + restoredDb);
    }
  }

  /**
   * Tests LISTEN/NOTIFY isolation between a source database and its clone.
   * Verifies:
   *   - Fresh LISTEN/NOTIFY works on the cloned database.
   *   - Notifications sent on the clone are NOT received by source listeners.
   *   - Notifications sent on the source are NOT received by clone listeners.
   */
  @Test
  public void testListenNotifyWithDbClone() throws Exception {
    YBClient client = miniCluster.getClient();

    for (HostAndPort master : miniCluster.getMasters().keySet()) {
      client.setFlag(master, "enable_db_clone", "true");
    }

    // A snapshot schedule is required for cloning.
    client.createSnapshotSchedule(
        CommonTypes.YQLDatabase.YQL_DATABASE_PGSQL, "yugabyte",
        /* retentionInSecs */ 600, /* timeIntervalInSecs */ 10);

    // Wait for at least one snapshot to reach COMPLETE state.
    TestUtils.waitFor(() -> {
      ListSnapshotSchedulesResponse resp = client.listSnapshotSchedules(null);
      for (SnapshotScheduleInfo info : resp.getSnapshotScheduleInfoList()) {
        for (SnapshotInfo snap : info.getSnapshotInfoList()) {
          if (snap.getState()
              == CatalogEntityInfo.SysSnapshotEntryPB.State.COMPLETE) {
            return true;
          }
        }
      }
      return false;
    }, 60000);

    // The snapshot's timestamp is assigned as MaxGlobalNow, which can be ahead
    // of the physical clock. The clone's restore timestamp uses the physical
    // clock, so wait for it to exceed the snapshot's timestamp.
    Thread.sleep(2000);

    final String cloneDb = "clone_db";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE " + cloneDb + " TEMPLATE yugabyte");
    }

    // Set up listeners on both source and clone databases.
    try (Connection sourceListener = getConnectionBuilder().connect();
         Connection cloneListener = getConnectionBuilder().withDatabase(cloneDb).connect()) {
      try (Statement stmt = sourceListener.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }
      try (Statement stmt = cloneListener.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }

      // NOTIFY on clone: clone listener should receive, source should NOT.
      try (Connection cloneNotifier = getConnectionBuilder().withDatabase(cloneDb).connect();
           Statement stmt = cloneNotifier.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'from_clone'");
      }
      waitForNotification(cloneListener, CHANNEL, "from_clone");

      // Use a sentinel to verify the source listener didn't get the clone's notification.
      try (Connection sourceNotifier = getConnectionBuilder().connect();
           Statement stmt = sourceNotifier.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'source_sentinel'");
      }
      List<PGNotification> sourceNotifs =
          waitForNotification(sourceListener, CHANNEL, "source_sentinel");
      for (PGNotification n : sourceNotifs) {
        assertTrue("Source should not receive notifications from clone, got: "
            + n.getParameter(), !n.getParameter().equals("from_clone"));
      }

      // NOTIFY on source: source listener should receive, clone should NOT.
      try (Connection sourceNotifier = getConnectionBuilder().connect();
           Statement stmt = sourceNotifier.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'from_source'");
      }
      waitForNotification(sourceListener, CHANNEL, "from_source");

      // Use a sentinel to verify the clone listener didn't get the source's notification.
      try (Connection cloneNotifier = getConnectionBuilder().withDatabase(cloneDb).connect();
           Statement stmt = cloneNotifier.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'clone_sentinel'");
      }
      List<PGNotification> cloneNotifs =
          waitForNotification(cloneListener, CHANNEL, "clone_sentinel");
      for (PGNotification n : cloneNotifs) {
        assertTrue("Clone should not receive notifications from source, got: "
            + n.getParameter(), !n.getParameter().equals("from_source"));
      }
    }

    try (Statement stmt = connection.createStatement()) {
      if (isTestRunningWithConnectionManager())
        waitForStatsToGetUpdated();
      stmt.execute("DROP DATABASE " + cloneDb);
    }
  }

  /**
   * Tests LISTEN/NOTIFY with a read replica node:
   *   1. Both LISTEN and NOTIFY on the read replica node.
   *   2. LISTEN on read replica, NOTIFY on primary.
   *   3. LISTEN on primary, NOTIFY on read replica.
   */
  @Test
  public void testListenNotifyWithReadReplica() throws Exception {
    final String RR_UUID = "readcluster";
    final String CLOUD = "cloud1";
    final String REGION = "datacenter1";
    final String ZONE = "rack1";

    YBClient client = miniCluster.getClient();

    // Start a read replica tserver with LISTEN/NOTIFY enabled.
    Map<String, String> rrFlags = new HashMap<>(getTServerFlags());
    rrFlags.put("placement_cloud", CLOUD);
    rrFlags.put("placement_region", REGION);
    rrFlags.put("placement_zone", ZONE);
    rrFlags.put("placement_uuid", RR_UUID);
    int rrIndex = miniCluster.getNumTServers();
    miniCluster.startTServer(rrFlags);
    miniCluster.waitForTabletServers(rrIndex + 1);

    // Configure cluster: live placement (default UUID) + read replica placement.
    CloudInfoPB cloudInfo = CloudInfoPB.newBuilder()
        .setPlacementCloud(CLOUD)
        .setPlacementRegion(REGION)
        .setPlacementZone(ZONE)
        .build();
    CommonNet.PlacementBlockPB placementBlock = CommonNet.PlacementBlockPB.newBuilder()
        .setCloudInfo(cloudInfo)
        .setMinNumReplicas(1)
        .build();

    CommonNet.PlacementInfoPB livePlacement = CommonNet.PlacementInfoPB.newBuilder()
        .addPlacementBlocks(placementBlock)
        .setNumReplicas(3)
        .setPlacementUuid(ByteString.copyFromUtf8(""))
        .build();
    CommonNet.PlacementInfoPB rrPlacement = CommonNet.PlacementInfoPB.newBuilder()
        .addPlacementBlocks(placementBlock)
        .setNumReplicas(1)
        .setPlacementUuid(ByteString.copyFromUtf8(RR_UUID))
        .build();

    new ModifyClusterConfigLiveReplicas(client, livePlacement).doCall();
    new ModifyClusterConfigReadReplicas(client, Arrays.asList(rrPlacement)).doCall();

    final int LB_WAIT_TIME_MS = 120 * 1000;
    assertTrue("Load balancer did not become active",
        client.waitForLoadBalancerActive(LB_WAIT_TIME_MS));
    assertTrue("Load balancer did not become idle",
        client.waitForLoadBalancerIdle(LB_WAIT_TIME_MS));

    // Verify the RR tserver is actually a read replica.
    try (Connection rrConn = getConnectionBuilder().withTServer(rrIndex).connect();
         Statement stmt = rrConn.createStatement()) {
      String rrHost = miniCluster.getPostgresContactPoints().get(rrIndex).getHostName();
      getSingleRow(stmt,
          "SELECT 1 FROM yb_servers() WHERE host = '" + rrHost
          + "' AND node_type = 'read_replica'");
    }

    // Case 1: LISTEN and NOTIFY both on the read replica node.
    LOG.info("Case 1: LISTEN and NOTIFY both on read replica");
    try (Connection rrListener = getConnectionBuilder().withTServer(rrIndex).connect();
         Connection rrNotifier = getConnectionBuilder().withTServer(rrIndex).connect()) {
      try (Statement stmt = rrListener.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }
      try (Statement stmt = rrNotifier.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'rr_to_rr'");
      }
      waitForNotification(rrListener, CHANNEL, "rr_to_rr");
    }

    // Case 2: LISTEN on read replica, NOTIFY on primary.
    LOG.info("Case 2: LISTEN on read replica, NOTIFY on primary");
    try (Connection rrListener = getConnectionBuilder().withTServer(rrIndex).connect();
         Connection primaryNotifier = getConnectionBuilder().withTServer(0).connect()) {
      try (Statement stmt = rrListener.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }
      try (Statement stmt = primaryNotifier.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'primary_to_rr'");
      }
      waitForNotification(rrListener, CHANNEL, "primary_to_rr");
    }

    // Case 3: LISTEN on primary, NOTIFY on read replica.
    LOG.info("Case 3: LISTEN on primary, NOTIFY on read replica");
    try (Connection primaryListener = getConnectionBuilder().withTServer(0).connect();
         Connection rrNotifier = getConnectionBuilder().withTServer(rrIndex).connect()) {
      try (Statement stmt = primaryListener.createStatement()) {
        stmt.execute("LISTEN " + CHANNEL);
      }
      try (Statement stmt = rrNotifier.createStatement()) {
        stmt.execute("NOTIFY " + CHANNEL + ", 'rr_to_primary'");
      }
      waitForNotification(primaryListener, CHANNEL, "rr_to_primary");
    }
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

        waitAndAssertNoNotifications(listener,
            "Notification should not arrive within 5s with 30s poll interval");
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

  /**
   * When the notifications poller crashes after writing notifications to the queue but before
   * persisting the CDC ack, virtual WAL replays the same transaction when the poller process
   * restarts. Test that duplicate queue entries do not produce duplicate NOTIFY deliveries
   * (txn-begin markers suppress duplicates on read).
   */
  @Test
  public void testListenNotifyNoDuplicateAfterPollerCrashBeforeAck() throws Exception {
    final String channel = "chan";
    final String payload = "payload";

    try {
      setFatalAfterNotifsQueueWriteFlag(true);
      Thread.sleep(2000);

      try (Connection listenerConn = getConnectionBuilder().withTServer(0).connect();
           Connection notifierConn = getConnectionBuilder().withTServer(0).connect()) {

        try (Statement listenerStmt = listenerConn.createStatement()) {
          listenerStmt.execute("LISTEN " + channel);
        }

        try (Statement notifierStmt = notifierConn.createStatement()) {
          notifierStmt.execute("NOTIFY " + channel + ", '" + payload + "'");
        }

        List<PGNotification> received = waitForNotification(listenerConn, channel, payload);
        assertEquals(
            "Expected exactly one NOTIFY for duplicate txn-begin replay suppression",
            1,
            received.size());

        waitAndAssertNoNotifications(listenerConn,
            "Old notifications should not be re-delivered after poller restart");
      }
    } finally {
      setFatalAfterNotifsQueueWriteFlag(false);
    }

    verifyListenNotifyWorks();
  }

  /**
   * When the NOTIFY queue fills up, verify that the slow listener is terminated.
   */
  @Test
  public void testQueueFullWithOneSlowListener() throws Exception {
    fillQueueAndAssertSlowListenersTerminated(1);
  }

  /**
   * When the NOTIFY queue fills up, verify that both the slow listeners are terminated.
   */
  @Test
  public void testQueueFullWithTwoSlowListeners() throws Exception {
    fillQueueAndAssertSlowListenersTerminated(2);
  }

  /**
   * When the NOTIFY queue fills up, verify that the slow listener is terminated and the fast
   * listener receives all the notifications even though a single transaction sends more
   * notifications than the queue capacity.
   */
  @Test
  public void testQueueFullWithLargeTransaction() throws Exception {
    try (Connection connFast = getConnectionBuilder().connect()) {
      try (Statement stmt = connFast.createStatement()) {
        stmt.execute("LISTEN ch1");
      }
      fillQueueAndAssertSlowListenersTerminated(1);
      waitForNotification(connFast, "ch1", LARGE_PAYLOAD);
    }
  }

  private Connection createSlowListener(String channel) throws Exception {
    Connection conn = getConnectionBuilder().connect();
    try (Statement stmt = conn.createStatement()) {
      stmt.execute("LISTEN " + channel);
      stmt.execute("BEGIN");
    }
    return conn;
  }

  private Connection fillQueue(String channel, int count) throws Exception {
    Connection conn = getConnectionBuilder().connect();
    try (Statement stmt = conn.createStatement()) {
      for (int i = 0; i < count; i++) {
        stmt.execute("NOTIFY " + channel + ", '" + LARGE_PAYLOAD + "'");
      }
    }
    return conn;
  }

  private void assertConnectionTerminated(Connection conn, String label) {
    try (Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT 1");
      fail(label + " should have been terminated");
    } catch (SQLException e) {
      LOG.info("{} error (expected): {}", label, e.getMessage());
    }
  }

  /**
   * Uses yb_test_notify_queue_max_pages to artificially limit the queue so it fills up with a
   * handful of large-payload notifications.
   */
  private void fillQueueAndAssertSlowListenersTerminated(int numSlowListeners) throws Exception {
    setNotifyQueueMaxPages("2");
    try {
      Connection[] slowListeners = new Connection[numSlowListeners];
      for (int i = 0; i < numSlowListeners; i++) {
        slowListeners[i] = createSlowListener("ch1");
      }
      Connection connNotifier = fillQueue("ch1", 10);
      Thread.sleep(5000);
      for (int i = 0; i < numSlowListeners; i++) {
        assertConnectionTerminated(slowListeners[i], "Slow listener " + (i + 1));
      }
      connNotifier.close();
    } finally {
      setNotifyQueueMaxPages("0");
    }
  }

  /**
   * Verifies that LISTEN/NOTIFY works even after a non-INSERT/DELETE record (i.e., an UPDATE)
   * appears in the pg_yb_notifications CDC stream. The notifications poller consumes the CDC
   * stream for pg_yb_notifications; an UPDATE record is unexpected (users never UPDATE this
   * table) and must not break the poller.
   *
   * Scenario:
   *   1. Session starts LISTEN.
   *   2. NOTIFY is sent and received by the listener.
   *   3. A dummy row is INSERTed into pg_yb_notifications and then UPDATEd (producing a
   *      non-INSERT/DELETE CDC record).
   *   4. Another NOTIFY is sent and received by the listener, proving the poller survived.
   */
  @Test
  public void testNotifyWorksAfterUpdateOnNotificationsTable() throws Exception {
    final String channel = "update_test";

    try (Connection listenerConn = getConnectionBuilder().connect();
         Connection notifierConn = getConnectionBuilder().connect()) {
      try (Statement stmt = listenerConn.createStatement()) {
        stmt.execute("LISTEN " + channel);
      }

      // First NOTIFY: baseline check.
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + channel + ", 'before_update'");
      }
      waitForNotification(listenerConn, channel, "before_update");
      LOG.info("Received notification before UPDATE on pg_yb_notifications");

      // INSERT a dummy row into pg_yb_notifications and UPDATE it.
      try (Connection ybSystemConn =
               getConnectionBuilder().withDatabase("yb_system").connect();
           Statement stmt = ybSystemConn.createStatement()) {
        stmt.execute("INSERT INTO pg_yb_notifications"
            + " (notif_uuid, sender_node_uuid, sender_pid, db_oid, is_listen, data)"
            + " VALUES ('00000000-0000-0000-0000-000000000001',"
            + " '00000000-0000-0000-0000-000000000002', 0, 0, false, '\\x00')");
        stmt.execute("UPDATE pg_yb_notifications SET sender_pid = 1"
            + " WHERE notif_uuid = '00000000-0000-0000-0000-000000000001'");
        LOG.info("Inserted and updated dummy row in pg_yb_notifications");
      }

      // Second NOTIFY: must still work after the UPDATE record hit the CDC stream.
      try (Statement stmt = notifierConn.createStatement()) {
        stmt.execute("NOTIFY " + channel + ", 'after_update'");
      }
      waitForNotification(listenerConn, channel, "after_update");
      LOG.info("Received notification after UPDATE on pg_yb_notifications");
    }
  }

  private void setFatalAfterNotifsQueueWriteFlag(boolean value) throws Exception {
    String v = value ? "true" : "false";
    for (HostAndPort tserver : miniCluster.getTabletServers().keySet()) {
      setServerFlag(tserver, "ysql_yb_test_fatal_after_notifs_queue_write", v);
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

  private void setNotifyQueueMaxPages(String value) throws Exception {
    for (HostAndPort tserver : miniCluster.getTabletServers().keySet()) {
      setServerFlag(tserver, "ysql_yb_test_notify_queue_max_pages", value);
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
