// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import com.google.common.net.HostAndPort;
import com.yugabyte.ysql.ClusterAwareLoadBalancer;
import com.yugabyte.jdbc.PgConnection;
import org.yb.AssertionWrappers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@RunWith(value = YBTestRunner.class)
public class TestLoadBalance extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgEncryption.class);

  @Override
  public ConnectionBuilder getConnectionBuilder() {
    ConnectionBuilder cb = new ConnectionBuilder(miniCluster);
    cb.setLoadBalance(true);
    return cb;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder){
    super.customizeMiniClusterBuilder(builder);
    builder.numTservers(7);
    builder.replicationFactor(3);
  }

  private static int parseYsqlPort(String[] cmds) {
    for (String cmd : cmds) {
      if (cmd.contains("pgsql_proxy_bind_address")) {
        int idx = cmd.indexOf(":");
        return Integer.parseInt(cmd.substring(idx + 1));
      }
    }

    return 5433;
  }

  private static int parseYsqlConnMgrPort(String[] cmds) {
    for (String cmd : cmds) {
      if (cmd.contains("ysql_conn_mgr_port")) {
        int idx = cmd.indexOf("=");
        return Integer.parseInt(cmd.substring(idx + 1));
      }
    }

    return 5433;
  }

  private int getSmartDriverPortFromTserverFlags(String[] cmds) {
    return isTestRunningWithConnectionManager() == true ?
       parseYsqlConnMgrPort(cmds) : parseYsqlPort(cmds);
  }

  @Test
  public void testYBServersFunction() throws Exception {
    Statement st = connection.createStatement();
    ResultSet rs = st.executeQuery("select * from yb_servers()");
    int cnt = 0;
    Map<HostAndPort, MiniYBDaemon> hostPortsDaemonMap = miniCluster.getTabletServers();
    Map<String, Integer> hostPorts = new HashMap<>();
    for (Map.Entry<HostAndPort, MiniYBDaemon> e : hostPortsDaemonMap.entrySet()) {
      hostPorts.put(e.getKey().getHost(), e.getKey().getPort());
    }

    while (rs.next()) {
      String host = rs.getString(1);
      int port = rs.getInt(2);
      int connections = rs.getInt(3);
      String node_type = rs.getString(4);
      String cloud = rs.getString(5);
      String region = rs.getString(6);
      String zone = rs.getString(7);
      String publicIp = rs.getString(8);
      String uuid = rs.getString(9);
      Integer portInMap = hostPorts.get(host);
      AssertionWrappers.assertNotNull(portInMap);
      HostAndPort hp = HostAndPort.fromParts(host, portInMap);
      MiniYBDaemon daemon = hostPortsDaemonMap.get(hp);
      int pg_port = getSmartDriverPortFromTserverFlags(daemon.getCommandLine());

      AssertionWrappers.assertEquals("port should be equal", pg_port, port);
      AssertionWrappers.assertEquals("primary", node_type);
      AssertionWrappers.assertEquals("connections has been hardcoded to 0", 0, connections);
      AssertionWrappers.assertEquals("cloud1", cloud);
      AssertionWrappers.assertEquals("datacenter1", region);
      AssertionWrappers.assertEquals("rack1", zone);
      AssertionWrappers.assertTrue(publicIp.isEmpty());
      AssertionWrappers.assertTrue(!uuid.isEmpty());
      cnt++;
    }
    AssertionWrappers.assertEquals(
      "expected servers started by minicluster", hostPortsDaemonMap.size(), cnt);
    ClusterAwareLoadBalancer clb = ClusterAwareLoadBalancer.instance();
    AssertionWrappers.assertNotNull(clb);
    List<Connection> connList = new ArrayList<>();
    try {
      Map<String, Integer> hostToNumConnections = new HashMap<>();
      for (int i = 0; i < 14; i++) {
        Connection c = getConnectionBuilder().connect();
        connList.add(c);
        String host = ((PgConnection)c).getQueryExecutor().getHostSpec().getHost();
        Integer numConns = 0;
        if (hostToNumConnections.containsKey(host)) {
          numConns = hostToNumConnections.get(host);
          numConns += 1;
        } else {
          numConns = 1;
        }
        hostToNumConnections.put(host, numConns);
      }
      // Add the first connection host port too
      String firstHost = ((PgConnection)connection).getQueryExecutor().getHostSpec().getHost();
      Integer numConns = hostToNumConnections.get(firstHost);
      hostToNumConnections.put(firstHost, numConns+1);
      clb.printHostToConnMap();
      AssertionWrappers.assertEquals(7, hostToNumConnections.size());
      for (Map.Entry<String, Integer> e : hostToNumConnections.entrySet()) {
        AssertionWrappers.assertTrue(e.getValue() >= 2);
      }
    } finally {
      for (Connection c : connList) c.close();
    }
    // Let's close the first connection as well, so that this connection does not interfere
    // with the accounting done later in the test when multiple threads try to create the
    // connections at the same time.
    connection.close();
    // Now let's test parallel connection attempts. Even then it should be properly balanced
    class ConnectionRunnable implements Runnable {
      volatile Connection conn;
      volatile Exception ex;
      @Override
      public void run() {
        try {
          conn = getConnectionBuilder().connect();
        } catch (Exception e) {
          ex = e;
        }
      }
    }
    Thread[] threads = new Thread[14];
    ConnectionRunnable[] runnables = new ConnectionRunnable[14];
    for(int i=0; i< 14; i++) {
      runnables[i] = new ConnectionRunnable();
      threads[i] = new Thread(runnables[i]);
    }
    for(Thread t : threads) {
      t.start();
    }
    for(Thread t : threads) {
      t.join();
    }
    Map<String, Integer> hostToNumConnections = new HashMap<>();
    for (int i = 0; i < 14; i++) {
      AssertionWrappers.assertNull(runnables[i].ex);
      Connection c = runnables[i].conn;
      String host = ((PgConnection)c).getQueryExecutor().getHostSpec().getHost();
      Integer numConns;
      if (hostToNumConnections.containsKey(host)) {
        numConns = hostToNumConnections.get(host);
        numConns += 1;
      } else {
        numConns = 1;
      }
      hostToNumConnections.put(host, numConns);
      c.close();
    }
    for (Map.Entry<String, Integer> e : hostToNumConnections.entrySet()) {
      AssertionWrappers.assertTrue(e.getValue() >= 2);
    }
  }

  @Test
  public void TestWithBlacklistedServer() throws Exception{

    Map<HostAndPort, MiniYBDaemon> hostPortsDaemonMap = miniCluster.getTabletServers();
    Map<String, Integer> hostPorts = new HashMap<>();
    for (Map.Entry<HostAndPort, MiniYBDaemon> e : hostPortsDaemonMap.entrySet()) {
      hostPorts.put(e.getKey().getHost(), e.getKey().getPort());
    }

    Statement st = connection.createStatement();
    st.execute("create table users (id int, name varchar(20))");
    String insertStmt = "insert into users(id, name) select generate_series(1,10000),'Username'";
    st.execute(insertStmt);

    ResultSet rs = st.executeQuery("select * from yb_servers()");
    int rows = 0;
    while (rs.next()) {
      ++rows;
    }
    AssertionWrappers.assertTrue("Expected 7 tservers, found " + rows, rows == 7);

    Map.Entry<String, Integer> e = hostPorts.entrySet().iterator().next();
    String decommissionedServer = e.getKey() + ":" + String.valueOf(e.getValue());
    LOG.info("Decommissioning/Blacklisting the server: " + decommissionedServer);

    // Decommission/Blacklist the server
    runProcess(
      TestUtils.findBinary("yb-admin"),
      "--master_addresses",
      masterAddresses,
      "change_blacklist",
      "ADD",
      decommissionedServer);

    rs = st.executeQuery("select * from yb_servers()");
    rows = 0;
    while (rs.next()) {
      ++rows;
    }
    AssertionWrappers.assertTrue("Expected 7 tservers, found " + rows, rows == 7);

    AssertionWrappers.assertTrue("Expected 6 tservers not found",
      verifyResultUntil(10, 3000, e.getKey(), 6));

    runProcess(
      TestUtils.findBinary("yb-admin"),
      "--master_addresses",
      masterAddresses,
      "change_blacklist",
      "REMOVE",
      decommissionedServer);
  }

  private boolean verifyResultUntil(int retries, int retryIntervalMilis,
    String decommissionedServer, int numTservers) throws SQLException {
    boolean found = false;
    int rows = 0;
    Statement st = connection.createStatement();

    for (int i = 0; i < retries; i++) {
      ResultSet rs = st.executeQuery("select * from yb_servers()");
      rows = 0;
      found = false;
      while (rs.next()) {
        ++rows;
        String host = rs.getString(1).trim();
        if (host.contains(decommissionedServer)) {
          found = true;
        }
      }
      if (!found && rows == numTservers) {
        return true;
      }
      try {
        Thread.sleep(retryIntervalMilis);
      } catch (InterruptedException ie) {}
    }
    return !found && rows == numTservers;
  }

}
