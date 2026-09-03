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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertNotNull;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.ProcessUtil;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
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
