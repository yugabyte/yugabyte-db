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

import static org.yb.AssertionWrappers.*;

import com.datastax.driver.core.*;

import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;

import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.ConsistencyLevel;
import com.datastax.driver.core.QueryOptions;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.Statement;
import com.datastax.driver.core.Session;
import com.datastax.driver.core.SocketOptions;
import com.datastax.driver.core.exceptions.QueryValidationException;
import com.google.common.base.Stopwatch;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.GetTableSchemaResponse;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.CommonTypes;
import org.yb.IndexInfo;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.RocksDBMetrics;
import org.yb.util.BuildTypeUtil;
import org.yb.util.StringUtil;

import java.io.Closeable;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.sql.SQLException;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.stream.Collectors;

public class BaseCQLTest extends BaseMiniClusterTest {

  private static final Logger LOG = LoggerFactory.getLogger(BaseCQLTest.class);

  // Integer.MAX_VALUE seconds is the maximum allowed TTL by Cassandra.
  protected static final int MAX_TTL_SEC = Integer.MAX_VALUE;

  protected static final String DEFAULT_ROLE = "cassandra";
  protected static final String DEFAULT_TEST_KEYSPACE = "cql_test_keyspace";

  protected static final String TSERVER_READ_METRIC =
    "handler_latency_yb_tserver_TabletServerService_Read";

  protected static final String TSERVER_SELECT_METRIC =
      "handler_latency_yb_cqlserver_SQLProcessor_SelectStmt";

  protected static final String TSERVER_FLUSHES_METRIC =
      "handler_latency_yb_cqlserver_SQLProcessor_NumFlushesToExecute";

  // CQL and Redis settings, will be reset before each test via resetSettings method.
  protected boolean startCqlProxy = true;
  protected boolean startRedisProxy = false;
  protected boolean systemQueryCacheEmptyResponses = false;
  protected int cqlClientTimeoutMs = 120 * 1000;
  protected int clientReadWriteTimeoutMs = 180 * 1000;
  protected int systemQueryCacheUpdateMs = 4 * 1000;

  /** Convenient default cluster for tests to use, cleaned after each test. */
  protected Cluster cluster;

  /** Convenient default session for tests to use, cleaned after each test. */
  protected Session session;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();

    flagMap.put("start_cql_proxy",
        String.valueOf(startCqlProxy));
    flagMap.put("start_redis_proxy",
        String.valueOf(startRedisProxy));
    flagMap.put("cql_update_system_query_cache_msecs",
        String.valueOf(systemQueryCacheUpdateMs));
    flagMap.put("cql_system_query_cache_empty_responses",
        String.valueOf(systemQueryCacheEmptyResponses));
    flagMap.put("client_read_write_timeout_ms",
        String.valueOf(BuildTypeUtil.adjustTimeout(clientReadWriteTimeoutMs)));

    return flagMap;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enableYsql(false);
    // Prevent YB server processes from closing connections which are idle for less than client
    // timeout period.
    builder.addCommonFlag("rpc_default_keepalive_time_ms",
        String.valueOf(cqlClientTimeoutMs + 5000));
  }

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    LOG.info("BaseCQLTest.setUpBeforeClass is running");
    // Disable extended peer check, to ensure "SELECT * FROM system.peers" works without
    // all columns.
    System.setProperty("com.datastax.driver.EXTENDED_PEER_CHECK", "false");
  }

  public Cluster.Builder getDefaultClusterBuilder() {
    // Set default consistency level to strong consistency
    QueryOptions queryOptions = new QueryOptions();
    queryOptions.setConsistencyLevel(ConsistencyLevel.YB_STRONG);
    // Set a long timeout for CQL queries since build servers might be really slow (especially Mac
    // Mini).
    SocketOptions socketOptions = new SocketOptions();
    socketOptions.setReadTimeoutMillis(cqlClientTimeoutMs);
    socketOptions.setConnectTimeoutMillis(cqlClientTimeoutMs);
    return Cluster.builder()
              .addContactPointsWithPorts(miniCluster.getCQLContactPoints())
              .withQueryOptions(queryOptions)
              .withSocketOptions(socketOptions);
  }

  public ClusterAndSession connectWithTestDefaults() {
    return new ClusterAndSession(getDefaultClusterBuilder().build());
  }

  public ClusterAndSession connectWithCredentials(String username, String password) {
    return new ClusterAndSession(
        getDefaultClusterBuilder().withCredentials(username, password).build());
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
      ClusterAndSession cs = connectWithTestDefaults();
      cluster = cs.getCluster();
      session = cs.getSession();
      LOG.info("Connected to cluster: " + cluster.getMetadata().getClusterName());
    } catch (Exception ex) {
      LOG.error("Error while setting up a CQL client", ex);
      throw ex;
    }

    final int numTServers = miniCluster.getTabletServers().size();
    final int expectedNumPeers = Math.max(0, Math.min(numTServers - 1, 2));
    LOG.info("Waiting until system.peers contains at least " + expectedNumPeers + " entries (" +
        "number of tablet servers: " + numTServers + ")");
    int attemptsMade = 0;
    boolean waitSuccessful = false;

    while (attemptsMade < 30) {
      int numPeers = execute("SELECT peer FROM system.peers").all().size();
      if (numPeers >= expectedNumPeers) {
        waitSuccessful = true;
        break;
      }
      LOG.info("system.peers still contains only " + numPeers + " entries, waiting");
      attemptsMade++;
      Thread.sleep(1000);
    }
    if (waitSuccessful) {
      LOG.info("Succeeded waiting for " + expectedNumPeers + " peers to show up in system.peers");
    } else {
      LOG.warn("Timed out waiting for " + expectedNumPeers + " peers to show up in system.peers");
    }

    // Create and use test keyspace to be able to use short table names later.
    createKeyspaceIfNotExists(DEFAULT_TEST_KEYSPACE);
    useKeyspace(DEFAULT_TEST_KEYSPACE);
  }

  @Override
  protected void resetSettings() {
    super.resetSettings();
    startCqlProxy = true;
    startRedisProxy = false;
    systemQueryCacheEmptyResponses = false;
    cqlClientTimeoutMs = 120 * 1000;
    clientReadWriteTimeoutMs = 180 * 1000;
    systemQueryCacheUpdateMs = 4 * 1000;
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
    LOG.info(logPrefix + "End test: " + getCurrentTestMethodName());

    if (miniCluster == null) {
      LOG.info(logPrefix + "mini cluster has been destroyed");
      return;
    }

    LOG.info(logPrefix + "dropping tables / types / keyspaces");

    // Clean up all tables before end of each test to lower high-water-mark disk usage.
    dropTables();

    // Delete all types to make sure we can delete keyspaces below.
    dropUDTypes();

    // Delete all non-system keyspaces.
    dropKeyspaces();

    // Delete all roles except a default one.
    dropRoles();

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

  protected void restartClusterWithTSFlags(Map<String, String> tserverFlags) throws Exception {
    destroyMiniCluster();
    createMiniCluster(Collections.emptyMap(), tserverFlags);
    setUpCqlClient();
  }

  protected void restartClusterWithFlag(String flag, String value) throws Exception {
    restartClusterWithTSFlags(Collections.singletonMap(flag, value));
  }

  protected void afterBaseCQLTestTearDown() throws Exception {
  }

  protected void createTable(String tableName, String columnType) throws Exception {
    final long startTimeMs = System.currentTimeMillis();
    LOG.info("CREATE TABLE " + tableName);
    String create_stmt = String.format("CREATE TABLE %s " +
                    " (h1 int, h2 %2$s, " +
                    " r1 int, r2 %2$s, " +
                    " v1 int, v2 %2$s, " +
                    " primary key((h1, h2), r1, r2));",
            tableName, columnType);
    session.execute(create_stmt);
    long elapsedTimeMs = System.currentTimeMillis() - startTimeMs;
    LOG.info("Table creation took " + elapsedTimeMs + " for table " + tableName +
        " with column type " + columnType);
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
    final AtomicLong lastLogTimeMs = new AtomicLong(startTimeMs);
    final int LOG_EVERY_MS = 5 * 1000;
    // INSERT: A valid statement with a column list.
    final PreparedStatement preparedStmt = session.prepare(
        "INSERT INTO " + tableName + "(h1, h2, r1, r2, v1, v2) VALUES(" +
            "?, ?, ?, ?, ?, ?);");

    final int numThreads = 4;
    ExecutorService executor = Executors.newFixedThreadPool(numThreads);
    final AtomicInteger currentRowIndex = new AtomicInteger();
    final AtomicInteger numInsertedRows = new AtomicInteger();
    List<Future<Void>> futures = new ArrayList<>();
    for (int i = 0; i < numThreads; ++i) {
      Callable<Void> task =
          () -> {
            int idx;
            while ((idx = currentRowIndex.get()) < numRows) {
              if (!currentRowIndex.compareAndSet(idx, idx + 1)) {
                // Not using incrementAndGet because we don't want currentRowIndex to become higher
                // than numRows.
                continue;
              }
              session.execute(preparedStmt.bind(
                  idx, "h" + idx, idx + 100, "r" + (idx + 100), idx + 1000, "v" + (idx + 1000)));
              numInsertedRows.incrementAndGet();
              if (idx % 10 == 0) {
                long currentTimeMs;
                long currentLastLogTimeMs;
                while ((currentTimeMs = System.currentTimeMillis()) -
                       (currentLastLogTimeMs = lastLogTimeMs.get()) > LOG_EVERY_MS) {
                  if (!lastLogTimeMs.compareAndSet(currentLastLogTimeMs, currentTimeMs)) {
                    // Someone updated lastLogTimeMs concurrently.
                    continue;
                  }
                  final int numRowsInsertedSoFar = numInsertedRows.get();
                  LOG.info("Inserted " + numRowsInsertedSoFar + " rows out of " + numRows + " (" +
                      String.format(
                          "%.2f", numRowsInsertedSoFar * 1000.0 / (currentTimeMs - startTimeMs)) +
                      " rows/second on average)."
                  );
                }
              }
            }
            return null;
          };
      futures.add(executor.submit(task));
    }

    // Wait for all writer threads to complete.
    for (Future<Void> future : futures) {
      future.get();
    }
    executor.shutdown();
    executor.awaitTermination(10, TimeUnit.SECONDS);

    final long totalTimeMs = System.currentTimeMillis() - startTimeMs;
    assertEquals(numRows, currentRowIndex.get());
    assertEquals(numRows, numInsertedRows.get());
    LOG.info("INSERT INTO TABLE " + tableName + " FINISHED: inserted " + numInsertedRows.get() +
             " rows (total time: " + totalTimeMs + " ms, " +
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

  protected void dropIndex(String indexName) throws Exception {
    String dropStmt = String.format("DROP INDEX %s;", indexName);
    session.execute(dropStmt);
  }

  protected void dropTable(String tableName) throws Exception {
    String dropStmt = String.format("DROP TABLE %s;", tableName);
    session.execute(dropStmt);
  }

  protected void dropUDType(String keyspaceName, String typeName) throws Exception {
    String dropStmt = String.format("DROP TYPE %s.%s;", keyspaceName, typeName);
    session.execute(dropStmt);
  }

  protected boolean isSystemKeyspace(String keyspaceName) {
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
    if (cluster == null) {
      return;
    }
    for (Row row : session.execute("SELECT keyspace_name, table_name FROM system_schema.tables")) {
      String ksName = row.getString("keyspace_name");
      String tableName = row.getString("table_name");
      if (!isSystemKeyspace(ksName)) {
        LOG.info("Dropping table " + ksName + "." + tableName);
        dropTable(ksName + "." + tableName);
      }
    }
  }

  protected void dropUDTypes() throws Exception {
    if (cluster == null) {
      return;
    }
    for (Row row : session.execute("SELECT keyspace_name, type_name FROM system_schema.types")) {
      String ksName = row.getString("keyspace_name");
      String typeName = row.getString("type_name");
      if (!isSystemKeyspace(ksName)) {
        LOG.info("Dropping UDT " + ksName + "." + typeName);
        dropUDType(ksName, typeName);
      }
    }
  }

  protected void dropKeyspaces() throws Exception {
    if (cluster == null) {
      return;
    }
    for (Row row : session.execute("SELECT keyspace_name FROM system_schema.keyspaces")) {
      String ksName = row.getString("keyspace_name");
      if (!isSystemKeyspace(ksName)) {
        LOG.info("Dropping keyspace " + ksName);
        dropKeyspace(ksName);
      }
    }
  }

  protected void dropRoles() throws Exception {
    if (cluster == null) {
      return;
    }
    for (Row row : session.execute("SELECT * FROM system_auth.roles")) {
      String roleName = row.getString("role");
      if (!DEFAULT_ROLE.equals(roleName)) {
        LOG.info("Dropping role " + roleName);
        session.execute("DROP ROLE '" + roleName + "'");
      }
    }
  }

  protected Iterator<Row> runSelect(String select_stmt) {
    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertTrue(iter.hasNext());
    return iter;
  }

  protected Iterator<Row> runSelect(String select_stmt, Object... args) {
    return runSelect(String.format(select_stmt, args));
  }

  /**
   * DEPRECATED: use method with error message checking
   *             {@link #runInvalidStmt(Statement stmt, String errorSubstring)}
   */
  @Deprecated
  protected String runInvalidQuery(Statement stmt) {
    return runInvalidStmt(stmt);
  }

  /**
   * DEPRECATED: use method with error message checking
   *             {@link #runInvalidStmt(String stmt, String errorSubstring)}
   */
  @Deprecated
  protected String runInvalidQuery(String stmt) {
    return runInvalidStmt(new SimpleStatement(stmt));
  }

  protected String runInvalidStmt(Statement stmt, Session s) {
    try {
      s.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
      return null; // Never happens, but keeps compiler happy
    } catch (QueryValidationException qv) {
      LOG.info("Expected exception", qv);
      return qv.getCause().getMessage();
    }
  }

  protected String runValidSelect(String stmt) {
    ResultSet rs = session.execute(stmt);
    String result = "";
    for (Row row : rs) {
      result += row.toString();
    }
    return result;
  }

  /**
   * DEPRECATED: use version with error message checking
   *             {@link #runInvalidStmt(Statement stmt, String errorSubstring)}
   */
  @Deprecated
  protected String runInvalidStmt(Statement stmt) {
    return runInvalidStmt(stmt, session);
  }

  /**
   * DEPRECATED: use version with error message checking
   *             {@link #runInvalidStmt(String stmt, Session s, String errorSubstring)}
   */
  @Deprecated
  protected String runInvalidStmt(String stmt, Session s) {
    return runInvalidStmt(new SimpleStatement(stmt), s);
  }

  /**
   * DEPRECATED: use version with error message checking
   *             {@link #runInvalidStmt(String stmt, String errorSubstring)}
   */
  @Deprecated
  protected String runInvalidStmt(String stmt) {
    return runInvalidStmt(stmt, session);
  }

  protected void runInvalidStmt(Statement stmt, Session s, String errorSubstring) {
    String errorMsg = runInvalidStmt(stmt, s);
    assertTrue("Error message '" + errorMsg + "' should contain '" + errorSubstring + "'",
               errorMsg.contains(errorSubstring));
  }

  protected void runInvalidStmt(Statement stmt, String errorSubstring) {
    runInvalidStmt(stmt, session, errorSubstring);
  }

  protected void runInvalidStmt(String stmt, String errorSubstring) {
    runInvalidStmt(new SimpleStatement(stmt), errorSubstring);
  }

  protected void assertNoRow(String select_stmt) {
    ResultSet rs = session.execute(select_stmt);
    Iterator<Row> iter = rs.iterator();
    assertFalse(iter.hasNext());
  }

  protected static String resultSetToString(ResultSet rs) {
    String result = "";
    for (Row row : rs) {
      result += row.toString();
    }
    return result;
  }

  protected void assertQuery(Statement stmt, String expectedResult) {
    assertEquals(expectedResult, resultSetToString(session.execute(stmt)));
  }

  protected void assertQuery(String stmt, String expectedResult) {
    assertQuery(new SimpleStatement(stmt), expectedResult);
  }

  protected void assertQuery(Statement stmt, String expectedColumns, String expectedResult) {
    ResultSet rs = session.execute(stmt);
    assertEquals(expectedColumns, rs.getColumnDefinitions().toString());
    assertEquals(expectedResult, resultSetToString(rs));
  }

  protected void assertQuery(String stmt, String expectedColumns, String expectedResult) {
    assertQuery(new SimpleStatement(stmt), expectedColumns, expectedResult);
  }

  protected void assertQuery(String stmt, Set<String> expectedRows) {
    assertQuery(new SimpleStatement(stmt), expectedRows);
  }

  protected void assertQuery(Statement stmt, Set<String> expectedRows) {
    ResultSet rs = session.execute(stmt);
    Set<String> actualRows = new HashSet<>();
    for (Row row : rs) {
      actualRows.add(row.toString());
    }
    assertEquals(expectedRows, actualRows);
  }

  // Assert presence of expected rows while ensuring no duplicates exist in actual output.
  protected void assertQueryWithoutDups(Statement stmt, Set<String> expectedRows) {
    ResultSet rs = session.execute(stmt);
    Set<String> actualRows = new HashSet<>();
    int row_cnt = 0;
    for (Row row : rs) {
      row_cnt++;
      actualRows.add(row.toString());
    }
    assertEquals(expectedRows, actualRows);
    assertEquals(row_cnt, expectedRows.size());
  }

  /**
   * Assert the result of a query when the order of the rows is not enforced.
   * To be used, for instance, when querying over multiple hashes where the order is defined by the
   * hash function not just the values.
   *
   * @param stmt The (select) query to be executed
   * @param expectedRows the expected rows in no particular order
   */
  protected void assertQueryRowsUnordered(String stmt, String... expectedRows) {
    assertQuery(stmt, Arrays.stream(expectedRows).collect(Collectors.toSet()));
  }

  /**
   * Assert the result of a query when the order of the rows is not enforced.
   * To be used, for instance, when querying over multiple hashes where the order is defined by the
   * hash function not just the values. Also, ensure that there are no duplicates in the actual
   * output.
   *
   * @param stmt The (select) query to be executed
   * @param expectedRows the expected rows in no particular order
   */
  protected void assertQueryRowsUnorderedWithoutDups(String stmt,
                                                     String... expectedRows) {
    assertQueryWithoutDups(new SimpleStatement(stmt),
      Arrays.stream(expectedRows).collect(Collectors.toSet()));
  }

  /**
   * Assert the result of a query when the order of the rows is enforced.
   * To be used, for instance, for queries where (range) column ordering (ASC/DESC) is being tested.
   *
   * @param stmt The (select) query to be executed
   * @param expectedRows the rows in the expected order
   */
  protected void assertQueryRowsOrdered(Statement stmt, List<String> expectedRows) {
    ResultSet rs = session.execute(stmt);
    List<String> actualRows = new ArrayList<String>();
    for (Row row : rs) {
      actualRows.add(row.toString());
    }
    assertEquals(expectedRows, actualRows);
  }

  /**
   * Assert the result of a query when the order of the rows is enforced.
   * To be used, for instance, for queries where (range) column ordering (ASC/DESC) is being tested.
   *
   * @param stmt The (select) query to be executed
   * @param expectedRows the rows in the expected order
   */
  protected void assertQueryRowsOrdered(String stmt, String... expectedRows) {
    assertQueryRowsOrdered(new SimpleStatement(stmt),
                           Arrays.stream(expectedRows).collect(Collectors.toList()));
  }

  /** Checks that the query yields an error containing the given (case-insensitive) substring */
  protected void assertQueryError(Statement stmt, String expectedErrorSubstring) {
    String result = runInvalidStmt(stmt);
    assertTrue("Query error '" + result + "' did not contain '" + expectedErrorSubstring + "'",
        result.toLowerCase().contains(expectedErrorSubstring.toLowerCase()));
  }

  protected void assertQueryError(String stmt, String expectedErrorSubstring) {
    assertQueryError(new SimpleStatement(stmt), expectedErrorSubstring);
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

  public ResultSet execute(String stmt, Session s) throws Exception {
    LOG.info("EXEC CQL: " + stmt);
    final ResultSet result = s.execute(stmt);
    LOG.info("EXEC CQL FINISHED: " + stmt);
    return result;
  }

  public ResultSet execute(String stmt) throws Exception { return execute(stmt, session); }

  public void executeInvalid(String stmt, Session s) throws Exception {
    LOG.info("EXEC INVALID CQL: " + stmt);
    runInvalidStmt(stmt, s);
    LOG.info("EXEC INVALID CQL FINISHED: " + stmt);
  }

  public void executeInvalid(String stmt) throws Exception { executeInvalid(stmt, session); }

  public void createKeyspace(String keyspaceName) throws Exception {
    String createKeyspaceStmt = "CREATE KEYSPACE \"" + keyspaceName + "\";";
    execute(createKeyspaceStmt);
  }

  public void createKeyspaceIfNotExists(String keyspaceName) throws Exception {
    String createKeyspaceStmt = "CREATE KEYSPACE IF NOT EXISTS \"" + keyspaceName + "\";";
    execute(createKeyspaceStmt);
  }

  public void useKeyspace(String keyspaceName) throws Exception {
    String useKeyspaceStmt = "USE \"" + keyspaceName + "\";";
    execute(useKeyspaceStmt);
  }

  public void dropKeyspace(String keyspaceName) throws Exception {
    String deleteKeyspaceStmt = "DROP KEYSPACE \"" + keyspaceName + "\";";
    execute(deleteKeyspaceStmt);
  }

  private String getTableUUID(String keyspaceName, String tableName)  throws Exception {
    return miniCluster.getClient().openTable(keyspaceName, tableName).getTableId();
  }

  public RocksDBMetrics getRocksDBMetric(String tableName) throws Exception {
    return getRocksDBMetricByTableUUID(getTableUUID(DEFAULT_TEST_KEYSPACE, tableName));
  }

  public int getTableCounterMetric(String keyspaceName,
                                   String tableName,
                                   String metricName) throws Exception {
    return getTableCounterMetricByTableUUID(getTableUUID(keyspaceName, tableName), metricName);
  }

  public int getRestartsCount(String tableName) throws Exception {
    return getTableCounterMetricByTableUUID(getTableUUID(DEFAULT_TEST_KEYSPACE, tableName),
                                "restart_read_requests");
  }

  public int getRetriesCount() throws Exception {
    int totalSum = 0;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      totalSum += new Metrics(ts.getLocalhostIP(), ts.getCqlWebPort(), "server")
                  .getHistogram("handler_latency_yb_cqlserver_SQLProcessor_NumRetriesToExecute")
                  .totalSum;
    }
    return totalSum;
  }

  public void waitForYcqlConnectivity() {
    final int maxAttempts = 20;
    Stopwatch stopwatch = Stopwatch.createStarted();
    LOG.info("Starting to wait for YCQL connectivity");
    boolean success = false;
    for (int connectionAttempt = 1; connectionAttempt <= maxAttempts; ++connectionAttempt) {
      // We have to connect with the test's default settings, because for authenticated clusters
      // we might be required to provide username/password.
      try (ClusterAndSession clusterAndSession = connectWithTestDefaults()) {
        success = true;
        break;
      } catch (com.datastax.driver.core.exceptions.AuthenticationException authEx) {
        LOG.info(
            "Received an AuthenticationException, which means YCQL connectivity is established",
            authEx);
        success = true;
        break;
      } catch (Exception ex) {
        long sleepMs = 100 * connectionAttempt;
        LOG.info(
            "Temporary connection failure. This was attempt " + connectionAttempt + ", " +
            "elapsed time waiting: " +
            StringUtil.toDecimalString(stopwatch.elapsed(TimeUnit.MILLISECONDS) / 1000.0, 3) +
            "sec. Will re-try after " + sleepMs + " ms.");
        try {
          Thread.sleep(sleepMs);
        } catch (InterruptedException iex) {
          LOG.warn("Caught an InterruptedException, breaking the connectivity check loop.", iex);
          Thread.currentThread().interrupt();
          break;
        }
      }
    }

    LOG.info(
        (success ? "YCQL connectivity established" : "Still no YCQL connectivity") + " after " +
        StringUtil.toDecimalString(stopwatch.elapsed(TimeUnit.MILLISECONDS) / 1000.0, 3) + " sec");
  }

  public void restartYcqlMiniCluster() throws Exception {
    miniCluster.restart();
    waitForYcqlConnectivity();
  }

  protected boolean doesQueryPlanContainSubstring(String query, String substring)
      throws SQLException {
    ResultSet rs = session.execute("EXPLAIN " + query);

    for (Row row : rs) {
      if (row.toString().contains(substring)) return true;
    }
    return false;
  }

  protected void waitForReadPermsOnAllIndexes(String tableName) throws Exception {
    waitForReadPermsOnAllIndexes(DEFAULT_TEST_KEYSPACE, tableName);
  }

  protected void waitForReadPermsOnAllIndexes(String keyspace, String tableName) throws Exception {
    TestUtils.waitFor(
      () -> {
        boolean all_indexes_have_read_perms = true;
        GetTableSchemaResponse response = miniCluster.getClient().getTableSchema(
          keyspace, tableName);
        List<IndexInfo> indexes = response.getIndexes();

        for (IndexInfo index : indexes) {
          if (index.getIndexPermissions() !=
                CommonTypes.IndexPermissions.INDEX_PERM_READ_WRITE_AND_DELETE) {
            LOG.info("Found index with permissions=" + index.getIndexPermissions() +
              " != INDEX_PERM_READ_WRITE_AND_DELETE");
            return false;
          }
        }
        return true;
      }, 20000 /* timeoutMs */, 100 /* sleepTime */);
  }
}
