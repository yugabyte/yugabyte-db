package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonSanOrAArch64Mac;

import java.sql.Statement;
import java.util.Collections;
import java.util.Map;

import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertLessThan;

/*
 * Verify that the freed memory allocated by a query is released to OS.
 * Skip verifying for override threshold for non Linux distribution as Mac doesn't use TCmalloc
 */
@RunWith(value = YBTestRunnerNonSanOrAArch64Mac.class)
public class TestPgMemoryGC extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgMemoryGC.class);

  private static final long PG_GC_THRESHOLD_BYTES = 10 * 1024 * 1024; // 10MB
  private static final long HIGH_PG_GC_THRESHOLD_BYTES = 500 * 1024 * 1024; // 500MB
  private static final long RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES = PG_GC_THRESHOLD_BYTES * 2;
  private static final long RSS_ACCEPTED_DIFF_NO_GC_KILOBYTES = 50 * 1024; // 50MB
  private static final String PG_GC_OVERRIDE_FLAG = "pg_mem_tracker_tcmalloc_gc_release_bytes";

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
  public void testPgMemoryGcOrderBy() throws Exception {
    // Prepare for the queries
    setupTestTable();

    // Clean the memories and start with fresh new connection
    reconnect();
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET work_mem='1GB'");

      final int pgPid = getPgBackendPid(connection);
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

        logMemoryInfo(pgPid, rssBefore, rssAfter, stmt);
        assertLessThan(
            "Freed bytes should be recycled when GC threshold is reached",
            rssAfter - rssBefore, RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES);
      }
    }
  }

  private void logMemoryInfo(
      int pgPid, long rssBefore, long rssAfter, Statement stmt) throws Exception {
    LOG.info("PG connection {} RSS before: {} kB, after: {} kB, diff: {} kB.",
             pgPid, rssBefore, rssAfter, rssAfter - rssBefore);
    Row yb_heap_stats = getSingleRow(stmt, "SELECT * FROM yb_heap_stats()");
    LOG.info(yb_heap_stats.toString(true));
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
}
