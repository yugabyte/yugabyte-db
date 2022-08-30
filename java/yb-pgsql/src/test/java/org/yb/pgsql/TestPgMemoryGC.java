package org.yb.pgsql;

import java.sql.Statement;
import java.util.Collections;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

import static org.yb.AssertionWrappers.assertTrue;

/*
 * Verify that the freed memory allocated by a query is released to OS.
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgMemoryGC extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgMemoryGC.class);

  private static final long RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES = 10 * 1024; // 10MB
  private static final long RSS_ACCEPTED_DIFF_NO_GC_KILOBYTES = 50 * 1024; // 50MB
  private static final long HIGH_PG_GC_THRESHOLD_BYTES = 500 * 1024 * 1024; // 500MB
  private static final long LOW_PG_GC_THRESHOLD_BYTES = 5 * 1024 * 1024; // 5MB
  private static final Map<String, String> EMPTY_ADD_MASTER_FLAGS = Collections.emptyMap();
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

  @Test
  public void testPgMemoryGcOrderBy() throws Exception {
    // Prepare for the queries
    setupTestTable();

    // Clean the memories and start with fresh new connection
    reconnect();
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET work_mem='1GB'");

      final int pg_pid = getPgBackendPid(connection);
      final long rssBefore = getRssForPid(pg_pid);

      /*
       * Run the query multiple times to verify
       * 1. freed RSS memory is recycled.
       * 2. no memory leak (if so, leaks can accumulate).
       * Only run once if on Debug build. as it is too slow to run the query multiple times.
       */
      for (int i = 0; i < (TestUtils.isReleaseBuild() ? 10 : 1); ++i) {
        runSimpleOrderBy(stmt);
        final long rssAfter = getRssForPid(pg_pid);
        assertTrue("Freed bytes should be recycled when GC threshold is reached",
            (rssAfter - rssBefore) < RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES);
      }
    }
  }

  @Test
  public void testPgMemoryGcThresholdOverride() throws Exception {
    // Skip verifying for override threshold for non Linux distribution as Mac doesn't use TCmalloc
    if (!TestUtils.IS_LINUX) {
      return;
    }

    // Set the GC threshold to a high value and verify the GC is not triggered.
    checkRssDiffForGcThreshold(HIGH_PG_GC_THRESHOLD_BYTES,
        "Freed bytes should be not recycled when GC threshold is high",
        (rss) -> rss > RSS_ACCEPTED_DIFF_NO_GC_KILOBYTES);

    // Set the GC threshold to a low value and verify the GC is triggered.
    checkRssDiffForGcThreshold(LOW_PG_GC_THRESHOLD_BYTES,
        "Freed bytes should be recycled when GC threshold is low",
        (rss) -> rss < RSS_ACCEPTED_DIFF_AFTER_GC_KILOBYTES);
  }

  interface Cond {
    public boolean evl(long input);
  }

  void checkRssDiffForGcThreshold(long gcThreshold, String msg, Cond cond) throws Exception {
    assertTrue(msg, cond.evl(getRssDiffOnNewClusterGc(gcThreshold)));
  }

  private long getRssDiffOnNewClusterGc(long threshold) throws Exception {
      restartClusterWithNewGc(threshold);
      setupTestTable();
      return getRssDiffForOrderBy();
  }

  private void restartClusterWithNewGc(long gcThreshold) throws Exception {
    restartClusterWithFlags(EMPTY_ADD_MASTER_FLAGS, Collections
        .singletonMap(PG_GC_OVERRIDE_FLAG, Long.toString(gcThreshold)));
  }

  private long getRssDiffForOrderBy() throws Exception {
    // start a new connection with fresh memory
    reconnect();
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET work_mem='100MB'");
      final int pgPid = getPgBackendPid(connection);
      final long rssBefore = getRssForPid(pgPid);

      // For quick sorting 1M rows, it takes around 78MB memory.
      runSimpleOrderBy(stmt);
      final long rssAfter = getRssForPid(pgPid);
      final long rssDiff = rssAfter - rssBefore;
      LOG.info("PG connection {} RSS before: {} bytes, after: {} bytes, diff: {} bytes.", pgPid,
          rssBefore, rssAfter, rssDiff);
      return rssDiff;
    }
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
