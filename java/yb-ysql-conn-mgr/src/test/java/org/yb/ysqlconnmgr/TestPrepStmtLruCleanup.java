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

import static org.junit.Assume.assumeFalse;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import com.google.common.net.HostAndPort;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.BuildTypeUtil;

// Tests for the LRU eviction of prepared statements on Connection Manager
// backends. Without eviction, prepared statements accumulate indefinitely
// on long-running backends, causing unbounded memory growth. The
// ysql_conn_mgr_max_prepared_statements flag caps the number of statements
// kept per backend; excess LRU statements are closed via ForceClose on detach.
@RunWith(value = YBParameterizedTestRunnerYsqlConnMgr.class)
public class TestPrepStmtLruCleanup extends BaseYsqlConnMgr {

  private final boolean optimizedMode;

  public TestPrepStmtLruCleanup(boolean optimizedMode) {
    this.optimizedMode = optimizedMode;
  }

  @Parameterized.Parameters
  public static List<Boolean> optimizedPreparedStatementModes() {
    return Arrays.asList(true, false);
  }
  private static final int MAX_PREPARED_STATEMENTS = 10;
  private static final int NUM_STATEMENTS_TO_CREATE = 5000;
  private static final long MAX_BACKEND_RSS_INCREASE_KB = 10 * 1024; // 10 MB
  private static final long MAX_ODYSSEY_RSS_INCREASE_KB = 10 * 1024; // 10 MB

  @Test
  public void testPreparedStatementMemoryBounded() throws Exception {
    assumeFalse("RSS-based memory assertions are unreliable under ASAN builds",
        BuildTypeUtil.isASAN());
    LOG.info("Running with optimizedMode={}", optimizedMode);
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_conn_mgr_max_prepared_statements",
        String.valueOf(MAX_PREPARED_STATEMENTS));
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_optimized_extended_query_protocol",
        Boolean.toString(optimizedMode));
    tserverFlags.put("ysql_conn_mgr_enable_prep_stmt_close",
        Boolean.toString(optimizedMode));
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    Properties props = new Properties();
    // Default prepareThreshold is 5: the driver uses unnamed statements for the
    // first N-1 executions and only sends a named Parse on the Nth. Since we
    // execute each statement only once, we need threshold=1 so the first
    // execute() creates a named server-side prepared statement (and a cached
    // plan that persists on the backend).
    props.setProperty("prepareThreshold", "1");
    // Disable the driver's client-side prepared statement cache (default 256).
    // Without this, pstmt.close() may silently keep the name cached instead of
    // sending a Close packet through the connection manager.
    props.setProperty("preparedStatementCacheQueries", "0");

    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("yugabyte")
            .withPassword("yugabyte")
            .connect(props)) {

      Statement stmt = conn.createStatement();
      stmt.execute("CREATE TABLE IF NOT EXISTS t_lru_test (id int, data text)");

      // --- Baseline measurement: pin to one backend and record RSS ---
      stmt.execute("BEGIN");
      ResultSet rs = stmt.executeQuery("SELECT pg_backend_pid()");
      assertTrue("Expected a row from pg_backend_pid()", rs.next());
      int backendPid = rs.getInt(1);
      long backendRssBefore = getRssForPid(backendPid);
      LOG.info("Backend PID: {}, baseline RSS: {} KB", backendPid, backendRssBefore);

      int odysseyPid = getOdysseyPid();
      long odysseyRssBefore = getRssForPid(odysseyPid);
      LOG.info("Odyssey PID: {}, baseline RSS: {} KB", odysseyPid, odysseyRssBefore);
      stmt.execute("COMMIT");

      // --- Create many unique prepared statements ---
      for (int i = 0; i < NUM_STATEMENTS_TO_CREATE; i++) {
        String sql = "SELECT * FROM t_lru_test WHERE id = " + i + " AND data IS NOT NULL";
        PreparedStatement pstmt = conn.prepareStatement(sql);
        pstmt.execute();
        pstmt.close();
      }

      // --- Measure final memory ---
      stmt.execute("BEGIN");
      rs = stmt.executeQuery("SELECT pg_backend_pid()");
      assertTrue("Expected a row from pg_backend_pid()", rs.next());
      int currentPid = rs.getInt(1);
      long backendRssAfter = getRssForPid(currentPid);
      long odysseyRssAfter = getRssForPid(odysseyPid);
      stmt.execute("COMMIT");

      long backendRssDelta = backendRssAfter - backendRssBefore;
      long odysseyRssDelta = odysseyRssAfter - odysseyRssBefore;

      LOG.info("Backend PID {} RSS: before={} KB, after={} KB, delta={} KB",
          currentPid, backendRssBefore, backendRssAfter, backendRssDelta);
      LOG.info("Odyssey PID {} RSS: before={} KB, after={} KB, delta={} KB",
          odysseyPid, odysseyRssBefore, odysseyRssAfter, odysseyRssDelta);

      // --- Assert memory stays bounded ---
      assertTrue(
          String.format("Backend RSS increase (%d KB) should be < %d KB",
              backendRssDelta, MAX_BACKEND_RSS_INCREASE_KB),
          backendRssDelta < MAX_BACKEND_RSS_INCREASE_KB);

      assertTrue(
          String.format("Odyssey RSS increase (%d KB) should be < %d KB",
              odysseyRssDelta, MAX_ODYSSEY_RSS_INCREASE_KB),
          odysseyRssDelta < MAX_ODYSSEY_RSS_INCREASE_KB);

      stmt.close();
    } catch (Exception e) {
      LOG.error("Test failed with exception: ", e);
      fail("testPreparedStatementMemoryBounded failed: " + e.getMessage());
    }
  }

  // Multi-client test with round-robin backend assignment. Multiple logical
  // clients interleave prepared statement creation, rotating across backends.
  // Validates that:
  // 1. ForceClose during eviction doesn't break other clients sharing a backend.
  // 2. Each backend has at most max_prepared_statements after detach.
  // 3. The surviving statements are observable via pg_prepared_statements.
  @Test
  public void testLruWithMultipleClients() throws Exception {
    LOG.info("Running with optimizedMode={}", optimizedMode);
    final int maxPrepStmts = 1;
    final int numClients = 3;
    final int stmtsPerClient = 50;

    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "round_robin");
    tserverFlags.put("ysql_conn_mgr_max_prepared_statements", String.valueOf(maxPrepStmts));
    tserverFlags.put("ysql_conn_mgr_stats_interval", Integer.toString(STATS_UPDATE_INTERVAL));
    tserverFlags.put("ysql_conn_mgr_optimized_extended_query_protocol",
        Boolean.toString(optimizedMode));
    tserverFlags.put("ysql_conn_mgr_enable_prep_stmt_close",
        Boolean.toString(optimizedMode));
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    Properties props = new Properties();
    props.setProperty("prepareThreshold", "1");
    props.setProperty("preparedStatementCacheQueries", "0");

    List<Connection> conns = new ArrayList<>();
    for (int i = 0; i < numClients; i++) {
      conns.add(getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
          .withUser("yugabyte").withPassword("yugabyte").connect(props));
    }

    try (Statement setupStmt = conns.get(0).createStatement()) {
      setupStmt.execute("CREATE TABLE IF NOT EXISTS t_lru_multi (id int, data text)");
    }

    // Interleave prepared statement creation across clients. Round-robin
    // attach means clients rotate across backends, triggering detach and
    // ForceClose eviction on each backend between requests.
    int nextId = 0;
    for (int i = 0; i < stmtsPerClient; i++) {
      for (int c = 0; c < numClients; c++) {
        String sql = "SELECT * FROM t_lru_multi WHERE id = " + nextId++
            + " AND data IS NOT NULL";
        try (PreparedStatement pstmt = conns.get(c).prepareStatement(sql)) {
          pstmt.execute();
        }
      }
    }

    int numBackends = getTotalPhysicalConnections("yugabyte", "yugabyte",
        STATS_UPDATE_INTERVAL + 2);
    LOG.info("Pool has {} physical backend(s)", numBackends);

    // Validation: query pg_prepared_statements on each backend via a new
    // connection using preferQueryMode=simple (so validation queries themselves
    // don't create server-side prepared statements). Round-robin ensures each
    // iteration lands on a different backend. We check:
    //  - Each backend has at most maxPrepStmts prepared statements.
    //  - Each surviving statement matches the expected SQL pattern.
    //  - The set of surviving IDs is exactly
    //    {nextId - maxPrepStmts * numBackends, ..., nextId - 1},
    //    i.e. the last maxPrepStmts statements prepared on each backend.

    props = new Properties();
    props.setProperty("preferQueryMode", "simple");
    Connection verifyConn =
        getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("yugabyte").withPassword("yugabyte").connect(props);
    Statement s = verifyConn.createStatement();

    Pattern stmtPattern = Pattern.compile(
        "SELECT \\* FROM t_lru_multi WHERE id = (\\d+) AND data IS NOT NULL");
    Set<Integer> survivingIds = new HashSet<>();

    for (int c = 0; c < numBackends; c++) {
      ResultSet rs = s.executeQuery("SELECT name, statement FROM pg_prepared_statements");
      int count = 0;
      while (rs.next()) {
        count++;
        String stmtText = rs.getString("statement");
        String name = rs.getString("name");
        LOG.info("Backend {} statement {} [name='{}']: {}", c, count, name, stmtText);

        Matcher m = stmtPattern.matcher(stmtText);
        assertTrue("Unexpected statement: " + stmtText, m.matches());
        survivingIds.add(Integer.parseInt(m.group(1)));
      }

      LOG.info("Backend {}: {} prepared statement(s)", c, count);
      assertTrue(String.format("Backend %d has %d prepared statements, expected <= %d", c, count,
          maxPrepStmts), count <= maxPrepStmts);
    }

    Set<Integer> expectedIds = new HashSet<>();
    for (int id = nextId - maxPrepStmts * numBackends; id < nextId; id++) {
      expectedIds.add(id);
    }
    LOG.info("Surviving IDs: {}, expected IDs: {}", survivingIds, expectedIds);
    assertTrue(
        String.format("Surviving IDs %s should equal expected %s", survivingIds, expectedIds),
        survivingIds.equals(expectedIds));
  }

  @FunctionalInterface
  private interface Function<T, R> {
    R apply(T t) throws Exception;
  }

  // Validates that dynamically changing ysql_conn_mgr_max_prepared_statements
  // via setServerFlag (without a cluster restart) is picked up by Odyssey and
  // correctly caps the number of prepared statements on each backend.
  @Test
  public void testRuntimeFlagChange() throws Exception {
    LOG.info("Running with optimizedMode={}", optimizedMode);
    final int initialLimit = 3;
    final int reducedLimit = 1;
    final int restoredLimit = 3;

    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "round_robin");
    tserverFlags.put("ysql_conn_mgr_max_prepared_statements", String.valueOf(initialLimit));
    tserverFlags.put("ysql_conn_mgr_stats_interval", Integer.toString(STATS_UPDATE_INTERVAL));
    tserverFlags.put("ysql_conn_mgr_optimized_extended_query_protocol",
        Boolean.toString(optimizedMode));
    tserverFlags.put("ysql_conn_mgr_enable_prep_stmt_close",
        Boolean.toString(optimizedMode));
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    Properties props = new Properties();
    props.setProperty("prepareThreshold", "1");
    props.setProperty("preparedStatementCacheQueries", "0");

    Connection conn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser("yugabyte").withPassword("yugabyte").connect(props);
    Statement stmt = conn.createStatement();
    stmt.execute("CREATE TABLE IF NOT EXISTS t_flag_change (id int, data text)");

    Properties simpleProps = new Properties();
    simpleProps.setProperty("preferQueryMode", "simple");
    Connection verifyConn = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser("yugabyte").withPassword("yugabyte").connect(simpleProps);
    Statement verifyStmt = verifyConn.createStatement();

    Function<Statement, Integer> countPrepStmts = (s) -> {
      ResultSet rs = s.executeQuery("SELECT count(*) FROM pg_prepared_statements");
      assertTrue(rs.next());
      return rs.getInt(1);
    };

    // --- Phase 1: limit = 3 ---
    // Create 30 unique prepared statements, well beyond 3 per backend.
    int nextId = 0;
    for (int i = 0; i < 30; i++) {
      String sql = "SELECT * FROM t_flag_change WHERE id = " + nextId++
          + " AND data IS NOT NULL";
      try (PreparedStatement pstmt = conn.prepareStatement(sql)) {
        pstmt.execute();
      }
    }

    int numBackends = getTotalPhysicalConnections("yugabyte", "yugabyte",
        STATS_UPDATE_INTERVAL + 2);
    LOG.info("Pool has {} physical backend(s)", numBackends);

    for (int b = 0; b < numBackends; b++) {
      int count = countPrepStmts.apply(verifyStmt);
      LOG.info("Phase 1: backend {} has {} prepared statement(s)", b, count);
      assertEquals(String.format(
          "Phase 1: backend %d should have %d prepared stmts, got %d",
          b, initialLimit, count),
          initialLimit, count);
    }

    // --- Phase 2: shrink limit to 1 ---
    for (HostAndPort tServer : miniCluster.getTabletServers().keySet()) {
      setServerFlag(tServer, "ysql_conn_mgr_max_prepared_statements",
          String.valueOf(reducedLimit));
    }
    // Eviction (ForceClose of excess LRU statements) happens during backend
    // detach, not at prepare time. Running simple queries cycles through
    // backends in round-robin, triggering detach and cleanup on each one.
    for (int i = 0; i < numBackends; i++) {
      stmt.execute("SELECT 1");
    }

    for (int b = 0; b < numBackends; b++) {
      int count = countPrepStmts.apply(verifyStmt);
      LOG.info("Phase 2: backend {} has {} prepared statement(s)", b, count);
      assertEquals(String.format(
          "Phase 2: backend %d should have %d prepared stmts, got %d",
          b, reducedLimit, count),
          reducedLimit, count);
    }

    // --- Phase 3: grow limit back to 3 ---
    for (HostAndPort tServer : miniCluster.getTabletServers().keySet()) {
      setServerFlag(tServer, "ysql_conn_mgr_max_prepared_statements",
          String.valueOf(restoredLimit));
    }
    for (int i = 0; i < 10; i++) {
      String sql = "SELECT * FROM t_flag_change WHERE id = " + nextId++
          + " AND data IS NOT NULL";
      try (PreparedStatement pstmt = conn.prepareStatement(sql)) {
        pstmt.execute();
      }
    }

    for (int b = 0; b < numBackends; b++) {
      int count = countPrepStmts.apply(verifyStmt);
      LOG.info("Phase 3: backend {} has {} prepared statement(s)", b, count);
      assertEquals(String.format(
          "Phase 3: backend %d should have %d prepared stmts, got %d",
          b, restoredLimit, count),
          restoredLimit, count);
    }

    verifyConn.close();
    conn.close();
  }

}
