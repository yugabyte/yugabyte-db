package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import java.io.IOException;
import java.net.URL;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Scanner;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.pgsql.ConnectionEndpoint;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestStatsAndMetrics extends BaseYsqlConnMgr {
  private static final int TSERVER_IDX = 0;
  private static final String[] FIELDS_IN_CONNECTION_STATS =
      {"database_name",
       "user_name",
       "active_logical_connections",
       "queued_logical_connections",
       "idle_or_pending_logical_connections",
       "active_physical_connections",
       "idle_physical_connections",
       "avg_wait_time_ns",
       "qps",
       "tps"};

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
        Integer.toString(2));
  }

  private JsonObject getConnectionStats() throws IOException {
    String host_name = getPgHost(TSERVER_IDX);
    MiniYBDaemon[] ts_list = miniCluster.getTabletServers()
                                        .values()
                                        .toArray(new MiniYBDaemon[0]);
    MiniYBDaemon ts = null;

    for (MiniYBDaemon daemon : ts_list) {
      if (host_name.equals(daemon.getLocalhostIP())) {
        ts = daemon;
        break;
      }
    }

    assertNotNull(ts);

    String connection_endpoint = String.format("http://%s:%d/connections",
      ts.getLocalhostIP(), ts.getPgsqlWebPort());
    URL url = new URL(connection_endpoint);
    LOG.info("Trying to gather stats at the endpoint " + connection_endpoint);

    try (Scanner scanner = new Scanner(url.openConnection().getInputStream())) {
      JsonElement tree = JsonParser.parseString(scanner.useDelimiter("\\A").next());
      return tree.getAsJsonObject();
    } catch (Exception e) {
       LOG.error(e.getMessage());
      return null;
    }
  }

  private void testStatsFields() throws Exception {
    JsonObject obj = getConnectionStats();
    assertNotNull("Got a null response from the connections endpoint",
        obj);
    JsonArray pools = obj.getAsJsonArray("pools");
    assertNotNull("Got empty pool", pools);
    assertEquals("Pool size must be 2", 2, pools.size());
    for (int i = 0; i < pools.size(); ++i) {
      JsonObject pool = (JsonObject) pools.get(i);

      for (int j = 0; j < FIELDS_IN_CONNECTION_STATS.length; ++j) {
        String fieldName = FIELDS_IN_CONNECTION_STATS[j];
        assertNotNull("Stats for " + fieldName + " not found", pool.get(fieldName));
      }
    }
  }

  private JsonObject getPool(String db_name, String user_name) throws Exception {
    JsonObject obj = getConnectionStats();
    assertNotNull("Got a null response from the connections endpoint",
        obj);
    JsonArray pools = obj.getAsJsonArray("pools");
    assertNotNull("Got empty pool", pools);
    assertEquals("Pool size must be 2", 2, pools.size());
    for (int i = 0; i < pools.size(); ++i) {
      JsonObject pool = pools.get(i).getAsJsonObject();
      String databaseName = pool.get("database_name").getAsString();
      String userName = pool.get("user_name").getAsString();

      if (db_name.equals(databaseName) && user_name.equals(userName)) {
          return pool;
      }
    }

    return null;
  }

  private void testNumLogicalConnections(String db_name,
      String user_name, int exp_val) throws Exception {
    JsonObject pool = getPool(db_name, user_name);
    assertNotNull("Did not find a pool with the database "
        + db_name + " and user " + user_name, pool);
    int num_logical_conn =
    pool.get("active_logical_connections").getAsInt() +
    pool.get("queued_logical_connections").getAsInt() +
    pool.get("idle_or_pending_logical_connections").getAsInt();
    assertEquals("Did not get the expected number of logical connections for pool with user "
        + user_name + " and database " + db_name, exp_val, num_logical_conn);
  }

  private void testNumPhysicalConnections(String db_name,
      String user_name, int exp_val) throws Exception {
    JsonObject pool = getPool(db_name, user_name);
    assertNotNull(pool);
    int num_physical_conn =
    pool.get("active_physical_connections").getAsInt() +
    pool.get("idle_physical_connections").getAsInt();
    assertEquals("Did not get the expected number of logical connections",
        exp_val, num_physical_conn);
  }

  @Test
  public void testConnections() throws Exception {
    // Create a connection on the Ysql Connection Manager port
    Connection conn = getConnectionBuilder().withTServer(TSERVER_IDX)
                                        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                                        .connect();
    Statement stmt = conn.createStatement();
    ResultSet rs = stmt.executeQuery("SELECT 1");
    assertTrue(rs.next());

    Thread.sleep(4000);

    testStatsFields();

    testNumPhysicalConnections("control_connection", "control_connection", 1);
    testNumLogicalConnections("control_connection", "control_connection", 0);

    testNumPhysicalConnections("yugabyte", "yugabyte", 1);
    testNumLogicalConnections("yugabyte", "yugabyte", 1);

    stmt.close();
    conn.close();

    Thread.sleep(4000);
    testNumPhysicalConnections("yugabyte", "yugabyte", 1);
    testNumLogicalConnections("yugabyte", "yugabyte", 0);
  }
}
