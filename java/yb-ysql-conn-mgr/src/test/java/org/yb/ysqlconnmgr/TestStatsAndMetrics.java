package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
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

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestStatsAndMetrics extends BaseYsqlConnMgr {
  private static final String[] FIELDS_IN_CONNECTION_STATS =
      {"database_name",
       "user_name",
       "active_logical_connections",
       "queued_logical_connections",
       "waiting_logical_connections",
       "active_physical_connections",
       "idle_physical_connections",
       "sticky_connections",
       "avg_wait_time_ns",
       "qps",
       "tps"};

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
        Integer.toString(STATS_UPDATE_INTERVAL));
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

  private void testNumLogicalConnections(String db_name,
      String user_name, int exp_val) throws Exception {
    JsonObject pool = getPool(db_name, user_name);
    assertNotNull("Did not find a pool with the database "
        + db_name + " and user " + user_name, pool);
    int num_logical_conn =
    pool.get("active_logical_connections").getAsInt() +
    pool.get("queued_logical_connections").getAsInt() +
    pool.get("waiting_logical_connections").getAsInt();
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
    assertEquals("Did not get the expected number of physical connections",
        exp_val, num_physical_conn);
  }

  // This function is primarily being used to test this stat in the control pool
  // (after the fix for #28083). However, connections do not wait in the control
  // pool unless there is a delay during routing, and even then, avg_wait_time_ns
  // is updated in only 1 tick before being reset.
  // Hence, it is checked in 2 adjacent ticks to ensure it is not missed (i.e.
  // immediately upon creating the connection, and the tick after creating the
  // connection).
  private void testAvgWaitTime(String db_name, String user_name, int min_val,
      int stats_update_interval_ms) throws Exception {
    JsonObject pool;

    pool = getPool(db_name, user_name);
    assertNotNull(pool);
    int avgWaitTimeNs1 = pool.get("avg_wait_time_ns").getAsInt();
    Thread.sleep(stats_update_interval_ms);
    pool = getPool(db_name, user_name);
    assertNotNull(pool);
    int avgWaitTimeNs2 = pool.get("avg_wait_time_ns").getAsInt();

    Thread.sleep(stats_update_interval_ms);
    pool = getPool(db_name, user_name);
    assertNotNull(pool);
    int avgWaitTimeNs3 = pool.get("avg_wait_time_ns").getAsInt();

    assertGreaterThan("Routing time (avg_wait_time_ns) not updated, expect greater than min_val",
        avgWaitTimeNs1 + avgWaitTimeNs2, min_val);
    assertEquals("Routing time (avg_wait_time_ns) not reset, expect == 0", avgWaitTimeNs3, 0);
  }

  private void testStickyConnections(String db_name,
      String user_name, int exp_val) throws Exception {
    JsonObject pool = getPool(db_name, user_name);
    assertNotNull(pool);
    int sticky_conn = pool.get("sticky_connections").getAsInt();
    assertEquals("Did not get the expected number of sticky connections",
        exp_val, sticky_conn);
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

    Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);

    testStatsFields();

    // The physical connection is created only for authentication purposes and
    // closed after it.
    testNumPhysicalConnections("control_connection", "control_connection", 0);
    testNumLogicalConnections("control_connection", "control_connection", 0);

    testNumPhysicalConnections("yugabyte", "yugabyte",
                              isTestRunningInWarmupRandomMode() ? 3 : 1);
    testNumLogicalConnections("yugabyte", "yugabyte", 1);

    stmt.close();
    conn.close();

    Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);
    testNumPhysicalConnections("yugabyte", "yugabyte",
                              isTestRunningInWarmupRandomMode() ? 3 : 1);
    testNumLogicalConnections("yugabyte", "yugabyte", 0);
  }
  @Test
  public void testControlPoolConnections() throws Exception {
    // Create a connection. This will spin up a control backend
    Connection conn =
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();

    // Stats should also be correctly populated for the control pool for the 'tick' right after
    // initiating a connection. No sleep as control pool stats are updated quick enough
    testAvgWaitTime("control_connection", "control_connection", 100, STATS_UPDATE_INTERVAL * 1000);

    conn.close();
  }

  @Test
  public void testSingleStickyConnectionClose() throws Exception {
    // Create a connection on the Ysql Connection Manager port
    try (Connection conn = getConnectionBuilder().withTServer(TSERVER_IDX)
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .connect();
         Statement stmt = conn.createStatement()) {
        stmt.execute("CREATE TEMP TABLE names(id int)");

        Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);
        testStatsFields();

        // The physical connection is created only for authentication purposes and
        // closed after it.
        testNumPhysicalConnections("control_connection", "control_connection", 0);
        testNumLogicalConnections("control_connection", "control_connection", 0);

        testNumPhysicalConnections("yugabyte", "yugabyte",
                                  isTestRunningInWarmupRandomMode() ? 3 : 1);
        testNumLogicalConnections("yugabyte", "yugabyte", 1);
        testStickyConnections("yugabyte", "yugabyte", 1);
    }

    Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);

    JsonObject pool = getPool("yugabyte", "yugabyte");
    if (pool != null) {
        testNumPhysicalConnections("yugabyte", "yugabyte",
                                  isTestRunningInWarmupRandomMode() ? 2 : 0);
        testNumLogicalConnections("yugabyte", "yugabyte", 0);
        testStickyConnections("yugabyte", "yugabyte", 0);
    }
  }

  @Test
  public void testMultipleStickyConnections() throws Exception {
    // Create a few connections on the Ysql Connection Manager port
    try (Connection conn1 = getConnectionBuilder().withTServer(TSERVER_IDX)
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .connect();
         Connection conn2 = getConnectionBuilder().withTServer(TSERVER_IDX)
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .connect();
         Statement stmt1 = conn1.createStatement();
         Statement stmt2 = conn2.createStatement()) {

        stmt1.execute("BEGIN");

        // Active transactions should not imply that the connection is sticky.
        Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);
        testStatsFields();
        testNumLogicalConnections("yugabyte", "yugabyte", 2);
        testNumPhysicalConnections("yugabyte", "yugabyte",
          isTestRunningInWarmupRandomMode() ? 3 : 1);
        testStickyConnections("yugabyte", "yugabyte", 0);

        stmt1.execute("COMMIT");

        // Incrementally ensure sticky connections can be observed.
        stmt1.execute("CREATE TEMP TABLE t1(id int)");
        Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);
        testStickyConnections("yugabyte", "yugabyte", 1);

        stmt2.execute("CREATE TEMP TABLE t2(id int)");
        Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);
        testStickyConnections("yugabyte", "yugabyte", 2);
    }

    // Finally, ensure that closed sticky connections are reflected in the stats.
    Thread.sleep(2 * STATS_UPDATE_INTERVAL * 1000);

    JsonObject pool = getPool("yugabyte", "yugabyte");
    if (pool != null) {
        testStickyConnections("yugabyte", "yugabyte", 0);
    }
  }
}
