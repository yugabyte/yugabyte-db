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
import org.junit.Before;

import java.io.File;
import java.sql.*;
import java.util.*;
import java.util.concurrent.ConcurrentSkipListSet;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicReference;

import org.postgresql.core.TransactionState;
import org.postgresql.jdbc.PgConnection;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.LogPrinter;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.EnvAndSysPropertyUtil;

import static org.yb.AssertionWrappers.fail;
import static org.yb.client.TestUtils.findFreePort;
import static org.yb.client.TestUtils.getBaseTmpDir;
import static org.yb.client.TestUtils.getBinDir;
import static org.yb.client.TestUtils.pidStrOfProcess;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

public class BasePgSQLTest extends BaseMiniClusterTest {
  private static final Logger LOG = LoggerFactory.getLogger(BasePgSQLTest.class);

  // Postgres settings.
  protected static final String DEFAULT_DATABASE = "postgres";
  protected static final String DEFAULT_USER = "postgres";
  protected static final String DEFAULT_PASSWORD = "";

  // Postgres flags.
  private static final String MASTERS_FLAG = "FLAGS_pggate_master_addresses";
  private static final String PG_DATA_FLAG = "PGDATA";
  private static final String YB_ENABLED_FLAG = "YB_ENABLED_IN_POSTGRES";

  protected Connection connection;
  private Process postgresProc;
  private LogPrinter logPrinter;

  protected File pgDataDir;

  private String postgresExecutable;

  private List<Connection> connectionsToClose = new ArrayList<>();
  private String pgHost = "127.0.0.1";
  private int pgPort;

  protected static final int DEFAULT_STATEMENT_TIMEOUT_MS = 30000;

  protected ConcurrentSkipListSet<Integer> stuckBackendPidsConcMap = new ConcurrentSkipListSet<>();

  /**
   * This is used during shutdown to prevent trying to kill the same backend multiple times, so not
   * using a concurrent data structure.
   */
  protected Set<Integer> killedStuckBackendPids = new HashSet<>();

  /**
   * This allows us to run the same test against the vanilla PostgreSQL code (still compiled as
   * part of the YB codebase).
   */
  private final boolean useVanillaPostgres =
      EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_USE_VANILLA_POSTGRES_IN_TEST");

  @Override
  protected boolean miniClusterEnabled() {
    return !useVanillaPostgres;
  }

  @Override
  protected int overridableNumShardsPerTServer() {
    return 1;
  }

  protected void overridableCustomizePostgresEnvVars(Map<String, String> envVars) { }

  //------------------------------------------------------------------------------------------------
  // Postgres process integration.

  // TODO Postgres may eventually be integrated into the tserver as a child process.
  // For now doing this here so we can already write tests.
  @Before
  public void initPostgresBefore() throws Exception {
    pgPort = findFreePort(pgHost);
    LOG.info("initPostgresBefore: will start PostgreSQL server on host " + pgHost +
        ", port " + pgPort);
    startPgWrapper(pgHost, pgPort);

    // Register PostgreSQL JDBC driver.
    Class.forName("org.postgresql.Driver");

    if (connection != null) {
      LOG.info("Closing previous connection");
      connection.close();
      connection = null;
    }
    connection = createConnection();
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

  protected Connection createConnection() throws Exception {
    String url = String.format("jdbc:postgresql://%s:%d/%s", pgHost, pgPort, DEFAULT_DATABASE);
    if (EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_PG_JDBC_TRACE_LOGGING")) {
      url += "?loggerLevel=TRACE";
    }

    int delayMs = 1000;
    Connection connection = null;
    for (int attemptsLeft = 10; attemptsLeft >= 1; --attemptsLeft) {
      try {
        connection = DriverManager.getConnection(url, DEFAULT_USER, DEFAULT_PASSWORD);
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

        if (attemptsLeft > 1 &&
            sqlEx.getMessage().contains("FATAL: the database system is starting up") ||
            sqlEx.getMessage().contains("refused. Check that the hostname and port are correct " +
                "and that the postmaster is accepting")) {
          LOG.info("Postgres is still starting up, waiting for " + delayMs + " ms. " +
              "Got message: " + sqlEx.getMessage());
          Thread.sleep(delayMs);
          delayMs += 1000;
          continue;
        }
        LOG.error("Exception while trying to create connection: " + sqlEx.getMessage());
        throw sqlEx;
      }
    }
    throw new IllegalStateException("Should not be able to reach here");
  }

  private void startPgWrapper(String host, int port) throws Exception {
    String pgdataDirPath = getBaseTmpDir() + "/ybpgdata-" + System.currentTimeMillis();
    pgDataDir = new File(pgdataDirPath);
    if (!pgDataDir.mkdir()) {
      throw new Exception("Failed to create postgres data dir " + pgdataDirPath);
    }

    Map<String, String> postgresEnvVars = new HashMap<>();
    postgresEnvVars.put(PG_DATA_FLAG, pgdataDirPath);
    postgresEnvVars.put(MASTERS_FLAG, masterAddresses);
    if (useVanillaPostgres) {
      LOG.info("NOT using YugaByte-enabled PostgreSQL");
    } else {
      postgresEnvVars.put(YB_ENABLED_FLAG, "1");
    }
    postgresEnvVars.put("FLAGS_yb_num_shards_per_tserver",
        String.valueOf(overridableNumShardsPerTServer()));

    // Disable reporting signal-unsafe behavior for PostgreSQL because it does a lot of work in
    // signal handlers on shutdown.
    postgresEnvVars.put("TSAN_OPTIONS",
        System.getenv().getOrDefault("TSAN_OPTIONS", "") + " report_signal_unsafe=0");
    for (Map.Entry<String, String> entry : System.getenv().entrySet()) {
      String envVarName = entry.getKey();
      if (envVarName.startsWith("postgres_FLAGS_")) {
        String downstreamEnvVarName = envVarName.substring(9);
        LOG.info("Found env var " + envVarName + ", setting " + downstreamEnvVarName + " for " +
            "PostgreSQL to " + entry.getValue());
        postgresEnvVars.put(downstreamEnvVarName, entry.getValue());
      }
    }

    // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
    postgresEnvVars.put("YB_PG_FALLBACK_SYSTEM_USER_NAME", "postgres");

    // Allow test subclasses to add/remove env vars to test specific features.
    overridableCustomizePostgresEnvVars(postgresEnvVars);

    {
      List<String> postgresEnvVarsDump = new ArrayList<>();
      for (Map.Entry<String, String> entry : postgresEnvVars.entrySet()) {
        postgresEnvVarsDump.add(entry.getKey() + ": " + entry.getValue());
      }
      Collections.sort(postgresEnvVarsDump);
      LOG.info(
          "Setting the following environment variables for the PostgreSQL process:\n    " +
              String.join("\n    ", postgresEnvVarsDump));
    }

    String portStr = String.valueOf(port);

    // Postgres bin directory.
    String pgBinDir = getBinDir() + "/../postgres/bin";

    // Run initdb to initialize the postgres data folder.

    runInitDb(postgresEnvVars, pgBinDir);

    // Start the postgres server process.
    startPostgresProcess(host, port, postgresEnvVars, portStr, pgBinDir);
  }

  private void startPostgresProcess(String host, int port, Map<String, String> envVars,
                                    String portStr, String pgBinDir) throws Exception {
    LOG.info("Postgres: Starting postgres process on port " + portStr);
    postgresExecutable = String.format("%s/%s", pgBinDir, "postgres");

    {
      ProcessBuilder procBuilder =
          new ProcessBuilder(postgresExecutable, "-p", portStr, "-h", host);
      procBuilder.environment().putAll(envVars);
      procBuilder.directory(pgDataDir);
      procBuilder.redirectErrorStream(true);
      postgresProc = procBuilder.start();
    }

    // Set up PostgreSQL logging.
    String logPrefix = MiniYBDaemon.makeLogPrefix(
        "pg",
        MiniYBDaemon.NO_DAEMON_INDEX,
        pidStrOfProcess(postgresProc),
        port,
        MiniYBDaemon.NO_WEB_UI_URL);
    this.logPrinter = new LogPrinter(postgresProc.getInputStream(), logPrefix);

    // Check that the process didn't die immediately.
    Thread.sleep(1500);
    try {
      int ev = postgresProc.exitValue();
      MiniYBCluster.processCoreFile(TestUtils.pidOfProcess(postgresProc), postgresExecutable,
          "postgres", pgDataDir, /* tryWithoutPid */ true);
      throw new Exception("We tried starting a postgres process but it exited with " +
                          "value=" + ev);
    } catch (IllegalThreadStateException ex) {
      // This means the process is still alive, which is what we expect.
    }

    LOG.info("Started postgres as pid " + TestUtils.pidOfProcess(postgresProc));
  }

  private void runInitDb(Map<String, String> envVars, String pgBinDir) throws Exception {
    LOG.info("Postgres: Running initdb");
    String initCmd = String.format("%s/%s", pgBinDir, "initdb");
    ProcessBuilder procBuilder =
        new ProcessBuilder(initCmd, "-U", DEFAULT_USER).redirectErrorStream(true);
    procBuilder.environment().putAll(envVars);
    // Make the current directory different from the data directory so that we can collect a core
    // file.
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
    MiniYBCluster.processCoreFile(TestUtils.pidOfProcess(initProc),
        initCmd, "initdb", initDbWorkDir, /* tryWithoutPid */ true);
  }

  @After
  public void tearDownAfter() throws Exception {
    LOG.info(getClass().getSimpleName() + ".tearDownAfter is running");
    try {
      tearDownPostgreSQL();
    } finally {
      if (miniClusterEnabled()) {
        LOG.info("Destroying mini-cluster");
        // We are destroying and re-creating the miniCluster between every test.
        // TODO Should just drop all tables/schemas/databases like for CQL.
        if (miniCluster != null) {
          destroyMiniCluster();
          miniCluster = null;
        }
      }
    }
  }

  private void processPgCoreFile(int pid) throws Exception {
    if (postgresExecutable != null) {
      MiniYBCluster.processCoreFile(pid, postgresExecutable, "postgres", pgDataDir,
          true /* tryWithoutPid */);
    } else {
      LOG.error("PostgreSQL executable path not known, cannot look for core files from pid " + pid);
    }
  }

  private void killAndCoreDumpBackends(Collection<Integer> stuckBackendPids) throws Exception {
    if (stuckBackendPids.isEmpty())
      return;
    stuckBackendPids.addAll(stuckBackendPidsConcMap);

    LOG.warn(String.format(
        "Found %d 'stuck' backends: %s", stuckBackendPids.size(), stuckBackendPids));
    for (int stuckBackendPid : stuckBackendPids) {
      if (killedStuckBackendPids.contains(stuckBackendPid)) {
        continue;
      }

      LOG.warn("Killing stuck backend with PID " + stuckBackendPid + " with a SIGSEGV");
      Runtime.getRuntime().exec("kill -SIGSEGV " + stuckBackendPid);
      killedStuckBackendPids.add(stuckBackendPid);
    }

    LOG.warn("waiting a bit for core dumps to finish");
    Thread.sleep(5000);
    for (int stuckBackendPid : stuckBackendPids) {
      processPgCoreFile(stuckBackendPid);
    }
  }

  private void tearDownPostgreSQL() throws Exception {
    Set<Integer> allBackendPids = new TreeSet<>();
    killAndCoreDumpBackends(new TreeSet<>(stuckBackendPidsConcMap));
    boolean pgFailedToTerminate = false;

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
            if (backendPid > 0) {
              allBackendPids.add(backendPid);
            }
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

    // Stop postgres server.
    LOG.info("Stopping postgres server.");
    if (postgresProc != null) {
      int postgresPid = TestUtils.pidOfProcess(postgresProc);
      // See https://www.postgresql.org/docs/current/static/server-shutdown.html for different
      // server shutdown modes of PostgreSQL.
      // SIGTERM = "Smart Shutdown"
      // SIGINT = "Fast Shutdown"
      // SIGQUIT = "Immediate Shutdown"
      Runtime.getRuntime().exec("kill -SIGTERM " + postgresPid);
      if (!postgresProc.waitFor(30, TimeUnit.SECONDS)) {
        LOG.info("Timed out while waiting for the PostgreSQL process to finish. " +
                 "Killing and core dumping all backends.");
        killAndCoreDumpBackends(allBackendPids);
        LOG.info("Killing the main PostgreSQL process with SIGKILL");
        Runtime.getRuntime().exec("kill -SIGKILL" + postgresPid);
        pgFailedToTerminate = true;
      }
      processPgCoreFile(postgresPid);
    }

    if (logPrinter != null) {
      logPrinter.stop();
    }

    LOG.info("Finished stopping postgres server.");
    LOG.info("Deleting PostgreSQL data directory at " + pgDataDir.getPath());
    FileUtils.deleteDirectory(pgDataDir);

    if (pgFailedToTerminate) {
      throw new AssertionError("PostgreSQL process failed to terminate normally");
    }
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

  protected class Row {
    List<Object> elems = new ArrayList<>();

    Row(Object... args) {
      Collections.addAll(elems, args);
    }

    Object get(int index) {
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
      return elems.equals(other.elems);
    }

    @Override
    public int hashCode() {
      return Objects.hash(elems);
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
      Object[] elems = new Object[rs.getMetaData().getColumnCount()];
      for (int i = 0; i < elems.length; i++) {
        elems[i] = rs.getObject(i + 1); // Column index starts from 1.
      }
      rows.add(new Row(elems));
    }
    return rows;
  }

  protected void assertNextRow(ResultSet rs, Object... values) throws SQLException {
    assertTrue(rs.next());
    for (int i = 0; i < values.length; i++) {
      assertEquals(values[i], rs.getObject(i + 1)); // Column index starts from 1.
    }
  }

  protected void assertOneRow(String stmt, Object... values)throws SQLException {
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery(stmt)) {
        assertNextRow(rs, values);
        assertFalse(rs.next());
      }
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

  protected void runInvalidQuery(Statement statement, String stmt) {
    try {
      statement.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
    } catch (SQLException e) {
      LOG.info("Expected exception", e);
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

}
