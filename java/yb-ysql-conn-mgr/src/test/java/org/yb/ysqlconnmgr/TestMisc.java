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

import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import java.util.Collections;
import java.util.HashSet;
import java.util.Properties;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;

import com.google.common.collect.ImmutableMap;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestMisc extends BaseYsqlConnMgr {

  private final static String ERROR_YBTSERVERKEY_AUTH_EXPECTED =
      "FATAL: yb_use_tserver_key_auth can only be set if the connection is made over " +
      "unix domain socket";

  private final static String GET_BACKEND_TYPE_QUERY =
      "SELECT backend_type FROM pg_stat_activity WHERE pid = %d";

  public void testBackendTypeForConn(Connection conn, String exp_backend_type) {
    try (Statement stmt = conn.createStatement()) {
      int processId = getProcessId(stmt);
      assertNotEquals("Failed to obtain the process ID.", processId, -1);

      ResultSet rs = stmt.executeQuery(String.format(GET_BACKEND_TYPE_QUERY, processId));
      assertTrue("No row found in pg_stat_activity table with the pid " + processId, rs.next());
      assertEquals("Got wrong backend type", rs.getString("backend_type"), exp_backend_type);

      // Check that there's no more rows
      if (rs.next()) {
        fail("Multiple rows found in pg_stat_activity table with the pid " + processId);
      }
    } catch (SQLException e) {
      LOG.error("Got SQL Exception while fetching backend type", e);
      fail();
    }
  }

  private int getProcessId(Statement stmt) {
    try (ResultSet rs = stmt.executeQuery("SELECT pg_backend_pid();")) {
      if (rs.next()) {
        return rs.getInt(1);
      }
    } catch (SQLException e) {
      LOG.error("Error fetching process ID ", e);
    }

    return -1;
  }

  public void assertStickyStateWithSequences(boolean expectedSticky) throws Exception{
    ResultSet rs;
     try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
                    Statement stmt = conn.createStatement()) {
      assertConnectionStickyState(stmt, false);

      // Create sequence and call nextval to make session sticky.
      stmt.execute("CREATE sequence my_seq");
      rs = stmt.executeQuery("SELECT nextval('my_seq')");
      assertTrue(rs.next());
      assertEquals(1, rs.getLong(1));

      assertConnectionStickyState(stmt, expectedSticky);
    }
  }

  @Test
  public void testBackendType() throws Exception {
    testBackendTypeForConn(
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .connect(),
        "client backend");
    testBackendTypeForConn(
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(),
        "yb-conn-mgr worker connection");
  }

  @Test
  public void testCreateIndex() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
         Statement statement = connection.createStatement()) {
      final String tableName = "t";
      final int numRows = 10;
      statement.execute(String.format("CREATE TABLE %s (id serial PRIMARY KEY, i int)", tableName));
      statement.execute(String.format("INSERT INTO %s SELECT g, -g FROM generate_series(1, %d) g",
                                      tableName, numRows));
      statement.execute(String.format("CREATE INDEX ON %s (i DESC)", tableName));
      // TODO(jason): add verification that this is an index scan.
      ResultSet rs = statement.executeQuery(String.format("SELECT * FROM %s ORDER BY i DESC",
                                                          tableName));
      for (int i = 1; i <= numRows; ++i) {
        assertTrue(rs.next());
        assertEquals(rs.getInt("id"), i);
      }
    }
  }

  // GH #19049: If template1 database is used for control connection, 'CREATE DATABASE'
  // query fails. This test ensures that a proper database is used for
  // creating control connection.
  @Test
  public void testCreateDb() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement statement = connection.createStatement()) {
      statement.execute("CREATE DATABASE db1");
    } catch (Exception e)
    {
      LOG.error("Unable to create database", e);
      fail();
    }
  }

  @Test
  public void testLargePacket() throws Exception {
    String CREATE_TABLE_SQL = "CREATE TABLE IF NOT EXISTS my_table"
        + " (ID serial PRIMARY KEY, name TEXT NOT NULL, age INT)";

    StringBuilder insertQuery = new StringBuilder(
        "INSERT INTO my_table (name, age) VALUES ");

    for (int i = 1; i <= 1000; ++i) {
      insertQuery.append("('Person', ").append(20 + i).append(")");
      if (i < 1000) {
        insertQuery.append(",");
      }
    }

    try (Connection connection =
            getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .withUser("yugabyte")
                                  .withPassword("yugabyte")
                                  .withPreferQueryMode("simple")
                                  .connect();
        Statement stmt = connection.createStatement()) {

      stmt.execute(String.format(CREATE_TABLE_SQL));

      // Insert query hangs if ysql conn mgr is unable to process large packet.
      int rowsAffected = stmt.executeUpdate(insertQuery.toString());
      assertEquals(1000, rowsAffected);
    } catch (Exception e) {
      LOG.error("Unable to execute large queries ", e);
      fail();
    }
  }

  // Tcp client can't set itself as a Ysql Connection Manager.
  @Test
  public void testNegSetYsqlConnMgr() throws Exception {
    Properties props = new Properties();
    props.put("options", String.format("-c %s=%s -c %s=%s",
        "yb_use_tserver_key_auth", "true",
        "yb_is_client_ysqlconnmgr", "true"));

    try (Connection conn =
                getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
                                      .connect(props)) {
      fail("Did not expected the connection to be successfully established");
    } catch (Exception e) {
      assertEquals("Got wrong error message",
          e.getMessage(), ERROR_YBTSERVERKEY_AUTH_EXPECTED);
    }
  }

  /**
   * Test creates, drops and creates same index. If ysql connection manager is enabled,
   * the second create index can occur at different backend and if by that time tserver
   * heartbeat RPC is not triggered, the shared memory will not be updated with bumped up
   * catalog version causing Catalog Version Mismatch error.
   */
  @Test
  public void testCreateDropIndex() throws Exception {
    String query = "CREATE TABLE sample_table(k INT PRIMARY KEY, v INT, v2 INT)";
    try (Connection connection = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .withPassword("yugabyte")
                    .connect();
              Statement statement = connection.createStatement()) {
      statement.execute(query);
      query = "CREATE INDEX sample_table_v_idx ON sample_table(v ASC)";
      statement.execute(query);

      // There are 10 rows with v IS NOT NULL, 100 rows with v IS NULL.
      query = "INSERT INTO sample_table SELECT i, NULL, i + 1000 FROM generate_series(1, 100) i";
      statement.execute(query);
      query = "INSERT INTO sample_table SELECT i, i, i + 1000 FROM generate_series(101, 110) i";
      statement.execute(query);

      query = "DROP INDEX sample_table_v_idx";
      statement.execute(query);
      query = "CREATE INDEX sample_table_v_idx ON sample_table(v DESC)";
      statement.execute(query);
    }
  }

  @Test
  public void testStickySuperuserConns() throws Exception {
    ResultSet rs;

    try (Connection conn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
        Statement stmt = conn.createStatement()) {
      // Sticky superuser flag is not yet enabled, connection should not
      // be sticky. Assert by verifying set of unique backend pids.
      assertTrue(verifySessionParameterValue(stmt, "is_superuser", "on"));
      assertConnectionStickyState(stmt, false);
    }

    // Enable sticky superuser connections and restart the cluster.
    enableStickySuperuserConnsAndRestartCluster();

    // Create only one superuser logical connection for the test.
    Connection suConn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
    Statement suStmt = suConn.createStatement();

    // Sticky superuser flag is enabled, connection should be sticky.
    assertTrue(verifySessionParameterValue(suStmt, "is_superuser", "on"));
    assertConnectionStickyState(suStmt, true);

    // Create a non-superuser to verify they do not have stickiness.
    suStmt.execute("CREATE ROLE test_role LOGIN");

    // Create only one test user logical connection for the test.
    Connection testConn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser("test_role")
        .connect();
    Statement testStmt = testConn.createStatement();

    assertTrue(verifySessionParameterValue(testStmt, "is_superuser", "off"));
    assertConnectionStickyState(testStmt, false);

    // Alter test_role to now be a superuser and verify stickiness.
    suStmt.execute("ALTER ROLE test_role SUPERUSER");

    assertConnectionStickyState(suStmt, true);

    // The most important part of the test - verify that testConn remains
    // sticky after removal of superuser privileges.
    suStmt.execute("ALTER ROLE test_role NOSUPERUSER");

    assertConnectionStickyState(suStmt, true);

    testConn.close();
    suConn.close();
  }

  @Test
  public void testStickySequence() throws Exception {
    // Default settings with ysql_conn_mgr_sequence_support_mode = "pooled_without_curval_lastval".
    // This will not make connection sticky.
    assertStickyStateWithSequences(false);

    // Restarting cluster with
    // ysql_conn_mgr_sequence_support_mode = "pooled_with_curval_lastval".
    // This will not make connection sticky.
    restartClusterWithFlags(
        Collections.emptyMap(),
        Collections.singletonMap(
            "ysql_conn_mgr_sequence_support_mode",
            "pooled_with_curval_lastval"
        )
    );
    assertStickyStateWithSequences(false);

    // ysql_conn_mgr_sequence_support_mode = "session".
    // This will make connection sticky.
    restartClusterWithFlags(Collections.emptyMap(),
                            Collections.singletonMap("ysql_conn_mgr_sequence_support_mode",
                                                     "session"));
    assertStickyStateWithSequences(true);
  }

  // The query string could be either SELECT currval('sequence_name') or SELECT lastval()
  public void testSequenceFunctions(String query) throws Exception {
      ResultSet rs;
      // ysql_conn_mgr_sequence_support_mode = "pooled_without_curval_lastval"
      // Not supported
      try (Connection conn = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
              .connect();
          Statement stmt = conn.createStatement()) {

          stmt.execute("CREATE SEQUENCE my_seq");

          rs = stmt.executeQuery("SELECT nextval('my_seq')");
          assertTrue(rs.next());
          assertEquals(1, rs.getLong(1));

          try {
              stmt.executeQuery(query);
              fail("Expected SQLException to be thrown");
          } catch (SQLException e) {
              String errMsg = "not supported for session created by connection manager";
              assertTrue(e.getMessage().contains(errMsg));
          }
      }

      // ysql_conn_mgr_sequence_support_mode = "pooled_with_curval_lastval"
      // Supported.
      restartClusterWithFlags(Collections.emptyMap(),
                          Collections.singletonMap("ysql_conn_mgr_sequence_support_mode",
                                                    "pooled_with_curval_lastval"));
      try (Connection conn = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
              .connect();
          Statement stmt = conn.createStatement()) {

          stmt.execute("CREATE SEQUENCE my_seq");

          rs = stmt.executeQuery("SELECT nextval('my_seq')");
          assertTrue(rs.next());
          assertEquals(1, rs.getLong(1));

          rs = stmt.executeQuery(query);
          assertTrue(rs.next());
          assertEquals(1, rs.getLong(1));
      }

      // ysql_conn_mgr_sequence_support_mode = "session"
      // Supported
      restartClusterWithFlags(Collections.emptyMap(),
                          Collections.singletonMap("ysql_conn_mgr_sequence_support_mode",
                                                    "session"));

      try (Connection conn = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
              .connect();
          Statement stmt = conn.createStatement()) {

          stmt.execute("CREATE SEQUENCE my_seq");

          rs = stmt.executeQuery("SELECT nextval('my_seq')");
          assertTrue(rs.next());
          assertEquals(1, rs.getLong(1));

          rs = stmt.executeQuery(query);
          assertTrue(rs.next());
          assertEquals(1, rs.getLong(1));
      }
  }

  @Test
  public void testCurrvalErrorOut() throws Exception {
    testSequenceFunctions("SELECT currval('my_seq')");
  }

  @Test
  public void testLastvalErrorOut() throws Exception {
    testSequenceFunctions("SELECT lastval()");
  }
}
