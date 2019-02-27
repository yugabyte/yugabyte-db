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

import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.postgresql.core.TransactionState;
import org.postgresql.jdbc.PgConnection;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.LogPrinter;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.*;

import java.io.File;
import java.sql.*;
import java.util.*;
import java.util.concurrent.ConcurrentSkipListSet;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicReference;

import static org.yb.AssertionWrappers.*;
import static org.yb.client.TestUtils.getBaseTmpDir;
import static org.yb.util.ProcessUtil.pidStrOfProcess;
import static org.yb.util.SanitizerUtil.isTSAN;
import static org.yb.util.SanitizerUtil.nonTsanVsTsan;

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

  protected static Connection connection;

  private static File initdbDataDir;

  protected File pgBinDir;

  private static List<Connection> connectionsToClose = new ArrayList<>();

  protected static final int DEFAULT_STATEMENT_TIMEOUT_MS = 30000;

  protected static ConcurrentSkipListSet<Integer> stuckBackendPidsConcMap =
      new ConcurrentSkipListSet<>();

  private boolean pgInitialized = false;

  public void runPgRegressTest(String schedule) throws Exception {
    final int tserverIndex = 0;
    PgRegressRunner pgRegress = new PgRegressRunner(schedule,
        getPgHost(tserverIndex), getPgPort(tserverIndex), DEFAULT_PG_USER);
    pgRegress.setEnvVars(getInitDbEnvVars());
    pgRegress.start();
    pgRegress.stop();
  }

  /**
   * @return flags shared between tablet server and initdb
   */
  private Map<String, String> getTServerAndInitDbFlags() {
    Map<String, String> flagMap = new TreeMap<>();

    int callTimeoutMs;
    if (TestUtils.isReleaseBuild()) {
      callTimeoutMs = 10000;
    } else if (TestUtils.IS_LINUX) {
      if (SanitizerUtil.isASAN()) {
        callTimeoutMs = 20000;
      } else if (SanitizerUtil.isTSAN()) {
        callTimeoutMs = 45000;
      } else {
        // Linux debug builds.
        callTimeoutMs = 15000;
      }
    } else {
      // We get a lot of timeouts in macOS debug builds.
      callTimeoutMs = 45000;
    }
    flagMap.put("retryable_rpc_single_call_timeout_ms", String.valueOf(callTimeoutMs));
    if (isTSAN()) {
      flagMap.put("yb_client_admin_operation_timeout_sec", "120");
    }
    flagMap.put("start_redis_proxy", "false");
    flagMap.put("start_cql_proxy", "false");

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
    for (Map.Entry<String, String> entry : getTServerAndInitDbFlags().entrySet()) {
      builder.addCommonTServerArgs("--" + entry.getKey() + "=" + entry.getValue());
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

    runInitDb();

    if (connection != null) {
      LOG.info("Closing previous connection");
      connection.close();
      connection = null;
    }
    connection = createConnection();
    pgInitialized = true;
  }

  protected void configureConnection(Connection connection) throws Exception {
    connection.setTransactionIsolation(Connection.TRANSACTION_REPEATABLE_READ);
  }

  protected Connection createConnectionNoAutoCommit() throws Exception {
    Connection conn = createConnection();
    conn.setAutoCommit(false);
    return conn;
  }

  protected Connection createConnectionWithAutoCommit() throws Exception {
    Connection conn = createConnection();
    conn.setAutoCommit(true);
    return conn;
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
    final String pgHost = getPgHost(tserverIndex);
    final int pgPort = getPgPort(tserverIndex);
    String url = String.format("jdbc:postgresql://%s:%d/%s", pgHost, pgPort, DEFAULT_PG_DATABASE);
    if (EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_PG_JDBC_TRACE_LOGGING")) {
      url += "?loggerLevel=TRACE";
    }

    final int MAX_ATTEMPTS = 10;
    int delayMs = 500;
    Connection connection = null;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
      try {
        connection = DriverManager.getConnection(url, DEFAULT_PG_USER, DEFAULT_PG_PASSWORD);
        connectionsToClose.add(connection);
        configureConnection(connection);
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

  protected Map<String, String> getInitDbEnvVars() {
    Map<String, String> initdbEnvVars = new TreeMap<>();
    assertNotNull(initdbDataDir);
    initdbEnvVars.put(PG_DATA_FLAG, initdbDataDir.toString());
    initdbEnvVars.put(MASTERS_FLAG, masterAddresses);
    initdbEnvVars.put(YB_ENABLED_IN_PG_ENV_VAR_NAME, "1");

    for (Map.Entry<String, String> entry : getTServerAndInitDbFlags().entrySet()) {
      initdbEnvVars.put("FLAGS_" + entry.getKey(), entry.getValue());
    }

    // Disable reporting signal-unsafe behavior for PostgreSQL because it does a lot of work in
    // signal handlers on shutdown.
    SanitizerUtil.addToSanitizerOptions(initdbEnvVars, "report_signal_unsafe=0");

    for (Map.Entry<String, String> entry : System.getenv().entrySet()) {
      String envVarName = entry.getKey();
      if (envVarName.startsWith("postgres_FLAGS_")) {
        String downstreamEnvVarName = envVarName.substring(9);
        LOG.info("Found env var " + envVarName + ", setting " + downstreamEnvVarName + " for " +
            "initdb to " + entry.getValue());
        initdbEnvVars.put(downstreamEnvVarName, entry.getValue());
      }
    }

    // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
    initdbEnvVars.put("YB_PG_FALLBACK_SYSTEM_USER_NAME", "postgres");

    return initdbEnvVars;
  }

  private void runInitDb() throws Exception {
    initdbDataDir = new File(getBaseTmpDir() + "/ybpgdata-" + System.currentTimeMillis());
    if (!initdbDataDir.mkdir()) {
      throw new Exception("Failed to create data dir for initdb: " + initdbDataDir);
    }

    final Map<String, String> postgresEnvVars = getInitDbEnvVars();

    // Dump environment variables we'll be using for the initdb command.
    LOG.info("Setting the following environment variables for the initdb command:\n" +
             StringUtil.getEnvVarMapDumpStr(postgresEnvVars, ": ", 4));

    LOG.info("Running initdb");
    String initCmd = String.format("%s/%s", pgBinDir, "initdb");
    ProcessBuilder procBuilder =
        new ProcessBuilder(
            initCmd, "-U", DEFAULT_PG_USER, "--encoding=UTF8").redirectErrorStream(true);
    procBuilder.environment().putAll(postgresEnvVars);
    // Change the directory to something different from the data directory so that we can collect
    // a core file there if necessary. The data directory may be deleted automatically.
    File initDbWorkDir = new File(TestUtils.getBaseTmpDir() + "/initdb_cwd");
    initDbWorkDir.mkdirs();
    procBuilder.directory(initDbWorkDir);
    Process initProc = procBuilder.start();
    String logPrefix = MiniYBDaemon.makeLogPrefix(
        "initdb",
        MiniYBDaemon.NO_DAEMON_INDEX,
        pidStrOfProcess(initProc),
        MiniYBDaemon.NO_RPC_PORT,
        MiniYBDaemon.NO_WEB_UI_URL);
    LogPrinter initDbLogPrinter = new LogPrinter(initProc.getInputStream(), logPrefix);
    initProc.waitFor();
    initDbLogPrinter.stop();
    CoreFileUtil.processCoreFile(ProcessUtil.pidOfProcess(initProc),
        initCmd, "initdb", initDbWorkDir, CoreFileUtil.CoreFileMatchMode.ANY_CORE_FILE);
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
          connection.close();
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

    LOG.info("Deleting PostgreSQL data directory at " + initdbDataDir.getPath());
    FileUtils.deleteDirectory(initdbDataDir);
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
    // Later we can determine stuck
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
      String sql =
          "CREATE TABLE " + tableName + "(h bigint, r float, vi int, vs text, PRIMARY KEY (h, r))";
      LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
      statement.execute(sql);
      LOG.info("Table creation finished: " + tableName);
    }
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
   * @param errorSubstring A substring of the expected error message.
   */
  protected void runInvalidQuery(Statement statement, String query, String errorSubstring) {
    try {
      statement.execute(query);
      fail(String.format("Statement did not fail: %s", query));
    } catch (SQLException e) {
      if (e.getMessage().contains(errorSubstring)) {
        LOG.info("Expected exception", e);
      } else {
        fail(String.format("Unexpected Error Message. Got: '%s', Expected to contain: '%s'",
                           e.getMessage(), errorSubstring));
      }
    }
  }

  protected String getSimpleTableCreationStatement(String tableName, String valueColumnName) {
    return "CREATE TABLE " + tableName + "(h int, r int, " + valueColumnName + " int, " +
        "PRIMARY KEY (h, r))";
  }

  protected void createSimpleTable(String tableName, String valueColumnName) throws SQLException {
    Statement statement = connection.createStatement();
    String sql = getSimpleTableCreationStatement(tableName, valueColumnName);
    LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
    statement.execute(sql);
    LOG.info("Table creation finished: " + tableName);
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

}
