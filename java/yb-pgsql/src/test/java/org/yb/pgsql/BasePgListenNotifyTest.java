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

import static org.yb.AssertionWrappers.assertTrue;

import com.yugabyte.PGConnection;
import com.yugabyte.PGNotification;
import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import org.junit.Before;

import org.yb.client.TestUtils;
import org.yb.util.Timeouts;

/**
 * Base class for LISTEN/NOTIFY tests. Sets the required flags and waits for the
 * pg_yb_notifications table to be created before each test.
 *
 * <p>Static utility methods ({@link #addListenNotifyFlags},
 * {@link #waitForNotificationsTableReady}, {@link #waitForCondition}) are
 * exposed so that test classes outside this hierarchy (e.g.
 * {@link TestPgRegressPgAsync}) can reuse the setup logic without inheriting
 * from this class.
 */
public class BasePgListenNotifyTest extends BasePgSQLTest {

  /**
   * Adds LISTEN/NOTIFY flags to the given map.
   */
  public static void addListenNotifyFlags(Map<String, String> flagMap) {
    flagMap.put("ysql_yb_enable_listen_notify", "true");
  }

  /**
   * Waits for the {@code yb_system} database and the
   * {@code pg_yb_notifications} table to exist.
   */
  public static void waitForNotificationsTableReady(
      Connection defaultConn, ConnectionBuilder connBuilder) throws Exception {
    waitForCondition(defaultConn,
        "SELECT CASE WHEN EXISTS ("
            + "SELECT 1 FROM pg_database WHERE datname = 'yb_system'"
            + ") THEN 1 ELSE 0 END");
    Connection ybSystemConn = connBuilder.withDatabase("yb_system").connect();
    try {
      waitForCondition(ybSystemConn,
          "SELECT CASE WHEN EXISTS ("
              + "SELECT 1 FROM pg_class"
              + " WHERE relname = 'pg_yb_notifications'"
              + " AND relkind = 'r'"
              + " AND relnamespace = 2200"
              + ") THEN 1 ELSE 0 END");
    } finally {
      ybSystemConn.close();
    }
  }

  /**
   * Polls until the given SQL statement returns {@code 1}.
   */
  protected static void waitForCondition(Connection conn, String stmt) throws Exception {
    Statement statement = conn.createStatement();
    TestUtils.waitFor(() -> {
      Row row = getSingleRow(statement, stmt);
      return row.getInt(0) == 1;
    }, Timeouts.adjustTimeoutSecForBuildType(120 * 1000));
  }

  /**
   * Polls the given connection for notifications until one matching the
   * expected channel and payload is found. Returns all notifications received.
   */
  public static List<PGNotification> waitForNotification(Connection connection,
      String expectedChannel, String expectedPayload) throws Exception {
    List<PGNotification> allNotifications = new ArrayList<>();
    PGConnection pgConn = connection.unwrap(PGConnection.class);
    boolean found = false;
    try (Statement stmt = connection.createStatement()) {
      for (int attempt = 0; attempt < 75 && !found; attempt++) {
        stmt.execute("SELECT 1");
        PGNotification[] notifications = pgConn.getNotifications();
        if (notifications != null) {
          for (PGNotification n : notifications) {
            allNotifications.add(n);
            if (n.getName().equals(expectedChannel)
                && n.getParameter().equals(expectedPayload)) {
              found = true;
            }
          }
        }
        if (!found) {
          Thread.sleep(200);
        }
      }
    }
    assertTrue("Expected to receive notification on channel '" + expectedChannel
        + "' with payload '" + expectedPayload + "'", found);
    return allNotifications;
  }

  /**
   * Waits briefly and asserts that no notifications are pending on the
   * given connection.
   */
  public static void waitAndAssertNoNotifications(Connection conn, String msg) throws Exception {
    Thread.sleep(5000);
    try (Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT 1");
      PGConnection pgConn = conn.unwrap(PGConnection.class);
      PGNotification[] n = pgConn.getNotifications();
      assertTrue(msg, n == null || n.length == 0);
    }
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    addListenNotifyFlags(flagMap);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    addListenNotifyFlags(flagMap);
    return flagMap;
  }

  @Before
  public void waitForNotificationsTable() throws Exception {
    waitForNotificationsTableReady(connection, getConnectionBuilder());
  }
}
