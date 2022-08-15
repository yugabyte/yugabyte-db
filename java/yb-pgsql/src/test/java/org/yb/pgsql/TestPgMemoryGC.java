package org.yb.pgsql;

import java.sql.Statement;
import java.util.Scanner;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

import static org.yb.AssertionWrappers.assertTrue;

/*
 * Verify that the freed memory allocated by a query is released to OS.
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgMemoryGC extends BasePgSQLTest {

  private static final long RSS_ACCEPTED_DIFF_AFTER_GC_BYTES = 10 * 1024;

  @Test
  public void testPgMemoryGcOrderBy() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Prepare for the queries
      stmt.execute("SET work_mem='1GB'");
      stmt.execute("CREATE TABLE tst (c1 INT PRIMARY KEY, c2 INT, c3 INT)");
      stmt.execute("INSERT INTO tst SELECT x, x+1, x+2 FROM GENERATE_SERIES(1, 1000000) x");

      final int pg_pid = getPgPid(stmt);
      final long rssBefore = getRssForPid(pg_pid);

      /*
       * Run the query multiple times to verify
       * 1. freed RSS memory is recycled.
       * 2. no memory leak (if so, leaks can accumulate).
       * Only run once if on Debug build. as it is too slow to run the query multiple times.
       */
      for (int i = 0; i < (TestUtils.isReleaseBuild() ? 10 : 1); ++i) {
        // For quick sorting 1M rows, it takes around 78MB memory.
        stmt.executeQuery("SELECT * FROM tst ORDER BY c2");
        final long rssAfter = getRssForPid(pg_pid);
        assertTrue("Freed bytes should be recycled when GC threshold is reached",
            (rssAfter - rssBefore) < RSS_ACCEPTED_DIFF_AFTER_GC_BYTES);
      }
    }
  }

  /*
   * A helper method to get current connection's PID.
   */
  private static int getPgPid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT pg_backend_pid()").getInt(0);
  }

  /*
   * A helper method to get the current RSS memory for a PID.
   */
  private static long getRssForPid(int pid) throws Exception {
    Process process = Runtime.getRuntime().exec(String.format("ps -p %d -o rss=", pid));
    try (Scanner scanner = new Scanner(process.getInputStream())) {
      return scanner.nextLong();
    }
  }
}
