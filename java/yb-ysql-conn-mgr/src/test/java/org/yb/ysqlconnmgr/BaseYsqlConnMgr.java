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

import static com.google.common.base.Preconditions.checkArgument;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertNotNull;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.sql.SQLException;
import java.util.ArrayList;
import java.io.IOException;
import java.net.URL;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Scanner;
import org.junit.AfterClass;
import org.junit.Before;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.TestUtils;
import org.yb.minicluster.*;
import org.yb.pgsql.ConnectionBuilder;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import com.yugabyte.jdbc.PgArray;
import com.yugabyte.util.PGobject;
import com.google.gson.JsonArray;

public class BaseYsqlConnMgr extends BaseMiniClusterTest {
  protected static final Logger LOG = LoggerFactory.getLogger(BaseYsqlConnMgr.class);
  protected static final int NUM_TSERVER = 3;
  private static final String DEFAULT_PG_USER = "yugabyte";
  protected static final int STATS_UPDATE_INTERVAL = 2;
  protected static final int TSERVER_IDX = 0;
  private boolean warmup_random_mode = true;
  private static boolean ysql_conn_mgr_superuser_sticky = false;
  private static boolean ysql_conn_mgr_optimized_extended_query_protocol = true;

  protected static final String DISABLE_TEST_WITH_ASAN =
        "Test is not working correctly with asan build";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enableYsql(true);
    builder.enableYsqlConnMgr(true);
    builder.numTservers(NUM_TSERVER);
    builder.replicationFactor(NUM_TSERVER);
    builder.addCommonTServerFlag("ysql_conn_mgr_dowarmup", "false");
    builder.addCommonTServerFlag("ysql_conn_mgr_superuser_sticky",
      Boolean.toString(ysql_conn_mgr_superuser_sticky));
    builder.addCommonTServerFlag("ysql_conn_mgr_reserve_internal_conns", "0");
    if (warmup_random_mode) {
      builder.addCommonTServerFlag(
      "TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "random");
    }
    builder.addCommonTServerFlag("ysql_conn_mgr_optimized_extended_query_protocol",
      Boolean.toString(ysql_conn_mgr_optimized_extended_query_protocol));
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    // Mimic what we do in customizeMiniClusterBuilder.
    Map<String, String> flagMap = super.getTServerFlags();

    flagMap.put("enable_ysql_conn_mgr", "true");
    flagMap.put("ysql_conn_mgr_dowarmup", "false");
    if (warmup_random_mode) {
      flagMap.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "random");
    }

    return flagMap;
  }

  protected ConnectionBuilder getConnectionBuilder() {
    return new ConnectionBuilder(miniCluster).withUser(DEFAULT_PG_USER);
  }

  protected void disableWarmupRandomMode(MiniYBClusterBuilder builder) {
    builder.addCommonTServerFlag(
        "TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    warmup_random_mode = false;
    return;
  }

  public String getPgHost(int tserverIndex) {
    return miniCluster.getPostgresContactPoints().get(tserverIndex).getHostName();
  }

  protected boolean isTestRunningInWarmupRandomMode() {
    return warmup_random_mode;
  }

  protected void restartClusterWithAdditionalFlags(
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags) throws Exception {
    Map<String, String> tserverFlags = getTServerFlags();
    Map<String, String> masterFlags = getMasterFlags();
    tserverFlags.putAll(additionalTserverFlags);
    masterFlags.putAll(additionalMasterFlags);

    destroyMiniCluster();
    waitForProperShutdown();
    createMiniCluster(masterFlags, tserverFlags);
    waitForDatabaseToStart();
  }

  protected void enableStickySuperuserConnsAndRestartCluster() throws Exception {
    ysql_conn_mgr_superuser_sticky = true;
    restartClusterWithAdditionalFlags(Collections.emptyMap(), Collections.emptyMap());
  }

  protected void reduceQuerySizePacketAndRestartCluster(int query_sz) throws Exception {
    Map<String, String> myMap = new HashMap<>();
    myMap.put("ysql_conn_mgr_max_query_size", Integer.toString(query_sz));
    restartClusterWithAdditionalFlags(Collections.emptyMap(), myMap);
  }

  protected void reduceMaxPoolSize(int max_pool_size) throws Exception {
    Map<String, String> myMap = new HashMap<>();
    myMap.put("ysql_conn_mgr_max_pools", Integer.toString(max_pool_size));
    restartClusterWithAdditionalFlags(Collections.emptyMap(), myMap);
  }

  protected void enableVersionMatchingAndRestartCluster(boolean higher_version_matching)
        throws Exception {
    Map<String, String> tsFlagMap = new HashMap<>();
    tsFlagMap.put("allowed_preview_flags_csv",
            "ysql_conn_mgr_version_matching");
    tsFlagMap.put("enable_ysql_conn_mgr", "true");
    tsFlagMap.put("ysql_conn_mgr_version_matching", "true");

    if (higher_version_matching) {
        tsFlagMap.put("allowed_preview_flags_csv",
                "ysql_conn_mgr_version_matching,"
                + "ysql_conn_mgr_version_matching_connect_higher_version");
        tsFlagMap.put("ysql_conn_mgr_version_matching_connect_higher_version", "true");
    } else {
       tsFlagMap.put("allowed_preview_flags_csv",
                "ysql_conn_mgr_version_matching,"
                + "ysql_conn_mgr_version_matching_connect_higher_version");
        tsFlagMap.put("ysql_conn_mgr_version_matching_connect_higher_version", "false");
    }

    restartClusterWithAdditionalFlags(Collections.emptyMap(), tsFlagMap);
  }

  protected void enableVersionMatchingAndRestartCluster() throws Exception {
    enableVersionMatchingAndRestartCluster(true);
  }

  protected void modifyExtendedQueryProtocolAndRestartCluster(
      boolean optimized_extended_query_protocol) throws Exception {
    ysql_conn_mgr_optimized_extended_query_protocol = optimized_extended_query_protocol;
    restartClusterWithAdditionalFlags(Collections.emptyMap(), Collections.emptyMap());
  }

  protected void disableWarmupModeAndRestartCluster() throws Exception {
    warmup_random_mode = false;
    destroyMiniCluster();
    waitForProperShutdown();
    createMiniCluster();
    waitForDatabaseToStart();
  }

  protected JsonObject getConnectionStats() throws IOException {
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

  protected JsonObject getPool(String db_name, String user_name) throws Exception {
    // Specifically fetches a non logical replication pool. Use `getRepPool()` for replication pool.
    JsonObject obj = getConnectionStats();
    assertNotNull("Got a null response from the connections endpoint", obj);
    JsonArray pools = obj.getAsJsonArray("pools");
    assertNotNull("Got empty pool", pools);
    for (int i = 0; i < pools.size(); ++i) {
      JsonObject pool = pools.get(i).getAsJsonObject();
      String databaseName = pool.get("database_name").getAsString();
      String userName = pool.get("user_name").getAsString();
      Boolean logicalRep = pool.get("logical_rep").getAsBoolean();

      if (db_name.equals(databaseName) && user_name.equals(userName) && !logicalRep) {
        return pool;
      }
    }

    return null;
  }

  protected JsonObject getRepPool(String db_name, String user_name) throws Exception {
    // Specifically fetches a logical replication pool. Use `getPool()` for non-rep pool.
    JsonObject obj = getConnectionStats();
    assertNotNull("Got a null response from the connections endpoint", obj);
    JsonArray pools = obj.getAsJsonArray("pools");
    assertNotNull("Got empty pool", pools);
    for (int i = 0; i < pools.size(); ++i) {
      JsonObject pool = pools.get(i).getAsJsonObject();
      String databaseName = pool.get("database_name").getAsString();
      String userName = pool.get("user_name").getAsString();
      Boolean logicalRep = pool.get("logical_rep").getAsBoolean();

      if (db_name.equals(databaseName) && user_name.equals(userName) && logicalRep) {
        return pool;
      }
    }

    return null;
  }

  protected int getTotalPhysicalConnections(String db_name,
                String user_name, int sleep_time) throws Exception {
    if (sleep_time > 0) {
      Thread.sleep(sleep_time * 1000);
    }
    JsonObject pool = getPool(db_name, user_name);
    assertNotNull(pool);
    return pool.get("active_physical_connections").getAsInt()
    + pool.get("idle_physical_connections").getAsInt();
  }

  protected static class Row implements Comparable<Row>, Cloneable {
    static Row fromResultSet(ResultSet rs) throws SQLException {
      List<Object> elems = new ArrayList<>();
      List<String> columnNames = new ArrayList<>();
      for (int i = 1; i <= rs.getMetaData().getColumnCount(); i++) {
        elems.add(rs.getObject(i));
        columnNames.add(rs.getMetaData().getColumnLabel(i));
      }
      // Pre-initialize stuff while connection is still available
      for (Object el : elems) {
        // TODO(alex): Store as List to begin with?
        if (el instanceof PgArray)
          ((PgArray) el).getArray();
      }
      return new Row(elems, columnNames);
    }

    List<Object> elems = new ArrayList<>();

    /**
     * List of column names, should have the same size as {@link #elems}.
     * <p>
     * Not used for equality, hash code and comparison.
     */
    List<String> columnNames = new ArrayList<>();

    Row(Object... elems) {
      Collections.addAll(this.elems, elems);
    }

    Row(List<Object> elems, List<String> columnNames) {
      checkArgument(elems.size() == columnNames.size());
      this.elems = elems;
      this.columnNames = columnNames;
    }

    /** Returns a column name if available, or {@code null} otherwise. */
    String getColumnName(int index) {
      return columnNames.size() > 0 ? columnNames.get(index) : null;
    }

    Object get(int index) {
      return elems.get(index);
    }

    Boolean getBoolean(int index) {
      return (Boolean) elems.get(index);
    }

    Integer getInt(int index) {
      return (Integer) elems.get(index);
    }

    Long getLong(int index) {
      return (Long) elems.get(index);
    }

    Double getDouble(int index) {
      return (Double) elems.get(index);
    }

    String getString(int index) {
      return (String) elems.get(index);
    }

    Float getFloat(int index) {
      return (Float) elems.get(index);
    }

    public boolean elementEquals(int idx, Object value) {
      return compare(elems.get(idx), value) == 0;
    }

    @Override
    public boolean equals(Object obj) {
      if (obj == this) {
        return true;
      }
      if (!(obj instanceof Row)) {
        return false;
      }
      Row other = (Row)obj;
      return compareTo(other) == 0;
    }

    @Override
    public int compareTo(Row that) {
      // In our test, if selected Row has different number of columns from expected row, something
      // must be very wrong. Stop the test here.
      assertEquals("Row width mismatch between " + this + " and " + that,
          this.elems.size(), that.elems.size());
      return compare(this.elems, that.elems);
    }

    @Override
    public int hashCode() {
      return elems.hashCode();
    }

    @Override
    public String toString() {
      return toString(false /* printColumnNames */);
    }

    public String toString(boolean printColumnNames) {
      StringBuilder sb = new StringBuilder();
      sb.append("Row[");
      for (int i = 0; i < elems.size(); i++) {
        if (i > 0) sb.append(',');
        if (printColumnNames) {
          String columnNameOrNull = getColumnName(i);
          sb.append((columnNameOrNull != null ? columnNameOrNull : i) + "=");
        }
        if (elems.get(i) == null) {
          sb.append("null");
        } else {
          sb.append(elems.get(i).getClass().getName() + "::");
          sb.append(elems.get(i).toString());
        }
      }
      sb.append(']');
      return sb.toString();
    }

    @Override
    public Row clone() {
      try {
        Row clone = (Row) super.clone();
        clone.elems = new ArrayList<>(this.elems);
        clone.columnNames = new ArrayList<>(this.columnNames);
        return clone;
      } catch (CloneNotSupportedException ex) {
        // Not possible
        throw new RuntimeException(ex);
      }
    }

    //
    // Helpers
    //

    /**
     * Compare two objects if possible. Is able to compare:
     * <ul>
     * <li>Primitives
     * <li>Comparables (including {@code String}) - but cannot handle the case of two unrelated
     * Comparables
     * <li>{@code PGobject}s wrapping {@code Comparable}s
     * <li>Arrays, {@code PgArray}s or lists of the above types
     * </ul>
     */
    @SuppressWarnings({ "unchecked", "rawtypes" })
    private static int compare(Object o1, Object o2) {
      if (o1 == null || o2 == null) {
        if (o1 != o2) {
          return o1 == null ? -1 : 1;
        }
        return 0;
      } else {
        Object promoted1 = promoteType(o1);
        Object promoted2 = promoteType(o2);
        if (promoted1 instanceof Long && promoted2 instanceof Long) {
          return ((Long) promoted1).compareTo((Long) promoted2);
        } else if (promoted1 instanceof Double && promoted2 instanceof Double) {
          return ((Double) promoted1).compareTo((Double) promoted2);
        } else if (promoted1 instanceof Number && promoted2 instanceof Number) {
          return Double.compare(
              ((Number) promoted1).doubleValue(),
              ((Number) promoted2).doubleValue());
        } else if (promoted1 instanceof Comparable && promoted2 instanceof Comparable) {
          // This is unsafe but we dont expect arbitrary types here.
          return ((Comparable) promoted1).compareTo((Comparable) promoted2);
        } else if (promoted1 instanceof List && promoted2 instanceof List) {
          List list1 = (List) promoted1;
          List list2 = (List) promoted2;
          if (list1.size() != list2.size()) {
            return Integer.compare(list1.size(), list2.size());
          }
          for (int i = 0; i < list1.size(); ++i) {
            int comparisonResult = compare(list1.get(i), list2.get(i));
            if (comparisonResult != 0) {
              return comparisonResult;
            }
          }
          return 0;
        } else {
          throw new IllegalArgumentException("Cannot compare "
              + o1 + " (of class " + o1.getClass().getCanonicalName() + ") with "
              + o2 + " (of class " + o2.getClass().getCanonicalName() + ")");
        }
      }
    }

    /** Converts the value to a widest one of the same type for comparison */
    @SuppressWarnings({ "rawtypes", "unchecked" })
    private static Object promoteType(Object v) {
      if (v instanceof Byte || v instanceof Short || v instanceof Integer) {
        return ((Number)v).longValue();
      } else if (v instanceof Float) {
        return ((Float)v).doubleValue();
      } else if (v instanceof Comparable) {
        return v;
      } else if (v instanceof List) {
        return v;
      } else if (v instanceof PGobject) {
        return promoteType(((PGobject) v).getValue()); // For PG_LSN type.
      } else if (v instanceof PgArray) {
        try {
          return promoteType(((PgArray) v).getArray());
        } catch (SQLException ex) {
          throw new RuntimeException("SQL exception during type promotion", ex);
        }
      } else if (v.getClass().isArray()) {
        List list = new ArrayList<>();
        // Unfortunately there's no easy way to automate that, we have to enumerate all array types
        // explicitly.
        if (v instanceof byte[]) {
          for (byte ve : (byte[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof short[]) {
          for (short ve : (short[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof int[]) {
          for (int ve : (int[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof long[]) {
          for (long ve : (long[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof float[]) {
          for (float ve : (float[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof double[]) {
          for (double ve : (double[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof boolean[]) {
          for (boolean ve : (boolean[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof char[]) {
          for (char ve : (char[]) v) {
            list.add(promoteType(ve));
          }
        } else if (v instanceof Object[]) {
          for (Object ve : (Object[]) v) {
            list.add(promoteType(ve));
          }
        }
        return list;
      } else {
        throw new IllegalArgumentException(v + " (of class " + v.getClass().getSimpleName() + ")"
            + " cannot be promoted for comparison!");
      }
    }
  }

  protected List<Row> getRowList(Statement stmt, String query) throws SQLException {
    try (ResultSet rs = stmt.executeQuery(query)) {
      return getRowList(rs);
    }
  }

  protected List<Row> getRowList(ResultSet rs) throws SQLException {
    List<Row> rows = new ArrayList<>();
    while (rs.next()) {
      rows.add(Row.fromResultSet(rs));
    }
    return rows;
  }

  @Before
  public void waitForDatabaseToStart() throws Exception {
    LOG.info("Waiting for initdb to complete on master");
    TestUtils.waitFor(
        () -> {
          IsInitDbDoneResponse initdbStatusResp = miniCluster.getClient().getIsInitDbDone();
          if (initdbStatusResp.hasError()) {
            throw new RuntimeException(
                "Could not request initdb status: " + initdbStatusResp.getServerError());
          }
          String initdbError = initdbStatusResp.getInitDbError();
          if (initdbError != null && !initdbError.isEmpty()) {
            throw new RuntimeException("initdb failed: " + initdbError);
          }
          return initdbStatusResp.isDone();
        },
        600000);
    LOG.info("initdb has completed successfully on master");
    verifyClusterAcceptsPGConnections();
  }

  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder;
  }

  public void verifyClusterAcceptsPGConnections() throws Exception {
    LOG.info("Waiting for the cluster to accept pg connections");
    TestUtils.waitFor(() -> {
        try {
          connectionBuilderForVerification(getConnectionBuilder()).connect().close();
          return true;
        } catch (Exception e) {
          return false;
        }
      },
      10000);
  }

  @AfterClass
  public static void waitForProperShutdown() throws InterruptedException {
    // Wait for 1 sec before stoping the miniCluster so that Ysql Connection Manger can clean the
    // shared memory.
    Thread.sleep(1000);
  }

  boolean verifySessionParameterValue(Statement stmt, String param, String expectedValue)
      throws Exception {
    String query = String.format("SHOW %s", param);
    LOG.info(String.format("Executing query `%s`", query));

    ResultSet resultSet = stmt.executeQuery(query);
    assertNotNull(resultSet);

    if (!resultSet.next()) {
      LOG.error("Got empty result for SHOW query");
      return false;
    }

    if (!resultSet.getString(1).toLowerCase().equals(expectedValue.toLowerCase())) {
      LOG.error("Expected value " + expectedValue + " is not same as the query result "
          + resultSet.getString(1));
      return false;
    }

    return true;
  }

  protected static void executeQuery(Connection conn, String query, boolean shouldSucceed) {
    try (Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery(query);
      assertEquals(shouldSucceed, rs.next());
      assertTrue("Expected the query to fail", shouldSucceed);
    } catch (Exception e) {
      LOG.info("Got an exception while executing the query", e);
      assertFalse("Expected the query to succeed", shouldSucceed);
    }
  }

  protected abstract class TestConcurrently implements Runnable {
    protected String oidObjName;
    public Boolean testSuccess;

    public TestConcurrently(String oidObjName) {
      this.oidObjName = oidObjName;
      this.testSuccess = false;
    }
  }

  protected void assertConnectionStickyState(Statement stmt,
                                           boolean expectedSticky)
                                           throws Exception {
    HashSet<Integer> pids = new HashSet<>();
    ResultSet rs;
    for (int i = 0; i < 10; i++) {
      rs = stmt.executeQuery("SELECT pg_backend_pid()");
      assertTrue(rs.next());
      pids.add(rs.getInt(1));
    }
   if (expectedSticky)
    assertTrue(pids.size() == 1);
   else
    assertTrue(pids.size() > 1);
  }

  protected void restartClusterWithFlags(
      Map<String, String> additionalMasterFlags,
      Map<String, String> additionalTserverFlags) throws Exception {
    destroyMiniCluster();
    waitForProperShutdown();

    createMiniCluster(additionalMasterFlags, additionalTserverFlags);
    waitForDatabaseToStart();
  }
}
