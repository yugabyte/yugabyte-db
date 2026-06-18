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
import static org.yb.AssertionWrappers.assertGreaterThanOrEqualTo;
import static org.yb.AssertionWrappers.assertLessThan;
import static org.yb.AssertionWrappers.assertLessThanOrEqualTo;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

import org.json.JSONArray;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

/**
 * Tests for the yb_prefetch_column_statistics GUC.
 *
 * On a cold backend, planning any query that scans a wide table reads
 * pg_statistic one column at a time: to cost the scan the planner estimates the
 * relation's full-row width, calling get_attavgwidth() for every column --
 * regardless of how many columns the query selects -- and each miss is an
 * individual one-row catalog read (one RPC per column). With the prefetch
 * enabled, get_relation_info() instead warms the catalog cache with all of the
 * relation's pg_statistic rows in a single batched list lookup, so the
 * subsequent per-column point lookups hit warm cache.
 *
 * These tests verify the observable contract of the feature: it issues
 * strictly fewer catalog read requests, the saving scales with the column
 * count, it is gated on the cost model, and -- crucially -- it changes neither
 * the chosen plan nor the cost estimates.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgPrefetchColumnStatistics extends BasePgSQLTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgPrefetchColumnStatistics.class);

  /** Number of non-key columns in the wide test table. */
  private static final int NUM_COLUMNS = 50;

  /**
   * Create a wide, fully-analyzed table so that ANALYZE records a pg_statistic
   * row for every column. With the cost model on, the planner reads every one
   * of those rows when it estimates the relation's full-row width to cost a
   * scan -- regardless of how many columns a query projects.
   */
  private void createWideTable(Statement stmt, String name) throws Exception {
    StringBuilder cols = new StringBuilder("id int PRIMARY KEY");
    StringBuilder vals = new StringBuilder("i");
    for (int i = 1; i <= NUM_COLUMNS; i++) {
      cols.append(", c").append(i).append(" int");
      vals.append(", i * ").append(i + 1);
    }
    stmt.execute("CREATE TABLE " + name + " (" + cols + ")");
    stmt.execute("INSERT INTO " + name + " SELECT " + vals +
                 " FROM generate_series(1, 1000) i");
    stmt.execute("ANALYZE " + name);
  }

  /**
   * Returns the number of catalog read requests reported by EXPLAIN's DIST
   * instrumentation when a brand-new (cold-cache) backend plans
   * "SELECT * FROM &lt;table&gt;" for the first time. YB's "Catalog Read
   * Requests" counter includes reads performed during planning, which is
   * exactly where the pg_statistic lookups happen.
   *
   * Each call opens a fresh connection, so the catalog cache starts cold.
   */
  private int catalogReadsForFirstPlan(String table, boolean costModel,
                                       boolean prefetch) throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET yb_enable_base_scans_cost_model = " +
                   (costModel ? "on" : "off"));
      stmt.execute("SET yb_prefetch_column_statistics = " +
                   (prefetch ? "on" : "off"));

      String query =
          "EXPLAIN (ANALYZE, DIST, FORMAT JSON) SELECT * FROM " + table;
      try (ResultSet rs = stmt.executeQuery(query)) {
        assertTrue("EXPLAIN returned no rows", rs.next());
        String json = rs.getString(1);
        LOG.info("costModel={} prefetch={} EXPLAIN: {}", costModel, prefetch,
                 json);
        return new JSONArray(json).getJSONObject(0)
            .getInt("Catalog Read Requests");
      }
    }
  }

  /** Collect the full EXPLAIN output (plan shape AND cost estimates) as text. */
  private String explainText(Statement stmt, String query) throws Exception {
    StringBuilder sb = new StringBuilder();
    try (ResultSet rs = stmt.executeQuery("EXPLAIN " + query)) {
      while (rs.next()) {
        sb.append(rs.getString(1)).append('\n');
      }
    }
    return sb.toString();
  }

  /**
   * The core of the feature: on a cold backend, prefetching collapses the burst
   * of one-RPC-per-column pg_statistic point lookups into a single batched list
   * lookup. The burst is the same for any query that scans the table -- the
   * cost model reads every column's stats to estimate the full row width,
   * regardless of projection -- so we use SELECT * here as a representative wide
   * read. Planning it on a wide analyzed table for the first time must
   * therefore issue strictly fewer catalog read requests with the prefetch on,
   * and the saving must scale with the column count.
   *
   * If the list lookup failed to populate the per-tuple catcache entries that
   * the later point lookups need, the prefetch-on path would do one extra list
   * read on top of the per-column reads and this test would fail -- so it is a
   * genuine check that the batching mechanism works.
   */
  @Test
  public void testPrefetchReducesCatalogReadRequests() throws Exception {
    // The catalog-read counts assume a cold per-connection catalog cache, which
    // the connection manager defeats by pooling warm physical backends.
    skipYsqlConnMgr(CATALOG_CACHE_MISS_NEED_UNIQUE_PHYSICAL_CONN,
                    isTestRunningWithConnectionManager());
    try (Statement stmt = connection.createStatement()) {
      createWideTable(stmt, "wide_reads");
    }

    int readsOff = catalogReadsForFirstPlan("wide_reads", true, false);
    int readsOn = catalogReadsForFirstPlan("wide_reads", true, true);
    LOG.info("Catalog read requests: prefetch off={}, on={}", readsOff, readsOn);

    assertLessThan(
        String.format("Prefetch should reduce catalog read requests "
                      + "(off=%d, on=%d)", readsOff, readsOn),
        readsOn, readsOff);

    // Off issues a per-column pg_statistic point read; on collapses them into a
    // single list read. The absolute saving tracks the version-specific
    // cold-cache baseline, so require only a conservative fraction of the column
    // count -- still well clear of off-by-one noise, which confirms batching.
    assertGreaterThanOrEqualTo(
        String.format("Expected the prefetch to collapse many per-column reads "
                      + "(off=%d, on=%d, columns=%d)",
                      readsOff, readsOn, NUM_COLUMNS),
        readsOff - readsOn, NUM_COLUMNS / 5);
  }

  /**
   * The prefetch is gated on the cost model: when it is off, get_attavgwidth()
   * returns immediately without reading pg_statistic and the prefetch is not
   * invoked, so toggling the GUC must not change the catalog read count. With
   * the cost model on the same toggle yields the large saving above. We assert
   * the saving is large with the cost model and negligible without it.
   */
  @Test
  public void testNoEffectWhenCostModelDisabled() throws Exception {
    // The catalog-read counts assume a cold per-connection catalog cache, which
    // the connection manager defeats by pooling warm physical backends.
    skipYsqlConnMgr(CATALOG_CACHE_MISS_NEED_UNIQUE_PHYSICAL_CONN,
                    isTestRunningWithConnectionManager());
    try (Statement stmt = connection.createStatement()) {
      createWideTable(stmt, "wide_gate");
    }

    int savingWithCostModel =
        catalogReadsForFirstPlan("wide_gate", true, false)
        - catalogReadsForFirstPlan("wide_gate", true, true);
    int savingWithoutCostModel =
        catalogReadsForFirstPlan("wide_gate", false, false)
        - catalogReadsForFirstPlan("wide_gate", false, true);
    LOG.info("Prefetch saving: withCostModel={}, withoutCostModel={}",
             savingWithCostModel, savingWithoutCostModel);

    assertGreaterThanOrEqualTo(
        "Prefetch should save many reads when the cost model is enabled",
        savingWithCostModel, NUM_COLUMNS / 5);
    assertLessThanOrEqualTo(
        String.format("Prefetch should have no real effect with the cost model "
                      + "off (saving=%d)", savingWithoutCostModel),
        savingWithoutCostModel, NUM_COLUMNS / 10);
  }

  /**
   * The feature must change only the number and shape of catalog reads, never
   * the resulting plan or cost estimates. We compare the full EXPLAIN output
   * (which includes the cost/row/width estimates) with the prefetch on vs off
   * and require it to be byte-identical for a variety of query shapes.
   */
  @Test
  public void testPlansAndCostsUnchanged() throws Exception {
    String[] queries = {
        "SELECT * FROM wide_plan",
        "SELECT * FROM wide_plan WHERE c1 > 100",
        "SELECT c1, c25, c50 FROM wide_plan WHERE c2 < 50",
        "SELECT c1, count(*) FROM wide_plan GROUP BY c1 ORDER BY c1",
    };
    try (Statement stmt = connection.createStatement()) {
      createWideTable(stmt, "wide_plan");
      stmt.execute("SET yb_enable_base_scans_cost_model = on");

      for (String query : queries) {
        stmt.execute("SET yb_prefetch_column_statistics = on");
        String planOn = explainText(stmt, query);
        stmt.execute("SET yb_prefetch_column_statistics = off");
        String planOff = explainText(stmt, query);
        assertEquals(
            "Plan and cost estimates must be identical with the prefetch on vs "
                + "off for query: " + query,
            planOff, planOn);
      }
    }
  }
}
