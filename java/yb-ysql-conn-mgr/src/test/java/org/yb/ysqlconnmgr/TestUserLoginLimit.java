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
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.BasePgSQLTest;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestUserLoginLimit extends BaseYsqlConnMgr {

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);

        builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
            Integer.toString(BasePgSQLTest.CONNECTIONS_STATS_UPDATE_INTERVAL_SECS));
    }

    @Test
    public void testUserLoginLimit() throws Exception {
        try (Connection connection = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .withPassword("yugabyte")
                    .connect();
            Statement statement = connection.createStatement()) {

            // By default minimum 3 physical connections will be created in
            // random warmup mode.
            // We need at least one connection for the authentication via
            // auth-backend, so the limit must be at least 4 to even allow a
            // single logical connection to be made.
            statement.execute("CREATE ROLE limit_role LOGIN CONNECTION LIMIT 4");

            ConnectionBuilder limitRoleUserConnBldr =
                                getConnectionBuilder()
                                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                .withUser("limit_role");
            try (Connection ignored1 = limitRoleUserConnBldr.connect()) {
                BasePgSQLTest.waitForStatsToGetUpdated();
                try (Connection connection2 = limitRoleUserConnBldr.connect()) {
                    BasePgSQLTest.waitForStatsToGetUpdated();
                    try (Connection ignored3 = limitRoleUserConnBldr.connect()) {
                        BasePgSQLTest.waitForStatsToGetUpdated();
                        try (Connection ignored4 = limitRoleUserConnBldr.connect()) {
                            BasePgSQLTest.waitForStatsToGetUpdated();
                            // Fifth concurrent connection causes error.
                            try (Connection ignored5 = limitRoleUserConnBldr.connect()) {
                                fail("Expected fifth login attempt to fail");
                            } catch (SQLException sqle) {
                                assertThat(
                                    sqle.getMessage(),
                                    CoreMatchers.containsString("too many connections for " +
                                            "role \"limit_role\"")
                                );
                            }
                        }
                    }

                    // Close second connection.
                    connection2.close();
                    BasePgSQLTest.waitForStatsToGetUpdated();

                    // New connection now succeeds.
                    try (Connection ignored2 = limitRoleUserConnBldr.connect()) {
                        // No-op.
                    }
                }
            }

        } catch (Exception e) {
            LOG.error("Allowing unexpected number of connections than what limit has been " +
                "set for given user: ", e);
            fail ("Creating unexpected number of connections than what limit has been set for " +
                "given user");
        }

    }
}
