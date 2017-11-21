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
package org.yb.cql;

import com.datastax.driver.core.*;
import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.Statement;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.SocketOptions;
import com.datastax.driver.core.exceptions.QueryValidationException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.YBClient;
import org.yb.master.Master;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.RocksDBMetrics;
import org.yb.util.ServerInfo;

import java.io.Closeable;
import java.io.IOException;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.util.*;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

public class BaseCQLTest extends BaseMiniClusterTest {
  protected static final Logger LOG = LoggerFactory.getLogger(BaseCQLTest.class);

  // Integer.MAX_VALUE seconds is the maximum allowed TTL by Cassandra.
  protected static final int MAX_TTL_SEC = Integer.MAX_VALUE;

  protected static final String DEFAULT_TEST_KEYSPACE = "cql_test_keyspace";

  protected static final String TSERVER_READ_METRIC =
    "handler_latency_yb_tserver_TabletServerService_Read";

  protected static final int PARTITION_POLICY_REFRESH_FREQUENCY_SECONDS = 10;

  protected Cluster cluster;
  protected Session session;

  /** These keyspaces will be dropped in after the current test method (in the @After handler). */
  protected Set<String> keyspacesToDrop = new TreeSet<>();

  float float_infinity_positive = createFloat(0, 0b11111111, 0);
  float float_infinity_negative = createFloat(1, 0b11111111, 0);
  float float_nan_0 = createFloat(0, 0b11111111, 1);
  float float_nan_1 = createFloat(1, 0b11111111, 1);
  float float_nan_2 = createFloat(0, 0b11111111, 0b10);
  float float_zero_positive = createFloat(0, 0, 0);
  float float_zero_negative = createFloat(1, 0, 0);
  float float_sub_normal = createFloat(0, 0, 1);

  double double_infinity_positive = createDouble(0, 0b11111111111, 0);
  double double_infinity_negative = createDouble(1, 0b11111111111, 0);
  double double_nan_0 = createDouble(0, 0b011111111111, 1);
  double double_nan_1 = createDouble(0, 0b111111111111, 1);
  double double_nan_2 = createDouble(0, 0b011111111111, 0b10);
  double double_zero_positive = createDouble(0, 0, 0);
  double double_zero_negative = createDouble(1, 0, 0);
  double double_sub_normal = createDouble(0, 0, 1);

  float[] float_all_literals = { float_infinity_positive, float_infinity_negative, float_nan_0,
      float_nan_1, float_nan_2, float_zero_positive, float_zero_negative, float_sub_normal };
  double[] double_all_literals = { double_infinity_positive, double_infinity_negative, double_nan_0,
      double_nan_1, double_nan_2, double_zero_positive, double_zero_negative, double_sub_normal };

  public float createFloat(int sign, int exp, int fraction) {
    return Float.intBitsToFloat((sign << 31) | (exp << 23) | fraction);
  }

  public double createDouble(long sign, long exp, long fraction) {
    return Double.longBitsToDouble((sign << 63) | (exp << 52) | fraction);
  }

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    LOG.info("BaseCQLTest.setUpBeforeClass is running");

    // Disable extended peer check, to ensure "SELECT * FROM system.peers" works without
    // all columns.
    System.setProperty("com.datastax.driver.EXTENDED_PEER_CHECK", "false");
  }

  public Cluster.Builder getDefaultClusterBuilder() {
    // Set a long timeout for CQL queries since build servers might be really slow (especially Mac
    // Mini).
    SocketOptions socketOptions = new SocketOptions();
    socketOptions.setReadTimeoutMillis(60 * 1000);
    socketOptions.setConnectTimeoutMillis(60 * 1000);
    return Cluster.builder()
              .addContactPointsWithPorts(miniCluster.getCQLContactPoints())
              .withSocketOptions(socketOptions);
  }

  public Session buildDefaultSession(Cluster cluster) {
    return cluster.connect();
  }

  @Before
  public void setUpCqlClient() throws Exception {
    LOG.info("BaseCQLTest.setUpCqlClient is running");
    if (miniCluster == null) {
      final String errorMsg =
          "Mini-cluster must already be running by the time setUpCqlClient is invoked";
      LOG.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }
    try {
      cluster = getDefaultClusterBuilder().build();
    } catch (Exception ex) {
      LOG.error("Error while setting up a CQL client", ex);
      throw ex;
    }
    LOG.info("Connected to cluster: " + cluster.getMetadata().getClusterName());
    session = buildDefaultSession(cluster);

    // Create and use test keyspace to be able to use short table names later.
    createKeyspaceIfNotExists(DEFAULT_TEST_KEYSPACE);
    useKeyspace();
  }

  public void useKeyspace() throws Exception{
    useKeyspace(DEFAULT_TEST_KEYSPACE);
  }

  private static void closeIfNotNull(String logPrefix,
                                     String clusterOrSessionStr,
                                     Closeable closeable) throws Exception {
    if (closeable != null) {
      LOG.info(logPrefix + "closing " + clusterOrSessionStr);
      try {
        closeable.close();
      } catch (IOException ex) {
        LOG.error(logPrefix + ": exception while trying to close " + clusterOrSessionStr, ex);
        throw ex;
      }
    } else {
      LOG.info(logPrefix + clusterOrSessionStr + " is already null, nothing to close");
    }
  }

  @After
  public void tearDownAfter() throws Exception {
    final String logPrefix = "BaseCQLTest.tearDownAfter: ";
    LOG.info(logPrefix + "dropping tables / types / keyspaces");

    // Clean up all tables before end of each test to lower high-water-mark disk usage.
    dropTables();

    // Also delete all types to make sure we can delete keyspaces below.
    dropUDTypes();

    // Also delete all keyspaces, because we may
    dropKeyspaces();

    closeIfNotNull(logPrefix, "session", session);
    // Cannot move this check to closeIfNotNull, because isClosed() is not part of Closeable.
    if (session != null && !session.isClosed()) {
      LOG.warn(logPrefix + "session is still not closed!");
    }
    session = null;

    closeIfNotNull(logPrefix, "cluster", cluster);
    // See the comment above.
    if (cluster  != null && !cluster.isClosed()) {
      LOG.warn(logPrefix + "cluster is still not closed!");
    }
    cluster = null;

    LOG.info(logPrefix +
        "finished attempting to drop tables and trying to close CQL session/client");
    afterBaseCQLTestTearDown();
  }

  protected void afterBaseCQLTestTearDown() throws Exception {
  }

  protected void createTable(String test_table, String column_type) throws Exception {
    LOG.info("CREATE TABLE " + test_table);
    String create_stmt = String.format("CREATE TABLE %s " +
                    " (h1 int, h2 %2$s, " +
                    " r1 int, r2 %2$s, " +
                    " v1 int, v2 %2$s, " +
                    " primary key((h1, h2), r1, r2));",
            test_table, column_type);
    session.execute(create_stmt);
  }

  protected void createTable(String tableName) throws Exception {
     createTable(tableName, "varchar");
  }

  public void setupTable(String tableName, int numRows) throws Exception {
    createTable(tableName);

    if (numRows <= 0) {
      LOG.info("No rows to create in setupTable (numRows=" + numRows + ")");
      return;
    }

    LOG.info("INSERT INTO TABLE " + tableName);
    final long startTimeMs = System.currentTimeMillis();
    long lastLogTimeMs = startTimeMs;
    final int LOG_EVERY_MS = 15 * 1000;
    // INSERT: A valid statement with a column list.
    final PreparedStatement preparedStmt = session.prepare(
        "INSERT INTO " + tableName + "(h1, h2, r1, r2, v1, v2) VALUES(" +
            "?, ?, ?, ?, ?, ?);");
    for (int idx = 0; idx < numRows; idx++) {
      session.execute(preparedStmt.bind(
          idx, "h" + idx, idx + 100, "r" + (idx + 100), idx + 1000, "v" + (idx + 1000)));
      if (idx % 10 == 0) {
        long currentTimeMs = System.currentTimeMillis();
        if (currentTimeMs - lastLogTimeMs > LOG_EVERY_MS) {
          // If we have to do this, we're probably running pretty slowly.
          LOG.info("Inserted " + (idx + 1) + " rows out of " + numRows + " (" +
              String.format("%.2f", (idx + 1) * 1000.0 / (currentTimeMs - startTimeMs)) +
              " rows/second on average)."
          );
        }
      }
    }

    final long totalTimeMs = System.currentTimeMillis() - startTimeMs;
    LOG.info("INSERT INTO TABLE " + tableName + " FINISHED: inserted " + numRows +
             "(" + totalTimeMs + " ms, " +
             String.format("%.2f", numRows * 1000.0 / totalTimeMs) + " rows/sec)");
  }

  public void setupTable(String tableName, int []ttls) throws Exception {
    LOG.info("CREATE TABLE " + tableName);
    String create_stmt = String.format("CREATE TABLE %s " +
                    " (h1 int, h2 %2$s, " +
                    " r1 int, r2 %2$s, " +
                    " v1 int, v2 int, " +
                    " primary key((h1, h2), r1, r2));",
            tableName, "varchar");
    session.execute(create_stmt);

    LOG.info("INSERT INTO TABLE " + tableName);
    for (int idx = 0; idx < ttls.length; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, %d) using ttl %d;",
        tableName, idx, idx, idx+100, idx+100, idx+1000, idx+1000, ttls[idx]);
      session.execute(insert_stmt);
    }
    LOG.info("INSERT INTO TABLE " + tableName + " FINISHED");
  }

  protected void dropTable(String tableName) throws Exception {
    String dropStmt = String.format("DROP TABLE %s;", tableName);
    session.execute(dropStmt);
  }

  protected void dropUDType(String keyspaceName, String typeName) throws Exception {
    String drop_stmt = String.format("DROP TYPE %s.%s;", keyspaceName, typeName);
    session.execute(drop_stmt);
  }

  protected boolean IsSystemKeyspace(String keyspaceName) {
    return keyspaceName.equals("system") ||
           keyspaceName.equals("system_auth") ||
           keyspaceName.equals("system_distributed") ||
           keyspaceName.equals("system_traces") ||
           keyspaceName.equals("system_schema");
  }

  private boolean IsRedisKeyspace(String keyspaceName) {
    return keyspaceName.equals(YBClient.REDIS_KEYSPACE_NAME);
  }

  protected void dropTables() throws Exception {
    if (miniCluster == null) {
      return;
    }
    for (Master.ListTablesResponsePB.TableInfo tableInfo :
        miniCluster.getClient().getTablesList().getTableInfoList()) {
      // Drop all non-system tables.
      String namespaceName = tableInfo.getNamespace().getName();
      if (!IsSystemKeyspace(namespaceName) && !IsRedisKeyspace(namespaceName)) {
        dropTable(namespaceName + "." + tableInfo.getName());
      }
    }
  }


  protected void dropUDTypes() throws Exception {
    if (cluster == null) {
      return;
    }

    for (KeyspaceMetadata keyspace : cluster.getMetadata().getKeyspaces()) {
      if (!IsSystemKeyspace(keyspace.getName())) {
        for (UserType udt : keyspace.getUserTypes()) {
          dropUDType(keyspace.getName(), udt.getTypeName());
        }
      }
    }
  }

  protected void dropKeyspaces() throws Exception {
    if (keyspacesToDrop.isEmpty()) {
      LOG.info("No keyspaces to drop after the test.");
    }

    // Copy the set of keyspaces to drop to avoid concurrent modification exception, because
    // dropKeyspace will also remove them from the keyspacesToDrop.
    final Set<String> keyspacesToDropCopy = new TreeSet<>();
    keyspacesToDropCopy.addAll(keyspacesToDrop);
    for (String keyspaceName : keyspacesToDropCopy) {
      dropKeyspace(keyspaceName);
    }
  }

  protected Iterator<Row> runSelect(String select_stmt) {
    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertTrue(iter.hasNext());
    return iter;
  }

  protected void runInvalidStmt(Statement stmt) {
    try {
      session.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
    } catch (QueryValidationException qv) {
      LOG.info("Expected exception", qv);
    }
  }

  protected void runInvalidStmt(String stmt) {
    runInvalidStmt(new SimpleStatement(stmt));
  }

  // generates a comprehensive map from valid date-time inputs to corresponding Date values
  // includes both integer and string inputs --  used for Timestamp tests
  public Map<String, Date> generateTimestampMap() {
    Map<String, Date> ts_values = new HashMap();
    Calendar cal = new GregorianCalendar();

    // adding some Integer input values
    cal.setTimeInMillis(631238400000L);
    ts_values.put("631238400000", cal.getTime());
    cal.setTimeInMillis(631238434123L);
    ts_values.put("631238434123", cal.getTime());
    cal.setTimeInMillis(631238445000L);
    ts_values.put("631238445000", cal.getTime());

    // generating String inputs as combinations valid components (date/time/frac_seconds/timezone)
    int nr_entries = 3;
    String[] dates = {"'1992-06-04", "'1992-6-4", "'1992-06-4"};
    String[] times_no_sec = {"12:30", "15:30", "9:00"};
    String[] times = {"12:30:45", "15:30:45", "9:00:45"};
    String[] times_frac = {"12:30:45.1", "15:30:45.10", "9:00:45.100"};
    // timezones correspond one-to-one with times
    //   -- so that the UTC-normalized time is the same
    String[] timezones = {" UTC'", "+03:00'", " UTC-03:30'"};
    for (String date : dates) {
      cal.setTimeZone(TimeZone.getTimeZone("GMT")); // resetting
      cal.setTimeInMillis(0); // resetting
      cal.set(1992, 5, 4); // Java Date month value starts at 0 not 1
      ts_values.put(date + " UTC'", cal.getTime());

      cal.set(Calendar.HOUR_OF_DAY, 12);
      cal.set(Calendar.MINUTE, 30);
      for (int i = 0; i < nr_entries; i++) {
        String time = times_no_sec[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
      cal.set(Calendar.SECOND, 45);
      for (int i = 0; i < nr_entries; i++) {
        String time = times[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
      cal.set(Calendar.MILLISECOND, 100);
      for (int i = 0; i < nr_entries; i++) {
        String time = times_frac[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
    }
    return ts_values;
  }

  protected void assertNoRow(String select_stmt) {
    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertFalse(iter.hasNext());
  }

  protected void assertQuery(String stmt, String expectedResult) {
    ResultSet rs = session.execute(stmt);
    String actualResult = "";
    for (Row row : rs) {
      actualResult += row.toString();
    }
    assertEquals(expectedResult, actualResult);
  }

  protected void assertQuery(String stmt, Set<String> expectedRows) {
    ResultSet rs = session.execute(stmt);
    HashSet<String> actualRows = new HashSet<String>();
    for (Row row : rs) {
      actualRows.add(row.toString());
    }
    assertEquals(expectedRows, actualRows);
  }

  // blob type utils
  protected String makeBlobString(ByteBuffer buf) {
    StringBuilder sb = new StringBuilder();
    char[] text_values = "0123456789abcdef".toCharArray();

    sb.append("0x");
    while (buf.hasRemaining()) {
      byte b = buf.get();
      sb.append(text_values[(b & 0xFF) >>> 4]);
      sb.append(text_values[b & 0x0F]);
    }
    return sb.toString();
  }

  ByteBuffer makeByteBuffer(long input) {
    ByteBuffer buffer = ByteBuffer.allocate(Long.BYTES);
    buffer.putLong(input);
    buffer.rewind();
    return buffer;
  }

  public ResultSet execute(String statement) throws Exception {
    LOG.info("EXEC CQL: " + statement);
    final ResultSet result = session.execute(statement);
    LOG.info("EXEC CQL FINISHED: " + statement);
    return result;
  }

  public void createKeyspace(String keyspaceName) throws Exception {
    String createKeyspaceStmt = "CREATE KEYSPACE \"" + keyspaceName + "\";";
    execute(createKeyspaceStmt);
    keyspacesToDrop.add(keyspaceName);
  }

  public void createKeyspaceIfNotExists(String keyspaceName) throws Exception {
    String createKeyspaceStmt = "CREATE KEYSPACE IF NOT EXISTS \"" + keyspaceName + "\";";
    execute(createKeyspaceStmt);
    keyspacesToDrop.add(keyspaceName);
  }

  public void useKeyspace(String keyspaceName) throws Exception {
    String useKeyspaceStmt = "USE \"" + keyspaceName + "\";";
    execute(useKeyspaceStmt);
  }

  public void dropKeyspace(String keyspaceName) throws Exception {
    String deleteKeyspaceStmt = "DROP KEYSPACE \"" + keyspaceName + "\";";
    execute(deleteKeyspaceStmt);
    keyspacesToDrop.remove(keyspaceName);
  }

  // Get IO metrics of all tservers.
  public Map<MiniYBDaemon, IOMetrics> getTSMetrics() throws Exception {
    Map<MiniYBDaemon, IOMetrics> initialMetrics = new HashMap<>();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = new IOMetrics(new Metrics(ts.getLocalhostIP(),
                                                    ts.getCqlWebPort(),
                                                    "server"));
      initialMetrics.put(ts, metrics);
    }
    return initialMetrics;
  }

  // Get combined IO metrics of all tservers since a certain point.
  public IOMetrics getCombinedMetrics(Map<MiniYBDaemon, IOMetrics> initialMetrics)
      throws Exception {
    IOMetrics totalMetrics = new IOMetrics();
    int tsCount = miniCluster.getTabletServers().values().size();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = new IOMetrics(new Metrics(ts.getLocalhostIP(),
                                                    ts.getCqlWebPort(),
                                                    "server"))
                          .subtract(initialMetrics.get(ts));
      LOG.info("Metrics of " + ts.toString() + ": " + metrics.toString());
      totalMetrics.add(metrics);
    }
    LOG.info("Total metrics: " + totalMetrics.toString());
    return totalMetrics;
  }

  public RocksDBMetrics getRocksDBMetric(String tableName) throws IOException {
    Set<String> tabletIDs = new HashSet<>();
    for (Row row : session.execute("SELECT id FROM system_schema.partitions " +
                                   "WHERE keyspace_name = ? and table_name = ?;",
                                   DEFAULT_TEST_KEYSPACE, tableName).all()) {
      tabletIDs.add(ServerInfo.UUIDToHostString(row.getUUID("id")));
    }

    RocksDBMetrics metrics = new RocksDBMetrics();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      try {
        URL url = new URL(String.format("http://%s:%d/metrics",
                                        ts.getLocalhostIP(),
                                        ts.getWebPort()));
        Scanner scanner = new Scanner(url.openConnection().getInputStream());
        JsonParser parser = new JsonParser();
        JsonElement tree = parser.parse(scanner.useDelimiter("\\A").next());
        for (JsonElement elem : tree.getAsJsonArray()) {
          JsonObject obj = elem.getAsJsonObject();
          if (obj.get("type").getAsString().equals("tablet") &&
              tabletIDs.contains(obj.get("id").getAsString())) {
            metrics.add(new RocksDBMetrics(new Metrics(obj)));
          }
        }
      } catch (MalformedURLException e) {
        throw new InternalError(e.getMessage());
      }
    }
    return metrics;
  }
}
