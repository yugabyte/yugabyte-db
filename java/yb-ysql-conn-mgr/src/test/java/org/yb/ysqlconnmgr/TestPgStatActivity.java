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

import java.lang.management.ManagementFactory;
import java.net.InetSocketAddress;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.fail;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.RequiresLinux;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.ProcessUtil;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestPgStatActivity extends BaseYsqlConnMgr {

    private static final String QUERY = "SELECT client_addr, client_port, " +
                       "client_hostname, query, application_name FROM " +
                       "pg_stat_activity WHERE backend_type = " +
                       "'yb-conn-mgr worker connection'";
    private static final String expectedApplicationName = "PostgreSQL JDBC Driver";
    private static final String expectedClientHostname = "localhost";

    private static Map<String, String> createTserverFlags(
        boolean isInit, boolean enableAuthBackend) {
        Map<String, String> flags = new HashMap<>();
        flags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
        flags.put("ysql_pg_conf_csv", "log_hostname=on");
        flags.put("ysql_conn_mgr_log_settings", "log_debug,log_query");

        // Need to do this weird hack as flags set in CustomizeMiniClusterBuilder are not
        // overridden by restartClusterWithFlags for some reason.
        if (!isInit) {
            flags.put("ysql_conn_mgr_use_auth_backend",
                enableAuthBackend ? "true" : "false");
        }
        return flags;
    }

    @Override
    protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
        super.customizeMiniClusterBuilder(builder);
        builder.addCommonTServerFlags(createTserverFlags(true, false));
    }

    /**
     * Find out all the connections with destHost:destPort using ss command.
     */
    private List<InetSocketAddress> getJvmConnectionsTo(String destHost, int destPort)
            throws Exception {
        String jvmPid = ManagementFactory.getRuntimeMXBean().getName().split("@")[0];
        String output = ProcessUtil.runProcess(
            Arrays.asList("ss", "-tnp", "dst", destHost + ":" + destPort), 30);

        LOG.info("ss output for dest {}:{}:\n{}", destHost, destPort, output);

        List<InetSocketAddress> connections = new ArrayList<>();
        for (String line : output.split("\n")) {
            if (!line.contains("pid=" + jvmPid)) {
                continue;
            }
            // ss -tnp output fields (space-separated):
            // [0] State  [1] Recv-Q  [2] Send-Q
            // [3] Local Address:Port  [4] Peer Address:Port  [5] Process
            String[] fields = line.trim().split("\\s+");
            if (fields.length < 4) {
                continue;
            }
            String localAddrPort = fields[3];
            int lastColon = localAddrPort.lastIndexOf(':');
            if (lastColon < 0) {
                continue;
            }
            String addr = localAddrPort.substring(0, lastColon);
            int port = Integer.parseInt(localAddrPort.substring(lastColon + 1));
            connections.add(new InetSocketAddress(addr, port));
        }
        return connections;
    }

    @Test
    public void testPgStatActivityAuthBackend() throws Exception {
        restartClusterWithFlags(Collections.emptyMap(), createTserverFlags(false, true));
        runPgStatActivityTest();
    }

    @Test
    public void testPgStatActivityAuthPassthrough() throws Exception {
        restartClusterWithFlags(Collections.emptyMap(), createTserverFlags(false, false));
        runPgStatActivityTest();
    }

    private Connection connectViaConnMgr(int tserverIndex) throws Exception {
        return getConnectionBuilder()
            .withTServer(tserverIndex)
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("yugabyte")
            .withPassword("yugabyte")
            .connect();
    }

    /*
     * Returns the single JVM -> Ysql Conn Mgr connection present in after but
     * not in before.
     */
    private InetSocketAddress newConnection(List<InetSocketAddress> before,
            List<InetSocketAddress> after) {
        List<InetSocketAddress> added = new ArrayList<>(after);
        added.removeAll(before);
        assertEquals("expected exactly one new JVM connection to Ysql Conn Mgr, got " + added,
            1, added.size());
        return added.get(0);
    }

    private void assertInetClientAddrPort(Connection conn, InetSocketAddress expected)
            throws Exception {
        ResultSet rs = conn.createStatement().executeQuery(
            "SELECT host(inet_client_addr()), inet_client_port()");
        assertTrue(rs.next());
        String clientAddr = rs.getString(1);
        assertNotNull("inet_client_addr() should be populated", clientAddr);
        int clientPort = rs.getInt(2);
        LOG.info("inet_client_addr()={} inet_client_port()={}", clientAddr, clientPort);
        assertEquals("inet_client_addr():inet_client_port() should match the logical client",
            expected, new InetSocketAddress(clientAddr, clientPort));
    }

    /*
     * Verifies that inet_client_addr() and inet_client_port() return the
     * address of the logical client, not of Ysql Conn Mgr, and follow the
     * logical client when the backend is reused by another one.
     */
    @Test
    public void testInetClientAddrPort() throws Exception {
        final int tserverIndex = 0;
        InetSocketAddress ysqlConnMgrAddr =
            miniCluster.getYsqlConnMgrContactPoints().get(tserverIndex);
        String ysqlConnMgrHost = ysqlConnMgrAddr.getHostName();
        int ysqlConnMgrPort = ysqlConnMgrAddr.getPort();

        PgLogCollector pgLog = new PgLogCollector(SAVED_DEFAULT_WARNING, LOG_MARKER);
        miniCluster.getTabletServers().values()
            .forEach(ts -> ts.getLogPrinter().addErrorListener(pgLog));

        List<InetSocketAddress> before = getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort);
        try (Connection conn1 = connectViaConnMgr(tserverIndex)) {
            List<InetSocketAddress> afterConn1 =
                getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort);
            InetSocketAddress client1 = newConnection(before, afterConn1);

            try (Connection conn2 = connectViaConnMgr(tserverIndex)) {
                InetSocketAddress client2 = newConnection(afterConn1,
                    getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort));

                // Alternate between the clients so that a shared backend is
                // re-attached to a different logical client each time.
                for (int i = 0; i < 3; ++i) {
                    assertInetClientAddrPort(conn1, client1);
                    assertInetClientAddrPort(conn2, client2);
                }

                // The 'g' packet must restore the defaults the 'G' packet set
                // for these PGC_BACKEND GUCs, or the next 'G' warns. Postgres
                // logs in order, so once the marker is seen any such warning
                // has been seen too.
                conn2.createStatement().execute(
                    "DO $$ BEGIN RAISE WARNING '" + LOG_MARKER + "'; END $$");
                assertTrue("marker not found in postgres log",
                    pgLog.waitFor(LOG_MARKER, 30_000));
                assertEquals("unexpected postgres log lines",
                    Collections.emptyList(), pgLog.linesWith(SAVED_DEFAULT_WARNING));
            }
        }
    }

    private static final int ASH_PG_SLEEP_SECONDS = 3;

    /*
     * Runs a write, which makes tserver RPCs, and a pg_sleep, which is always
     * sampled, on the client's own table so that the queries have the client's
     * own query ids.
     */
    private void runAshWorkload(Connection conn, String table) throws Exception {
        conn.createStatement().execute(
            "INSERT INTO " + table + " SELECT i FROM generate_series(1, 1000) i");
        conn.createStatement().execute(
            "SELECT pg_sleep(" + ASH_PG_SLEEP_SECONDS + ") FROM " + table + " LIMIT 1");
    }

    /*
     * Checks that every ASH sample of the queries on table, from postgres and
     * from the tserver RPCs they made, reports the expected logical client.
     */
    private void assertAshClientNodeIp(Connection conn, String table,
            InetSocketAddress expected) throws Exception {
        ResultSet rs = conn.createStatement().executeQuery(
            "SELECT wait_event_component, wait_event, client_node_ip" +
            " FROM yb_active_session_history WHERE query_id IN" +
            " (SELECT queryid FROM pg_stat_statements WHERE query LIKE '%" + table + "%')");
        int pgSleepSamples = 0;
        while (rs.next()) {
            String clientNodeIp = rs.getString("client_node_ip");
            LOG.info("{} {} client_node_ip={}", rs.getString("wait_event_component"),
                rs.getString("wait_event"), clientNodeIp);
            assertNotNull("client_node_ip should be populated", clientNodeIp);
            // ip:port for IPv4, [ip]:port for IPv6.
            int lastColon = clientNodeIp.lastIndexOf(':');
            String addr = clientNodeIp.substring(0, lastColon).replaceAll("[\\[\\]]", "");
            int port = Integer.parseInt(clientNodeIp.substring(lastColon + 1));
            assertEquals("client_node_ip should match the logical client",
                expected, new InetSocketAddress(addr, port));
            if ("PgSleep".equals(rs.getString("wait_event"))) {
                ++pgSleepSamples;
            }
        }
        assertTrue("expected PgSleep samples in yb_active_session_history",
            pgSleepSamples > 0);
    }

    /*
     * Verifies that yb_active_session_history reports the address and port of
     * the logical client, not of Ysql Conn Mgr, and follows the logical client
     * when the backend is reused by another one.
     */
    @Test
    public void testAshClientNodeIp() throws Exception {
        final int tserverIndex = 0;
        InetSocketAddress ysqlConnMgrAddr =
            miniCluster.getYsqlConnMgrContactPoints().get(tserverIndex);
        String ysqlConnMgrHost = ysqlConnMgrAddr.getHostName();
        int ysqlConnMgrPort = ysqlConnMgrAddr.getPort();

        List<InetSocketAddress> before = getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort);
        try (Connection conn1 = connectViaConnMgr(tserverIndex)) {
            List<InetSocketAddress> afterConn1 =
                getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort);
            InetSocketAddress client1 = newConnection(before, afterConn1);

            try (Connection conn2 = connectViaConnMgr(tserverIndex)) {
                InetSocketAddress client2 = newConnection(afterConn1,
                    getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort));

                conn1.createStatement().execute("CREATE TABLE ash_t1 (k INT)");
                conn2.createStatement().execute("CREATE TABLE ash_t2 (k INT)");

                // Alternate between the clients so that a shared backend is
                // re-attached to a different logical client each time.
                for (int i = 0; i < 2; ++i) {
                    runAshWorkload(conn1, "ash_t1");
                    runAshWorkload(conn2, "ash_t2");
                }
                assertAshClientNodeIp(conn1, "ash_t1", client1);
                assertAshClientNodeIp(conn2, "ash_t2", client2);
            }
        }
    }

    private static final String SAVED_DEFAULT_WARNING = "ysql_conn_mgr_saved_default already set";
    private static final String LOG_MARKER = "TestPgStatActivity log marker";

    /* Keeps the tserver log lines that contain one of the given substrings. */
    private static class PgLogCollector implements LogErrorListener {
        private final List<String> substrings;
        private final List<String> lines = new ArrayList<>();

        PgLogCollector(String... substrings) {
            this.substrings = Arrays.asList(substrings);
        }

        @Override
        public void handleLine(String line) {
            if (substrings.stream().anyMatch(line::contains)) {
                synchronized (lines) {
                    lines.add(line);
                    lines.notifyAll();
                }
            }
        }

        @Override
        public void reportErrorsAtEnd() {}

        List<String> linesWith(String substring) {
            synchronized (lines) {
                return lines.stream().filter(l -> l.contains(substring))
                    .collect(Collectors.toList());
            }
        }

        boolean waitFor(String substring, long timeoutMs) throws InterruptedException {
            long deadline = System.currentTimeMillis() + timeoutMs;
            synchronized (lines) {
                while (linesWith(substring).isEmpty()) {
                    long waitMs = deadline - System.currentTimeMillis();
                    if (waitMs <= 0) {
                        return false;
                    }
                    lines.wait(waitMs);
                }
                return true;
            }
        }
    }

    /*
     * Verifies that a client cannot override the logical client details that
     * Ysql Conn Mgr forwards, since inet_client_addr(), inet_client_port() and
     * pg_stat_activity report them.
     */
    @Test
    public void testClientCannotSetConnMgrClientInfo() throws Exception {
        final int tserverIndex = 0;
        InetSocketAddress ysqlConnMgrAddr =
            miniCluster.getYsqlConnMgrContactPoints().get(tserverIndex);
        String ysqlConnMgrHost = ysqlConnMgrAddr.getHostName();
        int ysqlConnMgrPort = ysqlConnMgrAddr.getPort();

        List<InetSocketAddress> before = getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort);
        try (Connection conn = connectViaConnMgr(tserverIndex)) {
            InetSocketAddress client = newConnection(before,
                getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort));

            String[] statements = {
                "SET yb_ycm_internal_client_addr = '192.0.2.1'",
                "SET yb_ycm_internal_client_port = 4242",
                "SET yb_conn_mgr_client_hostname = 'example.com'",
                "SELECT set_config('yb_ycm_internal_client_addr', '192.0.2.1', false)",
                // Clearing them would make inet_client_addr() fall back to
                // Ysql Conn Mgr's own socket.
                "SET yb_ycm_internal_client_addr = ''",
                "SET yb_ycm_internal_client_port = -1",
                "RESET yb_ycm_internal_client_addr",
            };
            for (String stmt : statements) {
                try {
                    conn.createStatement().execute(stmt);
                    fail("expected '" + stmt + "' to fail");
                } catch (SQLException e) {
                    assertTrue("unexpected error for '" + stmt + "': " + e.getMessage(),
                        e.getMessage().contains("cannot be set after connection start"));
                }
            }
            // RESET ALL succeeds but must leave them alone.
            conn.createStatement().execute("RESET ALL");
            assertInetClientAddrPort(conn, client);
        }
    }

    /*
     * Verifies that pg_stat_activity shows the correct information of logical
     * client connections via Ysql Conn Mgr.
     */
    private void runPgStatActivityTest() throws Exception {
        final int tserverIndex = 0;
        InetSocketAddress ysqlConnMgrAddr =
            miniCluster.getYsqlConnMgrContactPoints().get(tserverIndex);
        String ysqlConnMgrHost = ysqlConnMgrAddr.getHostName();
        int ysqlConnMgrPort = ysqlConnMgrAddr.getPort();
        LOG.info("Ysql Conn Mgr address: {}:{}", ysqlConnMgrHost, ysqlConnMgrPort);
        String expectedClientAddr = null;
        int expectedClientPort = 0;

        try (Connection conn = getConnectionBuilder()
            .withTServer(tserverIndex)
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("yugabyte")
            .withPassword("yugabyte")
            .connect())
        {
            // Snapshot JVM -> Ysql Conn Mgr connections after the connection is established.
            List<InetSocketAddress> jvmConnections =
                getJvmConnectionsTo(ysqlConnMgrHost, ysqlConnMgrPort);
            assertFalse("expected at least one JVM connection to Ysql Conn Mgr",
                jvmConnections.isEmpty());
            LOG.info("JVM connections to Ysql Conn Mgr {}:{}: {}",
                ysqlConnMgrHost, ysqlConnMgrPort, jvmConnections);

            ResultSet rs = conn.createStatement().executeQuery(QUERY);
            rs.next();
            do {
                String clientAddr = rs.getString("client_addr");
                assertNotNull("client_addr should be populated", clientAddr);
                int clientPort = rs.getInt("client_port");
                assertTrue("client_port should be a valid port number",
                    clientPort > 0 && clientPort < 65536);
                LOG.info("pg_stat_activity client_addr={} client_port={}", clientAddr, clientPort);

                InetSocketAddress reported = new InetSocketAddress(clientAddr, clientPort);
                assertTrue(
                    "pg_stat_activity client_addr:client_port (" + clientAddr + ":" + clientPort +
                    ") should match a JVM connection to Ysql Conn Mgr " + jvmConnections,
                    jvmConnections.contains(reported));

                expectedClientAddr = clientAddr;
                expectedClientPort = clientPort;

                String clientHostname = rs.getString("client_hostname");
                assertEquals("client_hostname should be populated",
                    expectedClientHostname, clientHostname);

                String query = rs.getString("query");
                assertEquals("query should match", QUERY, query);

                String applicationName = rs.getString("application_name");
                assertEquals("application_name should match",
                    expectedApplicationName, applicationName);
            }
            while (rs.next());
        }

        // After logical connection is disconnected, the backend entry should
        // contain the stale client information in pg_stat_activity.
        try (Connection conn = getConnectionBuilder()
            .withTServer(tserverIndex)
            .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .withUser("yugabyte")
            .withPassword("yugabyte")
            .connect())
        {
            ResultSet rs = conn.createStatement().executeQuery(QUERY);
            rs.next();
            do {
                String clientAddr = rs.getString("client_addr");
                assertEquals("client_addr should match",
                    expectedClientAddr, clientAddr);
                int clientPort = rs.getInt("client_port");
                assertEquals("client_port should match",
                    expectedClientPort, clientPort);
                String clientHostname = rs.getString("client_hostname");
                assertEquals("client_hostname should match",
                    expectedClientHostname, clientHostname);
                String query = rs.getString("query");
                assertEquals("query should match", QUERY, query);
                String applicationName = rs.getString("application_name");
                assertEquals("application_name should match",
                    expectedApplicationName, applicationName);
            }
            while (rs.next());
        }
    }
}
