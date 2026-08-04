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
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import java.util.HashMap;
import java.util.Properties;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestPreparedStatements extends BaseYsqlConnMgr {
  // It is required to test prepared statements in case of many to one mapping of logical and
  // physical connections by Ysql Connection Manager.
  // Thus the number of client connections should be significantly more than the pool size (20 in
  // this case). The pool size should not be further decreased.
  private static final int NUMBER_OF_CLIENTS = 100;
  private static final String tableName = "TEST_TABLE_PROTO_PREP_STMT";
  private static final String sqlProtocolPreparedStmt = "SELECT * from " + tableName;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_max_conns_per_db", Integer.toString(NUMBER_OF_CLIENTS / 5));
        put("ysql_conn_mgr_stats_interval", Integer.toString(STATS_UPDATE_INTERVAL));
      }
    };
    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  @Test
  public void testInsert() throws Exception {
    Connection[] connection = new Connection[NUMBER_OF_CLIENTS];

    try {
      // Create the test table
      getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.DEFAULT).connect()
          .createStatement().execute(
              "CREATE TABLE IF NOT EXISTS TEST_TABLE_INSERT (name TEXT)");

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        connection[i] = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
      }

      PreparedStatement[] statement = new PreparedStatement[NUMBER_OF_CLIENTS];
      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        statement[i] =
            connection[i].prepareStatement("INSERT INTO TEST_TABLE_INSERT (name) VALUES (?)");
        statement[i].setString(1, String.format("John_%d", i));
      }

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        int rowsAffected = statement[i].executeUpdate();
        assertEquals(1, rowsAffected);
      }

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        ResultSet resultSet = connection[i].createStatement().executeQuery(
            String.format("SELECT * FROM TEST_TABLE_INSERT WHERE name = 'John_%d'", i));
        assertNotNull(resultSet);
        assertTrue(resultSet.next());
        assertEquals(String.format("John_%d", i), resultSet.getString("name"));
        // Validate that only one row is returned.
        assertFalse("Not expecting more than one row in the resultSet", resultSet.next());
      }
    } finally {
      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        if (connection[i] != null && !connection[i].isClosed())
          connection[i].close();
      }
    }
  }

  @Test
  public void testUpdate() throws Exception {
    Connection[] connection = new Connection[NUMBER_OF_CLIENTS];

    try {
      // Create the test table
      getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.DEFAULT).connect()
          .createStatement().execute(
              "CREATE TABLE IF NOT EXISTS TEST_TABLE_UPDATE (name TEXT)");

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        connection[i] = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
      }

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++)
        connection[i].createStatement().execute(
            String.format("INSERT INTO TEST_TABLE_UPDATE (name) VALUES ('John_Original_%d')", i));

      PreparedStatement[] statement = new PreparedStatement[NUMBER_OF_CLIENTS];
      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        statement[i] =
            connection[i].prepareStatement("UPDATE TEST_TABLE_UPDATE SET name = ? WHERE name = ?");
        statement[i].setString(1, String.format("John_Updated_%d", i));
        statement[i].setString(2, String.format("John_Original_%d", i));
      }

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        int rowsAffected = statement[i].executeUpdate();
        assertEquals(1, rowsAffected);
      }

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        ResultSet resultSet = connection[i].createStatement().executeQuery(
            String.format("SELECT * FROM TEST_TABLE_UPDATE WHERE name = 'John_Updated_%d'", i));
        assertNotNull(resultSet);
        assertTrue(resultSet.next());
        assertEquals(String.format("John_Updated_%d", i), resultSet.getString("name"));
        // Validate that only one row is returned.
        assertFalse("Not expecting more than one row in the resultSet", resultSet.next());
      }
    } finally {
      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        if (connection[i] != null && !connection[i].isClosed())
          connection[i].close();
      }
    }
  }

  @Test
  public void testDelete() throws Exception {
    Connection[] connection = new Connection[NUMBER_OF_CLIENTS];

    try {
      // Create the test table
      getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.DEFAULT).connect()
          .createStatement().execute(
              "CREATE TABLE IF NOT EXISTS TEST_TABLE_DELETE (name TEXT)");

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        connection[i] = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
      }

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        connection[i].createStatement().execute(
            String.format("INSERT INTO TEST_TABLE_DELETE (name) VALUES ('John_delete_%d')", i));
      }

      PreparedStatement[] statement = new PreparedStatement[NUMBER_OF_CLIENTS];
      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        statement[i] =
            connection[i].prepareStatement("DELETE FROM TEST_TABLE_DELETE WHERE name = ?");
        statement[i].setString(1, String.format("John_delete_%d", i));
      }

      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        int rowsAffected = statement[i].executeUpdate();
        assertEquals(1, rowsAffected);

        ResultSet resultSet = connection[i].createStatement().executeQuery(
            String.format("SELECT * FROM TEST_TABLE_DELETE WHERE name = 'John_delete_%d'", i));
        assertNotNull(resultSet);
        assertFalse(resultSet.next());
      }
    } finally {
      for (int i = 0; i < NUMBER_OF_CLIENTS; i++) {
        if (connection[i] != null && !connection[i].isClosed())
          connection[i].close();
      }
    }
  }

  // There are two ways of using prepared statements
  // - Protocol level prepared statements
  // - PREPARE and EXECUTE query
  // testSqlPrepareStatement tests the working of PREPARE and EXECUTE queries
  // and checks that the connection gets sticky when PREPARE query is executed.
  @Test
  public void testSqlPrepareStatement() throws Exception {
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {

      stmt.execute("CREATE TABLE test_table(id int)");
      stmt.execute("INSERT INTO test_table values (1), (2), (3)");
      assertTrue("Expected the connection to be unsticky " +
          "before executing PREPARE query",
          verifySessionParameterValue(stmt,
              "ysql_conn_mgr_sticky_object_count", "0"));

      // PREPARE query
      stmt.execute("PREPARE testPlan (int) AS SELECT * FROM test_table WHERE id = $1");
      assertTrue("Expected the connection to be sticky " +
          "after executing PREPARE query",
          verifySessionParameterValue(stmt,
              "ysql_conn_mgr_sticky_object_count", "1"));

      // EXECUTE query
      stmt.executeQuery("EXECUTE testPlan(1)");

    } catch (Exception e) {
      fail("Got exception " + e.getMessage());
    }
  }

  // This test verifies, if the protocol level prepared statement fails to parse on database
  // connection manager should not record the prepared statement in the server hashmap.
  @Test
  public void testProtocolLevelPreparedFailParseStatements() throws Exception {
    disableWarmupModeAndRestartCluster();

    createOrDropTable(tableName, false);

    Properties props = new Properties();
    props.setProperty("prepareThreshold", "1");

    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(props)) {
      PreparedStatement pstmt = conn.prepareStatement(sqlProtocolPreparedStmt);
      pstmt.execute();
      fail("Expected to fail as table is dropped");
    } catch (Exception e) {
      if (e.getMessage().contains("relation \"test_table_proto_prep_stmt\" does not exist")) {
        LOG.info("Expected to fail with exception " + e.getMessage());
      } else {
        LOG.error("Got exception " + e.getMessage());
        fail("Got exception " + e.getMessage());
      }
    }

    // JDBC will do one retry on receiving the error "prepare statement does not exist" from
    // server. Therefore this time if prev parse failed entry was not removed from server hashmap,
    // then this connection attempt will fail with "prepare statement does not exist".
    // Then JDBC will retry parsing with new statement name (S_2) again which will fail with
    // "TEST_TABLE_PROTO_PREP_STMT does not exist".
    // If the entry is not removed from server hashmap, then server hashmap will have two stale
    // entries for the statement name (S_1 and S_2) i.e for which there is no cache plan on server.
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(props)) {
      // It should use already created physical backend.
      assertEquals(1, getTotalPhysicalConnections("yugabyte", "yugabyte",
                      STATS_UPDATE_INTERVAL + 2));
      PreparedStatement pstmt = conn.prepareStatement(sqlProtocolPreparedStmt);
      pstmt.execute();
      fail("Expected to fail as table is dropped");
    } catch (Exception e) {
      if (e.getMessage().contains("relation \"test_table_proto_prep_stmt\" does not exist")) {
        LOG.info("Expected to fail with exception " + e.getMessage());
      } else {
        LOG.error("Got exception " + e.getMessage());
        fail("Got exception " + e.getMessage());
      }
    }

    createOrDropTable(tableName, true);

    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(props)) {
      // It should use already created physical backend.
      assertEquals(1, getTotalPhysicalConnections("yugabyte", "yugabyte",
                      STATS_UPDATE_INTERVAL + 2));
      // It must generate same server key for server object in conn mgr with
      // given sql stmt & driver given prepared statement name.
      // The prev error must have removed the key entry from server hashmap.
      PreparedStatement pstmt = conn.prepareStatement(sqlProtocolPreparedStmt);
      // First time PARSE, BIND, EXECUTE will be send by driver.
      pstmt.execute();
      // From second time onwards, only BIND, EXECUTE will be send by driver.
      pstmt.execute();
      // LOG.info("Expected to execute successfully");

      createOrDropTable(tableName, false);
      // Close the existing backend by making it sticky and closing the connection.
      // So that next BIND, EXECUTE will be send to a new backend where PARSE is not yet done.
      makeBackendStickySoGetClosed();
      assertEquals(0, getTotalPhysicalConnections("yugabyte", "yugabyte",
                      STATS_UPDATE_INTERVAL + 2));

      try {
        // BIND, EXECUTE will be send by driver to a new BACKEND where PARSE is not yet done.
        // So conn mgr will implicitly send PARSE to the new backend.
        // We want to verify for implicit PARSE packet too, then false entry is removed
        // from server hashmap on receiving the error response from the backend.
        // The next try block will verify that the entry is removed from server hashmap.
        pstmt.execute();
        LOG.error("Expected to fail as table is dropped");
        fail("Expected to fail as table is dropped");
      } catch (Exception e) {
        if (e.getMessage().contains("relation \"test_table_proto_prep_stmt\" does not exist")) {
          LOG.info("Expected to fail with exception " + e.getMessage());
        } else {
          LOG.error("Got exception " + e.getMessage());
          fail("Got exception " + e.getMessage());
        }
      }
    }
    catch (Exception e) {
      LOG.error("Got exception " + e.getMessage());
      fail("Got exception " + e.getMessage());
    }

    createOrDropTable(tableName, true);

    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(props)) {
      assertEquals(1, getTotalPhysicalConnections("yugabyte", "yugabyte",
                      STATS_UPDATE_INTERVAL + 2));
      PreparedStatement pstmt = conn.prepareStatement(sqlProtocolPreparedStmt);
      pstmt.execute();
    } catch (Exception e) {
      LOG.error("Got exception " + e.getMessage());
      fail("Got exception " + e.getMessage());
    }

  }

  private void createOrDropTable(String tableName, boolean create) throws Exception {
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {
      if (create) {
        stmt.execute("CREATE TABLE " + tableName + " (id int)");
      } else {
        stmt.execute("DROP TABLE IF EXISTS " + tableName);
      }
    }
    catch (Exception e) {
      LOG.error("Got exception while creating the table " + e.getMessage());
      fail("Got exception while creating the table " + e.getMessage());
    }
  }

  // Purpose of this function is to make the backend sticky so that backend is terminated
  // once the connection is closed.
  private void makeBackendStickySoGetClosed() throws Exception {
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE TEMPORARY TABLE random_temp_table (id int)");
    } catch (Exception e) {
      LOG.error("Got exception " + e.getMessage());
      fail("Got exception " + e.getMessage());
    }
  }
}
