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
import java.sql.Statement;
import java.lang.Thread;
import java.sql.ResultSet;
import java.util.concurrent.CountDownLatch;
import com.google.gson.JsonObject;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestReserveYsqlInternalConns extends BaseYsqlConnMgr {

    private static final int TOTAL_CONNECTIONS = 30;
    private static final int RESERVED_CONNS_FOR_NON_REPLICATION_SUPERUSER = 3;
    private static final int YSQL_MAX_CONNECTIONS = 20;
    private static final int NUM_CONNS_BYPASS_CM = 10;
    private final int num_phy_conn_mgr_connections =
            YSQL_MAX_CONNECTIONS - NUM_CONNS_BYPASS_CM;// 10
    private final int num_postgres_connections =
            NUM_CONNS_BYPASS_CM - RESERVED_CONNS_FOR_NON_REPLICATION_SUPERUSER;// 7
    private final CountDownLatch latch = new CountDownLatch(1);
    // A shared lock object for wait/notify mechanism.
    private final Object lock = new Object();
    private final Object postgres_conn_lock = new Object();
    private boolean wait_for_all_conn_mgr_phy_conns = true;

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        disableWarmupRandomMode(builder);
        builder.addCommonTServerFlag("ysql_max_connections",
            Integer.toString(YSQL_MAX_CONNECTIONS));
        builder.addCommonTServerFlag("ysql_conn_mgr_use_auth_backend", "true");
        builder.addCommonTServerFlag("ysql_conn_mgr_reserve_internal_conns",
            Integer.toString(NUM_CONNS_BYPASS_CM));
        builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
            Integer.toString(STATS_UPDATE_INTERVAL));
        builder.addCommonTServerFlag("ysql_pg_conf_csv",
            "superuser_reserved_connections=" + RESERVED_CONNS_FOR_NON_REPLICATION_SUPERUSER);
    }

    // Before starting the test, avoid making connection to conn mgr end point
    // even when test is ran with --enable-ysql-conn-mgr flag.
    // Otherwise it creates extra backend to default user / db which limits number
    // of connection to other user/db & can cause test to fail.
    @Override
    public void verifyClusterAcceptsPGConnections() throws Exception {
        LOG.info("Waiting for the cluster to accept pg connections");
        TestUtils.waitFor(() -> {
            try {
            connectionBuilderForVerification(getConnectionBuilder())
            .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .connect().close();
            return true;
        } catch (Exception e) {
            return false;
        }
    }, 10000);
    }

    private void createUser(String userName) throws Exception {
        try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
                    .withUser("yugabyte")
                    .connect();)
        {
            try (Statement stmt = conn.createStatement()) {
                stmt.execute("CREATE USER " + userName + " WITH LOGIN;");
                LOG.info("Created user " + userName);
            }
        }
        catch (Exception e) {
            LOG.error("Got an unexpected error while creating user: ", e);
            fail(String.format("User creation faced an unexpected issue %s", e.getMessage()));
        }
    }

    private void createConnections() throws Exception {
        Thread[] threads = new Thread[TOTAL_CONNECTIONS];
        Exception[] exceptions = new Exception[TOTAL_CONNECTIONS];
        for (int i = 0; i < TOTAL_CONNECTIONS; i++) {
            final int index = i;
            threads[i] = new Thread(() -> {
                        try (Connection conn = getConnectionBuilder()
                            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                            .withUser("us1")
                            .connect();
                            Statement stmt = conn.createStatement()) {
                                stmt.execute("BEGIN");
                                if (wait_for_all_conn_mgr_phy_conns) {
                                    // Use wait() to pause the thread and hold the connection.
                                    synchronized (lock) {
                                        lock.wait();
                                    }
                                }
                                stmt.execute("COMMIT");
                                stmt.close();
                                conn.close();
                                LOG.info("Created connection " + index + " and closed it");
                            } catch (Exception e) {
                                exceptions[index] = e;
                            }
                    });
        }
        for (int i = 0; i < TOTAL_CONNECTIONS; i++) {
            threads[i].start();
        }

        Thread read_stats_thread = new Thread(() -> {
            while (true) {
                try {
                    Thread.sleep((STATS_UPDATE_INTERVAL + 2) * 1000);
                    JsonObject pool = getPool("yugabyte", "us1");
                    JsonObject auth_pool = getPool("control_connection", "control_connection");
                    assertNotNull(pool);
                    LOG.info("Pool: " + pool.toString());
                    LOG.info("Auth pool: " + auth_pool.toString());
                    if (pool.get("active_physical_connections").getAsInt() >=
                        num_phy_conn_mgr_connections) {
                        // We can be sure, all the physical connections has been exhausted
                        // by connection manager.
                        LOG.info("All the physical connections has been exhausted by connection " +
                            "manager. Notifying all waiting threads to proceed.");
                        wait_for_all_conn_mgr_phy_conns = false;

                        // We can now start making connections to postgres end point. To validate
                        // conn mgr has not created more than what percentage of total ysql
                        // connections has been set for it.
                        latch.countDown();
                        synchronized (lock) {
                            lock.notifyAll();
                        }
                        break;
                    }
                } catch (Exception e) {
                    LOG.error("Got an unexpected error while reading stats: " + e.getMessage());
                    fail(String.format("Reading stats faced an unexpected issue %s",
                        e.getMessage()));
                }
            }
        });
        read_stats_thread.start();

        // Need to make sure all the slots are created for US1 user via connection manager.
        // Before started making super user connections to end postgres end point. In order
        // to utilize slots reserved for non-replication superuser connections.
        latch.await();

        LOG.info("Num postgres connections: " + num_postgres_connections + ". Keep " +
            RESERVED_CONNS_FOR_NON_REPLICATION_SUPERUSER + " connections reserved " +
            "for non-replication superuser connections.");
        Thread[] postgres_threads = new Thread[num_postgres_connections];
        Exception[] postgres_exceptions = new Exception[num_postgres_connections];
        for (int i = 0; i < num_postgres_connections; i++) {
            final int index = i;
            postgres_threads[i] = new Thread(() -> {
                try (Connection conn = getConnectionBuilder()
                    .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
                    .withUser("yugabyte")
                    .connect();) {
                    Statement stmt = conn.createStatement();
                    ResultSet rs = stmt.executeQuery("SELECT count(*) FROM" +
                        " pg_stat_activity WHERE backend_type = " +
                        "'client backend'");
                    int backends = 0;
                    while (rs.next()) {
                        backends = rs.getInt(1);
                        LOG.info("Num of client backend connections: " +
                            backends + " index: " + index);
                    }
                    if (backends == num_postgres_connections) {
                        synchronized (postgres_conn_lock) {
                            postgres_conn_lock.notifyAll();
                        }
                        stmt.close();
                        conn.close();
                        LOG.info("Created connection " + index + " at postgres end point and " +
                            "closed it");
                        return;
                    }
                    synchronized (postgres_conn_lock) {
                        postgres_conn_lock.wait();
                    }
                    stmt.close();
                    conn.close();
                    LOG.info("Created connection " + index + " at postgres end point and " +
                        "closed it");
                }
                catch (Exception e) {
                    LOG.error("Got an unexpected error while creating connection to postgres " +
                        "end point: " + e.getMessage() + " on index " + index);
                    postgres_exceptions[index] = e;
                    fail(String.format("Connection creation faced an unexpected issue while " +
                        "creating connection to postgres end point: %s",
                        e.getMessage()));
                }
            });
            postgres_threads[i].start();
        }

        // Make sure all the threads/connections are created and closed successfully at both
        // end points with no errors.

        for (int i = 0; i < TOTAL_CONNECTIONS; i++) {
            threads[i].join(5000);
            if (exceptions[i] != null) {
                LOG.error("Got an unexpected error message: " + exceptions[i].getMessage()
                    + " for connection " + i);
                fail(String.format("Connection creation faced an unexpected issue %s",
                    exceptions[i].getMessage()));
            }
        }

        for (int i = 0; i < num_postgres_connections; i++) {
            postgres_threads[i].join(5000);
            if (postgres_exceptions[i] != null) {
                LOG.error("Got an unexpected error while creating connection to postgres end " +
                    "point: " + postgres_exceptions[i].getMessage());
                fail(String.format("Connection creation faced an unexpected issue while creating "
                    + "connection to postgres end point: %s",
                    postgres_exceptions[i].getMessage()));
            }
        }

    }

    @Test
    public void testReserveYsqlInternalConns() throws Exception {
        // This test validates connection manager doesn't exceeds from creating physical
        // connections than set for it.
        // It is done by validating:
        // 1. Conn mgr could not create more physical connections than what has been set for it
        // even there is demand to create more physical connections.
        //  It's expected that conn mgr can exceed the limit by 1 or 2 connections due to race
        //  conditions in connection manager while creating physical connections.
        // 2. When traffic is at it's peak at conn mgr end point, we should be able to create
        //  same number of connections to postgres end point that are not reserved to be used by
        //  conn mgr within total ysql connections limit.
        // 3. Avoid last RESERVED_CONNS_FOR_NON_REPLICATION_SUPERUSER conns to create either
        // via conn mgr or postgres. As connection manager is making non-superuser connections, so
        // can gurantee that last RESERVED_CONNS_FOR_NON_REPLICATION_SUPERUSER will be superuser or
        // non-superuser connections.

        createUser("us1");
        createConnections();
    }

}
