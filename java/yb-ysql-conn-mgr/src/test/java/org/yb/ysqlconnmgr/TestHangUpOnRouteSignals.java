package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.fail;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import com.google.gson.JsonObject;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestHangUpOnRouteSignals extends BaseYsqlConnMgr {

    private static final CountDownLatch startLatch = new CountDownLatch(1);
    private static final CountDownLatch testHangUpCountDown = new CountDownLatch(2);
    private static final int IDLE_TIMEOUT = 15;
    private static final int MAX_CONNECTIONS = 14;
    private static final int run_tests_for_secs = 30;
    private static final AtomicBoolean test_1_completed = new AtomicBoolean(false);
    private static final AtomicBoolean test_2_completed = new AtomicBoolean(false);

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        disableWarmupRandomMode(builder);
        builder.addCommonTServerFlag("ysql_conn_mgr_enable_multi_route_pool", "true");
        builder.addCommonTServerFlag("ysql_max_connections", String.valueOf(MAX_CONNECTIONS));
        builder.addCommonTServerFlag("ysql_conn_mgr_use_auth_backend", "true");
        builder.addCommonTServerFlag("ysql_conn_mgr_idle_time", String.valueOf(IDLE_TIMEOUT));
        builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
            String.valueOf(STATS_UPDATE_INTERVAL));
    }

    @Test
    public void testHangUpOnRouteSignals() throws Exception {

        try (Connection conn = getConnectionBuilder()
                    .withTServer(TSERVER_IDX)
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .withPassword("yugabyte")
                    .connect();)
        {
            // Setup script to create 1 user connecting to 2 databases

            // Create user us1
            try (Statement stmt = conn.createStatement()) {
                stmt.execute("CREATE USER us1 WITH LOGIN;");
            }

            // Create database db1
            try (Statement stmt = conn.createStatement()) {
                stmt.execute("CREATE DATABASE db1;");
            }

            // Create database db2
            try (Statement stmt = conn.createStatement()) {
                stmt.execute("CREATE DATABASE db2;");
            }

        } catch (Exception e) {
            LOG.error("Got an unexpected error while creating a connection: " + e.getMessage());
            fail(String.format("connection faced an unexpected issue %s", e.getMessage()));
        }

        // Create thread pool for concurrent execution
        final int NUM_TESTS = 3;
        Runnable[] runnables = new Runnable[NUM_TESTS];
        Thread[] threads = new Thread[NUM_TESTS];

        // Create test runnables
        runnables[0] = () -> createConn1Test();
        runnables[1] = () -> createConn2Test();
        runnables[2] = () -> createHangUpTest();

        // Create threads
        for (int i = 0; i < NUM_TESTS; i++) {
            threads[i] = new Thread(runnables[i]);
        }

        try {
            // Start first 3 threads (they will block at startLatch.await())
            for (int i = 0; i < NUM_TESTS; i++) {
                threads[i].start();
            }

            // Start first 3 tests simultaneously
            LOG.info("Starting first 3 tests in 2 seconds...");
            Thread.sleep(2000);
            startLatch.countDown();

            // Wait for all threads to complete
            for (Thread thread : threads) {
                thread.join();
            }

            LOG.info("=====================================================");
            LOG.info("All tests completed successfully!");

        } catch (Exception e) {
            LOG.error("Test failed: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private void createConn1Test() {
        try {
            startLatch.await();
            LOG.info("[TEST 1] Starting createSingleConn test - 5 connections to us1/db1");

            final int TOTAL_CONNECTIONS = 5;
            Connection[] connections = new Connection[TOTAL_CONNECTIONS];
            Statement[] statements = new Statement[TOTAL_CONNECTIONS];

            // Create all connections using the proper builder pattern
            for (int i = 0; i < TOTAL_CONNECTIONS; i++) {
                connections[i] = getConnectionBuilder()
                    .withTServer(TSERVER_IDX)
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("us1")
                    .withDatabase("db1")
                    .connect();
                LOG.info("[TEST 1] Connected to db1 successfully with us1 - Connection " +
                    i);

                statements[i] = connections[i].createStatement();
                statements[i].execute("BEGIN");
                LOG.info("[TEST 1] Transaction started on Connection " + i +
                    " - BEGIN executed");
            }

            LOG.info("[TEST 1] Keeping all 5 transactions open for " + run_tests_for_secs +
                " seconds...");
            testHangUpCountDown.countDown();
            Thread.sleep(run_tests_for_secs * 1000);

            // Close all resources in a loop which will roll back / close the transactions as well.
            for (int i = 0; i < TOTAL_CONNECTIONS; i++) {
                if (statements[i] != null) statements[i].close();
                if (connections[i] != null) {
                    connections[i].close();
                }
            }
            Thread.sleep((STATS_UPDATE_INTERVAL + 2) * 1000);
            LOG.info("[TEST 1] Test completed - all connections closed");

        } catch (Exception e) {
            LOG.error("[TEST 1] Error: " + e.getMessage());
            e.printStackTrace();
        }
        finally {
            test_1_completed.set(true);
            LOG.info("[TEST 1] Signaling completion to hang-up test");
        }
    }

    private void createConn2Test() {
        try {
            startLatch.await();
            LOG.info("[TEST 2] Starting createSingleConn test - 5 connections to us1/db2");

            final int TOTAL_CONNECTIONS = 5;
            Connection[] connections = new Connection[TOTAL_CONNECTIONS];
            Statement[] statements = new Statement[TOTAL_CONNECTIONS];

            // Create all connections using the proper builder pattern
            for (int i = 0; i < TOTAL_CONNECTIONS; i++) {
                connections[i] = getConnectionBuilder()
                    .withTServer(TSERVER_IDX)
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("us1")
                    .withDatabase("db2")
                    .connect();
                LOG.info("[TEST 2] Connected to db2 successfully with us1 - Connection " +
                    i);

                statements[i] = connections[i].createStatement();
                statements[i].execute("BEGIN");
                LOG.info("[TEST 2] Transaction started on Connection " + i +
                    " - BEGIN executed");
            }

            LOG.info("[TEST 2] Keeping all 5 transactions open for " + run_tests_for_secs +
                " seconds...");
            testHangUpCountDown.countDown();
            Thread.sleep(run_tests_for_secs * 1000);

            // Close all resources in a loop which will roll back / close the transactions as well.
            for (int i = 0; i < TOTAL_CONNECTIONS; i++) {
                if (statements[i] != null) statements[i].close();
                if (connections[i] != null) {
                    connections[i].close();
                }
            }
            Thread.sleep((STATS_UPDATE_INTERVAL + 2) * 1000);
            LOG.info("[TEST 2] Test completed - all connections closed");

        } catch (Exception e) {
            LOG.error("[TEST 2] Error: " + e.getMessage());
            e.printStackTrace();
        }
        finally {
            test_2_completed.set(true);
            LOG.info("[TEST 2] Signaling completion to hang-up test");
        }
    }

    private void createHangUpTest() {
        try {
            startLatch.await();
            // Start this test after the first 2 tests have opened 5 transactions each.
            testHangUpCountDown.await();
            LOG.info("[TEST 3] Starting createHangUp test - 16 connections to yugabyte/yugabyte");

            final int TOTAL_CONNECTIONS = 16;
            Connection[] connections = new Connection[TOTAL_CONNECTIONS + 1];
            Statement[] statements = new Statement[TOTAL_CONNECTIONS + 1];

            // Create all connections using the proper builder pattern
            // All the connections will be created using auth-backend successfully.
            for (int i = 1; i <= TOTAL_CONNECTIONS; i++) {
                connections[i] = getConnectionBuilder()
                    .withTServer(TSERVER_IDX)
                    .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                    .withUser("yugabyte")
                    .withDatabase("yugabyte")
                    .connect();
                LOG.info("[TEST 3] Connected to yugabyte successfully with " +
                    "yugabyte - Connection " + i);

                statements[i] = connections[i].createStatement();
            }

            LOG.info("[TEST 3] Created all connections");

            int i = 1;
            for (; i <= TOTAL_CONNECTIONS; i++) {
                if (i == 5) {
                    // 5 active connections in db1/us1 and 5 active connections in db2/us1.
                    // And 4 already active connections in yugabyte/yugabyte which exhaust the
                    // ysql_max_connections limit.
                    // So, the 5th connection will be queued until one of the active connections in
                    // db1/us1 or db2/us1 is closed.
                    // On detach signal will be send to all the routes. Which will close the idle
                    // connection in db1/us1 or db2/us1.

                    final int connectionIndex = i;
                    Thread beginThread = new Thread(() -> {
                        try {
                            statements[connectionIndex].execute("BEGIN");
                        } catch (Exception ex) {
                            LOG.error("[TEST 3] Exception in BEGIN thread: " + ex.getMessage());
                        }
                    });
                    beginThread.start();
                    Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
                    // Untill both the tests are running, queued client can not be served.
                    double startTime = System.currentTimeMillis();
                    double maxDurationMillis = (run_tests_for_secs / 2) * 1000L;
                    while (!test_1_completed.get() && !test_2_completed.get()) {
                        // Just to avoid flakiness, ensure for half of the time when tests are
                        // running, below stats are verified.
                        if ((System.currentTimeMillis() - startTime) > maxDurationMillis) {
                            // Both tests runs for 30 secs.
                            Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
                            continue;
                        }
                        JsonObject pool = getPool("yugabyte", "yugabyte");
                        assertNotNull(pool);

                        assertEquals(4, pool.get("active_physical_connections").getAsInt());
                        assertEquals(4, pool.get("active_logical_connections").getAsInt());

                        assertEquals(1, pool.get("queued_logical_connections").getAsInt());

                        assertEquals(11, pool.get("waiting_logical_connections").getAsInt());

                        pool = getPool("db1", "us1");
                        assertNotNull(pool);
                        assertEquals(5, pool.get("active_physical_connections").getAsInt());
                        assertEquals(5, pool.get("active_logical_connections").getAsInt());

                        pool = getPool("db2", "us1");
                        assertNotNull(pool);
                        assertEquals(5, pool.get("active_physical_connections").getAsInt());
                        assertEquals(5, pool.get("active_logical_connections").getAsInt());
                        Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
                    }
                    // As soon as one of the test gets completed, it will be in position to serve
                    // the queued client.
                    beginThread.join(500);
                    try {
                        if (beginThread.isAlive()) {
                            throw new Exception("Queue client should have been served on " +
                             "detaching a server from db1/us1 or db2/us1 route");
                        }
                    } catch (Exception e) {
                        LOG.error("[TEST 3] Exception in BEGIN thread: " + e.getMessage());
                        fail(String.format("No transactional backend to execute BEGIN %s",
                            e.getMessage()));
                    }

                    Thread.sleep((STATS_UPDATE_INTERVAL + 2) * 1000);
                    JsonObject pool = getPool("yugabyte", "yugabyte");
                    assertNotNull(pool);
                    assertEquals(5, pool.get("active_physical_connections").getAsInt());

                    // Queue client has been served with multi route pooling enabled.
                    assertEquals(0, pool.get("queued_logical_connections").getAsInt());

                    assertEquals(11, pool.get("waiting_logical_connections").getAsInt());

                    // Make sure both the tests are completed.
                    while (!test_1_completed.get() || !test_2_completed.get()) {
                        Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
                    }

                }
                // else if (i == 6) {
                // At this point, One of the route out of us1/db1 or us1/db2 will have 5 and other
                // will 4 idle connections. The per_route_quota limit is
                // ysql_max_connection(14)/active_routes(3) = 4. So, 6th connection would directly
                // close the 5th idle physical connection from the route it belongs to.
                // And the client would be served without ever get into the waiting state.
                // }
                else if (i == 7) {
                    // Wait for IDLE_TIMEOUT seconds to get server backend to expire. The cron
                    // thread will send signal on closing the idle connection to db1/us1 & db2/us1
                    // which will server the queued client in yugabyte/yugabyte route.

                    final int connectionIndex = i;
                    Thread beginThread = new Thread(() -> {
                        try {
                            statements[connectionIndex].execute("BEGIN");
                        } catch (Exception ex) {
                            LOG.error("[TEST 3] Exception in BEGIN thread: " + ex.getMessage());
                        }
                    });
                    beginThread.start();
                    Thread.sleep((STATS_UPDATE_INTERVAL + 2) * 1000);
                    double startTime = System.currentTimeMillis();
                    double duration = (0.3 * IDLE_TIMEOUT) * 1000L;
                    while (System.currentTimeMillis() - startTime < duration) {
                        JsonObject pool = getPool("yugabyte", "yugabyte");
                        assertNotNull(pool);
                        assertEquals(6, pool.get("active_physical_connections").getAsInt());
                        assertEquals(6, pool.get("active_logical_connections").getAsInt());

                        assertEquals(1, pool.get("queued_logical_connections").getAsInt());

                        assertEquals(9, pool.get("waiting_logical_connections").getAsInt());

                        pool = getPool("db1", "us1");
                        assertNotNull(pool);
                        assertEquals(4, pool.get("idle_physical_connections").getAsInt());

                        pool = getPool("db2", "us1");
                        assertNotNull(pool);
                        assertEquals(4, pool.get("idle_physical_connections").getAsInt());
                        Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
                    }
                    beginThread.join(IDLE_TIMEOUT * 1000);
                    // After IDLE_TIMEOUT seconds, idle connection in db1/us1 or db2/us1 will start
                    // getting closed by cron thread. And cron thread will send signal
                    // yugabyte/yugabyte route which will server the queued client with multi route
                    // pooling.

                    try {
                        if (beginThread.isAlive()) {
                            throw new Exception("After idle server expire time " +
                                "queued client should have been served by closing a backend from " +
                                "db1/us1 or db2/us1 route");
                        }
                    } catch (Exception e) {
                        LOG.error("[TEST 3] Exception in BEGIN thread: " + e.getMessage());
                        fail(String.format("No transactional backend to execute BEGIN %s",
                            e.getMessage()));
                    }

                    Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
                    JsonObject pool = getPool("yugabyte", "yugabyte");
                    assertNotNull(pool);
                    assertEquals(7, pool.get("active_physical_connections").getAsInt());
                    assertEquals(7, pool.get("active_logical_connections").getAsInt());

                    // Queue client has been served with multi route pooling enabled.
                    assertEquals(0, pool.get("queued_logical_connections").getAsInt());

                }
                // else if (i >= 8 && i <= 12) {
                // Connection of yugabyte/yugabyt pool would create a new transactional backend
                // as the idle physical connections in other routes will reduced to 1 each pool.
                // }
                else if (i == 13) {
                    // Now wait for 2*IDLE_TIMEOUT seconds (as 1*IDLE_TIMEOUT has already
                    // passed). So last connections in us1/db1 and us1/db2 will be closed.
                    // And as cron thread will send signal on closing the idle connection on
                    // db1/us1, db2/us1 and yugabyte/yugabyte routes. And queued client will be
                    // served.
                    final int connectionIndex = i;
                    Thread beginThread = new Thread(() -> {
                        try {
                            statements[connectionIndex].execute("BEGIN");
                        } catch (Exception ex) {
                            LOG.error("[TEST 3] Exception in BEGIN thread: " + ex.getMessage());
                        }
                    });
                    beginThread.start();
                    // By 1*IDLE_TIMEOUT + some STATS_UPDATE_INTERVAL has already passed. Setting
                    // max duration to 3*IDLE_TIMEOUT for the last connection of the us1/db1
                    //  and us1/db2 pool to get expired.
                    beginThread.join( 3 * IDLE_TIMEOUT * 1000);
                    try {
                        if (beginThread.isAlive()) {
                            throw new Exception("After waiting for more than 3*IDLE_TIMEOUT " +
                                "seconds to ensure last idle server backend of route is closed, " +
                                "transactional backend can not be still created for the queued " +
                                "client to serve");
                        }
                    } catch (Exception e) {
                        LOG.error("[TEST 3] Exception in BEGIN thread: " + e.getMessage());
                        fail(String.format("No transactional backend to execute BEGIN %s",
                            e.getMessage()));
                    }

                }
                else if (i == 15) {
                    // TODO: (mkumar) #28230 - The connection would wait infinitely and never be
                    // able to run BEGIN.
                    final boolean[] beginCompleted = {false};
                    Thread beginThread = new Thread(() -> {
                        try {
                            statements[15].execute("BEGIN");
                            beginCompleted[0] = true;
                        } catch (Exception ex) {
                            LOG.error("[TEST 3] Exception in BEGIN thread: " + ex.getMessage());
                        }
                    });
                    beginThread.start();
                    Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
                    beginThread.join(4 * IDLE_TIMEOUT * 1000);
                    JsonObject pool = getPool("yugabyte", "yugabyte");
                    assertNotNull(pool);
                    // All physical connections are exhausted.
                    assertEquals(MAX_CONNECTIONS,
                        pool.get("active_physical_connections").getAsInt());
                    assertEquals(MAX_CONNECTIONS,
                        pool.get("active_logical_connections").getAsInt());
                    assertEquals(1, pool.get("queued_logical_connections").getAsInt());
                    // The programs here and won't be able to proceed forward. Untill one of the
                    // transaction gets completed.
                    // TODO: (mkumar) #28230 - This particular connection would wait infinitely and
                    // never be able to run BEGIN. Which is in different from behaviour without
                    // multi route pooling.

                    try {
                        if (beginThread.isAlive() || !beginCompleted[0]) {
                            LOG.info("Test has hang up after all the physical connections are " +
                                "exhausted, expectedly");
                        }
                        else {
                            throw new Exception("BEGIN completed unexpectedly");
                        }
                    }
                    catch (Exception e) {
                        LOG.error("[TEST 3] Exception in BEGIN thread: " + e.getMessage());
                        fail(String.format("BEGIN completed unexpectedly %s", e.getMessage()));
                    }

                    break;
                }
                else {
                    statements[i].execute("BEGIN");
                }
                    LOG.info("[TEST 3] Transaction started on Connection " + i +
                        " - BEGIN executed");
            }

            assertEquals(i, 15);

            // Close all resources in a loop
            i = 1;
            for (; i <= TOTAL_CONNECTIONS; i++) {
                if (statements[i] != null) statements[i].close();
                if (connections[i] != null) {
                    connections[i].close();
                }
            }
            LOG.info("[TEST 3] Test completed - all connections closed");

        } catch (Exception e) {
            LOG.error("[TEST 3] Error: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
