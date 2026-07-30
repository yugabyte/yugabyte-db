package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.fail;
import java.sql.Connection;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;
import org.yb.pgsql.ConnectionEndpoint;
import com.google.gson.JsonObject;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestHangUpOnRouteSignals extends BaseYsqlConnMgr {

    private static final int BUFFER_TIME_SEC = 4;
    private static final int IDLE_TIMEOUT_SEC = 8;
    private static final int MAX_CONNECTIONS = 14;

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        disableWarmupRandomMode(builder);
        builder.addCommonTServerFlag("ysql_conn_mgr_jitter_time", "0");
        builder.addCommonTServerFlag("ysql_conn_mgr_enable_multi_route_pool", "true");
        builder.addCommonTServerFlag("ysql_max_connections",
                String.valueOf(MAX_CONNECTIONS));
        builder.addCommonTServerFlag("ysql_conn_mgr_use_auth_backend", "true");
        builder.addCommonTServerFlag("ysql_conn_mgr_idle_time", String.valueOf(IDLE_TIMEOUT_SEC));
        builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
                String.valueOf(STATS_UPDATE_INTERVAL));
    }

    private void ensureSetup() throws Exception {
        try (Connection conn = getConnectionBuilder()
                .withTServer(TSERVER_IDX)
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .withUser("yugabyte")
                .withPassword("yugabyte")
                .connect();
             Statement stmt = conn.createStatement()) {
            stmt.execute("DROP DATABASE IF EXISTS db1");
            stmt.execute("DROP USER IF EXISTS us1");
            stmt.execute("CREATE DATABASE db1");
            stmt.execute("CREATE USER us1 WITH LOGIN");
        }
    }

    private void waitForCleanPoolState() throws Exception {
        long deadlineMs = System.currentTimeMillis()
            + (IDLE_TIMEOUT_SEC + STATS_UPDATE_INTERVAL + 5) * 1000L;
        while (System.currentTimeMillis() < deadlineMs) {
            JsonObject pool = getPool("db1", "us1");
            if (pool == null) return;
            int active = pool.get("active_physical_connections").getAsInt();
            int idle = pool.get("idle_physical_connections").getAsInt();
            if (active == 0 && idle == 0) return;
            Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
        }
        fail("Failed to clear the test user pool");
    }

    private void closeAll(Connection[] connections) {
        for (Connection conn : connections) {
            if (conn != null) {
                try { conn.close(); } catch (Exception e) { /* ignore */ }
            }
        }
    }

    private Connection connectYugabyte() throws Exception {
        return getConnectionBuilder()
            .withTServer(TSERVER_IDX)
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("yugabyte")
            .withDatabase("yugabyte")
            .connect();
    }

    private Connection connectDb1() throws Exception {
        return getConnectionBuilder()
            .withTServer(TSERVER_IDX)
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("us1")
            .withDatabase("db1")
            .connect();
    }


    // With 10 active transactions in db1/us1 and 4 in yugabyte/yugabyte the
    // ysql_max_connections limit (14) is exhausted. The 5th yugabyte BEGIN
    // queues. Closing the db1/us1 connections fires a detach signal that lets
    // the queued client through.
    @Test
    public void testQueuedClientServedOnRouteDetach() throws Exception {
        ensureSetup();
        waitForCleanPoolState();

        final int DB1_COUNT = 10;
        final int YB_COUNT = 5;
        Connection[] db1Conns = new Connection[DB1_COUNT];
        Statement[] db1Stmts = new Statement[DB1_COUNT];
        Connection[] ybConns = new Connection[YB_COUNT];
        Statement[] ybStmts = new Statement[YB_COUNT];

        try {
            // Phase 1: authenticate all connections before any BEGINs
            // (auth-backend needs a free physical backend for each handshake).
            for (int i = 0; i < DB1_COUNT; i++) {
                db1Conns[i] = connectDb1();
                db1Stmts[i] = db1Conns[i].createStatement();
            }
            for (int i = 0; i < YB_COUNT; i++) {
                ybConns[i] = connectYugabyte();
                ybStmts[i] = ybConns[i].createStatement();
            }

            // Phase 2: occupy backends (10 db1 + 4 yb = 14 = MAX)
            for (int i = 0; i < DB1_COUNT; i++) {
                db1Stmts[i].execute("BEGIN");
            }
            for (int i = 0; i < YB_COUNT - 1; i++) {
                ybStmts[i].execute("BEGIN");
            }

            // Phase 3: 5th yugabyte BEGIN should queue
            final int queuedIdx = YB_COUNT - 1;
            Thread beginThread = new Thread(() -> {
                try {
                    ybStmts[queuedIdx].execute("BEGIN");
                } catch (Exception ex) {
                    LOG.error("Exception in queued BEGIN thread: " + ex.getMessage());
                }
            });
            beginThread.start();
            Thread.sleep(STATS_UPDATE_INTERVAL * 1000);

            JsonObject pool = getPool("yugabyte", "yugabyte");
            assertNotNull(pool);
            assertEquals(YB_COUNT - 1,
                pool.get("active_physical_connections").getAsInt());
            assertEquals(1, pool.get("queued_logical_connections").getAsInt());

            pool = getPool("db1", "us1");
            assertNotNull(pool);
            assertEquals(DB1_COUNT,
                pool.get("active_physical_connections").getAsInt());

            // Close db1 connections -> triggers detach signal
            for (int i = 0; i < DB1_COUNT; i++) {
                db1Stmts[i].close();
                db1Conns[i].close();
                db1Stmts[i] = null;
                db1Conns[i] = null;
            }

            beginThread.join(BUFFER_TIME_SEC * 1000);
            assertFalse(
                "Queued client should have been served on detaching db1/us1 route",
                beginThread.isAlive());

            Thread.sleep((STATS_UPDATE_INTERVAL + 2) * 1000);
            pool = getPool("yugabyte", "yugabyte");
            assertNotNull(pool);
            assertEquals(YB_COUNT,
                pool.get("active_physical_connections").getAsInt());
            assertEquals(0, pool.get("queued_logical_connections").getAsInt());

        } finally {
            closeAll(db1Conns);
            closeAll(ybConns);
        }
    }

    // 8 idle backends are spawned in the db1/us1 pool and 6 active transactions
    // run in yugabyte/yugabyte, totalling MAX_CONNECTIONS. The 7th yugabyte
    // BEGIN queues. After IDLE_TIMEOUT_SEC the cron thread closes the expired idle
    // backends and signals the yugabyte route, serving the queued client.
    @Test
    public void testQueuedClientServedOnIdleTimeout() throws Exception {
        ensureSetup();
        waitForCleanPoolState();

        final int IDLE_BACKEND_COUNT = 7;
        final int YB_COUNT = 8;
        Connection[] ybConns = new Connection[YB_COUNT];
        Statement[] ybStmts = new Statement[YB_COUNT];
        Connection[] idleConns = new Connection[IDLE_BACKEND_COUNT];

        try {
            // Phase 1: create idle backends in db1/us1.
            // All connections must be open simultaneously so each gets its own
            // physical backend; closing them afterwards leaves those backends
            // idle in the pool.
            for (int i = 0; i < IDLE_BACKEND_COUNT; i++) {
                idleConns[i] = connectDb1();
                idleConns[i].createStatement().execute("BEGIN");
            }

            for (int i = 0; i < IDLE_BACKEND_COUNT; i++) {
                idleConns[i].close();
                idleConns[i] = null;
            }

            // Phase 2: authenticate yugabyte connections
            for (int i = 0; i < YB_COUNT; i++) {
                ybConns[i] = connectYugabyte();
                ybStmts[i] = ybConns[i].createStatement();
            }

            // Phase 3: occupy backends (7 active yb + 7 idle db1 = 14 = MAX)
            for (int i = 0; i < YB_COUNT - 1; i++) {
                ybStmts[i].execute("BEGIN");
            }

            // Phase 4: 8th yugabyte BEGIN should queue
            final int queuedIdx = YB_COUNT - 1;
            Thread beginThread = new Thread(() -> {
                try {
                    ybStmts[queuedIdx].execute("BEGIN");
                } catch (Exception ex) {
                    LOG.error("Exception in queued BEGIN thread: " + ex.getMessage());
                }
            });
            beginThread.start();
            Thread.sleep((STATS_UPDATE_INTERVAL + 1) * 1000);

            JsonObject db1Pool = getPool("db1", "us1");
            assertNotNull(db1Pool);
            assertEquals(IDLE_BACKEND_COUNT,
                db1Pool.get("idle_physical_connections").getAsInt());

            JsonObject pool = getPool("yugabyte", "yugabyte");
            assertNotNull(pool);
            assertEquals(YB_COUNT - 1,
                pool.get("active_physical_connections").getAsInt());
            assertEquals(1, pool.get("queued_logical_connections").getAsInt());

            // Wait for idle timeout -- cron closes expired backends, signals
            // queued client
            long joinTimeoutMs = (IDLE_TIMEOUT_SEC + BUFFER_TIME_SEC) * 1000L;
            beginThread.join(joinTimeoutMs);
            assertFalse(
                "Queued client should have been served after idle backends expired",
                beginThread.isAlive());

            Thread.sleep(STATS_UPDATE_INTERVAL * 1000);
            pool = getPool("yugabyte", "yugabyte");
            assertNotNull(pool);
            assertEquals(YB_COUNT,
                pool.get("active_physical_connections").getAsInt());
            assertEquals(0, pool.get("queued_logical_connections").getAsInt());

        } finally {
            closeAll(idleConns);
            closeAll(ybConns);
        }
    }

    // Only 1 idle backend is spawned in db1/us1 alongside 13 active yugabyte
    // transactions, totalling MAX_CONNECTIONS. The 14th yugabyte BEGIN queues
    // and is served once the last idle backends expire after IDLE_TIMEOUT_SEC.
    // This is a separate test as CM only relinquishes backends after 3 * TTL when
    // the number of backends in a pool falls below `min_pool_size` (here, 1).
    @Test
    public void testQueuedClientServedAfterLastIdleExpires() throws Exception {
        ensureSetup();
        waitForCleanPoolState();

        final int IDLE_BACKEND_COUNT = 1;
        final int YB_COUNT = 14;
        Connection[] ybConns = new Connection[YB_COUNT];
        Statement[] ybStmts = new Statement[YB_COUNT];
        Connection[] idleConns = new Connection[IDLE_BACKEND_COUNT];

        try {
            // Phase 1: create idle backends in db1/us1
            for (int i = 0; i < IDLE_BACKEND_COUNT; i++) {
                idleConns[i] = connectDb1();
                idleConns[i].createStatement().execute("SELECT 1");
            }
            for (int i = 0; i < IDLE_BACKEND_COUNT; i++) {
                idleConns[i].close();
                idleConns[i] = null;
            }

            // Phase 2: authenticate yugabyte connections
            for (int i = 0; i < YB_COUNT; i++) {
                ybConns[i] = connectYugabyte();
                ybStmts[i] = ybConns[i].createStatement();
            }

            // Phase 3: occupy backends (13 active yb + 1 idle db1 = 14 = MAX)
            for (int i = 0; i < YB_COUNT - 1; i++) {
                ybStmts[i].execute("BEGIN");
            }

            // Phase 4: 14th yugabyte BEGIN should queue, then complete once the
            // last idle backends expire.
            final int queuedIdx = YB_COUNT - 1;
            Thread beginThread = new Thread(() -> {
                try {
                    ybStmts[queuedIdx].execute("BEGIN");
                } catch (Exception ex) {
                    LOG.error("Exception in queued BEGIN thread: " + ex.getMessage());
                }
            });
            beginThread.start();

            Thread.sleep((STATS_UPDATE_INTERVAL + 1) * 1_000);

            JsonObject db1Pool = getPool("db1", "us1");
            assertNotNull(db1Pool);
            assertEquals(IDLE_BACKEND_COUNT,
                db1Pool.get("idle_physical_connections").getAsInt());

            JsonObject pool = getPool("yugabyte", "yugabyte");
            assertNotNull(pool);
            assertEquals(YB_COUNT - 1,
                pool.get("active_physical_connections").getAsInt());
            assertEquals(1, pool.get("queued_logical_connections").getAsInt());

            long joinTimeoutMs = (3 * IDLE_TIMEOUT_SEC + BUFFER_TIME_SEC) * 1000L;
            beginThread.join(joinTimeoutMs);
            assertFalse(
                "Queued client should have been served after last idle backends expired",
                beginThread.isAlive());

            beginThread.interrupt();
            beginThread.join();

        } finally {
            closeAll(idleConns);
            closeAll(ybConns);
        }
    }

    // All MAX_CONNECTIONS physical backends are occupied by active yugabyte
    // transactions and no idle backends exist anywhere. The 15th BEGIN queues
    // and can never be served.
    @Test
    public void testHangOnAllPhysicalConnectionsExhausted() throws Exception {
        ensureSetup();
        waitForCleanPoolState();

        final int YB_COUNT = MAX_CONNECTIONS + 1;
        Connection[] ybConns = new Connection[YB_COUNT];
        Statement[] ybStmts = new Statement[YB_COUNT];

        try {
            // Phase 1: authenticate all connections
            for (int i = 0; i < YB_COUNT; i++) {
                ybConns[i] = connectYugabyte();
                ybStmts[i] = ybConns[i].createStatement();
            }

            // Phase 2: occupy all backends (14 = MAX_CONNECTIONS)
            for (int i = 0; i < MAX_CONNECTIONS; i++) {
                ybStmts[i].execute("BEGIN");
            }

            // Phase 3: 15th BEGIN should queue and never complete
            final boolean[] beginCompleted = {false};
            Thread beginThread = new Thread(() -> {
                try {
                    ybStmts[MAX_CONNECTIONS].execute("BEGIN");
                    beginCompleted[0] = true;
                } catch (Exception ex) {
                    LOG.error("Exception in queued BEGIN thread: " + ex.getMessage());
                }
            });
            beginThread.start();

            beginThread.join((IDLE_TIMEOUT_SEC + BUFFER_TIME_SEC) * 1000L);
            assertTrue(
                "BEGIN should NOT have completed when all physical connections "
                + "are exhausted (see #28230)",
                beginThread.isAlive() || !beginCompleted[0]);

            JsonObject pool = getPool("yugabyte", "yugabyte");
            assertNotNull(pool);
            assertEquals(MAX_CONNECTIONS,
                pool.get("active_physical_connections").getAsInt());
            assertEquals(MAX_CONNECTIONS,
                pool.get("active_logical_connections").getAsInt());
            assertEquals(1, pool.get("queued_logical_connections").getAsInt());

        } finally {
            closeAll(ybConns);
        }
    }

    // --- Potential additional test cases (from the original monolithic test) ---
    //
    // Per-route quota direct eviction (was i==6):
    //   After the detach test resolves, one route (e.g. db1/us1) has 5 idle
    //   backends and another has 4. The per_route_quota is
    //   ysql_max_connections / active_routes (e.g. 14/3 = 4). The 6th
    //   yugabyte BEGIN would directly close the 5th idle backend from the
    //   over-quota route, so the client is served without ever entering the
    //   waiting state. Validating this requires at least two non-yugabyte
    //   routes to produce the asymmetry.
    //
    // Gradual idle reclamation (was i==8 through i==12):
    //   As idle backends in other routes are progressively reclaimed (reduced
    //   to ~1 per pool), each new yugabyte BEGIN creates a fresh
    //   transactional backend in place of the closed idle one. This is the
    //   steady-state behaviour between the queuing edge-cases and does not
    //   need dedicated assertions, but could be tested by verifying the idle
    //   count decreases monotonically across successive BEGINs.
}
