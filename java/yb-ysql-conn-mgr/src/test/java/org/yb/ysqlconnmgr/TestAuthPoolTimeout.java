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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.SQLException;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import com.google.gson.JsonObject;
import java.sql.Statement;
import java.sql.ResultSet;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestAuthPoolTimeout extends BaseYsqlConnMgr {

    private static final int NUM_CONNECTIONS = 10;
    private static final int POOL_TIMEOUT = 2000;

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);

        builder.addCommonTServerFlag("ysql_conn_mgr_pool_timeout",
            Integer.toString(POOL_TIMEOUT));
        builder.addCommonTServerFlag("ysql_conn_mgr_use_auth_backend", "true");
        builder.addCommonTServerFlag("ysql_max_connections",
            Integer.toString(NUM_CONNECTIONS));
        builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
            Integer.toString(STATS_UPDATE_INTERVAL));
    }

    /*
     * This test verifies pool timeout happens for the control connection pool. It creates
     * NUM_CONNECTIONS transactional connections by opening a transaction on each connection.
     * It then tries to connect to the control connection pool with a timeout and verifies
     * that the connection times out.
     * Can't make superuser sticky because there is a race condition while cleaning up
     * auth backend and creating sticky transactional connection as JDBC implicitly sends SET
     * statements on successful authentication. Therefore first creating all logical connections
     * and then opening transaction to make parallel physical connections.
     * Read for GH #30324 for more details.
     */
    @Test
    public void testAuthPoolTimeout() throws Exception {
        Connection[] connections = new Connection[NUM_CONNECTIONS];
        // Just create logical connections.
        for (int i = 0; i < NUM_CONNECTIONS; i++) {
            try {
                connections[i] = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .withPassword("yugabyte")
                    .connect();
                // JDBC executes some SET statements on successful authentication.
            } catch (SQLException e) {
                LOG.info("Expected connection to succeed, but got: " + e.getMessage() +
                    " for connection " + (i + 1), e);
                fail("Expected connection to succeed, but got: " + e.getMessage() +
                    " for connection " + (i + 1));
            }
        }
        // Now open transactions on each connection to make NUM_CONNECTIONS physical connections.
        for (int i = 0; i < NUM_CONNECTIONS; i++) {
            try (Statement stmt = connections[i].createStatement()) {
                stmt.executeUpdate("BEGIN");
            } catch (SQLException e) {
                LOG.info("Exception in BEGIN for connection " + (i + 1) + ": " + e.getMessage());
                fail("Exception in BEGIN for connection " + (i + 1) + ": " + e.getMessage());
            }
        }

        Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);
        JsonObject pool = getPool("yugabyte", "yugabyte");
        LOG.info("Pool: " + pool.toString());

        pool = getPool("control_connection", "control_connection");
        LOG.info("Control pool: " + pool.toString());

        try {
            getConnectionBuilder()
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .withUser("yugabyte")
                .withPassword("yugabyte")
                .connect();
            fail("Expected connection to fail");
        } catch (SQLException e) {
            LOG.info("Expected connection to fail with timeout, got: " + e.getMessage());
        }

        for (int i = 0; i < NUM_CONNECTIONS; i++) {
            try (Statement stmt = connections[i].createStatement()) {
                stmt.executeUpdate("COMMIT");
            } catch (SQLException e) {
                LOG.info("Exception in COMMIT for connection " + (i + 1) + ": " + e.getMessage());
                fail("Exception in COMMIT for connection " + (i + 1) + ": " + e.getMessage());
            }
            connections[i].close();
        }
    }
}
