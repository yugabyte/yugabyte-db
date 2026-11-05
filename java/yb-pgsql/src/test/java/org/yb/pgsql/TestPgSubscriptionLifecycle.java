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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.util.BuildTypeUtil;

/**
 * Tests for the logical replication apply-worker / launcher lifecycle.
 *
 * Validates:
 *  - Launcher detects new/dropped subscriptions (catalog read-time reset)
 *  - Tablesync state transitions (SYNCDONE commit, stale origin cleanup,
 *    HT-based origin_startpos, fresh pg_subscription_rel reads)
 *  - sinval messages for pg_subscription_rel (indexing.c + inval.c)
 *
 * These tests do NOT assert on replicated data; they only observe worker
 * PIDs, table sync states, and replication slot metadata.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgSubscriptionLifecycle extends BasePgSubscriptionLifecycleTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgSubscriptionLifecycle.class);

  // The launcher scans pg_subscription every DEFAULT_NAPTIME_PER_CYCLE
  // (180s / 3 min).  Timeout must exceed that for disable/drop tests.
  private static final long LIFECYCLE_TIMEOUT_MS =
      BuildTypeUtil.adjustTimeout(240000);

  private Connection pubConn;
  private Connection subConn;

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  // ---------------------------------------------------------------------------
  //  Setup / Teardown
  // ---------------------------------------------------------------------------

  @Before
  public void initPubSubDatabases() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE " + PUB_DB);
      stmt.execute("CREATE DATABASE " + SUB_DB);
    }
    pubConn = getConnectionBuilder().withDatabase(PUB_DB).connect();
    subConn = getConnectionBuilder().withDatabase(SUB_DB).connect();
  }

  @After
  public void tearDownSubscription() throws Exception {
    if (subConn != null && !subConn.isClosed()) {
      try (Statement stmt = subConn.createStatement()) {
        stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
        stmt.execute(
            "ALTER SUBSCRIPTION " + SUB_NAME + " SET (slot_name = NONE)");
        stmt.execute("DROP SUBSCRIPTION IF EXISTS " + SUB_NAME);
      } catch (Exception e) {
        LOG.warn("Failed to drop subscription during cleanup", e);
      }
    }
    if (pubConn != null && !pubConn.isClosed()) {
      try (Statement stmt = pubConn.createStatement()) {
        stmt.execute(
            "SELECT pg_drop_replication_slot(slot_name) "
            + "FROM pg_replication_slots WHERE plugin = 'pgoutput'");
      } catch (Exception e) {
        LOG.warn("Failed to drop replication slots during cleanup", e);
      }
    }
    Thread.sleep(2000);
    if (pubConn != null && !pubConn.isClosed()) {
      try (Statement stmt = pubConn.createStatement()) {
        stmt.execute("DROP PUBLICATION IF EXISTS " + PUB_NAME);
        // Collect table names first to avoid closing ResultSet mid-iteration.
        java.util.List<String> tables = new java.util.ArrayList<>();
        try (ResultSet rs = stmt.executeQuery(
                 "SELECT tablename FROM pg_tables "
                 + "WHERE schemaname = 'public'")) {
          while (rs.next()) tables.add(rs.getString(1));
        }
        for (String t : tables) {
          stmt.execute("DROP TABLE IF EXISTS " + t + " CASCADE");
        }
      } catch (Exception e) {
        LOG.warn("Failed to drop publisher objects during cleanup", e);
      }
      pubConn.close();
    }
    if (subConn != null && !subConn.isClosed()) {
      try (Statement stmt = subConn.createStatement()) {
        java.util.List<String> tables = new java.util.ArrayList<>();
        try (ResultSet rs = stmt.executeQuery(
                 "SELECT tablename FROM pg_tables "
                 + "WHERE schemaname = 'public'")) {
          while (rs.next()) tables.add(rs.getString(1));
        }
        for (String t : tables) {
          stmt.execute("DROP TABLE IF EXISTS " + t + " CASCADE");
        }
      } catch (Exception e) {
        LOG.warn("Failed to drop subscriber tables during cleanup", e);
      }
      subConn.close();
    }
  }

  // ---------------------------------------------------------------------------
  //  Helpers: table + publication setup
  // ---------------------------------------------------------------------------

  @FunctionalInterface
  private interface TableCreator {
    void create(Statement stmt) throws Exception;
  }

  /**
   * Creates identical tables on publisher and subscriber, sets REPLICA IDENTITY
   * FULL, and creates a publication.  Does NOT create the subscription tests
   * do that explicitly so they can control lifecycle timing.
   */
  private void createPublishedTable(String tableName,
                                    TableCreator creator) throws Exception {
    try (Statement pub = pubConn.createStatement();
         Statement sub = subConn.createStatement()) {
      creator.create(pub);
      creator.create(sub);
      pub.execute("ALTER TABLE " + tableName + " REPLICA IDENTITY FULL");
      sub.execute("ALTER TABLE " + tableName + " REPLICA IDENTITY FULL");
    }
  }

  private void createPublication(String... tableNames) throws Exception {
    StringBuilder sb = new StringBuilder("CREATE PUBLICATION " + PUB_NAME
                                         + " FOR TABLE ");
    for (int i = 0; i < tableNames.length; i++) {
      if (i > 0) sb.append(", ");
      sb.append(tableNames[i]);
    }
    try (Statement pub = pubConn.createStatement()) {
      pub.execute(sb.toString());
    }
  }

  private void createSubscription(boolean copyData) throws Exception {
    try (Statement sub = subConn.createStatement()) {
      sub.execute(createSubscriptionSql(SUB_NAME, PUB_NAME, copyData));
    }
  }

  private void dropSubscriptionCleanly() throws Exception {
    try (Statement sub = subConn.createStatement()) {
      sub.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
      Thread.sleep(1000);
      sub.execute(
          "ALTER SUBSCRIPTION " + SUB_NAME + " SET (slot_name = NONE)");
      sub.execute("DROP SUBSCRIPTION " + SUB_NAME);
    }
    if (pubConn != null && !pubConn.isClosed()) {
      try (Statement pub = pubConn.createStatement()) {
        pub.execute(
            "SELECT pg_drop_replication_slot(slot_name) "
            + "FROM pg_replication_slots WHERE plugin = 'pgoutput'");
      } catch (Exception e) {
        LOG.warn("Failed to drop replication slots", e);
      }
    }
    Thread.sleep(1000);
  }

  // ---------------------------------------------------------------------------
  //  Polling helpers
  // ---------------------------------------------------------------------------

  /**
   * Waits until pg_stat_subscription shows a worker with a non-null PID for
   * the given subscription name.
   */
  private void waitForWorkerPid(String subName) throws Exception {
    LOG.info("Waiting for apply worker PID for subscription '{}'", subName);
    TestUtils.waitFor(() -> workerAlive(subConn, subName),
                      LIFECYCLE_TIMEOUT_MS, POLL_INTERVAL_MS);
    LOG.info("Apply worker is running for subscription '{}'", subName);
  }

  /**
   * Waits until pg_stat_subscription shows no worker with a non-null PID for
   * the given subscription name.
   */
  private void waitForNoWorker(String subName) throws Exception {
    LOG.info("Waiting for apply worker to exit for subscription '{}'",
             subName);
    TestUtils.waitFor(() -> !workerAlive(subConn, subName),
                      LIFECYCLE_TIMEOUT_MS, POLL_INTERVAL_MS);
    LOG.info("Apply worker has exited for subscription '{}'", subName);
  }

  /**
   * Waits until pg_subscription_rel shows the expected state for the given
   * table in the named subscription.
   */
  private void waitForTableState(String subName, String tableName,
                                 char expectedState) throws Exception {
    String stateStr = String.valueOf(expectedState);
    LOG.info("Waiting for table '{}' to reach state '{}' in subscription '{}'",
             tableName, stateStr, subName);
    TestUtils.waitFor(() -> {
      try (Statement stmt = subConn.createStatement();
           ResultSet rs = stmt.executeQuery(
               "SELECT srsubstate FROM pg_subscription_rel sr "
               + "JOIN pg_subscription s ON sr.srsubid = s.oid "
               + "WHERE s.subname = '" + subName + "' "
               + "AND sr.srrelid = '" + tableName + "'::regclass")) {
        return rs.next() && rs.getString(1).equals(stateStr);
      }
    }, LIFECYCLE_TIMEOUT_MS, POLL_INTERVAL_MS);
    LOG.info("Table '{}' reached state '{}' in subscription '{}'",
             tableName, stateStr, subName);
  }

  /**
   * Waits until all tables in the subscription have reached 'r' (ready) state.
   */
  private void waitForAllTablesReady(String subName) throws Exception {
    LOG.info("Waiting for all tables to reach 'r' state in subscription '{}'",
             subName);
    TestUtils.waitFor(() -> {
      try (Statement stmt = subConn.createStatement();
           ResultSet rs = stmt.executeQuery(
               "SELECT count(*) FROM pg_subscription_rel sr "
               + "JOIN pg_subscription s ON sr.srsubid = s.oid "
               + "WHERE s.subname = '" + subName + "' "
               + "AND sr.srsubstate != 'r'")) {
        return rs.next() && rs.getInt(1) == 0;
      }
    }, LIFECYCLE_TIMEOUT_MS, POLL_INTERVAL_MS);
    LOG.info("All tables are ready in subscription '{}'", subName);
  }

  /** Returns the count of pgoutput replication slots on the publisher. */
  private int getReplicationSlotCount() throws Exception {
    try (Statement stmt = pubConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FROM pg_replication_slots "
             + "WHERE plugin = 'pgoutput'")) {
      assertTrue(rs.next());
      return rs.getInt(1);
    }
  }

  /** Returns the count of tables tracked in pg_subscription_rel. */
  private int getSubscriptionRelCount(String subName) throws Exception {
    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FROM pg_subscription_rel sr "
             + "JOIN pg_subscription s ON sr.srsubid = s.oid "
             + "WHERE s.subname = '" + subName + "'")) {
      assertTrue(rs.next());
      return rs.getInt(1);
    }
  }

  // ===================================================================
  //  Tests
  // ===================================================================

  /**
   * Verifies that the launcher detects a newly created subscription and
   * starts an apply worker.  Tests the catalog read-time reset in
   * launcher.c:ApplyLauncherMain.
   */
  @Test
  public void testApplyWorkerLaunched() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t");
    createSubscription(false /* copyData */);

    waitForWorkerPid(SUB_NAME);

    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT pid FROM pg_stat_subscription "
             + "WHERE subname = '" + SUB_NAME + "' AND pid IS NOT NULL")) {
      assertTrue("Worker PID should be visible in pg_stat_subscription",
                 rs.next());
      int pid = rs.getInt(1);
      assertTrue("Worker PID should be positive", pid > 0);
      LOG.info("Apply worker PID: {}", pid);
    }
  }

  /**
   * Verifies that disabling a subscription causes the apply worker to exit.
   */
  @Test
  public void testApplyWorkerExitsOnDisable() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t");
    createSubscription(false);

    waitForWorkerPid(SUB_NAME);

    try (Statement stmt = subConn.createStatement()) {
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
    }

    waitForNoWorker(SUB_NAME);
  }

  /**
   * Verifies that re-enabling a subscription starts a new apply worker.  The
   * worker holds a cluster-wide advisory lock on the subscription OID, so this
   * also covers the lock being released when the previous worker exited.
   */
  @Test
  public void testApplyWorkerRestartsAfterEnable() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t");
    createSubscription(false);

    waitForWorkerPid(SUB_NAME);

    try (Statement stmt = subConn.createStatement()) {
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
    }
    waitForNoWorker(SUB_NAME);

    try (Statement stmt = subConn.createStatement()) {
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " ENABLE");
    }
    waitForWorkerPid(SUB_NAME);
  }

  /**
   * Verifies that DROP SUBSCRIPTION requires a disabled subscription (its
   * workers may run on another node, which DROP cannot stop), and that after
   * DISABLE it stops the apply worker and removes the slot on the publisher.
   */
  @Test
  public void testApplyWorkerExitsOnDrop() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t");
    createSubscription(false);

    waitForWorkerPid(SUB_NAME);
    assertEquals("Replication slot should exist", 1, getReplicationSlotCount());

    try (Statement stmt = subConn.createStatement()) {
      runInvalidQuery(stmt, "DROP SUBSCRIPTION " + SUB_NAME,
                      "while it is enabled");
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
      stmt.execute("DROP SUBSCRIPTION " + SUB_NAME);
    }

    waitForNoWorker(SUB_NAME);
    assertEquals("Replication slot should be removed after DROP SUBSCRIPTION",
                 0, getReplicationSlotCount());
  }

  /**
   * Verifies that table sync reaches 'r' (ready) state with copy_data=false.
   * Exercises the sinval path (indexing.c + inval.c) that lets the apply
   * worker see SYNCDONE state updates from the tablesync worker.
   */
  @Test
  public void testTableSyncReachesReady() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t");
    createSubscription(false);

    waitForWorkerPid(SUB_NAME);
    waitForTableState(SUB_NAME, "t", 'r');
  }

  /**
   * Verifies that table sync completes with copy_data=true on an empty table.
   * Exercises the SYNCDONE commit fix and HT-based origin_startpos extraction
   * in tablesync.c.
   */
  @Test
  public void testTableSyncWithCopyData() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    try (Statement pub = pubConn.createStatement()) {
      pub.execute("INSERT INTO t VALUES (1, 'x'), (2, 'y')");
    }
    createPublication("t");
    createSubscription(true /* copyData */);

    waitForWorkerPid(SUB_NAME);

    /*
     * The initial copy and the tablesync worker's state machine up to
     * SYNCDONE are covered here.  The final SYNCDONE -> READY transition is
     * performed by the apply worker based on the publisher's safe_time and
     * is only reachable with the HT-based state machine (later in this
     * stack), which extends this check to state 'r'.
     */
    waitForTableState(SUB_NAME, "t", 's');

    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT count(*) FROM t")) {
      assertTrue(rs.next());
      assertEquals("initial data should be copied", 2, rs.getInt(1));
    }
  }

  /**
   * A tablesync worker that fails mid-copy must be able to retry.  It creates
   * its replication origin before copying, and in legacy DDL mode that write is
   * durable even though the worker's transaction aborted, so the retry has to
   * drop the leftover origin or every attempt fails with "replication origin
   * already exists" and the table never syncs.
   */
  @Test
  public void testTableSyncRetriesAfterCopyFailure() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    try (Statement pub = pubConn.createStatement()) {
      pub.execute("INSERT INTO t VALUES (1, 'x'), (2, 'y')");
    }
    // Make the initial COPY hit a duplicate key, so the tablesync worker errors
    // out after having created its origin.
    try (Statement sub = subConn.createStatement()) {
      sub.execute("INSERT INTO t VALUES (1, 'conflict')");
    }
    createPublication("t");
    createSubscription(true /* copyData */);

    waitForWorkerPid(SUB_NAME);

    // The copy fails and rolls back, so only the pre-existing row is present.
    Thread.sleep(15000);
    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT count(*) FROM t")) {
      assertTrue(rs.next());
      assertEquals("copy must not have succeeded while the conflict exists",
                   1, rs.getInt(1));
    }

    // Remove the conflict; a retry must now get all the way through.
    try (Statement sub = subConn.createStatement()) {
      sub.execute("DELETE FROM t WHERE id = 1");
    }

    waitForTableState(SUB_NAME, "t", 's');
    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT count(*) FROM t")) {
      assertTrue(rs.next());
      assertEquals("initial copy should complete on retry", 2, rs.getInt(1));
    }
  }

  /**
   * Verifies that a subscription can be dropped and recreated successfully.
   * Tests the stale replication origin cleanup in tablesync.c (the
   * replorigin_drop_by_name call that handles origins surviving YB crashes).
   */
  @Test
  public void testSubscriptionRecreation() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t");

    // First creation.
    createSubscription(false);
    waitForWorkerPid(SUB_NAME);
    waitForTableState(SUB_NAME, "t", 'r');

    // Drop cleanly.
    dropSubscriptionCleanly();
    waitForNoWorker(SUB_NAME);

    // Recreate should succeed even if stale origins exist.
    createSubscription(false);
    waitForWorkerPid(SUB_NAME);
    waitForTableState(SUB_NAME, "t", 'r');
  }

  /**
   * Verifies that multiple tables all reach 'r' (ready) state.
   * Exercises the sinval path for pg_subscription_rel across multiple tables.
   */
  @Test
  public void testMultipleTableSync() throws Exception {
    createPublishedTable("t1", stmt ->
        stmt.execute("CREATE TABLE t1 (id INT PRIMARY KEY, val TEXT)"));
    createPublishedTable("t2", stmt ->
        stmt.execute("CREATE TABLE t2 (id INT PRIMARY KEY, val TEXT)"));
    createPublishedTable("t3", stmt ->
        stmt.execute("CREATE TABLE t3 (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t1", "t2", "t3");
    createSubscription(false);

    waitForWorkerPid(SUB_NAME);
    waitForAllTablesReady(SUB_NAME);

    assertEquals("All 3 tables should be tracked in pg_subscription_rel",
                 3, getSubscriptionRelCount(SUB_NAME));
  }

  /**
   * Verifies that ALTER SUBSCRIPTION ... REFRESH PUBLICATION picks up a
   * newly added table and syncs it to 'r'.  Tests the FetchTableStates
   * read-point reset in tablesync.c.
   */
  @Test
  public void testAlterSubscriptionRefresh() throws Exception {
    createPublishedTable("t1", stmt ->
        stmt.execute("CREATE TABLE t1 (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t1");
    createSubscription(false);

    waitForWorkerPid(SUB_NAME);
    waitForTableState(SUB_NAME, "t1", 'r');

    // Add a second table to the publication.
    createPublishedTable("t2", stmt ->
        stmt.execute("CREATE TABLE t2 (id INT PRIMARY KEY, val TEXT)"));
    try (Statement pub = pubConn.createStatement()) {
      pub.execute("ALTER PUBLICATION " + PUB_NAME + " ADD TABLE t2");
    }

    // Refresh the subscription to pick up the new table.
    try (Statement sub = subConn.createStatement()) {
      sub.execute("ALTER SUBSCRIPTION " + SUB_NAME
                  + " REFRESH PUBLICATION WITH (copy_data = false)");
    }

    waitForTableState(SUB_NAME, "t2", 'r');
    assertEquals("Both tables should be tracked", 2,
                 getSubscriptionRelCount(SUB_NAME));
  }

  /**
   * Verifies that a replication slot is created on the publisher when a
   * subscription is created.
   */
  @Test
  public void testReplicationSlotCreated() throws Exception {
    createPublishedTable("t", stmt ->
        stmt.execute("CREATE TABLE t (id INT PRIMARY KEY, val TEXT)"));
    createPublication("t");

    assertEquals("No replication slots before subscription creation",
                 0, getReplicationSlotCount());

    createSubscription(false);
    waitForWorkerPid(SUB_NAME);

    assertEquals("Replication slot should be created",
                 1, getReplicationSlotCount());

    // Verify the slot name contains the subscription name.
    try (Statement stmt = pubConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT slot_name FROM pg_replication_slots "
             + "WHERE plugin = 'pgoutput'")) {
      assertTrue(rs.next());
      String slotName = rs.getString(1);
      LOG.info("Replication slot name: {}", slotName);
      assertTrue("Slot name should contain subscription name",
                 slotName.contains(SUB_NAME));
    }
  }
}
