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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map.Entry;
import java.util.Properties;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.BuildTypeUtil;

import com.google.common.net.HostAndPort;

/**
 * Tests the YBC_LOG_INFO line emitted
 * from BackendInitialize() in src/postgres/src/backend/postmaster/postmaster.c.
 * eg. "Started client backend with pid: 11236, database_name: colocated,
 *     application_name: ysqlsh, remote_ps_data: 127.0.0.1(60220)"
 */

@RunWith(value=YBTestRunner.class)
public class TestBackendStartLog extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestBackendStartLog.class);

  // Matches the payload portion of the BackendInitialize() log line in postmaster.c.
  private static final Pattern START_LOG_PATTERN = Pattern.compile(
      "Started (?<started>[^,]+?) with pid: (?<pid>\\d+), "
    + "database_name: (?<db>[^,]+?), "
    + "application_name: (?<app>.*?), "
    + "remote_ps_data: (?<remote>.*)$");

  private final CurrentBackendStartLogs backendStartLogs = new CurrentBackendStartLogs();

  // Single node for backend-start log test
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
      int port = entry.getKey().getPort();
      MiniYBDaemon tserver = entry.getValue();
      // ErrorListener is a misnomer, we parse postgres backend-start logs here.
      tserver.getLogPrinter().addErrorListener(new BackendStartLogListener(backendStartLogs));
    }
  }

  /**
   * Find the captured log entry whose pid field matches the given backend pid.
   * Returns null if no match was found.
   */
  private static Matcher findEntryForPid(List<Matcher> entries, int pid) {
    for (Matcher m : entries) { // potential matches
      if (Integer.parseInt(m.group("pid")) == pid) {
        return m; // matched line with logged pid
      }
    }
    return null;
  }

  /**
   * Look up the connection's actual pid
   * return the matching captured log line with logged pid.
   * Logged pid should match the actual pid
   */
  private Matcher entryForConnection(Connection conn, List<Matcher> entries) {
    int pid = getPgBackendPid(conn); // actual pid
    Matcher m = findEntryForPid(entries, pid); // matched line with logged pid
    assertNotNull("Expected backend-start log for pid " + pid +
                  ", saw: " + describe(entries), m);
    LOG.info("Matched backend-start log for pid " + pid + ": " + m.group(0));
    return m;
  }

  /**
   * Assert invariants:
   * - Logged pid matches the actual pid.
   * - <started> string is "client backend".
   */
  private void assertClientBackend(Connection conn, Matcher m) {
    assertEquals(getPgBackendPid(conn), Integer.parseInt(m.group("pid")));
    assertEquals("client backend", m.group("started"));
  }

  /**
   * Test custom database and application name
   */
  @Test
  @BypassConnMgr(reason = BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED)
  public void testCustomDatabaseAndApplicationName() throws Exception {

    final String dbName = "test_backend_start_log_combo_db";
    final String appName = "combo_app";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE DATABASE " + dbName);
    }
    backendStartLogs.discard();

    try (Connection conn =
             getConnectionBuilder().withDatabase(dbName).withApplicationName(appName).connect()) {
      Matcher m = entryForConnection(conn, backendStartLogs.popAll());

      assertClientBackend(conn, m);
      assertEquals(dbName, m.group("db"));
      assertEquals(appName, m.group("app"));
    }
  }

  /**
   * Stringify the captured entries' raw matched text for failure diagnostics.
   */
  private static String describe(List<Matcher> entries) {
    List<String> lines = new ArrayList<>(entries.size());
    for (Matcher m : entries) {
      lines.add(m.group(0));
    }
    return lines.toString();
  }

  /**
   * Storage for the parsed backend-start log entries.
   *
   * Refer to TestPgAutoExplain's CurrentAutoExplainResults for the pattern:
   * a thread-safe buffer with discard()/popAll() so each test only observes
   * the lines produced by the connection it opens.
   * popAll() sleeps briefly to let the log line propagate
   * from the tserver process through the LogPrinter reader thread.
   */
  private static class CurrentBackendStartLogs {
    private final List<Matcher> storage = Collections.synchronizedList(new ArrayList<>());

    public void push(Matcher entry) {
      storage.add(entry);
    }

    /** Discard existing entries so that only newer ones will be retrieved. */
    public void discard() throws Exception {
      popAll();
    }

    /** Retrieve the entries added since the last call, discarding them. */
    public List<Matcher> popAll() throws Exception {
      Thread.sleep(BuildTypeUtil.adjustTimeout(200));
      synchronized (storage) {
        List<Matcher> result = new ArrayList<>(storage);
        storage.clear();
        return result;
      }
    }
  }

  /**
   * Listen for backend-start logs
   * LogErrorListener is a misnomer
   */
  private static class BackendStartLogListener implements LogErrorListener {
    private final CurrentBackendStartLogs output;

    BackendStartLogListener(CurrentBackendStartLogs output) {
      this.output = output;
    }

    @Override
    public void handleLine(String line) {
      // Only consider lines whose glog file stamp is postmaster.c.
      int pmIdx = line.indexOf("postmaster.c:");
      if (pmIdx < 0) {
        return;
      }
      int closeBracket = line.indexOf(']', pmIdx);
      if (closeBracket < 0) {
        return;
      }
      // Skip past the "] " that closes the glog prefix.
      int payloadStart = closeBracket + 1;
      while (payloadStart < line.length() && line.charAt(payloadStart) == ' ') {
        payloadStart++;
      }
      int idx = line.indexOf("Started ", payloadStart);
      if (idx < 0 || !line.contains("backend with pid:")) {
        return;
      }
      Matcher m = START_LOG_PATTERN.matcher(line.substring(idx));
      if (m.find()) {
        output.push(m);
      }
    }

    @Override
    public void reportErrorsAtEnd() {
      // NOOP
    }
  }
}
