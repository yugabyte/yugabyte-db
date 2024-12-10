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
import static org.yb.AssertionWrappers.assertThrows;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import java.util.List;
import java.util.Properties;
import java.util.stream.Collectors;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;


@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestLogicalClientVersion extends BaseYsqlConnMgr {

  public String LOGICAL_CLIENT_VERSION_TABLE = "pg_yb_logical_client_version";

  public String createLogicalClientSelectQuery(String database) {
    return "SELECT current_version FROM " + LOGICAL_CLIENT_VERSION_TABLE +
        " where db_oid IN (SELECT oid FROM pg_database WHERE datname = '" + database + "')";
  }

  @Test
  public void testLogicalClientVersionBump() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement statement = connection.createStatement()) {

        String query = createLogicalClientSelectQuery("yugabyte");
        List<Row> rows = getRowList(statement, query);

        assertEquals(rows.size(), 1);
        Row ver = rows.get(0);
        Long old_version = rows.get(0).getLong(0);

        // Atler role to bump up the version
        statement.execute("ALTER ROLE yugabyte set timezone = 'GMT'");
        rows = getRowList(statement, query);
        assertEquals(rows.size(), 1);
        Long new_version = rows.get(0).getLong(0);
        assertEquals((long)new_version, (long)old_version+1);

        // Atler database to bump up the version
        statement.execute("ALTER ROLE yugabyte set timezone = 'GMT'");
        rows = getRowList(statement, query);
        assertEquals(rows.size(), 1);
        new_version = rows.get(0).getLong(0);
        assertEquals((long)new_version, (long)old_version + 2);
    }
  }

  @Test
  public void testCreateDropDatabase() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement statement = connection.createStatement()) {

        statement.execute("CREATE DATABASE new_db");
        List<Row> rows = getRowList(statement, createLogicalClientSelectQuery("new_db"));
        assertEquals(rows.size(), 1);

        statement.execute("DROP DATABASE new_db");
        rows = getRowList(statement, createLogicalClientSelectQuery("new_db"));
        assertEquals(rows.size(), 0);
      }
   }

  // Two logical connections are created named c1 and c2 that will execute txns
  // on same backend B1. Even if c2 has executed "ALTER ROLE SET" command to bump
  // the logical cient version in pg_yb_logical_client_version table, the backend B1
  // was spwaned before hence having old version number.
  @Test
  public void testOnePhysicalConnectionWithTwoLogicalConnections() throws Exception {
    enableVersionMatchingAndRestartCluster();
    try (Connection c1 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection c2 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement s1 = c1.createStatement();
        Statement s2 = c2.createStatement()) {

      String query = createLogicalClientSelectQuery("yugabyte");
      List<Row> rows = getRowList(s1, query);

      assertEquals(rows.size(), 1);
      Row ver = rows.get(0);
      Long old_version = rows.get(0).getLong(0);

      // Executed on backend B1.
      s1.execute("SELECT 1");
      // Executed on backend B1, increase logical_client_version from 1 to 2
      // in pg_yb_logical_client_version catalog table.
      s2.execute("ALTER ROLE yugabyte SET timezone = 'GMT'");

      rows = getRowList(s2, query);
      assertEquals(rows.size(), 1);
      Long new_version = rows.get(0).getLong(0);
      assertEquals((long)new_version, (long)old_version+1);

      // Also executed on backend B1.
      s1.execute("SELECT 1");
    }
  }

  // This test is same as testOnePhysicalConnectionWithTwoLogicalConnections with the
  // difference that now Backend B1 is made sticky to logical connection c2. Due to this
  // new backend will be spwaned for c1 to execute next statement. But since there is bump
  // of version caused by "ALTER ROLE SET" executed by c2, c1 will not get any matching
  // backend hence connecting to backend of higher version.
  @Test
  public void testOnePhysicalConnectionWithTwoLogicalConnectionsSticky() throws Exception {
    enableVersionMatchingAndRestartCluster();
    try (Connection c1 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection c2 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement s1 = c1.createStatement();
        Statement s2 = c2.createStatement()) {

      String query = createLogicalClientSelectQuery("yugabyte");
      List<Row> rows = getRowList(s1, query);

      assertEquals(rows.size(), 1);
      Row ver = rows.get(0);
      Long old_version = rows.get(0).getLong(0);

      // Executed on backend B1.
      s1.execute("SELECT 1");
      // Executed on backend B1, increase logical_client_version from 1 to 2.
      s2.execute("ALTER ROLE yugabyte SET timezone = 'GMT'");
      // Now B1 is sticky with c2.
      s2.execute("BEGIN");

      rows = getRowList(s2, query);
      assertEquals(rows.size(), 1);
      Long new_version = rows.get(0).getLong(0);
      assertEquals((long)new_version, (long)old_version+1);

      // connect to backend of higher version.
      s1.execute("SELECT 1");
    }
  }

  // - Consider two logical connections c1 and c2 are made
  // - c2 bump the logical client version by "ALTER ROLE SET" command
  // - New connection c3 is made with bumped up version number = 2
  // - This would invalidate older logical client connection c1 and c2
  // - Next query that c1 or c2 will try to execute will cause connection close
  @Test
  public void testNewLogicalConnectionWithBumpedUpVersion() throws Exception {
    enableVersionMatchingAndRestartCluster(false);
    try (Connection c1 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection c2 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement s1 = c1.createStatement();
        Statement s2 = c2.createStatement()) {

      String query = createLogicalClientSelectQuery("yugabyte");
      List<Row> rows = getRowList(s1, query);

      assertEquals(rows.size(), 1);
      Row ver = rows.get(0);
      Long old_version = rows.get(0).getLong(0);

      s1.execute("SELECT 1");
      s2.execute("ALTER ROLE yugabyte SET timezone = 'GMT'");

      rows = getRowList(s2, query);
      assertEquals(rows.size(), 1);
      Long new_version = rows.get(0).getLong(0);
      assertEquals((long)new_version, (long)old_version+1);

      try (
        Connection c3 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement s3 = c3.createStatement()
      ) {
        s3.execute("SELECT 1");
      }
      assertThrows(
          "Old Logical connection",
           SQLException.class, () -> s1.execute("SELECT 1"));
      assertThrows(
          "Old Logical connection",
           SQLException.class, () -> s2.execute("SELECT 1"));
    }
  }
}
