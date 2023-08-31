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

import java.sql.*;
import java.util.HashMap;
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

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_max_conns_per_db", Integer.toString(NUMBER_OF_CLIENTS / 5));
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
              "CREATE TABLE IF NOT EXISTS TEST_TABLE_INSERT (id SERIAL PRIMARY KEY, name TEXT)");

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
              "CREATE TABLE IF NOT EXISTS TEST_TABLE_UPDATE (id SERIAL PRIMARY KEY, name TEXT)");

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
              "CREATE TABLE IF NOT EXISTS TEST_TABLE_DELETE (id SERIAL PRIMARY KEY, name TEXT)");

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
}
