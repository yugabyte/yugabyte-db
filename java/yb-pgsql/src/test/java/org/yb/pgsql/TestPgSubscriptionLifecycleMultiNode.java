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

import com.google.common.net.HostAndPort;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;

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
 * Multi-node lifecycle tests for logical replication apply workers.
 *
 * pg_subscription is cluster-wide, so every node's launcher sees every
 * subscription.  A launcher probes the subscription's cluster-wide advisory
 * lock before spending a worker slot (YbSubscriptionOwnedElsewhere in
 * launcher.c) and re-probes it on yb_subscription_takeover_probe_interval_ms
 * while another node owns it (10s here, see the base class); the worker's own
 * acquire (ApplyWorkerMain) stays the authoritative exclusion.  Subscription
 * DDL issued on another node also produces no sinval on the worker's node, so
 * the worker learns about it by re-reading pg_subscription on
 * yb_subscription_reread_interval_ms.  These tests cover all three.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgSubscriptionLifecycleMultiNode
    extends BasePgSubscriptionLifecycleTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgSubscriptionLifecycleMultiNode.class);

  private static final int NUM_NODES = 3;
  // Must comfortably exceed yb_subscription_reread_interval_ms and the
  // test's yb_subscription_takeover_probe_interval_ms (both 10s).
  private static final long CROSS_NODE_TIMEOUT_MS =
      BuildTypeUtil.adjustTimeout(60000);
  // Time allowed for a woken launcher to finish its scan, probe the
  // subscription's advisory lock, and decide not to start a worker.
  private static final long LOCK_LOSER_SETTLE_MS = 10000;
  // Initial copy of three tables is slower than a state-change wait.
  private static final long COPY_TIMEOUT_MS =
      BuildTypeUtil.adjustTimeout(180000);
  // Dominated by how long the dead node's session takes to release the
  // advisory lock; the survivors' launchers re-probe it every
  // yb_subscription_takeover_probe_interval_ms (10s here).
  private static final long FAILOVER_TIMEOUT_MS =
      BuildTypeUtil.adjustTimeout(240000);
  // Five subscriptions must launch, take their locks, and connect; launches
  // are paced by wal_retrieve_retry_interval and probe cycles.
  private static final long SLOT_PRESSURE_TIMEOUT_MS =
      BuildTypeUtil.adjustTimeout(240000);

  private Connection pubConn;
  private Connection subConn;

  @Override
  protected int getInitialNumTServers() {
    return NUM_NODES;
  }

  @Override
  protected int getReplicationFactor() {
    return NUM_NODES;
  }

  // ---------------------------------------------------------------------------
  //  Setup / teardown
  // ---------------------------------------------------------------------------

  @Before
  public void setUpPublisherAndSubscriber() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE " + PUB_DB);
      stmt.execute("CREATE DATABASE " + SUB_DB);
    }
    pubConn = nodeConn(0, PUB_DB);
    subConn = nodeConn(0, SUB_DB);
  }

  @After
  public void tearDownSubscription() throws Exception {
    if (subConn != null && !subConn.isClosed()) {
      // Drop every subscription in SUB_DB (some tests create several), so the
      // base class can drop the database afterwards.
      try (Statement stmt = subConn.createStatement()) {
        java.util.List<String> subs = new java.util.ArrayList<>();
        try (ResultSet rs = stmt.executeQuery(
                 "SELECT subname FROM pg_subscription "
                 + "WHERE subdbid = (SELECT oid FROM pg_database "
                 + "WHERE datname = current_database())")) {
          while (rs.next()) subs.add(rs.getString(1));
        }
        for (String s : subs) {
          try {
            stmt.execute("ALTER SUBSCRIPTION " + s + " DISABLE");
            stmt.execute("ALTER SUBSCRIPTION " + s + " SET (slot_name = NONE)");
            stmt.execute("DROP SUBSCRIPTION IF EXISTS " + s);
          } catch (Exception e) {
            LOG.warn("cleanup of subscription {} failed", s, e);
          }
        }
      } catch (Exception e) {
        LOG.warn("subscription cleanup failed", e);
      }
      subConn.close();
    }
    if (pubConn != null && !pubConn.isClosed()) {
      try (Statement stmt = pubConn.createStatement()) {
        stmt.execute("SELECT pg_drop_replication_slot(slot_name) "
                     + "FROM pg_replication_slots WHERE plugin = 'pgoutput'");
      } catch (Exception e) {
        LOG.warn("slot cleanup failed", e);
      }
      pubConn.close();
    }
  }

  // ---------------------------------------------------------------------------
  //  Helpers
  // ---------------------------------------------------------------------------

  private Connection nodeConn(int tserverIdx, String db) throws Exception {
    return getConnectionBuilder().withTServer(tserverIdx).withDatabase(db)
        .connect();
  }

  private boolean workerAliveOnNode(int tserverIdx) throws Exception {
    try (Connection conn = nodeConn(tserverIdx, SUB_DB)) {
      return workerAlive(conn, SUB_NAME);
    }
  }

  /** Number of nodes currently running an apply worker for SUB_NAME. */
  private int nodesRunningApplyWorker() throws Exception {
    return nodesRunningApplyWorkerFor(SUB_NAME);
  }

  /** Same, for an arbitrary subscription name. */
  private int nodesRunningApplyWorkerFor(String subName) throws Exception {
    int count = 0;
    for (int node = 0; node < NUM_NODES; node++) {
      try (Connection conn = nodeConn(node, SUB_DB)) {
        if (workerAlive(conn, subName)) {
          count++;
        }
      }
    }
    return count;
  }

  /**
   * Like nodesRunningApplyWorker, but only over nodes still in the cluster and
   * tolerating connection failures.  killTabletServerOnHostPort removes the
   * dead node from the PG contact points, so the surviving nodes are exactly
   * the current contact point indexes.
   */
  private int reachableNodesRunningApplyWorker() throws Exception {
    int count = 0;
    int nodes = miniCluster.getPostgresContactPoints().size();
    for (int node = 0; node < nodes; node++) {
      try {
        if (workerAliveOnNode(node)) {
          count++;
        }
      } catch (Exception e) {
        LOG.info("node {} not reachable while polling: {}", node, e.getMessage());
      }
    }
    return count;
  }

  /** Number of nodes whose apply worker has connected to the publisher. */
  private int nodesStreamingForSubscription() throws Exception {
    return nodesStreamingFor(SUB_NAME);
  }

  /** Same, for an arbitrary subscription name. */
  private int nodesStreamingFor(String subName) throws Exception {
    int count = 0;
    for (int node = 0; node < NUM_NODES; node++) {
      try (Connection conn = nodeConn(node, SUB_DB)) {
        if (streamingWorkerAlive(conn, subName)) {
          count++;
        }
      }
    }
    return count;
  }

  /** Index of the node running the apply worker, or -1. */
  private int findApplyWorkerNode() throws Exception {
    for (int node = 0; node < NUM_NODES; node++) {
      if (workerAliveOnNode(node)) {
        return node;
      }
    }
    return -1;
  }

  /**
   * Maps a PG contact point index to the tserver's RPC HostAndPort.  Both are
   * keyed by the tserver bind address, which is unique per node.
   */
  private HostAndPort tserverHostPortForNode(int tserverIdx) {
    String host =
        miniCluster.getPostgresContactPoints().get(tserverIdx).getHostName();
    for (HostAndPort hp : miniCluster.getTabletServers().keySet()) {
      if (hp.getHost().equals(host)) {
        return hp;
      }
    }
    throw new IllegalStateException("no tserver found for host " + host);
  }

  /**
   * Forces node tserverIdx's launcher to scan pg_subscription now rather than
   * after DEFAULT_NAPTIME_PER_CYCLE (180s).  ENABLE on an enabled
   * subscription calls ApplyLauncherWakeupAtCommit and leaves it enabled; it
   * still rewrites the pg_subscription row and bumps the catalog version.
   */
  private void wakeLauncherOnNode(int tserverIdx) throws Exception {
    try (Connection conn = nodeConn(tserverIdx, SUB_DB);
         Statement stmt = conn.createStatement()) {
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " ENABLE");
    }
  }

  private void createPublishedTable(String table) throws Exception {
    try (Statement pub = pubConn.createStatement();
         Statement sub = subConn.createStatement()) {
      pub.execute("CREATE TABLE " + table + " (id INT PRIMARY KEY, val TEXT)");
      pub.execute("ALTER TABLE " + table + " REPLICA IDENTITY FULL");
      sub.execute("CREATE TABLE " + table + " (id INT PRIMARY KEY, val TEXT)");
      sub.execute("ALTER TABLE " + table + " REPLICA IDENTITY FULL");
    }
  }

  /** Creates table t, a publication for it, and the subscription on node 0. */
  private void createSingleTableSubscription(boolean copyData)
      throws Exception {
    createPublishedTable("t");
    try (Statement pub = pubConn.createStatement()) {
      pub.execute("CREATE PUBLICATION " + PUB_NAME + " FOR TABLE t");
    }
    try (Statement sub = subConn.createStatement()) {
      sub.execute(createSubscriptionSql(SUB_NAME, PUB_NAME, copyData));
    }
  }

  // ---------------------------------------------------------------------------
  //  Tests
  // ---------------------------------------------------------------------------

  /**
   * Exactly one node may run the apply worker.  Every node's launcher sees the
   * subscription in the cluster-wide pg_subscription, so the others must lose
   * the advisory lock and exit instead of double-applying the stream.
   */
  @Test
  public void testOnlyOneApplyWorkerAcrossNodes() throws Exception {
    createSingleTableSubscription(false /* copyData */);

    TestUtils.waitFor(() -> nodesStreamingForSubscription() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    // Make the other nodes' launchers scan right now instead of napping.
    for (int node = 1; node < NUM_NODES; node++) {
      LOG.info("Waking launcher on node {}", node);
      wakeLauncherOnNode(node);
    }
    Thread.sleep(LOCK_LOSER_SETTLE_MS);

    /*
     * Only one worker may exist at all: the other nodes' launchers probe the
     * subscription's advisory lock, see it held, and never spend a worker
     * slot on it.  A losing worker can appear only when two nodes race for a
     * free lock, which cannot happen while the owner streams, so the raw
     * worker count must hold steady at one.
     */
    for (int i = 0; i < 5; i++) {
      assertEquals("exactly one node may stream for the subscription",
                   1, nodesStreamingForSubscription());
      assertEquals("no other node may run an apply worker at all",
                   1, nodesRunningApplyWorker());
      Thread.sleep(2000);
    }
  }

  /**
   * The advisory lock must not gate tablesync workers: they are per-table
   * children of the apply worker, and locking them would stall initial copy.
   */
  @Test
  public void testTablesyncWorkersNotBlockedByLock() throws Exception {
    createPublishedTable("t1");
    createPublishedTable("t2");
    createPublishedTable("t3");
    try (Statement pub = pubConn.createStatement()) {
      for (String t : new String[] {"t1", "t2", "t3"}) {
        pub.execute("INSERT INTO " + t + " SELECT g, 'v' || g "
                    + "FROM generate_series(1, 2000) g");
      }
      pub.execute("CREATE PUBLICATION " + PUB_NAME
                  + " FOR TABLE t1, t2, t3");
    }
    try (Statement sub = subConn.createStatement()) {
      sub.execute(createSubscriptionSql(SUB_NAME, PUB_NAME, true /* copyData */));
    }

    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    // Track how many tablesync workers we ever see at once, purely as
    // information: the number is capped by max_sync_workers_per_subscription
    // and depends on timing, so it is logged rather than asserted.
    final int[] maxSeen = {0};
    TestUtils.waitFor(() -> {
      try (Connection conn = nodeConn(0, SUB_DB)) {
        maxSeen[0] = Math.max(maxSeen[0], tablesyncWorkerCount(conn, SUB_NAME));
      }
      return allTablesCopied();
    }, COPY_TIMEOUT_MS, POLL_INTERVAL_MS);
    LOG.info("Peak concurrent tablesync workers observed: {}", maxSeen[0]);

    // The real regression guard: every table finished its initial copy, which
    // cannot happen if tablesync workers exit on the advisory lock.
    for (String t : new String[] {"t1", "t2", "t3"}) {
      try (Statement stmt = subConn.createStatement();
           ResultSet rs = stmt.executeQuery("SELECT count(*) FROM " + t)) {
        assertTrue(rs.next());
        assertEquals("initial copy must complete for " + t, 2000, rs.getInt(1));
      }
    }
  }

  /** True once all three tables have left the initial 'i'/'d' states. */
  private boolean allTablesCopied() throws Exception {
    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FROM pg_subscription_rel sr "
             + "JOIN pg_subscription s ON sr.srsubid = s.oid "
             + "WHERE s.subname = '" + SUB_NAME + "' "
             + "AND sr.srsubstate NOT IN ('s', 'r')")) {
      return rs.next() && rs.getInt(1) == 0;
    }
  }

  /**
   * A worker killed abnormally must release the subscription advisory lock, so
   * some node can run the subscription again.  Complements the clean-exit case
   * (DISABLE then ENABLE) in the single-node suite.
   */
  @Test
  public void testApplyWorkerRestartsAfterTerminate() throws Exception {
    createSingleTableSubscription(false /* copyData */);
    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    int ownerNode = -1;
    int oldPid = -1;
    for (int node = 0; node < NUM_NODES && ownerNode < 0; node++) {
      try (Connection conn = nodeConn(node, SUB_DB)) {
        int pid = applyWorkerPid(conn, SUB_NAME);
        if (pid > 0) {
          ownerNode = node;
          oldPid = pid;
        }
      }
    }
    assertTrue("an apply worker must be running somewhere", ownerNode >= 0);
    LOG.info("Terminating apply worker pid {} on node {}", oldPid, ownerNode);

    try (Connection conn = nodeConn(ownerNode, SUB_DB);
         Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT pg_terminate_backend(" + oldPid + ")");
    }

    // A new worker must appear; if the lock leaked with the dead process, no
    // node would ever be able to start one again.
    final int deadPid = oldPid;
    TestUtils.waitFor(() -> {
      for (int node = 0; node < NUM_NODES; node++) {
        try (Connection conn = nodeConn(node, SUB_DB)) {
          int pid = applyWorkerPid(conn, SUB_NAME);
          if (pid > 0 && pid != deadPid) {
            return true;
          }
        }
      }
      return false;
    }, CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    TestUtils.waitFor(() -> nodesStreamingForSubscription() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);
  }

  /**
   * Killing the node that runs the apply worker must hand the subscription to a
   * surviving node.  The advisory lock is session-scoped, so it dies with that
   * node's PG process and another launcher can claim it; a lock that outlived
   * the node would strand the subscription until the node returned.
   */
  @Test
  public void testApplyWorkerFailsOverAfterNodeKill() throws Exception {
    createSingleTableSubscription(false /* copyData */);
    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    int ownerNode = findApplyWorkerNode();
    assertTrue("an apply worker must be running somewhere", ownerNode >= 0);
    HostAndPort victim = tserverHostPortForNode(ownerNode);
    LOG.info("Killing tserver {} (node {}), which runs the apply worker",
             victim, ownerNode);

    miniCluster.killTabletServerOnHostPort(victim);

    // No launcher nudge needed: the survivors' launchers already see the
    // subscription as remotely owned and re-probe its advisory lock every
    // yb_subscription_takeover_probe_interval_ms.
    LOG.info("Waiting for a surviving node to take over the subscription");
    TestUtils.waitFor(() -> reachableNodesRunningApplyWorker() == 1,
                      FAILOVER_TIMEOUT_MS, POLL_INTERVAL_MS);
    LOG.info("Subscription failed over to a surviving node");

    // Leave a healthy cluster behind: the remaining tests expect NUM_NODES.
    restartClusterWithFlags(Collections.<String, String>emptyMap(),
                            Collections.<String, String>emptyMap());
  }

  /**
   * DISABLE issued on a different node than the apply worker's must still stop
   * the worker: no sinval reaches the worker's node, so this relies on the idle
   * loop's periodic subscription re-read.
   */
  @Test
  public void testCrossNodeDisableStopsWorker() throws Exception {
    createSingleTableSubscription(false /* copyData */);

    LOG.info("Waiting for the apply worker to start");
    TestUtils.waitFor(() -> workerAliveOnNode(0),
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    // Let the worker settle into its idle loop so the sinval-driven path
    // (which only fires while transactions are running) is out of play.
    Thread.sleep(3000);

    LOG.info("Issuing DISABLE from node 1");
    try (Connection other = nodeConn(1, SUB_DB);
         Statement stmt = other.createStatement()) {
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
    }

    LOG.info("Waiting for the worker to exit via periodic re-read");
    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 0,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);
  }

  /**
   * DISABLE then DROP (with slot_name = NONE) issued on a different node: the
   * worker exits on the DISABLE re-read, and DROP leaves no worker behind.
   */
  @Test
  public void testCrossNodeDropStopsWorker() throws Exception {
    createSingleTableSubscription(false /* copyData */);
    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);
    Thread.sleep(3000);

    LOG.info("Issuing DROP SUBSCRIPTION from node 2");
    try (Connection other = nodeConn(2, SUB_DB);
         Statement stmt = other.createStatement()) {
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " SET (slot_name = NONE)");
      stmt.execute("DROP SUBSCRIPTION " + SUB_NAME);
    }

    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 0,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    // The subscription is gone, so the @After cleanup has nothing to do.
    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FROM pg_subscription WHERE subname = '"
             + SUB_NAME + "'")) {
      assertTrue(rs.next());
      assertEquals("subscription row must be gone", 0, rs.getInt(1));
    }
  }

  /** Whether pg_subscription reports SUB_NAME enabled, read via a node. */
  private boolean subscriptionEnabledOnNode(int tserverIdx) throws Exception {
    try (Connection conn = nodeConn(tserverIdx, SUB_DB);
         Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT subenabled FROM pg_subscription WHERE subname = '"
             + SUB_NAME + "'")) {
      return rs.next() && rs.getBoolean(1);
    }
  }

  /**
   * A worker that hits an apply error under disable_on_error disables the
   * subscription itself - a catalog write made by a worker, not by DDL.
   * That write runs as a version-bumping DDL transaction
   * (DisableSubscription), so every other node sees the disable promptly:
   * launchers must stop launching for it even though its advisory lock is
   * now free.
   */
  @Test
  public void testDisableOnErrorSeenByAllNodes() throws Exception {
    createPublishedTable("t");
    try (Statement pub = pubConn.createStatement()) {
      pub.execute("CREATE PUBLICATION " + PUB_NAME + " FOR TABLE t");
    }
    try (Statement sub = subConn.createStatement()) {
      sub.execute(createSubscriptionSql(
          SUB_NAME, PUB_NAME, "copy_data = false, disable_on_error = true"));
    }

    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    // Drop the target table, then publish a row for it: the apply fails in
    // logicalrep_rel_open, before any DML executes, which keeps the error
    // independent of how far this branch's apply path goes.
    try (Statement sub = subConn.createStatement()) {
      sub.execute("DROP TABLE t");
    }
    try (Statement pub = pubConn.createStatement()) {
      pub.execute("INSERT INTO t VALUES (1, 'v1')");
    }

    // The worker errors, disables the subscription, and exits; every node
    // must observe the disable.
    for (int node = 0; node < NUM_NODES; node++) {
      final int n = node;
      LOG.info("Waiting for node {} to see the subscription disabled", n);
      TestUtils.waitFor(() -> !subscriptionEnabledOnNode(n),
                        CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);
    }

    // No launcher may start a worker for a disabled subscription, even
    // though its advisory lock is free now.
    Thread.sleep(LOCK_LOSER_SETTLE_MS);
    assertEquals("no worker may run for a disabled subscription",
                 0, nodesRunningApplyWorker());
  }

  /**
   * With more subscriptions than max_logical_replication_workers (default 4),
   * every subscription must still end up with exactly one apply worker.
   *
   * Before the launcher probed the advisory lock, every node launched a
   * worker for every subscription and the losers parked while holding their
   * worker slots.  With subscriptions >= slots that pinned every slot on
   * every node, so at least one subscription could never start and no
   * tablesync worker could ever run - a permanent stall, not a slowdown.
   * With the probe, a node spends slots only on subscriptions it owns, and a
   * subscription that found one node's slots full stays unlocked until
   * another node's probe picks it up.
   */
  @Test
  public void testMoreSubscriptionsThanWorkerSlots() throws Exception {
    final int numSubs = 5;  // > max_logical_replication_workers (4)

    for (int i = 1; i <= numSubs; i++) {
      createPublishedTable("t" + i);
      try (Statement pub = pubConn.createStatement()) {
        pub.execute("CREATE PUBLICATION " + PUB_NAME + "_" + i
                    + " FOR TABLE t" + i);
      }
    }
    for (int i = 1; i <= numSubs; i++) {
      try (Statement sub = subConn.createStatement()) {
        sub.execute(createSubscriptionSql(SUB_NAME + "_" + i,
                                          PUB_NAME + "_" + i,
                                          false /* copyData */));
      }
    }

    // CREATE SUBSCRIPTION wakes only node 0's launcher; nudge the others so
    // whichever subscription node 0 has no slot for is claimed without
    // waiting out their 180s nap (a no-op ENABLE wakes the local launcher).
    for (int node = 1; node < NUM_NODES; node++) {
      try (Connection conn = nodeConn(node, SUB_DB);
           Statement stmt = conn.createStatement()) {
        stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + "_1 ENABLE");
      }
    }

    /*
     * Every subscription must end up claimed by exactly one node.  The pid
     * check (workerAlive) is the readiness signal: received_lsn
     * (streamingWorkerAlive) is first set by a walsender keepalive, which
     * has been observed to lag by minutes when several idle walsenders
     * share a node.
     */
    for (int i = 1; i <= numSubs; i++) {
      final String name = SUB_NAME + "_" + i;
      LOG.info("Waiting for a worker to claim {}", name);
      TestUtils.waitFor(() -> nodesRunningApplyWorkerFor(name) == 1,
                        SLOT_PRESSURE_TIMEOUT_MS, POLL_INTERVAL_MS);
    }

    // And it must hold: exactly one worker per subscription.
    for (int round = 0; round < 3; round++) {
      for (int i = 1; i <= numSubs; i++) {
        assertEquals("exactly one node may run a worker for subscription " + i,
                     1, nodesRunningApplyWorkerFor(SUB_NAME + "_" + i));
      }
      Thread.sleep(2000);
    }
  }

  // ---------------------------------------------------------------------------
  //  Tablesync workers stay with their apply worker; cross-node DROP / REFRESH
  // ---------------------------------------------------------------------------

  /** Some node other than ownerNode. */
  private static int otherNode(int ownerNode) {
    return (ownerNode + 1) % NUM_NODES;
  }

  /** srsubstate of table for SUB_NAME, or null if it is not in the subscription. */
  private String relState(String table) throws Exception {
    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT sr.srsubstate FROM pg_subscription_rel sr "
             + "JOIN pg_subscription s ON sr.srsubid = s.oid "
             + "WHERE s.subname = '" + SUB_NAME + "' "
             + "AND sr.srrelid = '" + table + "'::regclass")) {
      return rs.next() ? rs.getString(1) : null;
    }
  }

  /** Whether the publisher still has a replication slot named slotName. */
  private boolean publisherHasSlot(String slotName) throws Exception {
    try (Statement stmt = pubConn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FROM pg_replication_slots WHERE slot_name = '"
             + slotName + "'")) {
      return rs.next() && rs.getInt(1) > 0;
    }
  }

  /**
   * {apply workers, tablesync workers} running for SUB_NAME on a node.  One
   * query, so both counts come from the same read of that node's worker slots.
   */
  private int[] workerCountsOnNode(int tserverIdx) throws Exception {
    try (Connection conn = nodeConn(tserverIdx, SUB_DB);
         Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FILTER (WHERE relid IS NULL), "
             + "count(*) FILTER (WHERE relid IS NOT NULL) "
             + "FROM pg_stat_subscription WHERE subname = '" + SUB_NAME + "' "
             + "AND pid IS NOT NULL")) {
      assertTrue(rs.next());
      return new int[] {rs.getInt(1), rs.getInt(2)};
    }
  }

  /** Tablesync workers may run only next to an apply worker, on one node. */
  private void assertTablesyncOnlyWithApplyWorker() throws Exception {
    int nodesWithTablesync = 0;
    for (int node = 0; node < NUM_NODES; node++) {
      int[] counts = workerCountsOnNode(node);
      if (counts[1] > 0) {
        nodesWithTablesync++;
        assertTrue("node " + node + " runs a tablesync worker without an "
                   + "apply worker", counts[0] > 0);
      }
    }
    assertTrue("tablesync workers run on " + nodesWithTablesync + " nodes",
               nodesWithTablesync <= 1);
  }

  /**
   * A tablesync worker must not outlive its apply worker.  The SYNCWAIT /
   * CATCHUP handoff is in the owner node's shared memory, and once the apply
   * worker's lock is free another node can take over and start a second
   * tablesync worker for the same table.  The apply worker's exit hook stops
   * its tablesync workers before it releases the lock.
   *
   * An ALWAYS row trigger that sleeps keeps the initial COPY running long
   * enough to kill the apply worker in the middle of it.  A table with a
   * trigger also makes YB COPY use one transaction, so the interrupted COPY
   * rolls back and the retry starts from an empty table.
   */
  @Test
  public void testApplyExitStopsTablesyncWorkers() throws Exception {
    final int numRows = 3000;
    createPublishedTable("t");
    try (Statement pub = pubConn.createStatement();
         Statement sub = subConn.createStatement()) {
      pub.execute("INSERT INTO t SELECT g, 'v' || g "
                  + "FROM generate_series(1, " + numRows + ") g");
      pub.execute("CREATE PUBLICATION " + PUB_NAME + " FOR TABLE t");
      sub.execute("CREATE FUNCTION slow_row() RETURNS trigger LANGUAGE plpgsql "
                  + "AS $$ BEGIN PERFORM pg_sleep(0.01); RETURN NEW; END $$");
      sub.execute("CREATE TRIGGER slow_copy BEFORE INSERT ON t "
                  + "FOR EACH ROW EXECUTE FUNCTION slow_row()");
      sub.execute("ALTER TABLE t ENABLE ALWAYS TRIGGER slow_copy");
      sub.execute(createSubscriptionSql(SUB_NAME, PUB_NAME, true /* copyData */));
    }

    // Wait for the COPY to be under way on the owner node.
    final int[] owner = {-1};
    TestUtils.waitFor(() -> {
      for (int node = 0; node < NUM_NODES; node++) {
        int[] counts = workerCountsOnNode(node);
        if (counts[0] > 0 && counts[1] > 0) {
          owner[0] = node;
          return true;
        }
      }
      return false;
    }, CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);

    int applyPid;
    int syncPid;
    try (Connection conn = nodeConn(owner[0], SUB_DB);
         Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT pid FROM pg_stat_subscription WHERE subname = '" + SUB_NAME
             + "' AND pid IS NOT NULL AND relid IS NOT NULL")) {
      assertTrue(rs.next());
      syncPid = rs.getInt(1);
      applyPid = applyWorkerPid(conn, SUB_NAME);
    }
    LOG.info("Terminating apply worker pid {} on node {} while tablesync "
             + "worker pid {} copies", applyPid, owner[0], syncPid);
    try (Connection conn = nodeConn(owner[0], SUB_DB);
         Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT pg_terminate_backend(" + applyPid + ")");
    }

    // The tablesync worker goes with it, long before its COPY could finish.
    final int deadSyncPid = syncPid;
    TestUtils.waitFor(() -> {
      try (Connection conn = nodeConn(owner[0], SUB_DB);
           Statement stmt = conn.createStatement();
           ResultSet rs = stmt.executeQuery(
               "SELECT count(*) FROM pg_stat_subscription WHERE pid = "
               + deadSyncPid)) {
        return rs.next() && rs.getInt(1) == 0;
      }
    }, CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);
    String state = relState("t");
    assertTrue("the COPY must have been interrupted, state " + state,
               "i".equals(state) || "d".equals(state));

    // Through the restart, possibly on another node, the copy completes and
    // tablesync workers only ever run next to an apply worker.
    TestUtils.waitFor(() -> {
      assertTablesyncOnlyWithApplyWorker();
      String s = relState("t");
      return "s".equals(s) || "r".equals(s);
    }, COPY_TIMEOUT_MS, POLL_INTERVAL_MS);

    try (Statement stmt = subConn.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT count(*) FROM t")) {
      assertTrue(rs.next());
      assertEquals("the retried COPY must load every row once",
                   numRows, rs.getInt(1));
    }
  }

  /**
   * DROP SUBSCRIPTION stops workers only on its own node and then drops the
   * slot with WAIT.  An apply worker on another node exits only once it sees
   * the DROP committed, so that WAIT used to hang.  DROP of an enabled
   * subscription therefore fails on every node.  Right after DISABLE, DROP
   * from another node must succeed: the owner exits on its own once it sees
   * the DISABLE, and the WAIT waits only for that.
   */
  @Test
  public void testDropRequiresDisable() throws Exception {
    createSingleTableSubscription(false /* copyData */);
    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);
    int owner = findApplyWorkerNode();
    int other = otherNode(owner);

    for (int node : new int[] {owner, other}) {
      try (Connection conn = nodeConn(node, SUB_DB);
           Statement stmt = conn.createStatement()) {
        runInvalidQuery(stmt, "DROP SUBSCRIPTION " + SUB_NAME,
                        "while it is enabled");
      }
    }
    assertEquals("the failed DROPs must leave the worker running",
                 1, nodesRunningApplyWorker());

    try (Connection conn = nodeConn(other, SUB_DB);
         Statement stmt = conn.createStatement()) {
      stmt.execute("ALTER SUBSCRIPTION " + SUB_NAME + " DISABLE");
      long start = System.currentTimeMillis();
      stmt.execute("DROP SUBSCRIPTION " + SUB_NAME);
      LOG.info("DROP on node {} right after DISABLE took {} ms (owner node {})",
               other, System.currentTimeMillis() - start, owner);
    }

    assertTrue("DROP must drop the slot on the publisher",
               !publisherHasSlot(SUB_NAME));
    Thread.sleep(LOCK_LOSER_SETTLE_MS);
    assertEquals("no node may run a worker for a dropped subscription",
                 0, nodesRunningApplyWorker());
  }

  /**
   * REFRESH that removes a table still syncing stops its tablesync worker,
   * which works only on the owner node, so elsewhere it must fail fast.
   * Removing a table that finished syncing touches no worker and works from
   * any node.
   */
  @Test
  public void testCrossNodeRefreshRemovingTables() throws Exception {
    createPublishedTable("t1");
    createPublishedTable("t2");
    try (Statement pub = pubConn.createStatement();
         Statement sub = subConn.createStatement()) {
      for (String t : new String[] {"t1", "t2"}) {
        pub.execute("INSERT INTO " + t + " SELECT g, 'v' || g "
                    + "FROM generate_series(1, 100) g");
      }
      pub.execute("CREATE PUBLICATION " + PUB_NAME + " FOR TABLE t1, t2");
      // t2's COPY hits this row's key and retries forever, so t2 stays
      // in DATASYNC.
      sub.execute("INSERT INTO t2 VALUES (1, 'conflict')");
      sub.execute(createSubscriptionSql(SUB_NAME, PUB_NAME, true /* copyData */));
    }
    TestUtils.waitFor(() -> nodesRunningApplyWorker() == 1,
                      CROSS_NODE_TIMEOUT_MS, POLL_INTERVAL_MS);
    TestUtils.waitFor(() -> {
      String s = relState("t1");
      return "s".equals(s) || "r".equals(s);
    }, COPY_TIMEOUT_MS, POLL_INTERVAL_MS);
    String t2State = relState("t2");
    assertTrue("t2 must still be syncing, state " + t2State,
               "i".equals(t2State) || "d".equals(t2State));

    int owner = findApplyWorkerNode();
    int other = otherNode(owner);
    String refresh = "ALTER SUBSCRIPTION " + SUB_NAME + " REFRESH PUBLICATION";

    try (Statement pub = pubConn.createStatement();
         Connection otherConn = nodeConn(other, SUB_DB);
         Statement otherStmt = otherConn.createStatement()) {
      pub.execute("ALTER PUBLICATION " + PUB_NAME + " DROP TABLE t1");
      otherStmt.execute(refresh);
      assertEquals("t1 must be removed", null, relState("t1"));

      pub.execute("ALTER PUBLICATION " + PUB_NAME + " DROP TABLE t2");
      runInvalidQuery(otherStmt, refresh, "while the table is being synchronized");
      assertTrue("the failed REFRESH must keep t2", relState("t2") != null);
    }

    try (Connection ownerConn = nodeConn(owner, SUB_DB);
         Statement ownerStmt = ownerConn.createStatement()) {
      ownerStmt.execute(refresh);
    }
    assertEquals("t2 must be removed", null, relState("t2"));
  }
}
