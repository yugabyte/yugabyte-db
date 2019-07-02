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

import org.apache.commons.lang3.StringUtils;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.postgresql.core.TransactionState;
import org.postgresql.jdbc.PgConnection;
import org.postgresql.util.PSQLException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.Partition;
import org.yb.client.TestUtils;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.*;

import java.io.File;
import java.sql.*;
import java.util.*;
import java.util.concurrent.ConcurrentSkipListSet;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicReference;

import static org.yb.AssertionWrappers.*;
import static org.yb.util.SanitizerUtil.isTSAN;

public class BasePgSQLTest extends BaseMiniClusterTest {
  private static final Logger LOG = LoggerFactory.getLogger(BasePgSQLTest.class);

  // Postgres settings.
  protected static final String DEFAULT_PG_DATABASE = "postgres";
  protected static final String DEFAULT_PG_USER = "postgres";
  protected static final String DEFAULT_PG_PASSWORD = "";

  // Postgres flags.
  private static final String MASTERS_FLAG = "FLAGS_pggate_master_addresses";
  private static final String PG_DATA_FLAG = "PGDATA";
  private static final String YB_ENABLED_IN_PG_ENV_VAR_NAME = "YB_ENABLED_IN_POSTGRES";

  // Extra Postgres flags.
  protected static Map<String, String> extraPostgresEnvVars;

  // CQL and Redis settings.
  protected static boolean startCqlProxy = false;
  protected static boolean startRedisProxy = false;

  protected static Connection connection;

  protected File pgBinDir;

  private static List<Connection> connectionsToClose = new ArrayList<>();

  protected static final int DEFAULT_STATEMENT_TIMEOUT_MS = 30000;

  protected static ConcurrentSkipListSet<Integer> stuckBackendPidsConcMap =
      new ConcurrentSkipListSet<>();

  protected static boolean pgInitialized = false;

  public void runPgRegressTest(String schedule) throws Exception {
    final int tserverIndex = 0;
    PgRegressRunner pgRegress = new PgRegressRunner(schedule,
        getPgHost(tserverIndex), getPgPort(tserverIndex), DEFAULT_PG_USER);
    pgRegress.setEnvVars(getPgRegressEnvVars());
    pgRegress.start();
    pgRegress.stop();
  }

  private static int getRetryableRpcSingleCallTimeoutMs() {
    if (TestUtils.isReleaseBuild()) {
      return 10000;
    } else if (TestUtils.IS_LINUX) {
      if (SanitizerUtil.isASAN()) {
        return 20000;
      } else if (SanitizerUtil.isTSAN()) {
        return 45000;
      } else {
        // Linux debug builds.
        return 15000;
      }
    } else {
      // We get a lot of timeouts in macOS debug builds.
      return 45000;
    }
  }

  protected static int getPerfMaxRuntime(int releaseRuntime,
                                         int debugRuntime,
                                         int asanRuntime,
                                         int tsanRuntime,
                                         int macRuntime) {
    if (TestUtils.isReleaseBuild()) {
      return releaseRuntime;
    } else if (TestUtils.IS_LINUX) {
      if (SanitizerUtil.isASAN()) {
        return asanRuntime;
      } else if (SanitizerUtil.isTSAN()) {
        return tsanRuntime;
      } else {
        // Linux debug builds.
        return debugRuntime;
      }
    } else {
      // We get a lot of timeouts in macOS debug builds.
      return macRuntime;
    }
  }

  private Map<String, String> getMasterAndTServerFlags() {
    Map<String, String> flagMap = new TreeMap<>();
    flagMap.put(
        "retryable_rpc_single_call_timeout_ms",
        String.valueOf(getRetryableRpcSingleCallTimeoutMs()));
    return flagMap;
  }

  protected String pgPrefetchLimit() {
    return null;
  }

  /**
   * @return flags shared between tablet server and initdb
   */
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = new TreeMap<>();

    if (isTSAN()) {
      flagMap.put("yb_client_admin_operation_timeout_sec", "120");
    }
    flagMap.put("start_cql_proxy", Boolean.toString(startCqlProxy));
    flagMap.put("start_redis_proxy", Boolean.toString(startRedisProxy));

    // Setup flag for postgres test on prefetch-limit when starting tserver.
    if (pgPrefetchLimit() != null) {
      flagMap.put("ysql_prefetch_limit", pgPrefetchLimit());
    }

    flagMap.put("ysql_beta_features", "true");

    return flagMap;
  }

  private Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = new TreeMap<>();
    flagMap.put("client_read_write_timeout_ms", "120000");
    flagMap.put("memory_limit_hard_bytes", String.valueOf(2L * 1024 * 1024 * 1024));
    flagMap.put("use_initial_sys_catalog_snapshot", "true");
    return flagMap;
  }

  @Override
  protected int overridableNumShardsPerTServer() {
    return 1;
  }

  @Override
  protected int getReplicationFactor() {
    return 3;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    for (Map.Entry<String, String> entry : getTServerFlags().entrySet()) {
      builder.addCommonTServerArgs("--" + entry.getKey() + "=" + entry.getValue());
    }
    for (Map.Entry<String, String> entry : getMasterFlags().entrySet()) {
      builder.addMasterArgs("--" + entry.getKey() + "=" + entry.getValue());
    }

    for (Map.Entry<String, String> entry : getMasterAndTServerFlags().entrySet()) {
      String flagStr = "--" + entry.getKey() + "=" + entry.getValue();
      builder.addCommonTServerArgs(flagStr);
      builder.addMasterArgs(flagStr);
    }
    builder.enablePostgres(true);
  }

  @Before
  public void initPostgresBefore() throws Exception {
    if (pgInitialized)
      return;

    LOG.info("Loading PostgreSQL JDBC driver");
    Class.forName("org.postgresql.Driver");

    // Postgres bin directory.
    pgBinDir = new File(TestUtils.getBuildRootDir(), "postgres/bin");

    LOG.info("Waiting for initdb to complete on master");
    TestUtils.waitFor(
        () -> {
          IsInitDbDoneResponse initdbStatusResp = miniCluster.getClient().getIsInitDbDone();
          if (initdbStatusResp.isDone()) {
            return true;
          }
          if (initdbStatusResp.hasError()) {
            throw new RuntimeException(
                "Could not request initdb status: " + initdbStatusResp.getServerError());
          }
          String initdbError = initdbStatusResp.getInitDbError();
          if (initdbError != null && !initdbError.isEmpty()) {
            throw new RuntimeException("initdb failed: " + initdbError);
          }
          return false;
        },
        600000);
    LOG.info("initdb has completed successfully on master");

    if (connection != null) {
      LOG.info("Closing previous connection");
      connection.close();
      connection = null;
    }
    connection = createConnection();
    pgInitialized = true;
  }

  private void configureDefaultConnectionOptions(Connection conn) throws Exception {
    conn.setTransactionIsolation(IsolationLevel.DEFAULT.pgIsolationLevel);
    conn.setAutoCommit(AutoCommit.DEFAULT.enabled);
  }

  protected Connection createConnection(
      IsolationLevel isolationLevel,
      AutoCommit autoCommit) throws Exception {
    Connection conn = createConnection();
    conn.setTransactionIsolation(isolationLevel.pgIsolationLevel);
    conn.setAutoCommit(autoCommit.enabled);
    return conn;
  }

  protected Connection createConnectionSerializableNoAutoCommit() throws Exception {
    return createConnection(
        IsolationLevel.SERIALIZABLE,
        AutoCommit.DISABLED
    );
  }

  public String getPgHost(int tserverIndex) {
    return miniCluster.getPostgresContactPoints().get(tserverIndex).getHostName();
  }

  public int getPgPort(int tserverIndex) {
    return miniCluster.getPostgresContactPoints().get(tserverIndex).getPort();
  }

  protected Connection createConnection() throws Exception {
    return createConnection(0);
  }

  protected Connection createConnection(int tserverIndex) throws Exception {
    return createConnection(tserverIndex, DEFAULT_PG_DATABASE);
  }

  protected Connection createPgConnectionToTServer(
      int tserverIndex,
      IsolationLevel isolationLevel,
      AutoCommit autoCommit) throws Exception {
    Connection conn = createConnection(tserverIndex, DEFAULT_PG_DATABASE);
    conn.setTransactionIsolation(isolationLevel.pgIsolationLevel);
    conn.setAutoCommit(autoCommit.enabled);
    return conn;
  }

  protected Connection createConnection(int tserverIndex, String pgDB) throws Exception {
    final String pgHost = getPgHost(tserverIndex);
    final int pgPort = getPgPort(tserverIndex);
    String url = String.format("jdbc:postgresql://%s:%d/%s", pgHost, pgPort, pgDB);
    if (EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_PG_JDBC_TRACE_LOGGING")) {
      url += "?loggerLevel=TRACE";
    }

    final int MAX_ATTEMPTS = 10;
    int delayMs = 500;
    Connection connection = null;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
      try {
        connection = DriverManager.getConnection(url, DEFAULT_PG_USER, DEFAULT_PG_PASSWORD);
        if (connection == null) {
          throw new NullPointerException("getConnection returned null");
        }
        connectionsToClose.add(connection);
        configureDefaultConnectionOptions(connection);
        // JDBC does not specify a default for auto-commit, let's set it to true here for
        // determinism.
        connection.setAutoCommit(true);
        return connection;
      } catch (SQLException sqlEx) {
        // Close the connection now if we opened it, instead of waiting until the end of the test.
        if (connection != null) {
          try {
            connection.close();
            connectionsToClose.remove(connection);
            connection = null;
          } catch (SQLException closingError) {
            LOG.error("Failure to close connection during failure cleanup before a retry:",
                closingError);
            LOG.error("When handling this exception when opening/setting up connection:", sqlEx);
          }
        }

        if (attempt < MAX_ATTEMPTS &&
            sqlEx.getMessage().contains("FATAL: the database system is starting up") ||
            sqlEx.getMessage().contains("refused. Check that the hostname and port are correct " +
                "and that the postmaster is accepting")) {
          LOG.info("Postgres is still starting up, waiting for " + delayMs + " ms. " +
              "Got message: " + sqlEx.getMessage());
          Thread.sleep(delayMs);
          delayMs = Math.min(delayMs + 500, 10000);
          continue;
        }
        LOG.error("Exception while trying to create connection (after " + attempt +
            " attempts): " + sqlEx.getMessage());
        throw sqlEx;
      }
    }
    throw new IllegalStateException("Should not be able to reach here");
  }

  protected Map<String, String> getPgRegressEnvVars() {
    Map<String, String> pgRegressEnvVars = new TreeMap<>();
    pgRegressEnvVars.put(MASTERS_FLAG, masterAddresses);
    pgRegressEnvVars.put(YB_ENABLED_IN_PG_ENV_VAR_NAME, "1");

    for (Map.Entry<String, String> entry : System.getenv().entrySet()) {
      String envVarName = entry.getKey();
      if (envVarName.startsWith("postgres_FLAGS_")) {
        String downstreamEnvVarName = envVarName.substring(9);
        LOG.info("Found env var " + envVarName + ", setting " + downstreamEnvVarName + " for " +
                 "pg_regress to " + entry.getValue());
        pgRegressEnvVars.put(downstreamEnvVarName, entry.getValue());
      }
    }

    // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
    pgRegressEnvVars.put("YB_PG_FALLBACK_SYSTEM_USER_NAME", "postgres");

    return pgRegressEnvVars;
  }

  @After
  public void cleanUpAfter() throws Exception {
    if (connection == null) {
      LOG.warn("No connection created, skipping dropping tables");
      return;
    }
    try (Statement statement = connection.createStatement())  {
      DatabaseMetaData dbmd = connection.getMetaData();
      String[] views = {"VIEW"};
      ResultSet rs = dbmd.getTables(null, null, "%", views);
      while (rs.next()) {
        statement.execute("DROP VIEW " + rs.getString("TABLE_NAME"));
      }
      String[] tables = {"TABLE"};
      rs = dbmd.getTables(null, null, "%", tables);
      while (rs.next()) {
        statement.execute("DROP TABLE " + rs.getString("TABLE_NAME"));
      }
    }
  }

  @AfterClass
  public static void tearDownAfter() throws Exception {
    try {
      tearDownPostgreSQL();
    } finally {
      LOG.info("Destroying mini-cluster");
      if (miniCluster != null) {
        destroyMiniCluster();
        miniCluster = null;
      }
    }
  }

  private static void tearDownPostgreSQL() throws Exception {
    if (connection != null) {
      try (Statement statement = connection.createStatement()) {
        try (ResultSet resultSet = statement.executeQuery(
            "SELECT client_hostname, client_port, state, query, pid FROM pg_stat_activity")) {
          while (resultSet.next()) {
            int backendPid = resultSet.getInt(5);
            LOG.info("Found connection: " +
                "hostname=" + resultSet.getString(1) + ", " +
                "port=" + resultSet.getInt(2) + ", " +
                "state=" + resultSet.getString(3) + ", " +
                "query=" + resultSet.getString(4) + ", " +
                "backend_pid=" + backendPid);
          }
        }
      }
      catch (SQLException e) {
        LOG.info("Exception when trying to list PostgreSQL connections", e);
      }

      LOG.info("Closing connections.");
      for (Connection connection : connectionsToClose) {
        try {
          if (connection == null) {
            LOG.error("connectionsToClose contains a null connection!");
          } else {
            connection.close();
          }
        } catch (SQLException ex) {
          LOG.error("Exception while trying to close connection");
          throw ex;
        }
      }
    } else {
      LOG.info("Connection is already null, nothing to close");
    }
    LOG.info("Finished closing connection.");

    LOG.info("Finished stopping postgres server.");
  }

  /**
   * Commit the current transaction on the given connection, catch and report the exception.
   * @param conn connection to use
   * @param extraMsg an extra part of the error message
   * @return whether commit succeeded
   */
  protected static boolean commitAndCatchException(Connection conn, String extraMsg) {
    extraMsg = extraMsg.trim();
    if (!extraMsg.isEmpty()) {
      extraMsg = " (" + extraMsg + ")";
    }
    try {
      conn.commit();
      return true;
    } catch (SQLException ex) {
      // TODO: validate the exception message.
      LOG.info("Error during commit" + extraMsg + ": " + ex.getMessage());
      return false;
    }
  }

  protected static PgConnection toPgConnection(Connection connection) {
    return (PgConnection) connection;
  }

  protected static TransactionState getPgTxnState(Connection connection) {
    return toPgConnection(connection).getTransactionState();
  }

  protected static int getPgBackendPid(Connection connection) {
    return toPgConnection(connection).getBackendPID();
  }

  protected void executeWithTimeout(Statement statement, String sql)
      throws SQLException, TimeoutException, InterruptedException {
    // Maintain our map saying how many statements are being run by each backend pid.
    // Later we can determine (possibly) stuck backends based on this.
    final int backendPid = getPgBackendPid(statement.getConnection());

    AtomicReference<SQLException> sqlExceptionWrapper = new AtomicReference<>();
    boolean timedOut = false;
    try {
      String taskDescription = "SQL statement (PG backend pid: " + backendPid + "): " + sql;
      runWithTimeout(DEFAULT_STATEMENT_TIMEOUT_MS, taskDescription, () -> {
        try {
          statement.execute(sql);
        } catch (SQLException e) {
          sqlExceptionWrapper.set(e);
        }
      });
    } catch (TimeoutException ex) {
      // Record that this backend is possibly "stuck" so we can force a core dump and examine it.
      stuckBackendPidsConcMap.add(backendPid);
      timedOut = true;
      throw ex;
    } finally {
      // Make sure we propagate the SQLException. But TimeoutException takes precedence.
      if (!timedOut && sqlExceptionWrapper.get() != null) {
        throw sqlExceptionWrapper.get();
      }
    }
  }

  public class PgTxnState {
    public PgTxnState(Connection connection, String connectionName) {
      this.connection = connection;
      this.connectionName = connectionName;
    }

    boolean isFinished() {
      return stmtExecuted != null &&
          beforeCommitState != null &&
          afterCommitState != null &&
          committed != null;
    }

    public boolean isSuccess() {
      return isFinished() &&
          stmtExecuted &&
          TransactionState.OPEN == beforeCommitState &&
          committed &&
          TransactionState.IDLE == afterCommitState;
    }

    public boolean isFailure() {
      if (!isFinished()) {
        return false;
      }

      // We have two cases:
      // 1. If stmt execution succeeded but commit failed.
      // 2. If stmt exec failed. Then txn should be in failed state and commit should succeed (but
      //    effectively do a rollback/abort).
      if (stmtExecuted) {
        return TransactionState.OPEN == beforeCommitState &&
            !committed &&
            TransactionState.IDLE == afterCommitState;

      } else {
        return TransactionState.FAILED == beforeCommitState &&
            committed &&
            TransactionState.IDLE == afterCommitState;
      }
    }

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append("PgTxnState: ").append(connectionName).append("\n");
      sb.append("{\n");
      sb.append("  stmtExecuted: ").append(String.valueOf(stmtExecuted)).append("\n");
      sb.append("  beforeCommitState: ").append(String.valueOf(beforeCommitState)).append("\n");
      sb.append("  committed: ").append(String.valueOf(committed)).append("\n");
      sb.append("  afterCommitState: ").append(String.valueOf(afterCommitState)).append("\n");
      sb.append("}\n");
      return sb.toString();
    }

    private Statement getStatement() throws SQLException {
      if (statement != null) {
        return statement;
      }
      return connection.createStatement();
    }

    private String connectionName;
    private Connection connection;
    private Statement statement = null;

    private Boolean stmtExecuted = null;
    private TransactionState beforeCommitState = null;
    private Boolean committed = null;
    private TransactionState afterCommitState = null;
  }


  protected void executeWithTxnState(PgTxnState txnState, String query) throws Exception {
    boolean previousStmtFailed = Boolean.FALSE.equals(txnState.stmtExecuted);
    txnState.stmtExecuted = false;
    try {
      executeWithTimeout(txnState.getStatement(), query);
      txnState.stmtExecuted = !previousStmtFailed;
    } catch (PSQLException ex) {
      // TODO: validate the exception message.
      // Not reporting a stack trace here on purpose, because this will happen a lot in a test.
      LOG.info("Error while inserting on the second connection:" + ex.getMessage());
    }
  }

  protected void commitWithTxnState(PgTxnState txnState) {
    txnState.beforeCommitState = getPgTxnState(txnState.connection);
    txnState.committed = commitAndCatchException(txnState.connection, txnState.connectionName);
    txnState.afterCommitState = getPgTxnState(txnState.connection);
  }

  //------------------------------------------------------------------------------------------------
  // Test Utilities

  protected class Row implements Comparable<Row> {
    ArrayList<Comparable> elems = new ArrayList<>();

    Row(Comparable... args) {
      Collections.addAll(elems, args);
    }

    Comparable get(int index) {
      return elems.get(index);
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
    public int compareTo(Row other) {
      // In our test, if selected Row has different number of columns from expected row, something
      // must be very wrong. Stop the test here.
      assertEquals(elems.size(), other.elems.size());
      for (int i = 0; i < elems.size(); i++) {
        if (elems.get(i) == null || other.elems.get(i) == null) {
          if (elems.get(i) != other.elems.get(i)) {
            return elems.get(i) == null ? -1 : 1;
          }
        } else {
          int compare_result = elems.get(i).compareTo(other.elems.get(i));
          if (compare_result != 0) {
            return compare_result;
          }
        }
      }
      return 0;
    }

    @Override
    public int hashCode() {
      return elems.hashCode();
    }

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append("Row[");
      for (int i = 0; i < elems.size(); i++) {
        if (i > 0) sb.append(',');
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
  }

  protected Set<Row> getRowSet(ResultSet rs) throws SQLException {
    Set<Row> rows = new HashSet<>();
    while (rs.next()) {
      Comparable[] elems = new Comparable[rs.getMetaData().getColumnCount()];
      for (int i = 0; i < elems.length; i++) {
        elems[i] = (Comparable)rs.getObject(i + 1); // Column index starts from 1.
      }
      rows.add(new Row(elems));
    }
    return rows;
  }

  protected List<Row> getRowList(ResultSet rs) throws SQLException {
    List<Row> rows = new ArrayList<>();
    while (rs.next()) {
      Comparable[] elems = new Comparable[rs.getMetaData().getColumnCount()];
      for (int i = 0; i < elems.length; i++) {
        elems[i] = (Comparable)rs.getObject(i + 1); // Column index starts from 1.
      }
      rows.add(new Row(elems));
    }
    return rows;
  }

  protected List<Row> getSortedRowList(ResultSet rs) throws SQLException {
    // Sort all rows and return.
    List<Row> rows = getRowList(rs);
    Collections.sort(rows);
    return rows;
  }

  protected void assertQuery(Statement stmt, String query, Row... expectedRows)
      throws SQLException {
    List<Row> actualRows = getRowList(stmt.executeQuery(query));
    assertEquals(
        "Expected " + expectedRows.length + " rows, got " + actualRows.size() + ": " + actualRows,
        expectedRows.length, actualRows.size());
    assertArrayEquals(expectedRows, actualRows.toArray(new Row[0]));
  }

  protected void assertNoRows(Statement stmt, String query) throws SQLException {
    List<Row> actualRows = getRowList(stmt.executeQuery(query));
    assertTrue("Expected no results, got " + actualRows, actualRows.isEmpty());
  }

  protected void assertNextRow(ResultSet rs, Object... values) throws SQLException {
    assertTrue(rs.next());
    for (int i = 0; i < values.length; i++) {
      assertEquals(values[i], rs.getObject(i + 1)); // Column index starts from 1.
    }
  }

  protected void assertOneRow(String stmt, Object... values) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(stmt)) {
        assertNextRow(rs, values);
        assertFalse(rs.next());
      }
    }
  }

  protected void assertRowSet(String stmt, Set<Row> expectedRows) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(stmt)) {
        assertEquals(expectedRows, getRowSet(rs));
      }
    }
  }

  /*
   * Returns whether or not this select statement uses index.
   */
  protected boolean useIndex(String stmt, String index) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery("EXPLAIN " + stmt)) {
        assert(rs.getMetaData().getColumnCount() == 1); // Expecting one string column.
        while (rs.next()) {
          if (rs.getString(1).contains("Index Scan using " + index)) {
            return true;
          }
        }
        return false;
      }
    }
  }

  /*
   * Returns whether or not this select statement requires filtering by Postgres (i.e. not all
   * conditions can be pushed down to YugaByte).
   */
  protected boolean needsPgFiltering(String stmt) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery("EXPLAIN " + stmt)) {
        assert(rs.getMetaData().getColumnCount() == 1); // Expecting one string column.
        while (rs.next()) {
          if (rs.getString(1).contains("Filter:")) {
            return true;
          }
        }
        return false;
      }
    }
  }

  protected void createSimpleTableWithSingleColumnKey(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      String sql =
          "CREATE TABLE " + tableName + "(h bigint PRIMARY KEY, r float, vi int, vs text)";
      LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
      statement.execute(sql);
      LOG.info("Table creation finished: " + tableName);
    }
  }

  protected void createSimpleTable(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable(statement, tableName);
    }
  }

  protected void createSimpleTable(Statement statement, String tableName) throws SQLException {
    String sql =
        "CREATE TABLE " + tableName + "(h bigint, r float, vi int, vs text, PRIMARY KEY (h, r))";
    LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
    statement.execute(sql);
    LOG.info("Table creation finished: " + tableName);
  }

  /**
   * Deprecated. Use the version below which requires an expected error message substring.
   * TODO Consider replacing all occurences of this version and then removing it.
   */
  @Deprecated
  protected void runInvalidQuery(Statement statement, String query) {
    try {
      statement.execute(query);
      fail(String.format("Statement did not fail: %s", query));
    } catch (SQLException e) {
      LOG.info("Expected exception", e);
    }
  }

  /**
   *
   * @param statement The statement used to execute the query.
   * @param query The query string.
   * @param errorSubstring A (case-insensitive) substring of the expected error message.
   */
  protected void runInvalidQuery(Statement statement, String query, String errorSubstring) {
    try {
      statement.execute(query);
      fail(String.format("Statement did not fail: %s", query));
    } catch (SQLException e) {
      if (StringUtils.containsIgnoreCase(e.getMessage(), errorSubstring)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
                           e.getMessage(), errorSubstring));
      }
    }
  }

  protected String getSimpleTableCreationStatement(
      String tableName,
      String valueColumnName,
      PartitioningMode partitioningMode) {
    String firstColumnIndexMode;
    if (partitioningMode == PartitioningMode.HASH) {
      firstColumnIndexMode = "HASH";
    } else {
      firstColumnIndexMode = "ASC";
    }
    return "CREATE TABLE " + tableName + "(h int, r int, " + valueColumnName + " int, " +
        "PRIMARY KEY (h " + firstColumnIndexMode + ", r))";
  }

  protected void createSimpleTable(
      String tableName,
      String valueColumnName,
      PartitioningMode partitioningMode) throws SQLException {
    Statement statement = connection.createStatement();
    String sql = getSimpleTableCreationStatement(tableName, valueColumnName, partitioningMode);
    LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
    statement.execute(sql);
    LOG.info("Table creation finished: " + tableName);
  }

  protected void createSimpleTable(String tableName, String valueColumnName) throws SQLException {
    createSimpleTable(tableName, valueColumnName, PartitioningMode.HASH);
  }

  protected List<Row> setupSimpleTable(String tableName) throws SQLException {
    List<Row> allRows = new ArrayList<>();
    try (Statement statement = connection.createStatement()) {
      createSimpleTable(tableName);
      String insertTemplate = "INSERT INTO %s(h, r, vi, vs) VALUES (%d, %f, %d, '%s')";

      for (int h = 0; h < 10; h++) {
        for (int r = 0; r < 10; r++) {
          statement.execute(String.format(insertTemplate, tableName,
                                          h, r + 0.5, h * 10 + r, "v" + h + r));
          allRows.add(new Row((long) h,
                              r + 0.5,
                              h * 10 + r,
                              "v" + h + r));
        }
      }
    }

    // Sort inserted rows and return.
    Collections.sort(allRows);
    return allRows;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    // initdb takes a really long time on macOS in debug mode.
    return 1200;
  }

  void waitForTServerHeartbeat() throws InterruptedException {
    // Wait an extra heartbeat interval to avoid race conditions due to deviations
    // in the real heartbeat frequency (due to latency, scheduling, etc.).
    Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 2);
  }

  // Time execution time of a statement.
  protected void timeQueryWithRowCount(String stmt, int expectedRowCount, long maxRuntimeMillis)
      throws Exception {
    LOG.info(String.format("Exec query: %s", stmt));
    final long runtimeMillis = System.currentTimeMillis();

    // Query and check row count.
    int rowCount = 0;
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(stmt)) {
        while (rs.next()) {
          rowCount++;
        }
      }
    }
    assertEquals(rowCount, expectedRowCount);

    // Check the elapsed time.
    long elapsedTimeMillis = System.currentTimeMillis() - runtimeMillis;
    LOG.info(String.format("Complete query: %s. Elapsed time = %d msecs", stmt, elapsedTimeMillis));
    assertTrue(elapsedTimeMillis < maxRuntimeMillis);
  }
}
