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
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import java.util.Properties;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestDeallocatePrepStmts extends BaseYsqlConnMgr {

  static final int IDLE_TIME = 1;
  static final String query_pg_prepared_statements =
              "SELECT name, statement FROM pg_prepared_statements WHERE" +
              " statement = 'select name FROM t_close_packet'";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Deallocate support has been added only in optimized extended query protocol mode.
    // GH: #30412 Adds the support in unoptimized mode as well.
    builder.addCommonTServerFlag("ysql_conn_mgr_optimized_extended_query_protocol", "true");
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings",
    "log_query,log_debug");
    builder.addCommonTServerFlag("ysql_conn_mgr_idle_time",
      Integer.toString(IDLE_TIME));
    builder.addCommonTServerFlag("ysql_conn_mgr_jitter_time", "0");
    disableWarmupRandomMode(builder);
  }

  @Test
  public void testClosePacket() throws Exception {
    // The following test, tests behaviour of CLOSE packet with conn mgr.
    // It tests if:
    // 1. CLOSE packet sent for valid prepared statement then DB doesn't
    // deallocate the prepared statement.
    // 2. CLOSE packet sent for invalid prepared statement then DB
    // deallocates the prepared statement.
    // 3. CLOSE packet is sent on backend where entry does not exist, it should
    // be no-op similar to PG.

    Properties props = new Properties();
    // Make sure to use named prepared statements.
    props.setProperty("prepareThreshold", "1");
    // Disable's driver's prepared statement cache entirely. This is done so
    // that driver always sends CLOSE packet on calling pstmt.close().
    props.setProperty("preparedStatementCacheQueries", "0");
    try (Connection conn = getConnectionBuilder()
         .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
         .withUser("yugabyte")
         .withPassword("yugabyte")
         .connect(props);)
      {
        Statement stmt = conn.createStatement();
        stmt.execute("create table if not exists t_close_packet (name varchar(20))");
        stmt.execute("insert into t_close_packet values ('test')");
        PreparedStatement pstmt = conn.prepareStatement("select name FROM t_close_packet");
        pstmt.execute();
        pstmt.execute();
        ResultSet rs = stmt.executeQuery(query_pg_prepared_statements);
        rs.next();
        assertTrue(rs.getRow() == 1);
        // SEND CLOSE PACKET
        pstmt.close();
        rs = stmt.executeQuery(query_pg_prepared_statements);
        rs.next();
        // Even CLOSE send by client, statement won't be deallocated from server as it's
        // still valid on backend.
        assertTrue(rs.getRow() == 1);

        pstmt = conn.prepareStatement("select name FROM t_close_packet");
        pstmt.execute();

        String name = null;
        rs = stmt.executeQuery(query_pg_prepared_statements);
        rs.next();
        assertTrue(rs.getRow() == 1);
        name = rs.getString("name");

        stmt.execute("ALTER TABLE t_close_packet ALTER COLUMN name TYPE VARCHAR(41)");

        try {
          pstmt.execute();
          LOG.info("JDBC internally retried after getting cache plan error");
          // JDBC must have sent CLOSE packet on getting cache plan error.
          // The invalid prepared statement must have been deallocated & conn mgr would have
          // removed it's entry from server hashmap.
          // The followed PARSE must have succeeded by creating a new cache plan on server.
        }
        catch (Exception e) {
          LOG.error("Got an unexpected error while executing prepared statement: ", e);
          fail("Got an unexpected error while executing prepared statement: " + e.getMessage());
        }

        rs = stmt.executeQuery(query_pg_prepared_statements);
        rs.next();
        // For the new plan created with new name.
        assertTrue(rs.getRow() == 1);
        assertNotEquals(name, rs.getString("name"));

        // This ensures after ALTER TABLE, the prepared statement became invalid and
        // got deallocated.

        // Wait for single backend to get expired out.
        Thread.sleep(4 * IDLE_TIME * 1000);

        // CLOSE packet will be sent to new backend where entry does not exist, it should be
        // no-op similar to PG. And must receive CloseComplete response without any error.
        // Although JDBC user, can't assert getting CloseComplete but it should be no error.
        pstmt.close();

        rs = stmt.executeQuery(query_pg_prepared_statements);
        rs.next();
        assertTrue(rs.getRow() == 0);

        stmt.close();


      } catch (Exception e) {
        LOG.error("Got an unexpected error while creating a connection: ", e);
        fail("connection faced an unexpected issue");
    }
  }
}
