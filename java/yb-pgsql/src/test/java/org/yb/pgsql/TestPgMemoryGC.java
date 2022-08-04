package org.yb.pgsql;

import java.sql.Statement;
import java.util.Scanner;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

import static org.yb.AssertionWrappers.assertTrue;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgMemoryGC extends BasePgSQLTest {

  private static final long RSS_ACCEPTED_DIFF_AFTER_GC_BYTES = 10 * 1024;

  /*
   * Verify that the freed memory allocated by a query is released to OS.
   * This is specically for Linux platforms. MAC doesn't use TCmalloc.
   */
  @Test
  public void testMetrics() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET work_mem='1GB';");
      stmt.execute("CREATE TABLE tst (c1 INT PRIMARY KEY, c2 INT, c3 INT);");
      stmt.execute("INSERT INTO tst SELECT x, x+1, x+2 FROM GENERATE_SERIES(1, 1000000) x;");

      final int pg_pid = getPgPid(stmt);
      long rssBefore = getRssForPid(pg_pid);
      // For quick sorting 1M rows, it takes around 78MB memory.
      stmt.executeQuery("SELECT * FROM tst ORDER BY c2;");
      long rssAfter = getRssForPid(pg_pid);

      assertTrue("Freed bytes should be freed when GC threshold is reached",
          (rssAfter - rssBefore) < RSS_ACCEPTED_DIFF_AFTER_GC_BYTES);

      // Debug build is very slow and will time out.
      if (TestUtils.isReleaseBuild()) {
        // Make sure no memory leak, freed memory recycled even after multiple queries.
        for (int i = 0; i < 10; ++i) {
          stmt.executeQuery("SELECT * FROM tst ORDER BY c2;");
        }

        rssAfter = getRssForPid(pg_pid);
        assertTrue("Freed bytes should be freed when GC threshold is reached",
            (rssAfter - rssBefore) < RSS_ACCEPTED_DIFF_AFTER_GC_BYTES);
      }
    }
  }

  /*
   * A helper method to get current connection's PID.
   */
  private static int getPgPid(Statement stmt) throws Exception {
    return getSingleRow(stmt, "SELECT pg_backend_pid();").getInt(0);
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
