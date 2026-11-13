// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestReplicationOriginStickiness extends BaseYsqlConnMgr {
  private static final String ORIGIN = "sticky_origin";
  private static final String SETUP = "SELECT pg_replication_origin_session_setup('%s')";
  private static final String RESET = "SELECT pg_replication_origin_session_reset()";
  private static final String SETUP_SHARED =
      "SELECT yb_replication_origin_session_setup_shared('%s')";
  private static final String RESET_SHARED =
      "SELECT yb_replication_origin_session_reset_shared()";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
  }

  @Before
  public void createOrigin() throws Exception {
    try (Connection conn = connectToPostgres();
         Statement stmt = conn.createStatement()) {
      stmt.execute(String.format(
          "SELECT pg_replication_origin_create('%s') "
              + "WHERE pg_replication_origin_oid('%s') IS NULL",
          ORIGIN, ORIGIN));
    }
  }

  private void setup(Statement stmt) throws Exception {
    stmt.execute(String.format(SETUP, ORIGIN));
  }

  private boolean isSetup(Statement stmt) throws Exception {
    try (ResultSet rs = stmt.executeQuery("SELECT pg_replication_origin_session_is_setup()")) {
      assertTrue(rs.next());
      return rs.getBoolean(1);
    }
  }

  private void waitForBackendExit(int pid) throws Exception {
    try (Connection conn = connectToPostgres();
         Statement stmt = conn.createStatement()) {
      TestUtils.waitFor(() -> queryInt(stmt,
          "SELECT count(*) FROM pg_stat_activity WHERE pid = " + pid) == 0, 30000);
    }
  }

  // pg_replication_origin_session_setup: a second client cannot get the only backend.
  // pg_replication_origin_session_reset: the second client is served.
  @Test
  public void testSetupReset() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      assertQueuedClientServed();

      setup(stmt);
      QueuedClient client = startQueuedClient();
      client.assertBlocked();
      assertTrue(isSetup(stmt));

      stmt.execute(RESET);
      client.assertServed();
      assertFalse(isSetup(stmt));
    }
  }

  // yb_replication_origin_session_setup_shared: the second client stays blocked.
  // yb_replication_origin_session_reset_shared: the second client is served.
  @Test
  public void testSharedSetupReset() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute(String.format(SETUP_SHARED, ORIGIN));
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute(RESET_SHARED);
      client.assertServed();
      assertFalse(isSetup(stmt));
    }
  }

  // Set up the origin in a transaction that is rolled back: the origin stays set up and the
  // second client stays blocked. Reset: the second client is served.
  @Test
  public void testSetupRolledBack() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      setup(stmt);
      stmt.execute("ROLLBACK");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();
      assertTrue(isSetup(stmt));

      stmt.execute(RESET);
      client.assertServed();
    }
  }

  // BEGIN, set up the origin, ROLLBACK, then disconnect. The next client gets a new backend, has
  // no origin set up and can set up the same origin.
  @Test
  public void testSetupRolledBackThenDisconnect() throws Exception {
    testSetupInTransactionThenDisconnect(true /* rollback */);
  }

  // Same as above, but disconnect with the transaction still open, so Conn Mgr rolls it back.
  @Test
  public void testSetupInOpenTransactionThenDisconnect() throws Exception {
    testSetupInTransactionThenDisconnect(false /* rollback */);
  }

  private void testSetupInTransactionThenDisconnect(boolean rollback) throws Exception {
    int pid;
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      pid = queryInt(stmt, "SELECT pg_backend_pid()");
      stmt.execute("BEGIN");
      setup(stmt);
      if (rollback) {
        stmt.execute("ROLLBACK");
      }
    }
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      assertNotEquals(pid, queryInt(stmt, "SELECT pg_backend_pid()"));
      assertFalse(isSetup(stmt));
      waitForBackendExit(pid);
      setup(stmt);
      stmt.execute(RESET);
    }
  }

  // Exclude replication_origin, then set up and reset the origin, exclusive and then shared: the
  // second client stays blocked until the first client disconnects.
  @Test
  public void testExclusionBeforeSetup() throws Exception {
    try {
      testExclusionBeforeSetup(String.format(SETUP, ORIGIN), RESET);
      testExclusionBeforeSetup(String.format(SETUP_SHARED, ORIGIN), RESET_SHARED);
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  private void testExclusionBeforeSetup(String setup, String reset) throws Exception {
    QueuedClient client;
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      setReversibleStickinessExclusions(stmt, "replication_origin");
      stmt.execute(setup);
      stmt.execute(reset);
      client = startQueuedClient();
      client.assertBlocked();
    }
    client.assertServed();
  }

  // Set up the origin, then exclude replication_origin at runtime and reset: the second client
  // stays blocked until the first client disconnects.
  @Test
  public void testExclusionAddedWhileSetup() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setup(stmt);
        setReversibleStickinessExclusions(stmt, "replication_origin");
        stmt.execute(RESET);
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Exclude every other source, then set up and reset the origin: the second client is served.
  @Test
  public void testOtherExclusionDoesNotLatchReplicationOrigin() throws Exception {
    try {
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, otherStickySources("replication_origin"));
        setup(stmt);
        stmt.execute(RESET);
        assertQueuedClientServed();
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }
}
