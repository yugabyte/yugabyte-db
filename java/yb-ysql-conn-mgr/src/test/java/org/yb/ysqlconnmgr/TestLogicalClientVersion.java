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
            .withDatabase("yugabyte")
            .withUser("yugabyte")
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
            .withDatabase("yugabyte")
            .withUser("yugabyte")
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
}
