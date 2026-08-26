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

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_MODIFY_TABLE;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_RESULT;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.Statement;
import java.sql.ResultSet;
import java.util.Map;
import java.util.Map.Entry;
import java.util.List;
import java.util.ArrayList;
import java.util.Collections;
import java.io.StringWriter;
import java.io.PrintWriter;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.json.Checker;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.pgsql.ExplainAnalyzeUtils.MetricsCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.BuildTypeUtil;

import org.yb.YBTestRunner;

import com.google.gson.JsonElement;
import com.google.gson.JsonParser;
import com.google.common.net.HostAndPort;

/**
 * Test the auto_explain module
 */
@RunWith(value=YBTestRunner.class)
public class TestPgAutoExplain extends BasePgSQLTest {
  private static final String TABLE_NAME = "auto_explain_test_table";
  private static final int TABLE_ROWS = 5000;
  private static final Logger LOG = LoggerFactory.getLogger(TestPgAutoExplain.class);

  private final CurrentAutoExplainResults autoExplainResults = new CurrentAutoExplainResults();

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  private boolean preloadAutoExplain = true;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_prefetch_limit", "1024");
    flagMap.put("ysql_session_max_batch_size", "512");
    if (preloadAutoExplain) {
      appendToYsqlPgConf(flagMap, "shared_preload_libraries=auto_explain");
    }
    return flagMap;
  }

  @Before
  public void setUp() throws Exception {
    registerAutoExplainLogListeners();
    createAndPopulateTestTable();

    setAutoExplainOption("log_analyze", "true");
    setAutoExplainOption("log_min_duration", "0");
    setAutoExplainOption("sample_rate", "1");
    setAutoExplainOption("log_format", "json");
  }

  private void registerAutoExplainLogListeners() {
    for (Entry<HostAndPort, MiniYBDaemon> entry : miniCluster.getTabletServers().entrySet()) {
      MiniYBDaemon tserver = entry.getValue();
      // ErrorListener is a misnomer, we parse postgres explain analyze logs here
      tserver.getLogPrinter().addErrorListener(new AutoExplainLogListener(autoExplainResults));
    }
  }

  private void createAndPopulateTestTable() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format(
          "CREATE TABLE %s (c1 bigint, c2 bigint, c3 bigint, c4 text, " +
          "PRIMARY KEY(c1 ASC, c2 ASC, c3 ASC))",
          TABLE_NAME));

      stmt.execute(String.format(
          "INSERT INTO %s SELECT i %% 1000, i %% 11, i %% 20, rpad(i::text, 256, '#') " +
          "FROM generate_series(1, %d) AS i",
          TABLE_NAME, TABLE_ROWS));
    }
  }

  /**
   * NOTE: auto_explain does not output the summary
   *   hence delegates most sanity checks to the PlanChecker
   */
  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  private static MetricsCheckerBuilder makeMetricsBuilder() {
    return JsonUtil.makeCheckerBuilder(MetricsCheckerBuilder.class, false);
  }

  private void testAutoExplain(
      String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Empty query results
      autoExplainResults.discard();

      // Execute query and retrieve results from the log
      LOG.info("Query: " + query);
      ResultSet rs = stmt.executeQuery(query);
      rs.next();
      List<JsonElement> jsonResults = autoExplainResults.popAll();

      // We expect only a single result
      assertEquals(1, jsonResults.size());
      JsonElement json = jsonResults.get(0);

      // Log the result
      LOG.info("Response:\n" + JsonUtil.asPrettyString(json));

      // Find conflicts and log them as well
      List<String> conflicts = JsonUtil.findConflicts(json, checker);
      assertTrue("Json conflicts:\n" + String.join("\n", conflicts),
                 conflicts.isEmpty());
    }
  }

  private void testAutoExplainDml(
      String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      autoExplainResults.discard();

      LOG.info("Query: " + query);
      stmt.execute(query);
      List<JsonElement> jsonResults = autoExplainResults.popAll();

      assertEquals(1, jsonResults.size());
      JsonElement json = jsonResults.get(0);

      LOG.info("Response:\n" + JsonUtil.asPrettyString(json));

      List<String> conflicts = JsonUtil.findConflicts(json, checker);
      assertTrue("Json conflicts:\n" + String.join("\n", conflicts),
                 conflicts.isEmpty());
    }
  }

  @Test
  public void testDistOn() throws Exception {
    setAutoExplainOption("log_analyze", "true");
    setAutoExplainOption("log_dist", "true");

    // NestLoop accesses the inner table as many times as the rows from the outer
    testAutoExplain(
        String.format(
            "/*+ SeqScan(%s) */ SELECT * FROM %s ",
            TABLE_NAME, TABLE_NAME),
        makeTopLevelBuilder()
            .plan(makePlanBuilder()
                .nodeType(NODE_SEQ_SCAN)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .storageTableReadRequests(Checkers.greaterOrEqual(0))
                .storageTableReadExecutionTime(Checkers.greaterOrEqual(0.0))
                .build())
            .build());
  }

  /**
   * use LOAD command to install auto_explain hook instead of preloading
   */
  private void startClusterWithoutAutoExplainPreload() throws Exception {
    preloadAutoExplain = false;
    restartCluster();
    // The restart created a fresh cluster: re-register the log listeners and
    // recreate the test table
    registerAutoExplainLogListeners();
    createAndPopulateTestTable();
  }

  /** Restore the default cluster (auto_explain preloaded) so subsequent tests are unaffected. */
  private void restoreDefaultCluster() throws Exception {
    preloadAutoExplain = true;
    restartCluster();
  }

  /**
   * LOAD installs the auto_explain executor hook in the current backend even when the module is not
   * preloaded, after which queries on that connection are logged.
   */
  @Test
  public void testLoadEnablesAutoExplain() throws Exception {
    startClusterWithoutAutoExplainPreload();
    try {
      try (Statement stmt = connection.createStatement()) {
        stmt.execute("LOAD 'auto_explain'");
      }
      setAutoExplainOption("log_analyze", "true");
      setAutoExplainOption("log_dist", "true");
      setAutoExplainOption("log_min_duration", "0");
      setAutoExplainOption("sample_rate", "1");
      setAutoExplainOption("log_format", "json");

      testAutoExplain(
          String.format(
              "/*+ SeqScan(%s) */ SELECT * FROM %s ",
              TABLE_NAME, TABLE_NAME),
          makeTopLevelBuilder()
              .plan(makePlanBuilder()
                  .nodeType(NODE_SEQ_SCAN)
                  .relationName(TABLE_NAME)
                  .alias(TABLE_NAME)
                  .storageTableReadRequests(Checkers.greaterOrEqual(0))
                  .storageTableReadExecutionTime(Checkers.greaterOrEqual(0.0))
                  .build())
              .build());
    } finally {
      restoreDefaultCluster();
    }
  }

  /**
   * Validate that the effect of LOAD is per-backend and does not persist across a restart.
   *
   * Connections used:
   *   c1 - runs LOAD 'auto_explain'; only this backend should log query plans.
   *   c2 - opened before the LOAD; must stay unaffected (a different backend).
   *   c3 - opened after the LOAD; a brand-new backend must also be unaffected.
   *   c4 - opened after a postmaster restart; the in-memory LOAD state cannot survive the restart.
   */
  @Test
  @BypassConnMgr(reason = BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED)
  public void testLoadScopeIsPerBackendAndNotPersisted() throws Exception {
    startClusterWithoutAutoExplainPreload();
    try {
      final String probeQuery = String.format("SELECT * FROM %s", TABLE_NAME);

      ConnectionBuilder connBuilder = getConnectionBuilder();
      try (Connection c1 = connBuilder.connect();
           Connection c2 = connBuilder.connect()) {
        // Before LOAD: auto_explain is installed nowhere, so nothing is logged.
        assertEquals("Query before LOAD must not be logged in c1",
                     0, countAutoExplainLogsFor(c1, probeQuery));
        assertEquals("Query before LOAD must not be logged in c2",
                     0, countAutoExplainLogsFor(c2, probeQuery));

        // Install + enable the executor hook in c1's backend only.
        try (Statement stmt = c1.createStatement()) {
          stmt.execute("LOAD 'auto_explain'");
          stmt.execute("SET auto_explain.log_min_duration = 0");
          stmt.execute("SET auto_explain.log_format = json");
          stmt.execute("SET auto_explain.sample_rate = 1");
        }

        // After LOAD: c1 logs its query; c2 (a different backend) is unaffected.
        assertEquals("Query after LOAD must be logged in c1",
                     1, countAutoExplainLogsFor(c1, probeQuery));
        assertEquals("LOAD in c1 must not affect c2",
                     0, countAutoExplainLogsFor(c2, probeQuery));

        // A brand-new backend created after the LOAD is also unaffected.
        try (Connection c3 = connBuilder.connect()) {
          assertEquals("LOAD in c1 must not affect a new connection c3",
                       0, countAutoExplainLogsFor(c3, probeQuery));
        }
      }

      // A postmaster restart starts fresh processes with fresh address spaces;
      // the in-memory library list populated by LOAD cannot survive it.
      restartCluster();
      registerAutoExplainLogListeners();
      createAndPopulateTestTable();

      try (Connection c4 = getConnectionBuilder().connect()) {
        assertEquals("LOAD must not persist across a postmaster restart (c4)",
                     0, countAutoExplainLogsFor(c4, probeQuery));
      }
    } finally {
      restoreDefaultCluster();
    }
  }

  /**
   * Run the given query on the given connection and return the number of auto_explain plans that
   * were emitted to the server log as a result.
   */
  private int countAutoExplainLogsFor(Connection conn, String query) throws Exception {
    autoExplainResults.discard();
    try (Statement stmt = conn.createStatement()) {
      LOG.info("Probe query: " + query);
      ResultSet rs = stmt.executeQuery(query);
      rs.next();
    }
    return autoExplainResults.popAll().size();
  }

  @Test
  public void testDistOff() throws Exception {
    setAutoExplainOption("log_analyze", "true");
    setAutoExplainOption("log_dist", "false");

    // NestLoop accesses the inner table as many times as the rows from the outer
    testAutoExplain(
        String.format(
            "/*+ SeqScan(%s) */ SELECT * FROM %s ",
            TABLE_NAME, TABLE_NAME),
        makeTopLevelBuilder()
            .plan(makePlanBuilder()
                .nodeType(NODE_SEQ_SCAN)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .actualStartupTime(Checkers.greaterOrEqual(0.0))
                .actualTotalTime(Checkers.greaterOrEqual(0.0))
                // Should not output DIST stats
                .storageTableReadRequests(JsonUtil.absenceCheckerOnNull(null))
                .storageTableReadExecutionTime(JsonUtil.absenceCheckerOnNull(null))
                .build())
            .build());
  }

  @Test
  public void testAnalyzeOff() throws Exception {
    setAutoExplainOption("log_analyze", "false");
    setAutoExplainOption("log_dist", "true");

    // NestLoop accesses the inner table as many times as the rows from the outer
    testAutoExplain(
        String.format(
            "/*+ SeqScan(%s) */ SELECT * FROM %s ",
            TABLE_NAME, TABLE_NAME),
        makeTopLevelBuilder()
            .plan(makePlanBuilder()
                .nodeType(NODE_SEQ_SCAN)
                // Startup cost and total cost because auto_explain
                .startupCost(Checkers.greaterOrEqual(0.0))
                .totalCost(Checkers.greaterOrEqual(0.0))
                // Use JsonUtil since AbsenceChecker is not public
                .actualStartupTime(JsonUtil.absenceCheckerOnNull(null))
                .actualTotalTime(JsonUtil.absenceCheckerOnNull(null))
                .build())
            .build());
  }

  @Test
  public void testDebugOnRead() throws Exception {
    setAutoExplainOption("log_analyze", "true");
    setAutoExplainOption("log_dist", "true");
    setAutoExplainOption("log_debug", "true");

    testAutoExplain(
        String.format(
            "SELECT * FROM %s WHERE c1 = 1 AND c2 = 1 AND c3 = 1",
            TABLE_NAME),
        makeTopLevelBuilder()
            .plan(makePlanBuilder()
                .nodeType(NODE_INDEX_SCAN)
                .relationName(TABLE_NAME)
                .alias(TABLE_NAME)
                .readMetrics(makeMetricsBuilder()
                    .metric("rocksdb_number_db_seek", Checkers.equal(1.0))
                    .metric("docdb_keys_found", Checkers.equal(1.0))
                    .build())
                .build())
            .build());
  }

  @Test
  public void testDebugOnWrite() throws Exception {
    setAutoExplainOption("log_analyze", "true");
    setAutoExplainOption("log_dist", "true");
    setAutoExplainOption("log_debug", "true");

    PlanCheckerBuilder insertNodeChecker = makePlanBuilder()
        .nodeType(NODE_MODIFY_TABLE)
        .relationName(TABLE_NAME)
        .alias(TABLE_NAME);

    PlanCheckerBuilder resultNodeChecker = makePlanBuilder()
        .nodeType(NODE_RESULT)
        .writeMetrics(makeMetricsBuilder()
            .metric("rocksdb_number_db_seek", Checkers.equal(1.0))
            .build());

    testAutoExplainDml(
        String.format(
            "INSERT INTO %s VALUES (9999, 9999, 9999, 'test')",
            TABLE_NAME),
        makeTopLevelBuilder()
            .plan(insertNodeChecker
                .plans(resultNodeChecker.build())
                .build())
            .build());
  }

  /**
   * Store parsed results of auto_explain in the log
   *
   * Each result is a JSON object and stored as a JSONElement
   * ==================
   * Example log output
   * ==================
   * 2023-05-26 12:02:20.621 PDT [42401] LOG:  duration: 123.238 ms  plan:
   *     {
   *       "Query Text": "select * from foo;",
   *       "Plan": {
   *         "Node Type": "Seq Scan",
   *         ...
   *       }
   *     }
   */
  private static class CurrentAutoExplainResults {
    private final List<JsonElement> storage = Collections
        .synchronizedList(new ArrayList<>());

    public void push(JsonElement entry) {
      storage.add(entry);
    }

    /** Discard existing results so that only newer ones will be retrieved. */
    public void discard() throws Exception {
      popAll();
    }

    /** Retrieve the results added to the log since last call, discarding them. */
    public List<JsonElement> popAll() throws Exception {
      Thread.sleep(BuildTypeUtil.adjustTimeout(200));
      synchronized (storage) {
        List<JsonElement> result = new ArrayList<>(storage);
        storage.clear();
        return result;
      }
    }
  }

  /**
   * Listen for auto_explain logs
   * ErrorLogListener is a misnomer
   */
  private static class AutoExplainLogListener implements LogErrorListener {
    // Marker indicating that the next line is a JSON object of interest
    private static final String START_SUFFIX = "plan:";

    // Use this constant to inidicate the region of no interest (we skip these lines)
    private static final int INVALID_NESTING = -1;

    // Useful to identify changes in nesting level
    private static final String OPEN_BRACE = "{";
    private static final String CLOSE_BRACE = "}";
    private static final String CLOSE_BRACE_COMMA = "},";

    // Store all JSON objects that occur
    // Only one such JSON object occurs in practice
    private final CurrentAutoExplainResults autoExplainResults;

    // Storage to maintain all the lines containing the JSON object before we can parse the object
    private final List<String> jsonLines = new ArrayList<>();

    // State machine impl
    int nesting = INVALID_NESTING;

    public AutoExplainLogListener(CurrentAutoExplainResults autoExplainResults) {
      this.autoExplainResults = autoExplainResults;
    }

    /**
     * Assumes that there is at least one line in the JSON output
     *
     * Parse Recipe
     * - identify lines ending with plan: as part of the auto explain result
     * - accumulate subsequent lines till we find a new entry
     * - to find out the new entry
     *   increment counter on `{`
     *   decrement counter on `}` or `},`
     *   new entry found when counter reaches zero
     * - parse the accumulated lines into a JSONElement object
     */
    @Override
    public void handleLine(String line) {
      // Strip the line of surrounding whitespaces
      line = line.trim();

      if (nesting >= 0) {
        // Check end of log
        if (line.endsWith(OPEN_BRACE)) {
          nesting++;
        }

        // NOTE: Trailing comma occurs when this is not the last entry in the JSON object
        if (line.endsWith(CLOSE_BRACE) || line.endsWith(CLOSE_BRACE_COMMA)) {
          nesting--;
        }

        jsonLines.add(line);
      }

      if (nesting == 0) {
        // Done with the JSON object
        nesting = INVALID_NESTING;

        // Collect the current lines into a JSONElement
        // See https://stackoverflow.com/a/2509256
        StringWriter stringWriter = new StringWriter();
        PrintWriter writer = new PrintWriter(stringWriter, true);
        for (String jsonLine : jsonLines) {
          writer.println(jsonLine);
        }
        jsonLines.clear();

        JsonElement jsonElement = JsonParser.parseString(stringWriter.toString());
        autoExplainResults.push(jsonElement);
      }
      // Avoid the else clause here since the current line might be starting a new result

      // Detect if we need to collect logs from the next line
      if (line.endsWith(START_SUFFIX)) {
        nesting = 0;
      }
    }

    @Override
    public void reportErrorsAtEnd() {
      // NOOP
    }
  }

  private static void setAutoExplainOption(String option, String value) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("SET auto_explain.%s = %s", option, value));
    }
  }
}
