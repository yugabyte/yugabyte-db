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
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestYCMConfiguration extends BaseYsqlConnMgr {

  private static final String LONG_STR = new String(new char[50]).replace('\0', 'a');

  private void createRole(String roleName) {
    try (Connection conn = getConnectionBuilder()
                           .withUser("yugabyte")
                           .withDatabase("yugabyte")
                           .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                           .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute(String.format("CREATE ROLE %s LOGIN", roleName));
      LOG.info("Created the role " + roleName);
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  @Test
  public void testQuerySizeGflag() throws Exception {
    // Force the deploy phase to always take place even after enabling
    // optimized support for session parameters by disabling warmup mode.
    disableWarmupModeAndRestartCluster();

    try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
          Statement stmt = conn.createStatement();
          Connection conn2 = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
          Statement stmt2 = conn2.createStatement()) {

          stmt.execute("SET application_name to " + LONG_STR);
          stmt2.execute("BEGIN");
          // Force logical connection 1 to open a new physical connection
          ResultSet rs = stmt.executeQuery("show application_name");

          if (rs.next())
            assertEquals(LONG_STR, rs.getString(1));
    }
    catch (Exception e) {
      LOG.error("Got an unexpected error: ", e);
      fail("Connection faced an unexpected issue");
    }

    // Decrease the size of query packet and restart the cluster.
    // The deploy phase should fail if we continue to use a larger
    // application name, we should expect only the reset phase to
    // be executed, leading to an empty string for application_name.
    reduceQuerySizePacketAndRestartCluster(75);

    try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
          Statement stmt = conn.createStatement();
          Connection conn2 = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .connect();
          Statement stmt2 = conn2.createStatement()) {

          stmt.execute("SET application_name to " + LONG_STR);
          stmt2.execute("BEGIN");
          // Force logical connection 1 to open a new physical connection
          ResultSet rs = stmt.executeQuery("show application_name");

          if (rs.next()) {
            // When switching between physical connections, the deploy phase
            // should fail with the reduced query size parameter. The
            // application name should not be set to LONG_STR on the new
            // connection due to this restriction.
            assertEquals("", rs.getString(1));
          }
    }
    catch (Exception e) {
      LOG.error("Got an unexpected error: ", e);
      fail("Connection faced an unexpected issue");
    }

  }

  @Test
  public void testMaxPoolSize() throws Exception {

    reduceMaxPoolSize(4);

    createRole("test_role_1");
    createRole("test_role_2");
    createRole("test_role_3");

    // Each connection will be created with different role which will lead
    // to creation of different pools.
    ConnectionBuilder roleUserConnectionBuilder = getConnectionBuilder()
                      .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR);

    try (Connection connection = roleUserConnectionBuilder
                                 .withUser("test_role_1").connect()) {
      // No-op.
    }
    catch (Exception e) {
      LOG.error("Got an unexpected error: ", e);
      fail("Connection faced an unexpected issue");
    }

    try (Connection connection = roleUserConnectionBuilder
                                 .withUser("test_role_2").connect()) {
      // No-op.
    }
    catch (Exception e) {
      LOG.error("Got an unexpected error: ", e);
      fail("Connection faced an unexpected issue");
    }

    // This connection will fail as the max pool size is set to 4.
    // 4 pools already created are:
      // 1. Control Connecton pool
      // 2. Default Pool (yugabyte/yugabyte)
      // 3. Pool for test_role_1
      // 4. Pool for test_role_2
    try (Connection connection = roleUserConnectionBuilder
                                 .withUser("test_role_3").connect()) {
      // No-op.
      LOG.error("Should have failed on reaching the max pool limit");
      fail("Connection is expected to fail on reaching max pool size");
    }
    catch (Exception e) {
      LOG.info("Connection is expected to fail on eaching max pool size", e);
    }

  }
}
