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

import org.junit.After;
import org.junit.Before;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.sql.DriverManager;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.LogPrinter;

import static org.junit.Assert.fail;
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
  private LogPrinter stdoutPrinter;
  private LogPrinter stderrPrinter;

  protected void runInvalidQuery(Statement statement, String stmt) {
    try {
      statement.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
    } catch (SQLException e) {
      LOG.info("Expected exception", e);
    }
  }

  // TODO Postgres may eventually be integrated into the tserver as a child process.
  // For now doing this here so we can already write tests.
  @Before
  public void initPostgresBefore() throws Exception {
    String pgHost = "127.0.0.1";
    int port = findFreePort(pgHost);
    startPgWrapper(pgHost, port);

    // Register PostgreSQL JDBC driver.
    Class.forName("org.postgresql.Driver");
    String url = String.format("jdbc:postgresql://%s:%d/%s", pgHost, port, DEFAULT_DATABASE);

    int delayMs = 1000;
    for (int attemptsLeft = 10; attemptsLeft >= 1; --attemptsLeft) {
      try {
        connection = DriverManager.getConnection(url, DEFAULT_USER, DEFAULT_PASSWORD);
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
    File dir = new File(pgdataDirPath);
    if (!dir.mkdir()) {
      throw new Exception("Failed to create postgres data dir " + pgdataDirPath);
    }

    Map<String, String> flags = new HashMap<>();
    flags.put(PG_DATA_FLAG, pgdataDirPath);
    flags.put(MASTERS_FLAG, masterAddresses);
    flags.put(YB_ENABLED_FLAG, "1");
    // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
    flags.put("YB_PG_FALLBACK_SYSTEM_USER_NAME", "postgres");

    String portStr = String.valueOf(port);
    String logPrefixTemplate = "pg1|%s|pid:%s ";

    // Postgres bin directory.
    String pgBinDir = getBinDir() + "/../postgres/bin";

    //----------------------------------------------------------------------------------------------
    // Run initdb to initialize the postgres data folder.

    LOG.info("Postgres: Running initdb");
    String initCmd = String.format("%s/%s", pgBinDir, "initdb");
    ProcessBuilder pb = new ProcessBuilder(initCmd, "-U", DEFAULT_USER).redirectErrorStream(true);
    pb.environment().putAll(flags);
    Process initProc = pb.start();
    String line;
    BufferedReader in = new BufferedReader(new InputStreamReader(initProc.getInputStream()));
    String logPrefix = String.format(logPrefixTemplate, "initdb", pidStrOfProcess(initProc));
    while ((line = in.readLine()) != null) {
      LOG.info(logPrefix + line);
    }
    in.close();
    initProc.waitFor();

    //----------------------------------------------------------------------------------------------
    // Start the postgres server process.
    LOG.info("Postgres: Starting postgres process on port " + portStr);
    String startCmd = String.format("%s/%s", pgBinDir, "postgres");
    pb = new ProcessBuilder(startCmd, "-p", portStr, "-h", host);
    pb.environment().putAll(flags);
    postgresProc = pb.start();

    // Set up postgres logging.
    logPrefix = String.format(logPrefixTemplate, "postgres", pidStrOfProcess(postgresProc));
    this.stdoutPrinter = new LogPrinter("stdout", postgresProc.getInputStream(), logPrefix);
    this.stderrPrinter = new LogPrinter("stderr", postgresProc.getErrorStream(), logPrefix);

    // Check that the process didn't die immediately.
    Thread.sleep(1500);
    try {
      int ev = postgresProc.exitValue();
      throw new Exception("We tried starting a postgres process but it exited with " +
                          "value=" + ev);
    } catch (IllegalThreadStateException ex) {
      // This means the process is still alive, which is what we expect.
    }

    LOG.info("Started postgres as pid " + TestUtils.pidOfProcess(postgresProc));
  }

  @After
  public void tearDownAfter() throws Exception {
    if (connection != null) {
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
    if (postgresProc != null) {
      Runtime.getRuntime().exec("kill -SIGINT " + TestUtils.pidOfProcess(postgresProc));
      postgresProc.waitFor();
    }
    if (stderrPrinter != null) {
      stderrPrinter.stop();
    }
    if (stdoutPrinter != null) {
      stdoutPrinter.stop();
    }

    LOG.info("Finished stopping postgres server.");

    // We are destroying and re-creating the miniCluster between every test.
    // TODO Should just drop all tables/schemas/databases like for CQL.
    if (miniCluster != null) {
      destroyMiniCluster();
    }
  }
}
