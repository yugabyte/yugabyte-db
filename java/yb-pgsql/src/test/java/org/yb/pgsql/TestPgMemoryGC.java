package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.util.YBTestRunnerNonSanOrAArch64Mac;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;
import java.util.Map;

import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertLessThan;
import static org.yb.AssertionWrappers.assertEquals;

/*
 * Verify that the freed memory allocated by a query is released to OS.
 * Skip verifying for override threshold for non Linux distribution as Mac doesn't use TCmalloc
 */
@RunWith(value = YBTestRunnerNonSanOrAArch64Mac.class)
public class TestPgMemoryGC extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgMemoryGC.class);

  private static final long PG_GC_THRESHOLD_BYTES = 10 * 1024 * 1024; // 10MB
  private static final long HIGH_PG_GC_THRESHOLD_BYTES = 500 * 1024 * 1024; // 500MB
  private static final long RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES = 2 * PG_GC_THRESHOLD_BYTES / 1024;
  private static final long RSS_ACCEPTED_DIFF_NO_GC_KILOBYTES = 50 * 1024; // 50MB
  private static final String PG_GC_OVERRIDE_FLAG = "pg_mem_tracker_tcmalloc_gc_release_bytes";
  private static final int N_QUERIES = 500;

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
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put(PG_GC_OVERRIDE_FLAG, Long.toString(PG_GC_THRESHOLD_BYTES));
    return flagMap;
  }

  @Test
  public void testWebserverMemoryGc() throws Exception {
    // take initial measurement with no data in /statements
    accessStatementsEndpoint();
    final int webserverPid = getWebserverPid();
    final long rssBefore = getRssForPid(webserverPid);

    // Add entries to /statements
    setupTestTable();
    try (Statement stmt = connection.createStatement()) {
      runManyUniqueQueries(stmt);
    }

    for (int i = 0; i < (TestUtils.isReleaseBuild() ? 50 : 5); i++) {
      accessStatementsEndpoint();

      final long rssAfter = getRssForPid(webserverPid);
      logRss("YSQL webserver", webserverPid, rssBefore, rssAfter);
      assertLessThan(
          "Freed bytes should be recycled when GC threshold is reached",
          rssAfter - rssBefore, RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES);
    }
  }


  @Test
  public void testPgMemoryGcOrderBy() throws Exception {
    // Prepare for the queries
    setupTestTable();

    // Clean the memories and start with fresh new connection
    reconnect();
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET work_mem='1GB'");

      ResultSet rs = stmt.executeQuery("SELECT pg_backend_pid()");
      rs.next();
      final int pgPid = rs.getInt("pg_backend_pid");
      final long rssBefore = getRssForPid(pgPid);

      /*
       * Run the query multiple times to verify
       * 1. freed RSS memory is recycled.
       * 2. no memory leak (if so, leaks can accumulate).
       * Only run once if on Debug build. as it is too slow to run the query multiple times.
       */
      for (int i = 0; i < (TestUtils.isReleaseBuild() ? 10 : 1); ++i) {
        runSimpleOrderBy(stmt);
        final long rssAfter = getRssForPid(pgPid);

        logMemoryInfo("PG connection", pgPid, rssBefore, rssAfter, stmt);
        assertLessThan(
            "Freed bytes should be recycled when GC threshold is reached",
            rssAfter - rssBefore, RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES);
      }
    }
  }

  private int getWebserverPid() throws Exception {
    Process p = Runtime.getRuntime().exec(
      new String[] {"/bin/sh", "-c", "pgrep -f 'YSQL webserver'"});
    BufferedReader input = new BufferedReader(new InputStreamReader(p.getInputStream()));
    String line = input.readLine();
    return Integer.parseInt(line);
  }

  private void accessStatementsEndpoint() throws Exception {
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      LOG.info("Accessing /statements");
      String[] curl = {"/bin/sh", "-c", String.format("curl -s http://%s:%d/statements > /dev/null",
                                            ts.getLocalhostIP(), ts.getPgsqlWebPort())};

      assertEquals("curl failed", 0, Runtime.getRuntime().exec(curl).waitFor());

      LOG.info("Successfully accessed");
    }
  }

  private void logMemoryInfo(
      String pName, int pgPid, long rssBefore, long rssAfter, Statement stmt) throws Exception {
    logRss(pName, pgPid, rssBefore, rssAfter);
    Row yb_heap_stats = getSingleRow(stmt, "SELECT * FROM yb_heap_stats()");
    LOG.info(yb_heap_stats.toString(true));
  }

  private void logRss(String pName, int pgPid, long rssBefore, long rssAfter) throws Exception {
    LOG.info("{} {} RSS before: {} kB, after: {} kB, diff: {} kB.",
             pName, pgPid, rssBefore, rssAfter, rssAfter - rssBefore);
  }

  private void reconnect() throws Exception {
    connection = getConnectionBuilder().connect();
  }

  private void setupTestTable() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET work_mem='1GB'");
      stmt.execute("CREATE TABLE tst (c1 INT PRIMARY KEY, c2 INT, c3 INT)");
      stmt.execute("INSERT INTO tst SELECT x, x+1, x+2 FROM GENERATE_SERIES(1, 1000000) x");
    }
  }

  private void runSimpleOrderBy(Statement stmt) throws Exception {
    // For quick sorting 1M rows, it takes around 78MB memory.
    stmt.executeQuery("SELECT * FROM tst ORDER BY c2");
  }

  private void runManyUniqueQueries(Statement stmt) throws Exception {
    // Generate 70 queries -
    String falseClause = " AND 1 = 0";
    String clauses = "";
    for (int i = 0; i < N_QUERIES; i++) {
      stmt.executeQuery(String.format("SELECT c1 FROM tst WHERE false %s", clauses));
      stmt.executeQuery(String.format("SELECT c2 FROM tst WHERE false %s", clauses));
      stmt.executeQuery(String.format("SELECT c3 FROM tst WHERE false %s", clauses));
      stmt.executeQuery(String.format("SELECT c1, c2 FROM tst WHERE false %s", clauses));
      stmt.executeQuery(String.format("SELECT c1, c3 FROM tst WHERE false %s", clauses));
      stmt.executeQuery(String.format("SELECT c2, c3 FROM tst WHERE false %s", clauses));
      stmt.executeQuery(String.format("SELECT c1, c2, c3 FROM tst WHERE false %s", clauses));
      clauses += falseClause;
    }
  }
}
