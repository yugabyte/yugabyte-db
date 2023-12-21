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
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestMisc extends BaseYsqlConnMgr {
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
}
