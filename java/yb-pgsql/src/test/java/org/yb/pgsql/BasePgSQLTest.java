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
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.LogPrinter;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;

import static org.yb.AssertionWrappers.fail;
import static org.yb.client.TestUtils.findFreePort;
import static org.yb.client.TestUtils.getBaseTmpDir;
import static org.yb.client.TestUtils.getBinDir;
import static org.yb.client.TestUtils.pidStrOfProcess;

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

  @Override
  protected int overridableNumShardsPerTServer() {
    return 1;
  }

  //------------------------------------------------------------------------------------------------
  // Postgres process integration.

  // TODO Postgres may eventually be integrated into the tserver as a child process.
  // For now doing this here so we can already write tests.
  @Before
  public void initPostgresBefore() throws Exception {
    String pgHost = "127.0.0.1";
    int port = findFreePort(pgHost);
    LOG.info("initPostgresBefore: will start PostgreSQL server on host " + pgHost +
        ", port " + port);
    startPgWrapper(pgHost, port);

    // Register PostgreSQL JDBC driver.
    Class.forName("org.postgresql.Driver");
    String url = String.format("jdbc:postgresql://%s:%d/%s", pgHost, port, DEFAULT_DATABASE);

    int delayMs = 1000;
    for (int attemptsLeft = 10; attemptsLeft >= 1; --attemptsLeft) {
      try {
        if (connection != null) {
          LOG.info("Closing previous connection");
          connection.close();
          connection = null;
        }
        connection = DriverManager.getConnection(url, DEFAULT_USER, DEFAULT_PASSWORD);
        // Break when a connection has been successfully established.
        // NOTE: if we forget to break here, we will create a lot of connections that won't get
        // closed, and that will prevent PostgreSQL's "smart shutdown" from completing.
        break;
      } catch (SQLException e) {
        if (attemptsLeft > 1 &&
            e.getMessage().contains("FATAL: the database system is starting up") ||
            e.getMessage().contains("refused. Check that the hostname and port are correct and " +
                                    "that the postmaster is accepting")) {
          LOG.info("Postgres is still starting up, waiting for " + delayMs + " ms. " +
              "Got message: " + e.getMessage());
          Thread.sleep(delayMs);
          delayMs += 1000;
          continue;
        }
        LOG.error("Exception while trying to create connection: " + e.getMessage());
        throw e;
      }
    }
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
    postgresEnvVars.put(YB_ENABLED_FLAG, "1");
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
    try {
      if (connection != null) {
        try (Statement statement = connection.createStatement()) {
          try (ResultSet resultSet = statement.executeQuery(
              "SELECT client_hostname, client_port, state, query FROM pg_stat_activity")) {
            while (resultSet.next()) {
              LOG.info("Found connection: " +
                  "hostname=" + resultSet.getString(1) + ", " +
                  "port=" + resultSet.getInt(2) + ", " +
                  "state=" + resultSet.getString(3) + ", " +
                  "query=" + resultSet.getString(4));
            }
          }
        }
        catch (SQLException e) {
          LOG.info("Exception when trying to list PostgreSQL connections", e);
        }

        LOG.info("Closing connection.");
        try {
          connection.close();
          connection = null;
        } catch (SQLException ex) {
          LOG.error("Exception while trying to close connection");
          throw ex;
        }
      } else {
        LOG.info("Connection is already null, nothing to close");
      }
      LOG.info("Finished closing connection.");

      // Stop postgres server.
      LOG.info("Stopping postgres server.");
      int postgresPid = TestUtils.pidOfProcess(postgresProc);
      if (postgresProc != null) {
        // See https://www.postgresql.org/docs/current/static/server-shutdown.html for different
        // server shutdown modes of PostgreSQL.
        // SIGTERM = "Smart Shutdown"
        // SIGINT = "Fast Shutdown"
        // SIGQUIT = "Immediate Shutdown"
        Runtime.getRuntime().exec("kill -SIGTERM " + postgresPid);
        postgresProc.waitFor();
      }
      if (postgresExecutable != null) {
        MiniYBCluster.processCoreFile(postgresPid, postgresExecutable, "postgres", pgDataDir,
            /* tryWithoutPid */ true);
      }

      if (logPrinter != null) {
        logPrinter.stop();
      }

      LOG.info("Finished stopping postgres server.");
      LOG.info("Deleting PostgreSQL data directory at " + pgDataDir.getPath());
      FileUtils.deleteDirectory(pgDataDir);
    } finally {
      LOG.info("Destroying mini-cluster");
      // We are destroying and re-creating the miniCluster between every test.
      // TODO Should just drop all tables/schemas/databases like for CQL.
      if (miniCluster != null) {
        destroyMiniCluster();
        miniCluster = null;
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
        sb.append(elems.get(i).toString());
      }
      sb.append(']');
      return sb.toString();
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
}
