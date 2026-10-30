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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.junit.After;
import org.junit.AssumptionViolatedException;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.yb.YBParameterizedTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * Sequence functions called by one logical client whose transactions run on different backends.
 * Each test checks what a client of a plain PostgreSQL session would see, and uses
 * {@link BackendSteering} to move the client between backends where a pooled client could land.
 *
 * In session mode a client that called nextval is meant to stay on its backend, so the steering
 * is best effort there and only the values are checked. In pooled_without_curval_lastval mode
 * currval and lastval are refused, so the tests expect that error instead.
 *
 * Cases listed in KNOWN_FAILURES fail on master by design until per-client sequence state lands
 * (#36293); they report as skipped while they fail, and fail once they pass, so #36302 has to
 * remove them.
 */
@RequiresLinux
@RunWith(value = YBParameterizedTestRunner.class)
public class TestSequenceBackendSwitch extends BaseYsqlConnMgr {
  private static final String SESSION = "session";
  private static final String POOLED_WITH = "pooled_with_curval_lastval";
  private static final String POOLED_WITHOUT = "pooled_without_curval_lastval";

  private static final String APP_USER = "seq_app";

  private static final String NOT_SUPPORTED =
      "not supported for session created by connection manager";

  private static final String NOT_YET_DEFINED = "is not yet defined in this session";

  // Test name with parameters -> {bug, part of the failure message the bug produces}.
  private static final Map<String, String[]> KNOWN_FAILURES = new HashMap<>();

  private static void knownFailure(String bug, String symptom, String test, String mode,
                                   String... cacheMethods) {
    for (String cacheMethod : cacheMethods) {
      KNOWN_FAILURES.put(test + "[" + mode + "-" + cacheMethod + "]", new String[] {bug, symptom});
    }
  }

  static {
    // currval and lastval read the backend's state, whichever client left it there...
    for (String test : Arrays.asList("testOtherClientDoesNotSeeCurrvalLastval",
                                     "testCurrvalErrorsAfterDiscardSequences")) {
      knownFailure("#36294", "to fail on backend", test, POOLED_WITH, "connection", "server");
    }
    // ...and the client's own state stays behind on the backend it left.
    for (String test : Arrays.asList("testCurrvalLastvalFollowClientAcrossSwitch",
                                     "testCurrvalAfterRolledBackNextvalOnOtherBackend")) {
      knownFailure("#36293", NOT_YET_DEFINED, test, POOLED_WITH, "connection", "server");
    }
    // With the connection cache method each backend hands out its own cached range.
    for (String mode : Arrays.asList(POOLED_WITHOUT, POOLED_WITH)) {
      knownFailure("#36294", "went backwards", "testNextvalMonotonicPerClientAcrossBackends",
                   mode, "connection");
      knownFailure("#36296", "nextval after setval", "testSetvalSeenOnOtherBackend", mode,
                   "connection");
      knownFailure("#36296", "nextval after RESTART", "testRestartSeenOnOtherBackend", mode,
                   "connection");
    }
    // A rolled-back nextval does not make the client sticky.
    knownFailure("#36295", NOT_YET_DEFINED, "testCurrvalAfterRolledBackNextvalOnOtherBackend",
                 SESSION, "connection", "server");
  }

  // Reports a listed test as skipped while it fails with its bug's symptom. Any other failure,
  // such as a missed steer or a setup error, still fails the test.
  @Rule
  public final TestRule knownFailures = (base, description) ->
      new org.junit.runners.model.Statement() {
        @Override
        public void evaluate() throws Throwable {
          String[] known = KNOWN_FAILURES.get(description.getMethodName());
          if (known == null) {
            base.evaluate();
            return;
          }
          String bug = known[0];
          try {
            base.evaluate();
          } catch (AssumptionViolatedException e) {
            throw e;
          } catch (Throwable t) {
            if (t.getMessage() == null || !t.getMessage().contains(known[1])) {
              throw t;
            }
            LOG.info("Known failure, " + bug, t);
            throw new AssumptionViolatedException("Known failure, " + bug + ": " + t);
          }
          fail(description.getMethodName() + " passes; remove it from KNOWN_FAILURES (" +
               bug + ")");
        }
      };

  // Mode and cache method the running mini cluster was started with.
  private static String clusterConfig;

  // Direct connection used by BackendSteering to list backends.
  private Connection observer;

  private final String mode;
  private final String cacheMethod;

  public TestSequenceBackendSwitch(String mode, String cacheMethod) {
    this.mode = mode;
    this.cacheMethod = cacheMethod;
  }

  @Parameterized.Parameters(name = "{0}-{1}")
  public static List<Object[]> parameters() {
    List<Object[]> params = new ArrayList<>();
    for (String mode : Arrays.asList(POOLED_WITHOUT, POOLED_WITH, SESSION)) {
      for (String cacheMethod : Arrays.asList("connection", "server")) {
        params.add(new Object[] {mode, cacheMethod});
      }
    }
    return params;
  }

  @Override
  protected void resetSettings() {
    super.resetSettings();
    String config = mode + "/" + cacheMethod;
    if (!config.equals(clusterConfig)) {
      markClusterNeedsRecreation();
      clusterConfig = config;
    }
  }

  // One tserver, so every client and holder shares one pool and pg_stat_activity sees it all.
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumMasters() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // The base class asks for NUM_TSERVER tservers.
    builder.numTservers(1);
    builder.replicationFactor(1);
    // Random warmup hands clients random backends, which would defeat the steering.
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_sequence_support_mode", mode);
    builder.addCommonTServerFlag("ysql_sequence_cache_method", cacheMethod);
  }

  @Before
  public void setUpObserverAndAppUser() throws Exception {
    observer = getConnectionBuilder().withConnectionEndpoint(ConnectionEndpoint.POSTGRES).connect();
    try (Statement stmt = observer.createStatement()) {
      ResultSet rs = stmt.executeQuery(
          "SELECT 1 FROM pg_roles WHERE rolname = '" + APP_USER + "'");
      if (!rs.next()) {
        stmt.execute("CREATE ROLE " + APP_USER + " LOGIN");
        stmt.execute("GRANT ALL ON SCHEMA public TO " + APP_USER);
      }
    }
  }

  // A failed test leaves its sequence behind on the reused cluster. In pooled_with_curval_lastval
  // mode a backend's lastval() keeps pointing at it for the next client (#36294), which would
  // break later tests' lastval checks.
  @After
  public void dropSequencesAndCloseObserver() throws Exception {
    if (observer == null) {
      return;
    }
    try (Statement stmt = observer.createStatement()) {
      List<String> seqs = new ArrayList<>();
      try (ResultSet rs = stmt.executeQuery(
               "SELECT c.relname FROM pg_class c JOIN pg_roles r ON r.oid = c.relowner" +
               " WHERE c.relkind = 'S' AND r.rolname = '" + APP_USER + "'")) {
        while (rs.next()) {
          seqs.add(rs.getString(1));
        }
      }
      for (String seq : seqs) {
        stmt.execute("DROP SEQUENCE IF EXISTS " + seq);
      }
    } finally {
      observer.close();
    }
  }

  private BackendSteering steering() {
    return new BackendSteering(appBuilder(), observer);
  }

  private ConnectionBuilder appBuilder() {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser(APP_USER);
  }

  private boolean isSession() {
    return mode.equals(SESSION);
  }

  private static long[] pidAndValue(Statement stmt, String expr) throws SQLException {
    try (ResultSet rs = stmt.executeQuery("SELECT pg_backend_pid(), " + expr)) {
      assertTrue(rs.next());
      return new long[] {rs.getInt(1), rs.getLong(2)};
    }
  }

  /** Runs sql in a transaction, so it shares a backend with the pid query; returns that pid. */
  private static int exec(Statement stmt, String sql) throws SQLException {
    stmt.execute("BEGIN");
    int pid = BackendSteering.backendPid(stmt);
    stmt.execute(sql);
    stmt.execute("COMMIT");
    return pid;
  }

  /**
   * Runs SELECT expr in a transaction and expects it to fail with pgError, or with the
   * not-supported error in pooled_without_curval_lastval mode. Returns the backend pid.
   */
  private int expectSeqFnError(Statement stmt, String expr, String pgError) throws Exception {
    String query = "SELECT " + expr;
    stmt.execute("BEGIN");
    int pid = BackendSteering.backendPid(stmt);
    try {
      ResultSet rs = stmt.executeQuery(query);
      rs.next();
      fail(String.format("Expected '%s' to fail on backend %d, got %d", query, pid, rs.getLong(1)));
    } catch (SQLException e) {
      String expected = mode.equals(POOLED_WITHOUT) ? NOT_SUPPORTED : pgError;
      assertTrue(String.format("'%s' on backend %d: expected error containing '%s', got '%s'",
                               query, pid, expected, e.getMessage()),
                 e.getMessage().contains(expected));
    } finally {
      stmt.execute("ROLLBACK");
    }
    return pid;
  }

  /** Expects expr to return expected, or the not-supported error in that mode. */
  private int expectSeqFnValue(Statement stmt, String expr, long expected) throws Exception {
    if (mode.equals(POOLED_WITHOUT)) {
      return expectSeqFnError(stmt, expr, NOT_SUPPORTED);
    }
    long[] r = pidAndValue(stmt, expr);
    assertEquals(String.format("'%s' on backend %d", expr, r[0]), expected, r[1]);
    return (int) r[0];
  }

  /** Asserts the steering worked, except in session mode where the client may be sticky. */
  private void assertSteered(boolean steered, String what) {
    if (!isSession()) {
      assertTrue("Could not " + what, steered);
    } else if (!steered) {
      LOG.info("Could not " + what + "; client is sticky");
    }
  }

  private void assertSwitched(int before, int after) {
    if (!isSession()) {
      assertNotEquals("Client stayed on backend " + before, before, after);
    }
  }

  /** Asserts the client ran on the backend it was steered to, except in session mode. */
  private void assertLanded(int expected, int actual, String what) {
    if (!isSession()) {
      assertEquals(what + " did not run on backend " + expected, expected, actual);
    }
  }

  private static String notYetDefined(String seq) {
    return "currval of sequence \"" + seq + "\" " + NOT_YET_DEFINED;
  }

  private static final String LASTVAL_NOT_YET_DEFINED = "lastval " + NOT_YET_DEFINED;

  // 1. Client B never sees client A's currval or lastval, even on A's backend.
  @Test
  public void testOtherClientDoesNotSeeCurrvalLastval() throws Exception {
    String seq = "seq_other_client";
    try (Connection a = appBuilder().connect(); Statement sa = a.createStatement();
         Connection b = appBuilder().connect(); Statement sb = b.createStatement()) {
      sa.execute("CREATE SEQUENCE " + seq);
      long[] r = pidAndValue(sa, "nextval('" + seq + "')");
      int aPid = (int) r[0];

      for (String expr : Arrays.asList("currval('" + seq + "')", "lastval()")) {
        try (BackendSteering steering = steering()) {
          assertSteered(steering.steerTo(aPid), "steer B to A's backend " + aPid);
          String err = expr.contains("currval") ? notYetDefined(seq) : LASTVAL_NOT_YET_DEFINED;
          assertLanded(aPid, expectSeqFnError(sb, expr, err), "B's " + expr);
        }
      }
      sa.execute("DROP SEQUENCE " + seq);
    }
  }

  // 2. After a switch, currval and lastval return the client's own last value.
  @Test
  public void testCurrvalLastvalFollowClientAcrossSwitch() throws Exception {
    String seq = "seq_follow_client";
    try (Connection a = appBuilder().connect(); Statement sa = a.createStatement()) {
      sa.execute("CREATE SEQUENCE " + seq);
      pidAndValue(sa, "nextval('" + seq + "')");
      long[] r = pidAndValue(sa, "nextval('" + seq + "')");
      int before = (int) r[0];
      long last = r[1];

      for (String expr : Arrays.asList("currval('" + seq + "')", "lastval()")) {
        try (BackendSteering steering = steering()) {
          assertSteered(steering.hold(before), "hold A's backend " + before);
          assertSwitched(before, expectSeqFnValue(sa, expr, last));
        }
      }
      sa.execute("DROP SEQUENCE " + seq);
    }
  }

  // 3. Across N clients x M transactions, each moved off its previous backend before every
  //    transaction, each client's nextval values strictly increase.
  @Test
  public void testNextvalMonotonicPerClientAcrossBackends() throws Exception {
    final int numClients = 3;
    final int numTxns = 6;
    String seq = "seq_monotonic";
    List<Connection> conns = new ArrayList<>();
    try {
      for (int i = 0; i < numClients; i++) {
        conns.add(appBuilder().connect());
      }
      try (Statement stmt = conns.get(0).createStatement()) {
        stmt.execute("CREATE SEQUENCE " + seq);
      }
      int[] prevPid = new int[numClients];
      long[] prevVal = new long[numClients];
      Arrays.fill(prevPid, -1);
      Arrays.fill(prevVal, Long.MIN_VALUE);
      Set<Integer> backends = new HashSet<>();
      List<String> trace = new ArrayList<>();

      for (int t = 0; t < numTxns; t++) {
        for (int c = 0; c < numClients; c++) {
          try (BackendSteering steering = steering();
               Statement stmt = conns.get(c).createStatement()) {
            if (prevPid[c] != -1) {
              assertSteered(steering.hold(prevPid[c]),
                            "hold client " + c + "'s backend " + prevPid[c]);
            }
            long[] r = pidAndValue(stmt, "nextval('" + seq + "')");
            trace.add(String.format("c%d@%d=%d", c, r[0], r[1]));
            if (prevPid[c] != -1) {
              assertSwitched(prevPid[c], (int) r[0]);
            }
            assertTrue("Client " + c + " went backwards: " + trace, r[1] > prevVal[c]);
            prevPid[c] = (int) r[0];
            prevVal[c] = r[1];
            backends.add((int) r[0]);
          }
        }
      }
      LOG.info("nextval trace over " + backends.size() + " backends: " + trace);
      if (!isSession()) {
        assertTrue("Expected several backends, got " + backends, backends.size() > 1);
      }
      try (Statement stmt = conns.get(0).createStatement()) {
        stmt.execute("DROP SEQUENCE " + seq);
      }
    } finally {
      for (Connection conn : conns) {
        conn.close();
      }
    }
  }

  // 4. After setval(v), the client's nextval on another backend, one whose cache was filled
  //    before the setval, returns v + increment.
  @Test
  public void testSetvalSeenOnOtherBackend() throws Exception {
    String seq = "seq_setval";
    try (Connection a = appBuilder().connect(); Statement sa = a.createStatement();
         Connection b = appBuilder().connect(); Statement sb = b.createStatement()) {
      sa.execute("CREATE SEQUENCE " + seq);
      int p = (int) pidAndValue(sa, "nextval('" + seq + "')")[0];
      int q;
      // B fills another backend's cache.
      try (BackendSteering steering = steering()) {
        assertSteered(steering.hold(p), "hold A's backend " + p);
        q = (int) pidAndValue(sb, "nextval('" + seq + "')")[0];
      }
      try (BackendSteering steering = steering()) {
        assertSteered(steering.steerTo(p), "steer A to backend " + p);
        assertLanded(p, (int) pidAndValue(sa, "setval('" + seq + "', 1000)")[0], "setval");
      }
      try (BackendSteering steering = steering()) {
        assertSteered(steering.steerTo(q), "steer A to B's backend " + q);
        long[] r = pidAndValue(sa, "nextval('" + seq + "')");
        assertLanded(q, (int) r[0], "nextval");
        assertEquals("nextval after setval on backend " + r[0], 1001, r[1]);
      }
      sa.execute("DROP SEQUENCE " + seq);
    }
  }

  // 5. currval after a nextval whose transaction rolled back, on another backend, returns that
  //    value: nextval is not transactional.
  @Test
  public void testCurrvalAfterRolledBackNextvalOnOtherBackend() throws Exception {
    String seq = "seq_rolled_back";
    try (Connection a = appBuilder().connect(); Statement sa = a.createStatement()) {
      sa.execute("CREATE SEQUENCE " + seq);
      sa.execute("BEGIN");
      long[] r = pidAndValue(sa, "nextval('" + seq + "')");
      sa.execute("ROLLBACK");
      int before = (int) r[0];
      try (BackendSteering steering = steering()) {
        assertSteered(steering.hold(before), "hold A's backend " + before);
        assertSwitched(before, expectSeqFnValue(sa, "currval('" + seq + "')", r[1]));
      }
      sa.execute("DROP SEQUENCE " + seq);
    }
  }

  // 6. After DISCARD SEQUENCES, currval errors, including on the backend that ran the nextval.
  @Test
  public void testCurrvalErrorsAfterDiscardSequences() throws Exception {
    String seq = "seq_discard";
    try (Connection a = appBuilder().connect(); Statement sa = a.createStatement()) {
      sa.execute("CREATE SEQUENCE " + seq);
      int p = (int) pidAndValue(sa, "nextval('" + seq + "')")[0];
      int discardPid;
      try (BackendSteering steering = steering()) {
        assertSteered(steering.hold(p), "hold A's backend " + p);
        discardPid = exec(sa, "DISCARD SEQUENCES");
        assertSwitched(p, discardPid);
      }
      try (BackendSteering steering = steering()) {
        assertSteered(steering.steerTo(p), "steer A back to backend " + p);
        assertLanded(p, expectSeqFnError(sa, "currval('" + seq + "')", notYetDefined(seq)),
                     "currval");
      }
      expectSeqFnError(sa, "currval('" + seq + "')", notYetDefined(seq));
      sa.execute("DROP SEQUENCE " + seq);
    }
  }

  // 7. After DROP SEQUENCE, lastval errors, including on the backend that ran the nextval.
  @Test
  public void testLastvalErrorsAfterDropSequence() throws Exception {
    String seq = "seq_drop";
    try (Connection a = appBuilder().connect(); Statement sa = a.createStatement()) {
      sa.execute("CREATE SEQUENCE " + seq);
      int p = (int) pidAndValue(sa, "nextval('" + seq + "')")[0];
      try (BackendSteering steering = steering()) {
        assertSteered(steering.hold(p), "hold A's backend " + p);
        assertSwitched(p, exec(sa, "DROP SEQUENCE " + seq));
      }
      try (BackendSteering steering = steering()) {
        assertSteered(steering.steerTo(p), "steer A back to backend " + p);
        assertLanded(p, expectSeqFnError(sa, "lastval()", LASTVAL_NOT_YET_DEFINED), "lastval");
      }
      expectSeqFnError(sa, "lastval()", LASTVAL_NOT_YET_DEFINED);
    }
  }

  // 8. After ALTER SEQUENCE ... RESTART, the client's nextval on another backend, one whose cache
  //    was filled before the restart, follows the restart.
  @Test
  public void testRestartSeenOnOtherBackend() throws Exception {
    String seq = "seq_restart";
    try (Connection a = appBuilder().connect(); Statement sa = a.createStatement();
         Connection b = appBuilder().connect(); Statement sb = b.createStatement()) {
      sa.execute("CREATE SEQUENCE " + seq);
      int p = (int) pidAndValue(sa, "nextval('" + seq + "')")[0];
      int q;
      // B fills another backend's cache.
      try (BackendSteering steering = steering()) {
        assertSteered(steering.hold(p), "hold A's backend " + p);
        q = (int) pidAndValue(sb, "nextval('" + seq + "')")[0];
      }
      try (BackendSteering steering = steering()) {
        assertSteered(steering.steerTo(p), "steer A to backend " + p);
        assertLanded(p, exec(sa, "ALTER SEQUENCE " + seq + " RESTART WITH 1000"), "ALTER");
      }
      try (BackendSteering steering = steering()) {
        assertSteered(steering.steerTo(q), "steer A to B's backend " + q);
        long[] r = pidAndValue(sa, "nextval('" + seq + "')");
        assertLanded(q, (int) r[0], "nextval");
        assertEquals("nextval after RESTART on backend " + r[0], 1000, r[1]);
      }
      sa.execute("DROP SEQUENCE " + seq);
    }
  }
}
