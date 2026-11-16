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

import static org.yb.AssertionWrappers.*;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

/**
 * A scan with a storage filter must not use more memory the more times it is rescanned (#35431).
 *
 * Each test rescans a scan with a storage filter once per outer row, and compares how much peak
 * memory grows, every 500 rescans up to 4000, against a control query that rescans the same scan
 * without the filter.  Some scans use memory per rescan for reasons unrelated to the filter, which
 * the control cancels out.  Leaking the instantiated filter costs over 1kB per rescan, so 3500
 * extra rescans would add several MB.
 */
@RunWith(value=YBTestRunner.class)
public class TestPgPushdownRescanMemory extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgPushdownRescanMemory.class);

  private static final String PEAK_MEM_FIELD = "Peak Memory Usage:";
  private static final int RESCAN_STEP = 500;
  private static final int MAX_RESCANS = 4000;
  private static final long MAX_EXTRA_GROWTH_KB = 1500;

  @Before
  public void setUpTables() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t_outer (k int PRIMARY KEY, v int)");
      stmt.execute("CREATE TABLE t_inner (k int PRIMARY KEY, v int)");
      stmt.execute("CREATE TABLE t_idx (k int, v int, w int)");
      stmt.execute("CREATE INDEX t_idx_k_idx ON t_idx (k ASC) INCLUDE (v)");
      stmt.execute("CREATE TABLE t_small (v int)");
      stmt.execute(String.format(
          "INSERT INTO t_outer SELECT i, i %% 7 FROM generate_series(1, %d) i", MAX_RESCANS));
      stmt.execute(String.format(
          "INSERT INTO t_inner SELECT i, i %% 7 FROM generate_series(1, %d) i", MAX_RESCANS));
      stmt.execute(String.format(
          "INSERT INTO t_idx SELECT i, i %% 7, i %% 7 FROM generate_series(1, %d) i",
          MAX_RESCANS));
      stmt.execute("INSERT INTO t_small SELECT i FROM generate_series(0, 6) i");
      stmt.execute("ANALYZE t_outer, t_inner, t_idx, t_small");
      stmt.execute("SET yb_explain_hide_non_deterministic_fields = off");
    }
  }

  @Test
  public void testIndexScan() throws Exception {
    assertRescanMemoryBounded(
        "SELECT sum((SELECT count(*) FROM t_inner i WHERE i.k = o.k AND i.v >= 0))" +
        " FROM t_outer o WHERE o.k <= %d",
        "SELECT sum((SELECT count(*) FROM t_inner i WHERE i.k = o.k))" +
        " FROM t_outer o WHERE o.k <= %d",
        "Index Scan using t_inner_pkey on t_inner i", "Storage Filter: (v >= 0)");
  }

  @Test
  public void testIndexScanInNestedLoop() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET yb_bnl_batch_size = 1");
      stmt.execute("SET enable_material = off");
      stmt.execute("SET enable_memoize = off");
    }
    // The storage filter references the outer row.  Without the hint the planner may make t_inner
    // the outer side for some row counts.
    String hint = "/*+ Leading((o i)) NestLoop(o i) */ ";
    assertRescanMemoryBounded(
        hint + "SELECT count(*) FROM t_outer o JOIN t_inner i ON i.k = o.k AND i.v = o.v" +
        " WHERE o.k <= %d",
        hint + "SELECT count(*) FROM t_outer o JOIN t_inner i ON i.k = o.k WHERE o.k <= %d",
        "Index Scan using t_inner_pkey on t_inner i", "Nested Loop", "Storage Filter: (o.v = v)");
  }

  @Test
  public void testIndexOnlyScan() throws Exception {
    assertRescanMemoryBounded(
        "SELECT sum((SELECT count(*) FROM t_idx i WHERE i.k = o.k AND i.v >= 0))" +
        " FROM t_outer o WHERE o.k <= %d",
        "SELECT sum((SELECT count(*) FROM t_idx i WHERE i.k = o.k))" +
        " FROM t_outer o WHERE o.k <= %d",
        "Index Only Scan using t_idx_k_idx on t_idx i", "Storage Filter: (v >= 0)");
  }

  @Test
  public void testBitmapIndexScan() throws Exception {
    enableBitmapScan();
    assertRescanMemoryBounded(
        "SELECT sum((SELECT count(*) FROM t_idx i WHERE i.k = o.k AND i.v >= 0))" +
        " FROM t_outer o WHERE o.k <= %d",
        "SELECT sum((SELECT count(*) FROM t_idx i WHERE i.k = o.k))" +
        " FROM t_outer o WHERE o.k <= %d",
        "Bitmap Index Scan on t_idx_k_idx", "Storage Index Filter: (v >= 0)");
  }

  @Test
  public void testBitmapTableScan() throws Exception {
    enableBitmapScan();
    assertRescanMemoryBounded(
        "SELECT sum((SELECT count(*) FROM t_idx i WHERE i.k = o.k AND i.w >= 0))" +
        " FROM t_outer o WHERE o.k <= %d",
        "SELECT sum((SELECT count(*) FROM t_idx i WHERE i.k = o.k))" +
        " FROM t_outer o WHERE o.k <= %d",
        "YB Bitmap Table Scan on t_idx i", "Storage Filter: (w >= 0)");
  }

  @Test
  public void testSeqScan() throws Exception {
    // The control references the outer row too, so that it is rescanned rather than run once as
    // an InitPlan.
    assertRescanMemoryBounded(
        "SELECT sum((SELECT count(*) FROM t_small s WHERE s.v = o.v))" +
        " FROM t_outer o WHERE o.k <= %d",
        "SELECT sum((SELECT count(*) + o.v FROM t_small s))" +
        " FROM t_outer o WHERE o.k <= %d",
        "Seq Scan on t_small s", "Storage Filter: (v = o.v)");
  }

  private void enableBitmapScan() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("SET enable_indexscan = off");
      stmt.execute("SET enable_indexonlyscan = off");
      stmt.execute("SET yb_enable_bitmapscan = on");
    }
  }

  /**
   * query and control take the number of outer rows, and so of rescans, as their %d argument.
   * Both must rescan rescannedScan once per outer row; query must also have otherPlanLines.
   */
  private void assertRescanMemoryBounded(String query, String control, String rescannedScan,
                                         String... otherPlanLines) throws Exception {
    long[] queryPeaks = peakMemoryKbPerStep(query, rescannedScan, otherPlanLines);
    long[] controlPeaks = peakMemoryKbPerStep(control, rescannedScan);
    StringBuilder series = new StringBuilder();
    for (int i = 0; i < queryPeaks.length; i++) {
      series.append(String.format("\n  %d rescans: %d kB, control: %d kB",
                                  (i + 1) * RESCAN_STEP, queryPeaks[i], controlPeaks[i]));
    }
    LOG.info("Peak memory usage:" + series);

    for (int i = 1; i < queryPeaks.length; i++) {
      long extraGrowth = (queryPeaks[i] - queryPeaks[0]) - (controlPeaks[i] - controlPeaks[0]);
      assertLessThan(
          String.format("Peak memory grew %d kB more than the control from %d to %d rescans:%s",
                        extraGrowth, RESCAN_STEP, (i + 1) * RESCAN_STEP, series),
          extraGrowth, MAX_EXTRA_GROWTH_KB);
    }
  }

  private long[] peakMemoryKbPerStep(String query, String rescannedScan,
                                     String... expectedPlanLines) throws Exception {
    long[] peaks = new long[MAX_RESCANS / RESCAN_STEP];
    for (int i = 0; i < peaks.length; i++) {
      int rescans = (i + 1) * RESCAN_STEP;
      peaks[i] = peakMemoryKb(String.format(query, rescans), rescans, rescannedScan,
                              expectedPlanLines);
    }
    return peaks;
  }

  /**
   * The reported peak occasionally reads 0, or wraps around to an absurd value when memory drops
   * below what was in use at the start of the statement.  Retry those.
   */
  private long peakMemoryKb(String query, int rescans, String rescannedScan,
                            String... expectedPlanLines) throws Exception {
    long peak = -1;
    for (int attempt = 0; attempt < 5; attempt++) {
      List<String> plan = new ArrayList<>();
      try (Statement stmt = connection.createStatement();
           ResultSet rs = stmt.executeQuery("EXPLAIN (ANALYZE, COSTS OFF, SUMMARY) " + query)) {
        while (rs.next()) {
          plan.add(rs.getString(1));
        }
      }
      String planText = String.join("\n", plan);
      String loops = "loops=" + rescans + ")";
      assertTrue("Expected '" + rescannedScan + "' with " + loops + " in plan:\n" + planText,
                 plan.stream().anyMatch(row -> row.contains(rescannedScan) && row.contains(loops)));
      for (String line : expectedPlanLines) {
        assertTrue("Expected '" + line + "' in plan:\n" + planText, planText.contains(line));
      }

      peak = -1;
      for (String row : plan) {
        row = row.trim();
        if (row.startsWith(PEAK_MEM_FIELD)) {
          String[] tokens = row.split(" ");
          peak = Long.parseLong(tokens[tokens.length - 2]);
        }
      }
      assertNotEquals("No '" + PEAK_MEM_FIELD + "' in plan:\n" + planText, -1L, peak);
      if (peak > 0 && peak < 1000 * 1000)
        return peak;
      LOG.info("Discarding peak memory reading of " + peak + " kB");
    }
    fail("No plausible peak memory reading for: " + query + ", last was " + peak + " kB");
    return peak;
  }
}
