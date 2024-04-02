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

import static org.yb.AssertionWrappers.fail;
import java.sql.Connection;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestDropDb extends BaseYsqlConnMgr{

  // yb-tserver flag ysql_conn_mgr_stats_interval (stats_interval field
  // in the odyssey's config) controls the interval at which the
  // stats gets updated (src/odyssey/sources/cron.c:332).
  private static final int CONNECTIONS_STATS_UPDATE_INTERVAL_SECS = 1;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
        Integer.toString(CONNECTIONS_STATS_UPDATE_INTERVAL_SECS));
    }

  private void dropDatabase(String db_name, boolean should_succeed) {
    try (Connection conn = getConnectionBuilder()
                              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP DATABASE " + db_name);
      if (!should_succeed)
        fail("Executed 'DROP DATABASE' query successfully, expected it to fail");
    } catch (Exception e) {
      if (should_succeed)
        fail("DROP DATABASE query failed");
    }
  }

  // GH #20581: DROP DATABASE DB_1 query should fail if there is a client connection to the database
  // DB_1 on the same node.
  // This test ensures that:
  // a. DROP DATABASE DB_1 fails when there is a logical connection to the database
  //    DB_1 but no physical connection.
  // b. DROP DATABASE DB_1 should succeed when there is no logical connection to the database
  //    DB_1 but there is a physical connection to the database.
  @Test
  public void testDropDbFail() throws InterruptedException {
    try (Connection conn = getConnectionBuilder()
                              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE DATABASE testdb_1");
    } catch (Exception e) {
      LOG.error("Got error while creating the database", e);
      fail();
    }

    // Create a logical connection and try dropping the database
    try (Connection conn = getConnectionBuilder()
                                .withDatabase("testdb_1")
                                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                .connect();
        Statement stmt = conn.createStatement()) {
      // Wait till the stats gets updated
      Thread.sleep(CONNECTIONS_STATS_UPDATE_INTERVAL_SECS * 1000);

      dropDatabase("testdb_1", false);

      // Run a query on the logical connection so that the pool has atleast 1
      // physical connection.
      stmt.execute("SELECT 1");
    } catch (Exception e) {
      LOG.error("Got unexpected error", e);
      fail();
    }

    // Wait till the stats gets updated
    Thread.sleep(CONNECTIONS_STATS_UPDATE_INTERVAL_SECS * 1000);

    dropDatabase("testdb_1", true);
  }
}
