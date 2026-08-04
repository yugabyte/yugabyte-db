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

  public String createLogicalClientSelectQuery() {
    return "SELECT current_version FROM " + LOGICAL_CLIENT_VERSION_TABLE;
  }

  @Test
  public void testLogicalClientVersionBump() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement statement = connection.createStatement()) {

        String query = createLogicalClientSelectQuery();
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

      String query = createLogicalClientSelectQuery();
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

      String query = createLogicalClientSelectQuery();
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
}
