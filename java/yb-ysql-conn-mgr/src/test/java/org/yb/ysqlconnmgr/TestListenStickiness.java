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

import static org.yb.AssertionWrappers.assertEquals;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.BasePgListenNotifyTest;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestListenStickiness extends BaseYsqlConnMgr {
  private static final String PAYLOAD = "test_payload";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    BasePgListenNotifyTest.addListenNotifyFlags(flagMap);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    BasePgListenNotifyTest.addListenNotifyFlags(flagMap);
    return flagMap;
  }

  @Before
  public void waitForNotificationsTable() throws Exception {
    try (Connection conn = connectToPostgres()) {
      BasePgListenNotifyTest.waitForNotificationsTableReady(conn, getConnectionBuilder());
    }
  }

  private int listeningChannels(Statement stmt) throws Exception {
    return queryInt(stmt, "SELECT count(*) FROM pg_listening_channels()");
  }

  // LISTEN: a second client cannot get the only backend. UNLISTEN: the second client is served.
  @Test
  public void testListenUnlisten() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      assertQueuedClientServed();

      stmt.execute("LISTEN a");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("UNLISTEN a");
      client.assertServed();
      assertEquals(0, listeningChannels(stmt));
    }
  }

  // LISTEN on two channels and UNLISTEN one: the second client stays blocked. UNLISTEN the
  // other: the second client is served.
  @Test
  public void testUnlistenOneOfTwoChannels() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("LISTEN a");
      stmt.execute("LISTEN b");
      stmt.execute("UNLISTEN a");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();

      stmt.execute("UNLISTEN b");
      client.assertServed();
    }
  }

  // LISTEN in a transaction that is rolled back: the second client is served and no channel is
  // listened.
  @Test
  public void testListenRolledBack() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("LISTEN a");
      stmt.execute("ROLLBACK");
      assertQueuedClientServed();
      assertEquals(0, listeningChannels(stmt));
    }
  }

  // LISTEN, then UNLISTEN in a transaction that is rolled back: the second client stays blocked.
  // UNLISTEN again: the second client is served.
  @Test
  public void testUnlistenRolledBack() throws Exception {
    try (Connection conn = connectToConnMgr();
         Statement stmt = conn.createStatement()) {
      stmt.execute("LISTEN a");
      stmt.execute("BEGIN");
      stmt.execute("UNLISTEN a");
      stmt.execute("ROLLBACK");
      QueuedClient client = startQueuedClient();
      client.assertBlocked();
      assertEquals(1, listeningChannels(stmt));

      stmt.execute("UNLISTEN a");
      client.assertServed();
    }
  }

  // LISTEN and UNLISTEN on one client. A second client gets the same backend and sees no
  // listened channels. A NOTIFY on the channel reaches neither client.
  @Test
  public void testNextClientSeesNoChannels() throws Exception {
    try (Connection conn = connectToConnMgr();
         Connection nextConn = connectToConnMgr();
         Connection notifyConn = connectToPostgres();
         Statement stmt = conn.createStatement();
         Statement nextStmt = nextConn.createStatement();
         Statement notifyStmt = notifyConn.createStatement()) {
      stmt.execute("LISTEN a");
      int pid = queryInt(stmt, "SELECT pg_backend_pid()");
      stmt.execute("UNLISTEN a");

      assertEquals(pid, queryInt(nextStmt, "SELECT pg_backend_pid()"));
      assertEquals(0, listeningChannels(nextStmt));

      notifyStmt.execute("NOTIFY a, '" + PAYLOAD + "'");
      BasePgListenNotifyTest.waitAndAssertNoNotifications(conn,
          "Unlistened client should not receive notifications");
      BasePgListenNotifyTest.waitAndAssertNoNotifications(nextConn,
          "Next client should not receive notifications");
    }
  }

  // Exclude listen, then LISTEN and UNLISTEN: the second client stays blocked until the first
  // client disconnects.
  @Test
  public void testExclusionBeforeListen() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "prepare,listen");
        stmt.execute("LISTEN a");
        stmt.execute("UNLISTEN a");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // LISTEN, then exclude listen at runtime and UNLISTEN: the second client stays blocked until
  // the first client disconnects.
  @Test
  public void testExclusionAddedWhileListening() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN a");
        setReversibleStickinessExclusions(stmt, "listen");
        stmt.execute("UNLISTEN a");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();
    } finally {
      clearReversibleStickinessExclusions();
    }
  }

  // Exclude listen, LISTEN, then clear the list and UNLISTEN: the second client stays blocked
  // until the first client disconnects. A new client that listens and unlistens does not block
  // others.
  @Test
  public void testExclusionRemovedWhileListening() throws Exception {
    try {
      QueuedClient client;
      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        setReversibleStickinessExclusions(stmt, "listen");
        stmt.execute("LISTEN a");
        setReversibleStickinessExclusions(stmt, "");
        stmt.execute("UNLISTEN a");
        client = startQueuedClient();
        client.assertBlocked();
      }
      client.assertServed();

      try (Connection conn = connectToConnMgr();
           Statement stmt = conn.createStatement()) {
        stmt.execute("LISTEN a");
        stmt.execute("UNLISTEN a");
        assertQueuedClientServed();
      }
    } finally {
      clearReversibleStickinessExclusions();
    }
  }
}
