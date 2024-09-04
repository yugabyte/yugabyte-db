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
import java.util.Properties;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;

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
}
