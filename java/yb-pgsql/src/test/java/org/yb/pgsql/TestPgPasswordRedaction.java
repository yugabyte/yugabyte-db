// Copyright (c) YugabyteDB, Inc.
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

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map.Entry;
import java.util.stream.Collectors;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBDaemon;

import com.google.common.net.HostAndPort;

@RunWith(value = YBTestRunner.class)
public class TestPgPasswordRedaction extends BasePgSQLTest {
  private static final String MARKER = "PWLEAK";
  private static final String ROLE = "redact_role";

  private final List<String> logLines = Collections.synchronizedList(new ArrayList<>());

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Before
  public void setUp() throws Exception {
    for (Entry<HostAndPort, MiniYBDaemon> entry : miniCluster.getTabletServers().entrySet()) {
      entry.getValue().getLogPrinter().addErrorListener(new LogErrorListener() {
        @Override
        public void handleLine(String line) {
          logLines.add(line);
        }

        @Override
        public void reportErrorsAtEnd() {}
      });
    }
  }

  private static void expectError(Statement stmt, String query) {
    try {
      stmt.execute(query);
      fail("Expected an error from: " + query);
    } catch (SQLException e) {
      // Expected.
    }
  }

  private void waitForLogLine(String needle) throws Exception {
    for (int i = 0; i < 300; i++) {
      synchronized (logLines) {
        if (logLines.stream().anyMatch(l -> l.contains(needle))) {
          return;
        }
      }
      Thread.sleep(100);
    }
    fail("Log line not seen: " + needle);
  }

  private void assertLogged(String... needles) {
    synchronized (logLines) {
      assertTrue("No log line contains all of: " + String.join(" | ", needles),
                 logLines.stream().anyMatch(l -> {
                   for (String n : needles) {
                     if (!l.contains(n)) {
                       return false;
                     }
                   }
                   return true;
                 }));
    }
  }

  // Each statement carries a password marker. The marker must not reach the postgres log or
  // pg_stat_activity through log_statement, log_min_duration_statement, STATEMENT/QUERY/CONTEXT
  // lines of failed statements, or conn mgr's post-DDL LOG lines.
  @Test
  public void testPasswordsRedactedInLogs() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE ROLE " + ROLE + " LOGIN");
      stmt.execute("CREATE FOREIGN DATA WRAPPER redact_fdw");
      stmt.execute("CREATE SERVER redact_srv FOREIGN DATA WRAPPER redact_fdw");
      stmt.execute("CREATE TABLE redact_par (a int)");
      stmt.execute("INSERT INTO redact_par VALUES (0)");
    }

    try (Connection conn = getConnectionBuilder().withPreferQueryMode("simple").connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET log_statement = 'all'");

      stmt.execute("ALTER ROLE " + ROLE + " PASSWORD '" + MARKER + "_single'");
      stmt.execute("SELECT 1; ALTER ROLE " + ROLE + " PASSWORD '" + MARKER + "_middle'; SELECT 2");
      stmt.execute("BEGIN; ALTER ROLE " + ROLE + " PASSWORD '" + MARKER + "_txn'; COMMIT");
      stmt.execute("DO $$ BEGIN EXECUTE 'ALTER ROLE " + ROLE + " PASSWORD ''" + MARKER +
                   "_do'''; END $$");
      stmt.execute("CREATE USER MAPPING FOR CURRENT_USER SERVER redact_srv OPTIONS (user 'u', " +
                   "password '" + MARKER + "_um')");
      stmt.execute("ALTER USER MAPPING FOR CURRENT_USER SERVER redact_srv OPTIONS (SET " +
                   "password '" + MARKER + "_um_alter')");

      expectError(stmt, "CREATE ROLE " + ROLE + " PASSWORD '" + MARKER + "_exists'");
      expectError(stmt, "ALTER ROLE redact_missing PASSWORD '" + MARKER + "_missing'");
      expectError(stmt, "SELEC 1; ALTER ROLE " + ROLE + " PASSWORD '" + MARKER + "_syntax'");
      expectError(stmt, "SELECT 1; ALTER ROLE redact_missing PASSWORD '" + MARKER +
                        "_missing_multi'");
      expectError(stmt, "DO $$ BEGIN EXECUTE 'ALTER ROLE redact_missing PASSWORD ''" + MARKER +
                        "_do_missing'''; END $$");
      expectError(stmt, "DO $$ BEGIN EXECUTE 'ALTER ROLE " + ROLE + " PASSWORD ''" + MARKER +
                        "_do_syntax'' VALID UNTILL ''infinity'''; END $$");

      // The division by zero fails inside a parallel worker, which logs its own STATEMENT line.
      stmt.execute("SET force_parallel_mode = on");
      stmt.execute("SET parallel_setup_cost = 0");
      stmt.execute("SET parallel_tuple_cost = 0");
      expectError(stmt, "SELECT 1 / a FROM redact_par; ALTER ROLE " + ROLE + " PASSWORD '" +
                        MARKER + "_parallel'");
      stmt.execute("RESET force_parallel_mode");
    }

    try (Connection conn = getConnectionBuilder().withPreferQueryMode("extended").connect()) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("SET log_statement = 'all'");
        stmt.execute("SET log_min_duration_statement = 0");
      }
      try (PreparedStatement ps = conn.prepareStatement(
               "ALTER ROLE " + ROLE + " PASSWORD '" + MARKER + "_extended'")) {
        ps.execute();
      }
      try (PreparedStatement ps = conn.prepareStatement(
               "ALTER ROLE " + ROLE + " PASSWORD '" + MARKER + "_extended_syntax'" +
               " VALID UNTILL 'infinity'")) {
        ps.execute();
        fail("Expected a syntax error");
      } catch (SQLException e) {
        // Expected.
      }
      try (Statement stmt = connection.createStatement();
           ResultSet rs = stmt.executeQuery(
               "SELECT query FROM pg_stat_activity WHERE query LIKE '%' || 'PW' || 'LEAK' || '%'" +
               " AND pid <> pg_backend_pid()")) {
        List<String> leaked = new ArrayList<>();
        while (rs.next()) {
          leaked.add(rs.getString(1));
        }
        assertTrue("pg_stat_activity shows a password: " + leaked, leaked.isEmpty());
      }
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET log_statement = 'all'");
      stmt.execute("SELECT 'redaction_sentinel'");
    }
    waitForLogLine("redaction_sentinel");

    assertLogged("SELECT 1; ALTER ROLE " + ROLE + " PASSWORD <REDACTED>");
    assertLogged("execute ", ": ALTER ROLE " + ROLE + " PASSWORD <REDACTED>");
    assertLogged("password <REDACTED>");
    assertLogged("STATEMENT:  SELECT 1; ALTER ROLE redact_missing PASSWORD <REDACTED>");
    assertLogged("CONTEXT:  SQL statement \"ALTER ROLE redact_missing PASSWORD <REDACTED>\"");
    assertLogged("QUERY:  ALTER ROLE " + ROLE + " PASSWORD <REDACTED>");
    assertLogged("STATEMENT:  ALTER ROLE " + ROLE + " PASSWORD <REDACTED>");
    assertLogged("parallel worker");

    List<String> leaked;
    synchronized (logLines) {
      leaked = logLines.stream().filter(l -> l.contains(MARKER)).collect(Collectors.toList());
    }
    assertTrue("Log lines with a password: " + leaked, leaked.isEmpty());
  }
}
