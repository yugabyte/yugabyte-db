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

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.ThreadLocalRandom;

import org.apache.commons.lang3.tuple.MutablePair;
import org.apache.commons.lang3.tuple.Triple;
import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;
import static org.yb.AssertionWrappers.*;
import static org.yb.pgsql.ExplainAnalyzeUtils.getExplainQueryId;
import static org.yb.pgsql.ExplainAnalyzeUtils.getExplainPlanId;

/**
 * Run tests for Query Plan Managment (QPM).
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestYbQpm extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestYbQpm.class);
  private String getQpmByLastUsed = new String("SELECT qpm.dbid, qpm.userid, query, " +
    "qpm.queryid, planid, qpm.calls, last_used, hints, plan " +
    "FROM yb_pg_stat_plans qpm LEFT JOIN pg_stat_statements pgss " +
    "ON qpm.queryid=pgss.queryid ORDER BY last_used DESC /* __YB_STAT_PLANS_SKIP */");
  private String getQpmByQueryIdPlanId = new String("SELECT qpm.dbid, qpm.userid, query, " +
    "qpm.queryid, planid, qpm.calls, last_used, hints, plan " +
    "FROM yb_pg_stat_plans qpm LEFT JOIN pg_stat_statements pgss ON " +
    "qpm.queryid=pgss.queryid ORDER BY queryId, planId /* __YB_STAT_PLANS_SKIP */");
  private String countStar = new String("SELECT COUNT(*) qpmRowCount FROM " +
    "yb_pg_stat_plans /* __YB_STAT_PLANS_SKIP */");
  private String getQpmByQueryIdPlanIdNoQuery = new String("SELECT queryid, planid, " +
    "hints, plan " +
    "FROM yb_pg_stat_plans ORDER BY queryid, planid /* __YB_STAT_PLANS_SKIP */");

  private static long seed;
  private static Random rand;
  private static boolean debug;

  @BeforeClass
    public static void setUp() {
        seed = -1;
        if (seed == -1) {
          seed = ThreadLocalRandom.current().nextLong();
        }

        rand = new Random(seed);
        debug = false;
        LOG.info("Seed = " + seed + " , debug = " + debug);
    }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }
  public class QueryInfo implements Cloneable {
    private String queryText;
    private long queryId;
    private long planId;

      public QueryInfo(String queryText) {
        this.queryText = queryText;
        this.queryId = -1;
        this.planId = -1;
    }

    public String getQueryText() {
        return queryText;
    }

    long getQueryId() {
      return queryId;
    }

    long getPlanId() {
      return planId;
    }
  }

  public QueryInfo[] getSchemaInfo() {
    double pgSeed = rand.nextDouble() * 2 - 1;
    return new QueryInfo[] {
        new QueryInfo("CREATE SCHEMA qpm"),
        new QueryInfo("SET SEARCH_PATH TO qpm"),
        new QueryInfo("SELECT setseed(" + pgSeed + ")"),
        new QueryInfo("CREATE TABLE t0(pk0_0 INT, pk0_1 INT, charCol0 CHAR(4) NOT NULL, " +
            "unn0 INT UNIQUE NOT NULL, a0 INT, b0 INT, c0 INT, d0 INT, e0 INT, f0 INT, " +
            "PRIMARY KEY(pk0_0, pk0_1))"),
        new QueryInfo("CREATE INDEX a0_idx ON t0(a0 ASC)"),
        new QueryInfo("CREATE INDEX b0_idx ON t0(b0 DESC)"),
        new QueryInfo("INSERT INTO t0 SELECT gs1.val, gs2.val + 0, " +
            "lpad(to_hex(floor(random() * 9)::int), 4, '0'), gs1.val * power(10, 0) + " +
            "gs2.val * power(10, 1), CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END FROM GENERATE_SERIES(0, 9) gs1(val), " +
            "GENERATE_SERIES(0, 9) gs2(val)"),
        new QueryInfo("ANALYZE t0"),
        new QueryInfo("CREATE TABLE t1(pk1_0 INT, pk1_1 INT, charCol1 CHAR(4) NOT NULL, " +
            "unn1 INT UNIQUE NOT NULL, a1 INT, b1 INT, c1 INT, d1 INT, e1 INT, f1 INT, " +
            "PRIMARY KEY(pk1_0, pk1_1))"),
        new QueryInfo("CREATE INDEX a1_idx ON t1(a1 ASC)"),
        new QueryInfo("CREATE INDEX b1_idx ON t1(b1 DESC)"),
        new QueryInfo("INSERT INTO t1 SELECT gs1.val, gs2.val + 1, " +
            "lpad(to_hex(floor(random() * 9)::int), 4, '0'), gs1.val * power(10, 0) + " +
            "gs2.val * power(10, 1), CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END FROM GENERATE_SERIES(0, 9) gs1(val), " +
            "GENERATE_SERIES(0, 9) gs2(val)"),
        new QueryInfo("ANALYZE t1"),
        new QueryInfo("CREATE TABLE t2(pk2_0 INT, pk2_1 INT, charCol2 CHAR(4) NOT NULL, " +
            "unn2 INT UNIQUE NOT NULL, a2 INT, b2 INT, c2 INT, d2 INT, e2 INT, f2 INT, " +
            "PRIMARY KEY(pk2_0, pk2_1))"),
        new QueryInfo("CREATE INDEX a2_idx ON t2(a2 ASC)"),
        new QueryInfo("CREATE INDEX b2_idx ON t2(b2 DESC)"),
        new QueryInfo("INSERT INTO t2 SELECT gs1.val, gs2.val + 2, " +
            "lpad(to_hex(floor(random() * 9)::int), 4, '0'), gs1.val * power(10, 0) + " +
            "gs2.val * power(10, 1), CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END FROM GENERATE_SERIES(0, 9) gs1(val), " +
            "GENERATE_SERIES(0, 9) gs2(val)"),
        new QueryInfo("ANALYZE t2"),
        new QueryInfo("CREATE TABLE t3(pk3_0 INT, pk3_1 INT, charCol3 CHAR(4) NOT NULL, " +
            "unn3 INT UNIQUE NOT NULL, a3 INT, b3 INT, c3 INT, d3 INT, e3 INT, f3 INT, " +
            "PRIMARY KEY(pk3_0, pk3_1))"),
        new QueryInfo("CREATE INDEX a3_idx ON t3(a3 ASC)"),
        new QueryInfo("CREATE INDEX b3_idx ON t3(b3 DESC)"),
        new QueryInfo("INSERT INTO t3 SELECT gs1.val, gs2.val + 3, " +
            "lpad(to_hex(floor(random() * 9)::int), 4, '0'), gs1.val * power(10, 0) + " +
            "gs2.val * power(10, 1), CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END, CASE WHEN random() < 0.100000 THEN NULL ELSE " +
            "floor(random() * 99)::int END FROM GENERATE_SERIES(0, 9) gs1(val), " +
            "GENERATE_SERIES(0, 9) gs2(val)"),
        new QueryInfo("ANALYZE t3"),
        new QueryInfo("SET YB_ENABLE_OPTIMIZER_STATISTICS TO ON"),
        new QueryInfo("SET YB_ENABLE_BASE_SCANS_COST_MODEL TO ON")
      };
  }

  public void checkCountStar(Statement stmt, long expectedRowCount) throws Exception {
        try (ResultSet rs = stmt.executeQuery(countStar)) {
            assertTrue(rs.next());
            long qpmRowCount = rs.getLong("qpmRowCount");
            assertEquals(qpmRowCount, expectedRowCount);
            assertFalse(rs.next());
        }
    }

    public Triple<String, String, String> getHints(String queryText) {

        int start = queryText.indexOf("Leading");
        int end = queryText.indexOf("))") + 2;

        String leadingHint = queryText.substring(start, end);

        start = queryText.indexOf("HashJoin");

        String planJoinText = null;
        String joinHint = null;

        if (start == -1) {
            start = queryText.indexOf("MergeJoin");

            if (start == -1) {
                start = queryText.indexOf("NestLoop");
                assertNotEquals(start, -1);
                planJoinText = new String("Nested Loop");
                joinHint = "NestLoop";
            }
            else {
                planJoinText = new String("Merge Join");
                joinHint = "MergeJoin";
            }
        }
        else {
            planJoinText = new String("Hash Join");
            joinHint = "HashJoin";
        }

        return Triple.of(leadingHint, joinHint, planJoinText);
    }

  /**
   * testYbQpmSimpleClockLru
   *   (1) tests the simple clock LRU algorithm
   *   (2) tests insertion/removal, i.e. yb_pg_stat_plans_insert() and yb_pg_stat_plans_reset()
   *   (3) runs the stress where all results do not fit in the cache
   *
   * Make the cache size (N) < the number of query/plan pairs we expect,
   * which is the number of queries since the plans should be unique.
   * After running all of the queries, we expect the last N query/plan
   * pairs to be in QPM.
   *
   * Clock-based LRU should give the same results as true LRU for this
   * set of queries.
   *
   * @throws Exception
   */
  @Test
  public void testYbQpmSimpleClockLru() throws Exception {

    long cacheSize = 50;
    int threadCount = 10;

    /*
     * Only track top-level statements and no catalog queries.
     */
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_max_cache_size=" + cacheSize);
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_cache_replacement_algorithm=simple_clock_lru");
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_track=top");
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_track_catalog_queries=false");

    restartClusterWithFlags(Collections.emptyMap(), flagMap);

    QueryInfo[] schemaInfo = getSchemaInfo();
    executeStmts(schemaInfo, false, debug, null);

    Statement stmt = connection.createStatement();

    /*
     * Clear the QPM table.
     */
    stmt.execute("SELECT yb_pg_stat_plans_reset(null, null, null, null)");

    /*
     * Turn off BNL to make generated hint and plan text checking simpler.
     */
    stmt.execute("SET yb_enable_batchednl TO FALSE;");
    stmt.execute("SET yb_prefer_bnl TO FALSE;");

    /*
     * Execute all the queries and collect query/plan ids.
     */
    QueryInfo[] joinQueryInfo = getJoinQueryInfo();
    executeStmts(joinQueryInfo, true, debug, null);
    int numQueries = joinQueryInfo.length;

    /*
     * Iterate over the result set, which is sorted in descending order
     * of the time last used. Compare these entries to the executed query
     * array members where the array is accessed in reverse order.
     */
    try (ResultSet rs = stmt.executeQuery(getQpmByLastUsed)) {

        int numQpmRows = 0;
        while (rs.next()) {
            long qpmQueryId = rs.getLong("queryid");
            long qpmPlanId = rs.getLong("planid");
            String qpmHintText = rs.getString("hints");
            String qpmPlanText = rs.getString("plan");

            int lastIndex = numQueries - numQpmRows - 1;
            QueryInfo lastQueryInfo = joinQueryInfo[lastIndex];

            Triple<String, String, String> hints = getHints(lastQueryInfo.getQueryText());

            assertTrue(qpmHintText.indexOf(hints.getLeft()) != -1);
            assertTrue(qpmHintText.indexOf(hints.getMiddle()) != -1);
            assertTrue(qpmPlanText.indexOf(hints.getRight()) != -1);

            assertEquals(lastQueryInfo.getQueryId(), qpmQueryId);
            assertEquals(lastQueryInfo.getPlanId(), qpmPlanId);

            ++numQpmRows;
        }

        assertEquals(numQpmRows, cacheSize);
    }

    /*
     * Insert rows and test removal.
     */
    Statement stmt2 = connection.createStatement();
    stmt2.execute("SELECT yb_pg_stat_plans_reset(null, null, null, null)");

    /*
     * Insert 9 unique entries with synthetic data. The last 2 are identical so 'calls' should be 2.
     */
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 1, 1, 'hint text 1', 'plan 1 text', " +
        "'2025-01-01 13:00:00+00', '2025-01-02 13:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 1, 2, 'hint text 2', 'plan 2 text', " +
        "'2025-01-01 14:00:00+00', '2025-01-02 14:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(2, 1, 1, 3, 'hint text 3', 'plan 3 text', " +
        "'2025-01-01 15:00:00+00', '2025-01-02 15:00:00+00', 0.0, 0.0)");

    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 2, 4, 'hint text 4', 'plan 4 text', " +
        "'2025-01-01 16:00:00+00', '2025-01-02 16:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 2, 1, 'hint text 1', 'plan 1 text', " +
        "'2025-01-01 17:00:00+00', '2025-01-02 17:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(2, 1, 2, 5, 'hint text 5', 'plan 5 text', " +
        "'2025-01-01 18:00:00+00', '2025-01-02 18:00:00+00', 0.0, 0.0)");

    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 3, 3, 'hint text 3', 'plan 3 text', " +
        "'2025-01-01 19:00:00+00', '2025-01-02 19:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 3, 4, 'hint text 4', 'plan 4 text', " +
        "'2025-01-01 20:00:00+00', '2025-01-02 20:00:00+00', 0.0, 0.0)");

    /*
     * 2 duplicates
     */
    stmt2.execute("SELECT yb_pg_stat_plans_insert(2, 1, 3, 7, 'hint text 7', 'plan 7 text', " +
        "'2025-01-01 21:00:00+00', '2025-01-02 21:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(2, 1, 3, 7, 'hint text 7', 'plan 7 text', " +
        "'2025-01-01 21:00:00+00', '2025-01-02 21:00:00+00', 0.0, 0.0)");

    /*
     * Should have 9 rows in the QPM table.
     */
    checkCountStar(stmt2, 9);

    /*
     * Check 'calls' == 2 for the last entry.
     */
    try (ResultSet rs = stmt2.executeQuery("SELECT calls FROM yb_pg_stat_plans " +
            "WHERE dbid = 2 AND userid = 1 AND queryid = 3 AND planid = 7")) {
        assertTrue(rs.next());
        long calls = rs.getLong("calls");
        assertEquals(calls, 2);
        assertFalse(rs.next());
    }

    /*
     * Remove 1 entry.
     */
    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(1, 1, 1, 1) removeCnt")) {
        assertTrue(rs.next());
        long removeCnt = rs.getLong("removeCnt");
        assertEquals(removeCnt, 1);
        assertFalse(rs.next());
    }

    /*
     * Should have 8 rows in the QPM table.
     */
    checkCountStar(stmt2, 8);

    /*
     * Remove all entries with dbid == 2. Should be 3 of these.
     */
    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(2, null, null, null) " +
                                            "removeCnt")) {
        assertTrue(rs.next());
        long removeCnt = rs.getLong("removeCnt");
        assertEquals(removeCnt, 3);
        assertFalse(rs.next());
    }

    /*
     * Remove all entries with plan id == 4. Should remove 2 of these.
     */
    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(null, null, null, 4) " +
                                            "removeCnt")) {
        assertTrue(rs.next());
        long removeCnt = rs.getLong("removeCnt");
        assertEquals(removeCnt, 2);
        assertFalse(rs.next());
    }

    /*
     * Should have 3 rows in the QPM table.
     */
    checkCountStar(stmt2, 3);

    /*
     * Insert 3 new entries into dbid 3 with userid 2. Should have 6 rows afterwards.
     */
    String insert = new String("SELECT yb_pg_stat_plans_insert(dbid::int + 2, userid::int + 1, " +
                                "queryid, planid + 10, " +
                                "'hint text ' || (planid + 10)::text, " +
                                "'plan text ' || (planid+10)::text, first_used, " +
                                "last_used, 0.0, 0.0) inserted FROM yb_pg_stat_plans");

    try (ResultSet rs = stmt2.executeQuery(insert)) {

        int numInserts = 0;
        while (rs.next()) {
            boolean inserted = rs.getBoolean("inserted");
            assertTrue(inserted);
            ++numInserts;
        }

        assertEquals(numInserts, 3);
    }

    /*
     * Remove 3 entries based on plan id. Should remove 3.
     */
    String removeQuery = new String("SELECT yb_pg_stat_plans_reset(null, null, null, planid-10) " +
                                    "AS removeCnt " +
                                    "FROM yb_pg_stat_plans ORDER BY removeCnt");

    try (ResultSet rs = stmt2.executeQuery(removeQuery)) {

        int numRows = 0;
        while (rs.next()) {
            long removeCnt = rs.getLong("removeCnt");

            if (numRows < 3)
                assertEquals(removeCnt, 0);
            else
                assertEquals(removeCnt, 1);

            ++numRows;
        }

        assertEquals(numRows, 6);
    }

    /*
     * Should have 3 rows in the QPM table.
     */
    checkCountStar(stmt2, 3);

    /*
     * Now remove the 3 remaining entries one at a time.
     */
    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(3, 2, 1, 12) " +
                                            "removeCnt")) {
        assertTrue(rs.next());
        long removeCnt = rs.getLong("removeCnt");
        assertEquals(removeCnt, 1);
        assertFalse(rs.next());
    }

    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(3, 2, 2, 11) " +
                                            "removeCnt")) {
        assertTrue(rs.next());
        long removeCnt = rs.getLong("removeCnt");
        assertEquals(removeCnt, 1);
        assertFalse(rs.next());
    }

    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(3, 2, 3, 13) " +
                                            "removeCnt")) {
        assertTrue(rs.next());
        long removeCnt = rs.getLong("removeCnt");
        assertEquals(removeCnt, 1);
        assertFalse(rs.next());
    }

    /*
     * Should have no entries left.
     */
    checkCountStar(stmt2, 0);

    /*
     * Remove based on user id.
     */
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 1, 1, 'hint text 1', 'plan 1 text', " +
        "'2025-01-01 13:00:00+00', '2025-01-02 13:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 1, 1, 2, 'hint text 2', 'plan 2 text', " +
        "'2025-01-01 14:00:00+00', '2025-01-02 14:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 2, 1, 3, 'hint text 3', 'plan 3 text', " +
        "'2025-01-01 15:00:00+00', '2025-01-02 15:00:00+00', 0.0, 0.0)");
    stmt2.execute("SELECT yb_pg_stat_plans_insert(1, 2, 1, 3, 'hint text 3', 'plan 3 text', " +
        "'2025-01-01 15:00:00+00', '2025-01-02 15:00:00+00', 0.0, 0.0)");

    checkCountStar(stmt2, 3);

    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(null, 1, null, null) " +
                                            "removeCnt")) {
      assertTrue(rs.next());
      long removeCnt = rs.getLong("removeCnt");
      assertEquals(removeCnt, 2);
      assertFalse(rs.next());
    }

    checkCountStar(stmt2, 1);

    try (ResultSet rs = stmt2.executeQuery("SELECT yb_pg_stat_plans_reset(null, 2, null, null) " +
                                            "removeCnt")) {
      assertTrue(rs.next());
      long removeCnt = rs.getLong("removeCnt");
      assertEquals(removeCnt, 1);
      assertFalse(rs.next());
    }

    checkCountStar(stmt2, 0);

    ArrayList<QueryInfo> workingList = new ArrayList<>();
    Collections.addAll(workingList, joinQueryInfo);
    QueryInfo[] inQueryInfo = getInQueryInfo();
    Collections.addAll(workingList, inQueryInfo);
    QueryInfo[] allQueryInfo = workingList.toArray(new QueryInfo[0]);

    /*
     * Run the stress test with cache size 50 and all 500 join +
     * 10 in-list queries.
     */
    boolean hitException = false;
    try {
        executeStress(allQueryInfo, threadCount, cacheSize);
    }
    catch (Exception e) {
        hitException = true;
    }

    assertFalse(hitException);
  }

  /**
   * testYbQpmTrueLru
   *   (1) tests the true LRU replacement algorithm
   *   (2) checks 'calls', which is the number of times the plans are used
   *   (3) runs the stress where all results fit in the cache
   *
   * Run the query set twice with a cache size that equals the
   * number of query/plan pairs we expect. Then perform the same check
   * as in the clock LRU test but also make sure each plan is used twice.
   *
   * @throws Exception
   */
  @Test
  public void testYbQpmTrueLru() throws Exception {

    /*
     * All query/plan pairs should fit in the cache if the cache size is >= 500.
     */
    long cacheSize = 500;
    int threadCount = 10;

    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_max_cache_size=" + cacheSize);
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_cache_replacement_algorithm=true_lru");
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_track=top");
    appendToYsqlPgConf(flagMap, "yb_pg_stat_plans_track_catalog_queries=false");
    restartClusterWithFlags(Collections.emptyMap(), flagMap);

    QueryInfo[] schemaInfo = getSchemaInfo();
    executeStmts(schemaInfo, false, debug, null);

    Statement stmt = connection.createStatement();
    stmt.execute("SELECT yb_pg_stat_plans_reset(null, null, null, null)");

    QueryInfo[] joinQueryInfo = getJoinQueryInfo();

    /*
     * Execute each query twice with random writes and reads of the dump file.
     */
    executeStmts(joinQueryInfo, false, debug, rand);
    executeStmts(joinQueryInfo, true, debug, rand);
    int numQueries = joinQueryInfo.length;

    try (ResultSet rs = stmt.executeQuery(getQpmByLastUsed)) {

        int numQpmRows = 0;
        while (rs.next()) {
            long qpmQueryId = rs.getLong("queryid");
            long qpmPlanId = rs.getLong("planid");
            long calls = rs.getLong("calls");

            /*
             * Each plan should appear exactly twice.
             */
            assertEquals(calls, 2);

            int lastIndex = numQueries - numQpmRows - 1;
            QueryInfo lastQueryInfo = joinQueryInfo[lastIndex];

            assertEquals(lastQueryInfo.getQueryId(), qpmQueryId);
            assertEquals(lastQueryInfo.getPlanId(), qpmPlanId);

            ++numQpmRows;
        }

        assertEquals(numQpmRows, cacheSize);
    }

    /*
     * Run the stress test with 500 (unique) join queries and a cache size of 500.
     * All the queries should fit in the QPM table.
     */
    boolean hitException = false;
    try {
        executeStress(joinQueryInfo, threadCount, cacheSize);
    }
    catch (Exception e) {
        hitException = true;
    }

    assertFalse(hitException);
  }

  public static char randomLetterOrDigit(Random rand) {
    String chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    return chars.charAt(rand.nextInt(chars.length()));
  }

  String generateText(boolean isPlanText, long id, int len) {
    String textString;

    if (isPlanText)
        textString = new String("Plan ");
    else
        textString = new String("Hint ");

    textString = textString + id + " ";

    int padLen = len - textString.length();
    for (int i = 0; i < padLen; ++i) {
        if (i % 2 == 0)
        {
            char letterOrDigit = randomLetterOrDigit(rand);
              textString = textString + letterOrDigit;
        }
        else
              textString = textString + ' ';
    }

    return textString;
  }

  public enum ExpectedCompressionResult {
    NONE,
    COMPRESSED,
    TRUNCATED
  }

  public class CompressTestData {
    int id;
    public long databaseId;
    public long userId;
    public long queryId;
    public long planId;
    public int hintTextLen;
    public int planTextLen;
    public int maxHintTextLen;
    public int maxPlanTextLen;
    public String hintText;
    public String planText;
    public String query;
    public ExpectedCompressionResult expectedHintCompressionResult;
    public ExpectedCompressionResult expectedPlanCompressionResult;
    public boolean compressionOn;

    public CompressTestData(int id, long databaseId, long userId, long queryId, long planId,
                            int hintTextLen, int planTextLen, int maxHintTextLen,
                            int maxPlanTextLen, boolean compressionOn,
                            int compressionFactor) {

      this.id = id;
      this.databaseId = databaseId;
      this.userId = userId;
      this.queryId = queryId;
      this.planId = planId;
      this.maxHintTextLen = maxHintTextLen;
      this.maxPlanTextLen = maxPlanTextLen;

      this.hintText = generateText(false, planId, hintTextLen);
      this.planText = generateText(true, planId, planTextLen);

      this.hintTextLen = this.hintText.length();
      this.planTextLen = this.planText.length();

      this.compressionOn = compressionOn;

      query = new String("SELECT yb_pg_stat_plans_insert(" + databaseId + ", " +
                    + userId + ", " + + queryId + ", " +
                    planId + ", '" + hintText + "', '" + planText + "', " +
                    "'2025-01-01 14:00:00+00', '2025-01-02 14:00:00+00', 0.0, 0.0)");

      if (compressionOn) {
        if (hintTextLen < maxHintTextLen)
          expectedHintCompressionResult = ExpectedCompressionResult.NONE;
        else if (hintTextLen <= maxHintTextLen * compressionFactor)
          expectedHintCompressionResult = ExpectedCompressionResult.COMPRESSED;
        else
          expectedHintCompressionResult = ExpectedCompressionResult.TRUNCATED;

        if (planTextLen < maxPlanTextLen)
          expectedPlanCompressionResult = ExpectedCompressionResult.NONE;
        else if (planTextLen < maxPlanTextLen * compressionFactor)
          expectedPlanCompressionResult = ExpectedCompressionResult.COMPRESSED;
        else
          expectedPlanCompressionResult = ExpectedCompressionResult.TRUNCATED;
      }
      else {
        if (hintTextLen < maxHintTextLen)
          expectedHintCompressionResult = ExpectedCompressionResult.NONE;
        else
          expectedHintCompressionResult = ExpectedCompressionResult.TRUNCATED;

        if (planTextLen < maxPlanTextLen)
          expectedPlanCompressionResult = ExpectedCompressionResult.NONE;
        else
          expectedPlanCompressionResult = ExpectedCompressionResult.TRUNCATED;
      }

      if (debug) {
        LOG.info("Allocated instance (id " + id + ") : " + expectedHintCompressionResult +
            " , " + queryId + " , " + planId + " , " + "hint text (len = " + hintText.length() +
            ") " + hintText);

        LOG.info("Allocated instance (id " + id + ") : " + expectedPlanCompressionResult +
            " , " + queryId + " , " + planId + " , " + "plan text (len = " + planText.length() +
            ") " + planText);

        LOG.info("Query (id " + id + ") : " + query);
      }
    }

    public boolean verifyResult(String actualHintText, String actualPlanText) {
      boolean result = false;

      if (debug) {
        LOG.info("Hint text verification (id " + id + ") : " + expectedHintCompressionResult +
            " , " + queryId + " , " + planId);
        LOG.info("Hint text (len = " + hintText.length() + ") :        " +
            hintText);
        LOG.info("Actual hint text (len = " + actualHintText.length() + ") : " +
            actualHintText);
        LOG.info("Plan text (len = " + planText.length() + "       ) : " +
            planText);
        LOG.info("Actual plan text (len = " + actualPlanText.length() + ") : " +
            actualPlanText);
      }

      switch (expectedHintCompressionResult) {
        case NONE:
            result = hintText.equals(actualHintText);
          break;
        case COMPRESSED:
        case TRUNCATED:
            result = hintText.regionMatches(0, actualHintText, 0, maxHintTextLen);
          break;
        default:
            assertTrue(false);
          break;
      };

      if (debug && !result) {
        LOG.info("Hint text verification failed (id " + id + ") : " +
          expectedHintCompressionResult + " , " + queryId + " , " + planId + " , "
          + "hint text len = " + hintText.length() + " , actual hint text len = "
          + actualHintText.length());
      }

      result = false;
      switch (expectedPlanCompressionResult) {
        case NONE:
            result = planText.equals(actualPlanText);
          break;
        case COMPRESSED:
        case TRUNCATED:
            result = planText.regionMatches(0, actualPlanText, 0, maxPlanTextLen);
          break;
        default:
            assertTrue(false);
          break;
      };

      if (debug && !result) {
        LOG.info("Plan text verification failed (id " + id + ") : " +
          expectedPlanCompressionResult + " , " + queryId + " , " + planId +
          " , " + "plan text len = " + planText.length() +
          " , actual plan text len = " + actualPlanText.length());
      }

      return result;
    }
  }

  /**
   * testYbQpmText
   *   tests text handling, including compression/decompression/truncation
   *
   * @throws Exception
   */
  @Test
  public void testYbQpmText() throws Exception {

    int maxPlanTextLen = 4095;
    int maxHintTextLen = 2047;
    int numQueries = 1000;
    int idMax = 40;
    int expectedMinCompressionFactor = 2;
    int unexpectedCompressionFactor = 10;

    ArrayList<Integer> testHintStringLengths = new ArrayList<>();

    testHintStringLengths.add(maxHintTextLen - 2);
    testHintStringLengths.add(maxHintTextLen - 1);
    testHintStringLengths.add(maxHintTextLen);
    testHintStringLengths.add(maxHintTextLen + 1);
    testHintStringLengths.add(maxHintTextLen + 2);
    testHintStringLengths.add(maxHintTextLen * expectedMinCompressionFactor);
    testHintStringLengths.add(maxHintTextLen * unexpectedCompressionFactor);

    for (int i = 0 ; i < 20; ++i) {
      int min = maxHintTextLen + 1;
      int max = (int) Math.round(maxHintTextLen * expectedMinCompressionFactor);

      int value = (int) Math.round(rand.nextInt(max - min + 1) + min);
      testHintStringLengths.add(value);
    }

    ArrayList<Integer> testPlanStringLengths = new ArrayList<>();

    testPlanStringLengths.add(maxPlanTextLen - 2);
    testPlanStringLengths.add(maxPlanTextLen - 1);
    testPlanStringLengths.add(maxPlanTextLen);
    testPlanStringLengths.add(maxPlanTextLen + 1);
    testPlanStringLengths.add(maxPlanTextLen + 2);
    testPlanStringLengths.add(maxPlanTextLen * expectedMinCompressionFactor);
    testPlanStringLengths.add(maxPlanTextLen * unexpectedCompressionFactor);

    for (int i = 0 ; i < 20; ++i) {
      int min = maxPlanTextLen + 1;
      int max = (int) Math.round(maxPlanTextLen * expectedMinCompressionFactor);

      int value = (int) Math.round(rand.nextInt(max - min + 1) + min);
      testPlanStringLengths.add(value);
    }

    Statement stmt = connection.createStatement();

    stmt.execute("SET yb_pg_stat_plans_track = top");
    stmt.execute("SET yb_pg_stat_plans_track_catalog_queries = false");

    Map<MutablePair<Long, Long>, CompressTestData> map = new HashMap<>();
    MutablePair<Long, Long> key = new MutablePair<>(new Long(0), new Long(0));

    /*
     * Clear the QPM table.
     */
    stmt.execute("SELECT yb_pg_stat_plans_reset(null, null, null, null)");

    for (int i = 0; i < numQueries; ++i) {

      long queryId = 1 + rand.nextInt(idMax);
      long planId = 1 + rand.nextInt(idMax);
      key.setLeft(new Long(queryId));
      key.setRight(new Long(planId));

      if (debug)
        LOG.info("Next key : " + queryId + " , " + planId);

      CompressTestData compressData = map.get(key);

      if (compressData == null) {

        boolean compressionOn = (rand.nextInt(2) == 0 ? false : true);
        int hintIndex = rand.nextInt(testHintStringLengths.size());
        int planIndex = rand.nextInt(testPlanStringLengths.size());

        compressData = new CompressTestData(i, 1, 1, queryId, planId,
                                            testHintStringLengths.get(hintIndex),
                                            testPlanStringLengths.get(planIndex),
                                            maxHintTextLen, maxPlanTextLen, compressionOn,
                                            expectedMinCompressionFactor);

        MutablePair<Long, Long> newKey = new MutablePair<>(queryId, planId);
        map.put(newKey, compressData);

        if (debug)
          LOG.info(" ** New instance (" + i + ") : " + compressData.queryId + " , " +
            compressData.planId + " , " + "hint text len = " +
            compressData.hintTextLen + ", plan text len = " + compressData.planTextLen);
      }
      else if (debug) {
        LOG.info(" ** Matched instance (" + i + ") : " + compressData.queryId + " , " +
          compressData.planId + " , hint text len = " +
          compressData.hintTextLen + " , plan text len = " + compressData.planTextLen);
      }

      if (debug) {
        LOG.info("Use compression? : " + compressData.compressionOn);
        LOG.info("Executing query : " + compressData.query);
      }

      stmt.execute("SET yb_qpm_compress_text = " + compressData.compressionOn);
      stmt.execute(compressData.query);
    }

    try (ResultSet rs = stmt.executeQuery(getQpmByQueryIdPlanIdNoQuery)) {

      while (rs.next()) {
        long queryId = rs.getLong("queryid");
        long planId = rs.getLong("planid");
        String hintText = rs.getString("hints");
        String planText = rs.getString("plan");

        if (debug)
          LOG.info("QPM row : " + queryId + " , " + planId + " , " + "hint text len = " +
                    hintText.length() + ", plan text len = " + planText.length());

        key.setLeft(new Long(queryId));
        key.setRight(new Long(planId));

        CompressTestData compressData = map.get(key);
        assertTrue(compressData != null);

        if (debug)
          LOG.info(compressData.query);

        assertTrue(compressData.verifyResult(hintText, planText));
      }
    }
  }

  public void writeReadQpmDumpFile(Statement stmt, boolean log) throws Exception {

    if (log)
    {
      try (ResultSet rs = stmt.executeQuery(countStar)) {
          assertTrue(rs.next());
          long qpmRowCount = rs.getLong("qpmRowCount");
          LOG.info("Writing and reading QPM dump file : row count = " + qpmRowCount);
          assertFalse(rs.next());
      }
    }

    int writeResult = -1;
    try (ResultSet rs = stmt.executeQuery("SELECT yb_pg_stat_plans_write_file() writeResult")) {
        assertTrue(rs.next());
        writeResult = rs.getInt("writeResult");
        if (log)
          LOG.info("writeResult = " + writeResult);
        assertFalse(rs.next());
    }

    if (writeResult == 0) {
      stmt.execute("SELECT yb_pg_stat_plans_reset(null, null, null, null)");

      try (ResultSet rs = stmt.executeQuery("SELECT yb_pg_stat_plans_read_file() readResult")) {
          assertTrue(rs.next());
          int readResult = rs.getInt("readResult");
          if (log)
            LOG.info("readResult = " + readResult);
          assertEquals(readResult, 0);
          assertFalse(rs.next());
      }
    }
  }

  public void executeStmts(QueryInfo[] queryInfoArr, boolean populateIds, boolean log,
                            Random randLocal)
      throws Exception {

    Statement stmt = connection.createStatement();
    for (QueryInfo queryInfo : queryInfoArr) {

      if (randLocal != null && randLocal.nextInt(2) == 1) {
        writeReadQpmDumpFile(stmt, debug);
      }

      if (populateIds) {
          queryInfo.queryId = getExplainQueryId(stmt, queryInfo.queryText);
          queryInfo.planId = getExplainPlanId(stmt, queryInfo.queryText);
      }

      if (log)
        if (populateIds)
            LOG.info("Executing " + queryInfo.queryText + " , " + queryInfo.queryId + " , "
                + queryInfo.planId);
        else
            LOG.info("Executing " + queryInfo.queryText);

      boolean hitException = false;
      try {
          stmt.execute(queryInfo.queryText);
      } catch (Exception e) {
          LOG.info("Hit exception : " + e.getMessage());
      }

      assertFalse(hitException);
    }
  }

  /*
   * Shuffle the array of queries.
   */
  public QueryInfo[] shuffleQueryInfo(QueryInfo[] queryInfo, Random randLocal) {

    List<QueryInfo> list = Arrays.asList(queryInfo);
    Collections.shuffle(list, randLocal);

    QueryInfo[] shuffledQueryInfo = (QueryInfo[]) list.toArray();

    return shuffledQueryInfo;
  }

  /*
   * Create specified number of threads and execute all the queries in each thread.
   */
  public void executeStress(final QueryInfo[] queryInfo, int threadCount, long cacheSize)
                throws Exception {

    LOG.info("\nStarted stress test : thread count = " + threadCount + " , cache size = "
        + cacheSize + " , seed = " + seed);

    Thread[] threads = new Thread[threadCount];
    StringBuilder result = new StringBuilder();

    Statement stmt = connection.createStatement();

    /*
     * Clear the QPM table.
     */
    stmt.execute("SELECT yb_pg_stat_plans_reset(null, null, null, null)");

    /*
     * Create threads and execute all queries in each thread.
     */
    for (int i = 0; i < threadCount; i++) {

        int threadId = i;

        threads[i] = new Thread(() -> {

            boolean hitException = false;
            try {
                /*
                 * Clone the query array and shuffle it.
                 */
                QueryInfo[] queryInfoCopy = queryInfo.clone();

                /*
                 * Random() is not thread safe so make a local instance.
                 */
                Random randLocal = (seed == -1) ? (new Random()) : (new Random(seed));

                QueryInfo[] shuffledQueryInfo = shuffleQueryInfo(queryInfoCopy, randLocal);

                /*
                 * Run each query.
                 */
                executeStmts(shuffledQueryInfo, false, debug, null);

            } catch (Exception e) {
                hitException = true;
            }

            assertFalse(hitException);

            synchronized (result) {
                result.append("Thread ").append(threadId).append(" done\n");
            }
        });

        threads[i].start();
    }

    /*
     * Wait for all threads to finish
     */
    for (Thread t : threads) {
        t.join();
    }

    /*
     * Simple sanity check to make sure all threads are done.
     */
    assertEquals(threadCount, result.toString().split("\n").length);

    try (ResultSet rs = stmt.executeQuery(getQpmByQueryIdPlanId)) {

        long actualNumQpmRows = 0;
        while (rs.next()) {

            long calls = rs.getLong("calls");

            if (cacheSize >= queryInfo.length)
                /*
                 * Each plan should appear as many times as there are threads since
                 * the cache is big enough to hold them all.
                 */
                assertEquals(calls, threadCount);
            else
                /*
                 * All we can say is that the number of calls is bounded since an entry
                 * may get replaced and then inserted again. However, 'calls' cannot
                 * be more than the total number of executions.
                 */
                assertTrue(calls <= threadCount);

            ++actualNumQpmRows;
        }

        if (queryInfo.length > cacheSize)
            assertEquals(actualNumQpmRows, cacheSize);
    }

    LOG.info("\nFinished stress test : thread count = " + threadCount + " , cache size = "
                + cacheSize);
  }

  public QueryInfo[] getInQueryInfo() {
    return new QueryInfo[] {
            new QueryInfo("/* *//* __QPM Query 1 */ SELECT MAX(b0) AS max_1 FROM t0 WHERE " +
            "charCol0 IN ('0001', '0004', '0004', '0008', '000a', '0004', '0006', '0003', " +
            "'0005', '0008', '0aad', '0267', '0cc1', '012f', '0240', '01ed', '0111', '016f', " +
            "'01a5', '0d8d', '02c2', '0a28', '08af', '01e5', '0b52', '026a', '021a', '0184', " +
            "'01b0', '0179', '0874', '013d', '012f', '01ec', '07ff', '0750', '01db', '09c1', " +
            "'0206', '02f4', '031e', '0294', '0c94', '021a', '01d2', '0d0a', '0aba', '0128', " +
            "'0222', '01a2', '02cb', '08f5', '0209', '04b8', '0181', '0232', '01c3', '0178', " +
            "'0e27', '0de9', '022f', '066a', '0137', '0110', '021a', '01c5', '0105', '0a64', " +
            "'0263', '0b8d', '08cc', '0114', '01bc', '0430', '0148', '01b6', '024b', '0f4f', " +
            "'0249', '0695', '01bb', '0b8e', '0117', '0223', '0264', '016a', '05c1', '01c6', " +
            "'01cc', '0210', '0177', '0eb3', '0233', '0984', '0663', '03ce', '0117', '020c', " +
            "'0973', '0d80', '0181', '016b', '0197', '0186', '01e3', '023a', '01a5', '0ae4', " +
            "'01c9', '010d', '0a33', '0159', '01b0', '01c2', '01b0', '0105', '0134', '014b', " +
            "'0481', '01b2', '0211', '014a', '04b9', '09bd', '0227', '0303', '0205', '0176', " +
            "'0929', '0c14', '0f49', '059b', '019c', '014f', '020b', '01be', '01f3', '0f0f', " +
            "'01f6', '01fc', '012e', '013f', '0dce', '0b7a', '0266', '0134', '0261', '0c99', " +
            "'0139', '046f', '001a', '025d', '03bd', '077a', '0b77', '025b', '0aa2', '03a1', " +
            "'015f', '0187', '0154', '01d6', '0220', '0748', '06b9', '0149', '0250', '0182', " +
            "'0913', '00d0', '010a', '0d5e', '0148', '020d', '0201', '013f', '01f0', '08eb', " +
            "'06cd', '017e', '016d', '0ec6', '0ad8', '0a2c', '01c0', '018d', '048b', '0270', " +
            "'0230', '0fd4', '04da', '0233', '0113', '04af', '0bb6', '0161', '0328', '084a', " +
            "'01f8', '0f2f', '022d', '017b', '0720', '00c8', '05a8', '0143', '0145', '0182', " +
            "'0130', '0182', '0701', '05cc', '020c', '037f', '014f', '0089', '0188', '0257', " +
            "'0d5c', '00a9', '0177', '0b25', '0261', '0114', '01f6', '021f', '0c73', '0171', " +
            "'0141', '06f2', '0d22', '0209', '0151', '010d', '011f', '0a03', '0184', '0238', " +
            "'0122', '0185', '01a8', '01ac', '03c8', '0111', '0235', '0182', '0145', '01b2', " +
            "'0185', '0596', '09f3', '014a', '0e1d', '020d', '01a6', '0f81', '01fd', '0411', " +
            "'0186', '020a', '0225', '01e1', '01a7', '014c', '024c', '0253', '023b', '0177', " +
            "'0aef', '020a', '0894', '0124', '019c', '0245', '0c0e', '0165', '0147', '02dd', " +
            "'013a', '0211', '01da', '011d', '0251', '01d1', '0d45', '014e', '01b7', '0254', " +
            "'0288', '023e', '01a8', '0160', '0210', '0148', '0240', '019a', '0115', '0bae', " +
            "'024b', '013e', '01db', '02e4', '0131', '0152', '01b6', '0f3b', '0738', '025f', " +
            "'03fd', '01b1', '03ae', '021c', '015e', '0233', '083c', '0269', '010a', '03b2', " +
            "'020c', '01d2', '070e', '0747', '0222', '0ea8', '0268', '07f9', '0388', '021f', " +
            "'01f5', '0170', '01f0', '01cd', '025c', '015d', '0c39', '01b7', '0eee', '0a98', " +
            "'0211', '0405', '0a65', '0932', '0161', '01e3', '0209', '01d6', '041e', '0ba2', " +
            "'0157', '0f36', '0a78', '0135', '0a6a', '0bf2', '01ab', '0f81', '0354', '0257', " +
            "'013a', '0ed2', '01c9', '01f7', '01bf', '0d04', '0207', '0172', '0224', '0161', " +
            "'0325', '0154', '0bd1', '0071', '07b0', '028b', '010e', '0569', '0040', '0fa0', " +
            "'0f95', '09c7', '0dbd', '0a70', '0954', '0151', '0d72', '01f4', '07ac', '0168', " +
            "'0b8d', '01ce', '0f3b', '0f82', '01c8', '0132', '022b', '0125', '0234', '0100', " +
            "'014e', '01b7', '012a', '01d1', '01b4', '0101', '02e1', '0116', '07b4', '020c', " +
            "'0688', '025f', '0849', '01b5', '0248', '06c4', '01e6', '0d44', '0186', '01f5', " +
            "'0225', '014e', '0901', '0d5c', '046c', '07d4', '01dd', '06ac', '0262', '0200', " +
            "'01d6', '02b4', '09f4', '0a7c', '0215', '01b5', '01ac', '0182', '019c', '0176', " +
            "'011a', '033c', '01ab', '0c85', '005e', '09a4', '0ec3', '0195', '013a', '024b', " +
            "'0178', '0f6a', '01f2', '01df', '01cc', '0259', '022e', '011a', '01a4', '0cdb', " +
            "'061b', '021c', '0c34', '072f', '025b', '01b3', '0128', '0d8b', '0fcc', '0178', " +
            "'0125', '0160', '01ea', '0159', '018a', '0bac', '0125', '0cdd', '0b42', '016a', " +
            "'01c7', '0214', '022e', '024d', '0206', '09a0', '01df', '020a', '0225', '01e7', " +
            "'0ea2', '0130', '0ee1', '0235', '0244', '015c', '087e', '0248', '0127', '01ad', " +
            "'049c', '022b', '01c7', '09c1', '0ac6', '01d2', '0fd0', '0804', '01ab', '0cef', " +
            "'01ea', '06d4', '01e9', '0238', '0f1d', '0127', '0138', '0134', '025d', '01fd', " +
            "'0e67', '0236', '0d7c', '0ec6', '0254', '0065', '012b', '0234', '0179', '015f', " +
            "'022a', '0e52', '0c24', '020e', '01af', '0251', '0016', '019a', '04e6', '017d', " +
            "'0219', '023e', '0c78', '026a', '01f8', '06c7', '0198', '0d5b', '0224', '013b', " +
            "'01c4', '0233', '0264', '0c86', '014a', '011c', '0188', '012f', '053a', '0ddc', " +
            "'0184', '0147', '0f53', '04c6', '0785', '01c1', '022c', '021c', '015f', '0229', " +
            "'0e84', '021a', '010a', '01e3', '024b', '0db8', '02a2', '0793', '01f6', '022f', " +
            "'01c8', '07b6', '0215', '0149', '01f2', '0074', '0165', '01f6', '01fe', '0164', " +
            "'0d4d', '0109', '0b05', '0708', '0a84', '0243', '0192', '025e', '0263', '0186', " +
            "'0fef', '0151', '0120', '0204', '019e', '0177', '0e15', '0cd1', '0142', '015f', " +
            "'001d', '059b', '0b58', '0cb5', '0193', '0266', '0230', '01d0', '0237', '0265', " +
            "'0105', '0244', '0226', '0260', '0100', '0166', '010a', '023a', '0257', '0239', " +
            "'0064', '01cd', '0933', '0171', '0649', '0151', '0239', '01e2', '011d', '0001', " +
            "'012c', '016d', '0b94', '024a', '0833', '0136', '0815', '01aa', '015e', '0196', " +
            "'01ec', '020e', '01e8', '0710', '0c77', '078d', '0f86', '010b', '0150', '0226', " +
            "'0255', '0973', '01ab', '01d4', '0129', '0160', '0227', '023c', '0118', '01a7', " +
            "'020b', '08ec', '010c', '01a4', '0079', '0220', '00df', '0e5d', '010e', '0e31', " +
            "'0262', '0239', '01c9', '01d8', '09f1', '0192', '003f', '0195', '07e8', '0209', " +
            "'0219', '0986', '0b25', '0080', '05e5', '09cd', '01c9', '0245', '07e7', '0248', " +
            "'0cb5', '0251', '0199', '0401', '0ed8', '0123', '0944', '025c', '01e5', '0075', " +
            "'022e', '01f0', '0339', '01bb', '01a3', '0149', '01fb', '0e0a', '014b', '022e', " +
            "'01cc', '021a', '08e5', '0748', '01d3', '0200', '019f', '0231', '0763', '0651', " +
            "'0249', '011f', '0273', '017d', '0174', '0231', '085b', '01aa', '01c4', '01b6', " +
            "'011a', '021b', '0186', '01b6', '06d2', '0206', '0582', '02fe', '0149', '0138', " +
            "'01fd', '010e', '017a', '0a97', '0c1f', '01a1', '0129', '01b6', '0a4b', '007c', " +
            "'0193', '065e', '0162', '024d', '0529', '09eb', '025d', '0a1e', '01e5', '01ce', " +
            "'0117', '01b6', '01ca', '01e9', '021f', '0c42', '0581', '0458', '0f26', '0157', " +
            "'012d', '05f8', '0246', '052a', '0f71', '0c2c', '046f', '0dc6', '0117', '0b94', " +
            "'01af', '01f8', '01f8', '0111', '0223', '0ab7', '0126', '0116', '01a5', '0259', " +
            "'015f', '0a03', '01d0', '0d27', '01ba', '0226', '011c', '0267', '023c', '0ea5', " +
            "'0386', '0906', '0182', '0185', '016d', '0197', '0250', '01c8', '0c19', '0265', " +
            "'0d83', '01f5', '0f84', '0e4e', '050c', '0218', '09e5', '0852', '0249', '06b0', " +
            "'016a', '01f7', '09ba', '0492', '0fda', '019d', '00b8', '0107', '014e', '0b0c', " +
            "'0186', '08ef', '0140', '01be', '0146', '0156', '092e', '0b07', '0a27', '0103', " +
            "'0241', '01f9', '0130', '075b', '01e7', '0159', '0286', '0157', '0fc8', '01b1', " +
            "'01b4', '025a', '018b', '0156', '0190', '02fa', '023b', '0f9e', '012b', '0272', " +
            "'0218', '016c', '0d38', '09fc', '011f', '014b', '01a6', '01e5', '0ced', '00a6', " +
            "'0588', '071e', '0244', '020c', '01c3', '0206', '02bc', '0188', '01c1', '01fd', " +
            "'0d8c', '0197', '0c10', '0084', '00d6', '0d1d', '064f', '0b70', '0234', '0134', " +
            "'050c', '0251', '0568', '0235', '012e', '015a', '0bff', '0291', '0a22', '0c22', " +
            "'01ca', '05e7', '0232', '046b', '0254', '012f', '05a1', '0139', '0145', '015a', " +
            "'0229', '01c4', '01ee', '0050', '0205', '01b1', '0140', '015f', '0266', '0235', " +
            "'06c4', '0551', '0154', '0248', '016c', '011d', '0953', '022b', '0d97', '01f1', " +
            "'0239', '0d48', '0113', '0b3b', '090e', '0179', '004f', '01fc', '0171', '0164', " +
            "'01cf', '0c5c', '0182', '01f1', '0658', '017c', '064e', '021f', '01d9', '0166', " +
            "'01e8', '013b', '0958', '01ed', '01c2', '025d', '06f1', '024c', '01bb', '0912', " +
            "'0541', '00ed', '0850', '0184', '04a3', '01aa', '0bf7', '0169', '01f2', '0217', " +
            "'09ab', '0258', '0264', '01af', '0f9c', '0931', '0158', '068b', '0251', '01b0', " +
            "'01e6', '0165', '067c', '01c9', '014c', '0dde', '01d3', '01ab', '018f', '00f4', " +
            "'0a16', '0c90', '0af1', '0190', '0692', '0be8', '031e', '01cc', '011d', '08e3', " +
            "'021b', '0246', '0678', '0977', '06f6', '0192', '051c', '0165', '0660', '0d39', " +
            "'01b1', '07e0', '015a', '020b', '0178', '0f7f', '0fd2', '015d', '01e9', '0163', " +
            "'03ed', '0140', '0ed5', '012e', '0cde', '0429', '020d', '0246', '0106', '0224', " +
            "'021e', '01bc', '0237', '0ff1', '0a25', '00a6', '022d', '0176', '025e', '01d2', " +
            "'017b', '0163', '0cba', '0110', '0183', '024b', '01ff', '09f4', '0142', '01d1', " +
            "'0167', '01c9', '0024', '0130', '0181', '003d', '01d5', '01b4', '0422', '011b', " +
            "'0f77', '059c', '0ce5', '0203', '0191', '0eeb', '01d4', '0a1e', '03be', '0263', " +
            "'0171', '0107', '019c', '0f65', '0302', '0110', '0743', '0efb', '015c', '0e9f', " +
            "'0628', '0164', '0bb2', '051b', '048d', '0cf2', '03c1', '0da5', '0f6b', '0192', " +
            "'025f', '01d1', '0add', '04ce', '01a3', '0253', '0128', '0e10', '01b1', '049e', " +
            "'0bbd', '09d3', '01a8', '0824', '0d14', '0f91', '0e18', '01e8', '0116', '01b6', " +
            "'01b1', '0bad', '0827', '01c2', '01bc', '016b', '0122', '0770', '05b4', '01b2', " +
            "'021f', '0708', '01bd', '00f0', '01c9', '0b4e', '017e', '0173', '017e', '01dc', " +
            "'003d', '0201', '0d6d', '01d2', '0170', '0107', '0101', '018d', '0232', '01ce', " +
            "'0a5f', '0755', '0f18', '0e90', '010a', '0147', '0638', '0192', '01be', '0ed8', " +
            "'01b1', '0158', '0217', '010e', '0372', '071d', '0845', '016f', '0173', '01ba', " +
            "'01de', '0263', '0146', '01fd', '0664', '0082', '010c', '0246', '0a4e', '01e6', " +
            "'0cb3', '059c', '0106', '062c', '0f14', '0196', '052e', '0206', '0115', '0137', " +
            "'01ba', '01a4', '0175', '0c61', '05e3', '0191', '0240', '019f', '0dc8', '08cc', " +
            "'00f4', '0177', '0104', '09d1', '0197', '0177', '018c', '0232', '0148', '0117', " +
            "'0f90', '0134', '07ed', '01b1', '05a4', '0ae2', '03b2', '0351', '0140', '01dd', " +
            "'025b', '014f', '0123', '0c6b', '01d1', '082e', '013e', '0263', '0c17', '01e6', " +
            "'054d', '02f8', '0119', '0d3c', '053d', '0169', '0148', '036e', '01f3', '0052', " +
            "'08c7', '011f', '010b', '0e57', '018b', '0259', '0173', '0248', '0127', '0439', " +
            "'011c', '0769')"),
        new QueryInfo("/* *//* __QPM Query 2 */ SELECT MAX(pk3_0) AS max_2 FROM t3 WHERE " +
            "charCol3 IN ('0009', '0009', '0002', '0007', '0008', '0008', '0009', '0002', " +
            "'0006', '000a', '0227', '0bea', '0192', '0ba3', '0161', '0183', '01dd', '021b', " +
            "'014c', '0f7c', '089b', '025d', '0986', '01b5', '0912', '0939', '0152', '004a', " +
            "'02e5', '0cb1', '02e4', '0150', '0123', '021a', '0219', '010a', '00b3', '0174', " +
            "'09a6', '0231', '0109', '02c4', '0255', '015f', '0c5e', '014d', '01e0', '0233', " +
            "'0141', '0228', '0205', '0e70', '0165', '0129', '0a61', '0258', '015e', '0218', " +
            "'0693', '020c', '0169', '090d', '01a7', '01f4', '01b2', '0ed1', '0859', '0192', " +
            "'0198', '018d', '00a6', '01b3', '07b1', '04cd', '0240', '013f', '013b', '04af', " +
            "'01d9', '0d4f', '0136', '0158', '0e05', '012d', '0151', '01c1', '023c', '01ca', " +
            "'04cc', '01cd', '0a17', '01c6', '05d5', '0eb8', '0483', '01e9', '0267', '0681', " +
            "'0d60', '00b9', '01c6', '013d', '01e2', '0145', '0ace', '068d', '0193', '0126', " +
            "'0fd4', '0137', '01f9', '0256', '01e5', '0f1e', '0243', '0ed5', '0151', '0228', " +
            "'05e3', '020c', '01c2', '047b', '0ab2', '015f', '0542', '0556', '09b3', '0254', " +
            "'01ba', '021e', '0898', '087b', '0853', '0146', '005a', '022e', '0c72', '0209', " +
            "'01f5', '0602', '090a', '0752', '0144', '08f8', '0174', '0dfa', '0203', '0525', " +
            "'0201', '03f0', '0ec6', '012d', '01a7', '014a', '00d1', '0a91', '08ba', '0917', " +
            "'0589', '0133', '04aa', '0206', '0265', '086d', '038c', '013a', '005c', '0732', " +
            "'025a', '044a', '0759', '0ae9', '024e', '012c', '0240', '06b5', '0f48', '01d6', " +
            "'0d06', '0222', '0215', '0297', '024d', '0139', '01f3', '0231', '0186', '0ed3', " +
            "'01e4', '0100', '01f6', '01a7', '018f', '0253', '0260', '0151', '0156', '068e', " +
            "'013c', '0103', '0180', '0219', '0182', '0166', '0566', '0264', '0776', '01bb', " +
            "'0109', '0d52', '017d', '0c93', '0155', '061e', '01d2', '0bb2', '014a', '011f', " +
            "'01ce', '0106', '0d0c', '0213', '01ff', '01dc', '0188', '0252', '018c', '0147', " +
            "'0176', '0342', '0182', '081a', '0184', '0b99', '0198', '01a0', '03b5', '09c9', " +
            "'014e', '08d2', '0498', '0167', '04b6', '0120', '015a', '015b', '022a', '0169', " +
            "'04eb', '076e', '013b', '0bac', '0253', '05cf', '0066', '010b', '01ab', '03db', " +
            "'0220', '0a6d', '0c8c', '019a', '0f7f', '0cb2', '0fcd', '017d', '014e', '0fcb', " +
            "'0173', '0124', '0162', '0dcd', '0af2', '01d2', '0260', '0230', '0257', '013f', " +
            "'004c', '07af', '0c34', '019f', '0ffb', '0e84', '0911', '01f0', '0f27', '0110', " +
            "'0239', '0125', '0c91', '097f', '0229', '0250', '0209', '0e3d', '024d', '0128', " +
            "'0138', '0144', '0192', '0635', '0959', '09a0', '022f', '0e97', '0252', '018b', " +
            "'0137', '017c', '0171', '0173', '01cc', '0195', '012e', '053c', '07ad', '049b', " +
            "'0e79', '0a76', '0127', '021a', '0168', '0d48', '06be', '0202', '0df7', '01d7', " +
            "'096e', '01ca', '09db', '0241', '01f4', '04cb', '0185', '006b', '01de', '0110', " +
            "'0265', '0cbb', '014d', '013b', '021b', '0231', '0d45', '0f7b', '033f', '0f25', " +
            "'022d', '020c', '0210', '0db1', '01e1', '0104', '098a', '01d6', '01bf', '04a5', " +
            "'0211', '015f', '03cb', '0260', '09a6', '01bc', '01c5', '06df', '0fd2', '0260', " +
            "'0070', '01b5', '06f4', '0125', '0133', '07be', '09eb', '0724', '0213', '0d72', " +
            "'0156', '0162', '01a1', '0208', '020a', '0245', '0103', '01a8', '04dc', '012e', " +
            "'0b61', '0182', '01f9', '0d7f', '0173', '01c1', '0f3e', '0f2a', '0136', '01e9', " +
            "'01cf', '0ae9', '0173', '0118', '0247', '0f49', '0cee', '0330', '0269', '017c', " +
            "'0a5f', '032b', '01ef', '0201', '0b87', '012d', '0c44', '0112', '0b1e', '0181', " +
            "'0ffe', '0250', '010e', '0110', '0d52', '0137', '0252', '0194', '0200', '0a78', " +
            "'0110', '010f', '01c0', '0117', '002c', '01d6', '0139', '0a18', '0152', '0d21', " +
            "'0b42', '0b16', '0a45', '01f2', '03e9', '0131', '0fb5', '0e89', '014e', '020f', " +
            "'0170', '0a3f', '061e', '008c', '0214', '0e3b', '01fd', '0194', '0229', '01ed', " +
            "'0337', '0147', '0154', '057a', '01eb', '01c8', '0375', '01bd', '0ff2', '0354', " +
            "'08fa', '0a26', '019b', '036d', '0260', '0bbd', '01bf', '0116', '05e9', '01ca', " +
            "'01e2', '01f6', '018c', '0185', '0154', '0240', '01f6', '0573', '0130', '083e', " +
            "'01bf', '01cb', '0df8', '0131', '04f0', '023f', '0101', '01c9', '011d', '0269', " +
            "'0d07', '0516', '0e78', '0b9b', '01fd', '01b2', '0579', '01dc', '0a8c', '01d3', " +
            "'0260', '01ef', '01f7', '01e4', '024e', '006f', '0a6d', '041f', '0f63', '0081', " +
            "'075e', '04ed', '0f80', '08ff', '0c01', '099e', '065c', '03f3', '01bf', '024e', " +
            "'04bc', '016d', '0266', '01c2', '01ad', '06ee', '01e1', '04ed', '0a73', '0210', " +
            "'078a', '03b4', '0230', '0dad', '018c', '01ed', '01fa', '04e6', '016b', '089e', " +
            "'0249', '01a9', '018c', '0d32', '0115', '0260', '0671', '0f3a', '0db3', '0084', " +
            "'01f7', '01be', '017b', '0109', '0bb6', '0263', '018c', '01d7', '0145', '0125', " +
            "'0850', '01ce', '0140', '01df', '039f', '0645', '0213', '0222', '0f26', '01ca', " +
            "'0227', '0191', '00fb', '0249', '031c', '0207', '0184', '0144', '0ccf', '0dcb', " +
            "'06f7', '01f5', '0182', '01c2', '024c', '0191', '0697', '0247', '0e4c', '011d', " +
            "'0bc5', '0264', '0130', '0137', '0249', '016c', '025c', '01cd', '0216', '0185', " +
            "'0224', '017b', '0f5f', '01f3', '020c', '0831', '0bc8', '01a7', '0e7c', '01ec', " +
            "'093f', '0926', '0c06', '01e6', '0038', '01dd', '0827', '0909', '024c', '0154', " +
            "'0143', '014b', '019a', '0d8b', '0101', '0246', '0170', '0e79', '0035', '021c', " +
            "'0786', '0d0f', '0267', '02ef', '0334', '01c2', '0219', '016f', '0235', '0130', " +
            "'044c', '0235', '09fd', '016c', '0142', '013a', '0203', '05c6', '01bb', '0124', " +
            "'0402', '02ce', '0265', '013e', '0c44', '076a', '0199', '001b', '0120', '0223', " +
            "'0203', '018d', '0218', '0191', '01ac', '0162', '0187', '055d', '0dfa', '01f4', " +
            "'0ebb', '01f1', '0388', '0ab6', '0e7c', '012c', '045e', '011b', '011d', '0b78', " +
            "'019b', '01fb', '0209', '0aba', '0168', '0a81', '0194', '0c33', '0d9f', '0e26', " +
            "'017b', '077f', '0fea', '01fd', '015d', '011b', '03c7', '0ccc', '0269', '0c20', " +
            "'012a', '0105', '0105', '02f6', '08b3', '064b', '01a4', '0108', '000e', '024a', " +
            "'021e', '0d8c', '0168', '0114', '017f', '0145', '0ab4', '018a', '017b', '0051', " +
            "'021d', '020e', '024d', '0269', '0d5f', '0066', '016b', '0257', '01fd', '013f', " +
            "'024b', '0d13', '01a0', '0b82', '0120', '0311', '0a7d', '0f07', '01a0', '0180', " +
            "'0184', '073c', '0ef9', '0197', '019a', '0ed6', '06f7', '0cc9', '0ca7', '07ab', " +
            "'0be2', '01ea', '0184', '0230', '0149', '01a8', '0255', '0248', '0226', '0784', " +
            "'01e1', '0906', '0e29', '025b', '06b6', '0211', '0a18', '020f', '0f7b', '023a', " +
            "'01ee', '01bc', '0198', '0159', '0264', '0141', '0263', '0f26', '0dcb', '01a7', " +
            "'01c0', '01e6', '023b', '0680', '01a0', '0131', '0176', '04f1', '060e', '0111', " +
            "'002a', '01da', '0246', '0df9', '0fa6', '0260', '02a5', '01ff', '0130', '0ba6', " +
            "'01b2', '0164', '0216', '01ef', '070b', '0e06', '01b9', '01bb', '0264', '01ca', " +
            "'01bf', '0185', '01dd', '01d1', '013e', '0c38', '054b', '0242', '0254', '0109', " +
            "'0b14', '0b28', '017a', '0262', '01b7', '0247', '018b', '018c', '0051', '09ff', " +
            "'0248', '0132', '019f', '0269', '012b', '025b', '0159', '0c87', '0227', '0148', " +
            "'0c9b', '018e', '0adc', '0a3f', '0c3a', '0926', '013a', '0d96', '006c', '021f', " +
            "'0134', '013f', '0141', '010d', '0b47', '022f', '004a', '01a4', '011d', '0149', " +
            "'0560', '01f5', '012f', '0ce6', '0927', '04b1', '06bd', '0207', '0278', '0220', " +
            "'024c', '0267', '0516', '032f', '0ca5', '0695', '01ce', '0162', '079f', '0ae5', " +
            "'064a', '0163', '0398', '0252', '01fa', '028e', '01f9', '0170', '023f', '0230', " +
            "'0c9c', '03e6', '0174', '0125', '0140', '0eeb', '0184', '012d', '0151', '01b0', " +
            "'0105', '024a', '0635', '0139', '0198', '01ff', '014b', '03ca', '0680', '0c9b', " +
            "'0234', '0215', '091c', '0d67', '01ed', '08f2', '0f18', '013e', '01d6', '01a6', " +
            "'0617', '055d', '0fef', '01c2', '038e', '04ce', '01da', '01a7', '021d', '015c', " +
            "'022f', '0148', '001c', '0189', '0eec', '014d', '0163', '00d8', '0c9f', '01ef', " +
            "'0166', '0137', '0128', '0204', '0de5', '08c9', '0107', '093b', '0d8e', '01e0', " +
            "'0a4f', '0196', '004e', '01e8', '012b', '024d', '01a9', '012a', '021e', '014f', " +
            "'013f', '0197', '0247', '0910', '0131', '0897', '02fb', '075c', '035c', '0206', " +
            "'0199', '016f', '016f', '0377', '0233', '0159', '025b', '054c', '0193', '01eb', " +
            "'03c9', '00e9', '01f1', '0d2d', '0ce3', '01eb', '002c', '0224', '087a', '0cad', " +
            "'0b0a', '0204', '0258', '0142', '0ddf', '01a0', '0152', '017d', '0b5f', '0208', " +
            "'0175', '0235', '0232', '0d5b', '0205', '0fd7', '0120', '0264', '01da', '0a02', " +
            "'0dca', '010b', '01b5', '020f', '0159', '03a4', '0200', '011b', '01d4', '0230', " +
            "'0107', '052c', '010a', '04f5', '0131', '0240', '0200', '0152', '0166', '0d8f', " +
            "'0707', '0225', '0140', '0545', '0466', '01b6', '079c', '0127', '01a4', '01f2', " +
            "'021a', '0130', '01f8', '0247', '016b', '01aa', '01de', '023c', '01b2', '0114', " +
            "'0129', '01de', '02eb', '0eb3', '0182', '0215', '0235', '079a', '010a', '0105', " +
            "'0228', '02c8', '0174', '01e6', '006b', '02f3', '065c', '01de', '03aa', '0119', " +
            "'0ff3', '0156', '0103', '01c1', '0221', '09b0', '0c3f', '01f5', '0fff', '01bd', " +
            "'0245', '018a', '050a', '0128', '0576', '0722', '02bf', '01a0', '0b46', '0127', " +
            "'0144', '02aa', '01f0', '06ea', '01d6', '0ab2', '0199', '01d2', '086c', '0127', " +
            "'01fe', '011c', '0ba7', '0187', '0116', '0029', '0105', '01ca', '079c', '02b9', " +
            "'01c8', '0520', '0b31', '01be', '0118', '011f', '06a8', '0140', '0dcc', '0167', " +
            "'0117', '013b', '0113', '0896', '020d', '017f', '0292', '019a', '01fa', '0267', " +
            "'0169', '019b', '09c2', '0252', '0231', '0138', '0089', '08d3', '0219', '01a4', " +
            "'0195', '03cd', '092e', '0f51', '01c9', '0aff', '01cf', '02eb', '03c9', '01ff', " +
            "'09d1', '013c', '0790', '021e', '0177', '0135', '016f', '0b41', '01d8', '0fdb', " +
            "'0b95', '013f', '09bf', '0c19', '017f', '012a', '01ad', '0254', '017c', '01f4', " +
            "'010e', '0141', '0e55', '016a', '0daf', '0122', '0162', '035d', '0182', '00e5', " +
            "'0438', '0176', '0164', '01c4', '014e', '0964', '01d6', '005c', '021f', '08fc', " +
            "'0136', '01a0', '0387', '01d0', '0aa8', '01c6', '0199', '012d', '0190', '0ade', " +
            "'034f', '0481', '0d01', '0186', '0775', '0181', '01d8', '0846', '012b', '0251', " +
            "'0126', '0201', '078a', '01ad', '0244', '014e', '06f7', '01db', '017d', '0187', " +
            "'0164', '0619', '0f06', '004d', '0e2c', '023d', '0233', '0d16', '01de', '01df', " +
            "'019f', '0196', '060d', '0228', '067d', '01a5', '0b30', '07fb', '0f07', '0045', " +
            "'014c', '01fa')"),
        new QueryInfo("/* *//* __QPM Query 3 */ SELECT MAX(unn1) AS max_3 FROM t1 WHERE " +
            "charCol1 IN ('000a', '0005', '0004', '0001', '0004', '0006', '0001', '0003', " +
            "'0004', '0009', '017c', '011d', '0511', '06e3', '01c9', '0147', '041d', '0a72', " +
            "'01fc', '01b6', '0486', '0643', '015a', '022c', '0a4e', '017e', '01de', '015d', " +
            "'01d0', '0185', '011e', '0c9b', '01d1', '03e8', '09a8', '006a', '01ab', '0117', " +
            "'015b', '0981', '0210', '020b', '0264', '03c5', '01f1', '019f', '022d', '0139', " +
            "'0244', '0403', '08e2', '025a', '0a45', '075f', '01d0', '0950', '0148', '0111', " +
            "'0f7a', '0246', '0803', '02be', '018a', '0146', '0921', '020e', '0ebe', '0218', " +
            "'0f5a', '0109', '0163', '0401', '00a4', '01c1', '014b', '0177', '0bb5', '0269', " +
            "'01aa', '0e82', '0904', '0104', '0256', '0147', '020a', '019a', '014e', '0886', " +
            "'0240', '022b', '0d04', '029f', '04da', '028d', '01e0', '010c', '012c', '01ac', " +
            "'010e', '0af4', '0202', '02c8', '0223', '02a3', '0231', '0003', '01ea', '0d0d', " +
            "'0641', '0137', '02fd', '0704', '07db', '0108', '024e', '0e87', '0199', '0133', " +
            "'03d6', '020b', '0260', '0eb5', '0e3b', '0106', '0165', '01de', '0104', '01c5', " +
            "'01ee', '0151', '0116', '037a', '0144', '0260', '0472', '0a32', '0208', '013a', " +
            "'0c74', '06b2', '0ee4', '004c', '0c12', '0045', '0193', '014b', '019a', '01a7', " +
            "'0cf6', '0367', '0625', '015d', '0222', '0dc8', '03c1', '0409', '0bc4', '0bfd', " +
            "'0d88', '01dc', '0053', '0261', '0f04', '01d0', '0203', '0704', '0112', '084c', " +
            "'0193', '0243', '024a', '012d', '0c67', '0123', '01de', '0195', '0d3f', '0576', " +
            "'09a4', '039d', '013b', '018e', '0145', '01cb', '0602', '01d9', '0139', '0e8b', " +
            "'0a79', '05aa', '08d3', '014d', '0688', '0213', '0215', '015a', '014d', '025d', " +
            "'0d87', '0589', '01bf', '01ad', '010f', '02a6', '013c', '02af', '02e9', '01aa', " +
            "'035d', '0c40', '0143', '023f', '01e6', '01a2', '012c', '025b', '02be', '016c', " +
            "'023b', '0216', '03a9', '0ab6', '0a08', '0216', '022c', '022a', '0231', '01cf', " +
            "'01b7', '0b56', '0130', '0120', '0913', '017a', '0202', '0247', '0189', '0243', " +
            "'01a7', '0994', '01e4', '017f', '03ad', '0266', '0aa3', '0117', '023a', '010d', " +
            "'05a8', '0119', '0ce0', '0770', '0469', '0197', '022c', '0147', '01d2', '08b8', " +
            "'01f7', '0366', '0dd6', '01e9', '0159', '01f6', '0424', '0136', '01f2', '0126', " +
            "'01cc', '0c9d', '01ef', '012c', '0268', '0f8c', '025d', '06eb', '0208', '0269', " +
            "'013d', '019a', '0200', '0446', '0263', '024c', '0692', '0850', '0139', '01e6', " +
            "'0260', '0ebd', '0971', '04a0', '01f3', '0062', '0768', '0253', '01e8', '01c8', " +
            "'0186', '020a', '0153', '0111', '0f6e', '0048', '0ee9', '023c', '0155', '022f', " +
            "'0fc6', '0262', '07c4', '0508', '0173', '0139', '0cd9', '0ff3', '0153', '0252', " +
            "'01ba', '0b6b', '01ed', '010f', '01a3', '0258', '0243', '0149', '0151', '0116', " +
            "'0269', '05ac', '0119', '0081', '0187', '09a8', '0183', '04c7', '0b84', '0230', " +
            "'0375', '01d4', '01f7', '0291', '04bf', '01d8', '0201', '0138', '0122', '0a85', " +
            "'01be', '0eea', '0219', '07bd', '01e2', '0120', '0b3b', '079a', '064d', '0103', " +
            "'00b2', '059d', '023f', '09c2', '0e6d', '0243', '0988', '0ab1', '011e', '0497', " +
            "'030f', '0214', '0756', '0123', '0188', '0c25', '01ec', '0995', '0a27', '0198', " +
            "'00fc', '0230', '012e', '0181', '01dd', '0228', '0178', '012f', '01d0', '0235', " +
            "'0f36', '025d', '0344', '024d', '0d7d', '08bd', '09fe', '01a1', '0181', '0e42', " +
            "'0445', '0142', '0241', '01d5', '017f', '01e4', '0525', '01f6', '019a', '0b92', " +
            "'0192', '021f', '0267', '013e', '00b3', '0231', '01f1', '0c80', '021c', '00a1', " +
            "'011d', '0122', '0214', '0244', '0497', '0188', '0593', '000b', '0213', '013d', " +
            "'0ad4', '0004', '01c0', '01bd', '0653', '01e4', '0196', '01d5', '0c24', '0123', " +
            "'0128', '08c7', '01c4', '06b8', '019b', '016a', '0150', '0bf3', '0b43', '015f', " +
            "'0248', '0c0b', '014c', '01d7', '0157', '0200', '01d2', '021b', '0dc2', '0186', " +
            "'020e', '064b', '0215', '09bd', '0194', '01a7', '0212', '0161', '0abd', '0133', " +
            "'0121', '0294', '01f6', '024f', '0225', '0c9b', '01e3', '01d3', '0f1d', '0bd8', " +
            "'021d', '03c2', '01a5', '0d23', '0225', '016e', '07e3', '0261', '0f1c', '0188', " +
            "'01fc', '01d5', '0ea5', '010a', '0205', '018d', '025a', '0f95', '030e', '0106', " +
            "'026a', '0dea', '0103', '0244', '0a32', '0111', '0213', '0245', '0bba', '012c', " +
            "'0b5b', '01d0', '0136', '09ee', '0190', '015e', '0347', '0d23', '01a2', '0195', " +
            "'025e', '0229', '0150', '0257', '023a', '0177', '0fb8', '01a9', '0213', '03e7', " +
            "'01c6', '01e6', '01ed', '01a5', '0150', '0174', '0515', '021d', '0121', '0d94', " +
            "'017e', '0bed', '0e17', '0057', '09ed', '018e', '01a3', '01aa', '0223', '023b', " +
            "'0427', '013d', '022b', '011a', '0092', '01a3', '0208', '0101', '0f20', '0616', " +
            "'010e', '01e6', '03b0', '04a7', '04a0', '024f', '0745', '09a6', '0124', '0816', " +
            "'0896', '0943', '0242', '010e', '026a', '024e', '017b', '013a', '0266', '0a64', " +
            "'01a5', '0190', '0e8a', '0187', '0114', '01ab', '010e', '0108', '010c', '0014', " +
            "'0ba7', '01ff', '0af8', '0228', '0200', '0235', '0156', '0223', '0929', '0204', " +
            "'021f', '01eb', '0483', '07c8', '0f2f', '0147', '02d2', '0247', '0e5d', '0124', " +
            "'0db9', '01a0', '0224', '053c', '0970', '0240', '01ec', '03a7', '0c87', '0774', " +
            "'01ac', '0134', '0144', '0c46', '0217', '003e', '0153', '00d1', '021d', '021f', " +
            "'0fdf', '01cc', '01be', '023c', '0112', '051a', '012b', '012b', '0167', '0162', " +
            "'03b3', '0abf', '0d2c', '0167', '01f2', '019e', '01ee', '0149', '01f9', '01d5', " +
            "'011a', '01f9', '0a84', '0bda', '01b7', '0205', '0118', '013c', '01aa', '0bb2', " +
            "'0c18', '070a', '0174', '01f9', '0209', '05d1', '024f', '07d4', '01e2', '0e5b', " +
            "'0244', '0c93', '0deb', '014c', '0117', '01f1', '0141', '0915', '0226', '0145', " +
            "'015a', '0237', '0d4e', '0238', '01c3', '0aeb', '0226', '052d', '01ee', '0226', " +
            "'0190', '0150', '0b9b', '01df', '0fab', '0356', '01f7', '025f', '023b', '0225', " +
            "'0ddd', '01e3', '01bf', '01ab', '01cc', '0268', '01e8', '0241', '0184', '0b1b', " +
            "'0f35', '0c6c', '0c9e', '0f82', '024c', '0a10', '0da2', '02a1', '02fe', '01ae', " +
            "'00c6', '01df', '025f', '01e3', '0117', '01b8', '017f', '0b9f', '0241', '019a', " +
            "'01c0', '0e6e', '0dd6', '0143', '0117', '0a5c', '0cb1', '01dd', '01a9', '0124', " +
            "'061b', '025e', '0d8f', '0147', '0188', '0c28', '01d1', '0f4e', '0202', '035a', " +
            "'0231', '05aa', '0b51', '057d', '0b3f', '0f4c', '0174', '0166', '0252', '019b', " +
            "'022a', '01b4', '02f2', '0230', '017e', '0b91', '0162', '01a6', '015f', '01d0', " +
            "'0de4', '0120', '0120', '046f', '0dff', '01b3', '0b01', '0123', '01e7', '022c', " +
            "'06bc', '0d42', '01e3', '01fb', '01d5', '0e52', '0229', '0ba4', '023a', '0238', " +
            "'0142', '0167', '011c', '0229', '06de', '0260', '0bb1', '0b9a', '015c', '016e', " +
            "'0182', '08cf', '0c4a', '017b', '001d', '0590', '0c25', '0288', '016a', '01ca', " +
            "'0b1d', '020b', '0187', '01aa', '05a0', '019f', '0134', '0a41', '0f92', '0845', " +
            "'017e', '014d', '0da8', '021f', '01ca', '015b', '01ef', '0668', '013f', '024f', " +
            "'0108', '0dad', '01d6', '056a', '012b', '01e3', '01da', '0585', '06fc', '03f2', " +
            "'01c2', '010d', '012e', '012a', '08a1', '0238', '012e', '04b0', '0ea3', '0152', " +
            "'01cc', '021a', '0efd', '023e', '01dd', '0edb', '066a', '0629', '0164', '042d', " +
            "'0b7e', '01d7', '0179', '01f5', '017c', '0158', '01f6', '022d', '0e09', '032a', " +
            "'013a', '04af', '022c', '0538', '06f5', '01a4', '0130', '0a08', '00f9', '0105', " +
            "'0245', '0014', '0dfc', '0116', '0557', '096a', '01f1', '01c8', '0104', '0139', " +
            "'024d', '01d4', '0ef0', '09dc', '0aa2', '0146', '0afa', '026a', '0834', '0be6', " +
            "'021d', '0113', '09ae', '024e', '0d5e', '0208', '0177', '01eb', '0311', '0436', " +
            "'07d6', '02c3', '0159', '023b', '0119', '01f2', '022e', '0241', '0d62', '0191', " +
            "'0d59', '0969', '01e5', '01d3', '0228', '0107', '0d2d', '0198', '023d', '0117', " +
            "'01ea', '01c8', '0252', '0226', '0191', '014b', '01b2', '076b', '0267', '0245', " +
            "'01c2', '08ac', '0602', '0163', '01db', '01c6', '0222', '0213', '01bd', '06b0', " +
            "'0265', '0158', '08d0', '0139', '018f', '024f', '0036', '01dd', '01ec', '01d5', " +
            "'0244', '06a2', '0238', '082f', '0e77', '0135', '01dd', '010f', '011b', '021e', " +
            "'08b7', '014b', '0b8a', '0262', '022a', '01af', '039b', '0e9e', '0261', '027f', " +
            "'0190', '092c', '01bb', '010e', '012e', '0131', '0118', '01c0', '0cc5', '05c3', " +
            "'0134', '04d8', '016b', '0d7b', '01a9', '0162', '0130', '0223', '02a8', '0e95', " +
            "'01ad', '0145', '019e', '0165', '01ff', '0365', '0109', '025e', '01f1', '0c4f', " +
            "'0e08', '020c', '016c', '0153', '0a27', '024a', '01fc', '0423', '020d', '01fb', " +
            "'01f7', '06a8', '00d6', '0131', '057e', '0177', '0a2c', '0aa6', '05da', '02c5', " +
            "'0eb5', '01c8', '0145', '0101', '0215', '0195', '0cf1', '011f', '0162', '015c', " +
            "'01e4', '01b1', '0144', '0254', '0f3f', '024f', '0466', '025f', '061a', '0fa6', " +
            "'0138', '022f', '0170', '0c0d', '0b11', '000e', '070c', '096b', '0150', '0199', " +
            "'0b71', '01a9', '0237', '014d', '01bd', '0259', '0170', '0a7a', '0216', '01aa', " +
            "'01fb', '01f3', '03e4', '0128', '0226', '01ab', '018c', '016e', '0233', '05d2', " +
            "'023b', '0559', '0859', '0106', '020c', '0195', '0457', '0172', '012f', '010d', " +
            "'0c21', '0d4e', '0268', '021a', '020f', '05bf', '0203', '0e65', '01be', '013b', " +
            "'0166', '056e', '010e', '0db0', '079a', '0551', '0cf1', '0171', '01df', '0451', " +
            "'01ee', '0200', '024f', '0192', '0222', '01f6', '0a26', '0267', '0266', '025c', " +
            "'0f57', '0fbd', '0cfb', '0efc', '0d48', '05a6', '07a0', '012e', '024f', '0bb2', " +
            "'0193', '0226', '0268', '0713', '01a8', '0110', '016b', '0269', '0e68', '0126', " +
            "'0140', '011b', '07d9', '08c0', '0250', '0117', '01ba', '0235', '0194', '016a', " +
            "'0a46', '019a', '0266', '0169', '0c23', '0be5', '0225', '016e', '0173', '01b7', " +
            "'0220', '05a5', '0e3d', '01d5', '016b', '0dcf', '0417', '0134', '01eb', '01d5', " +
            "'0219', '0224', '0115', '01cc', '01b9', '08fd', '022c', '01e7', '01f2', '0234', " +
            "'018d', '0240', '0237', '0135', '0167', '0233', '010b', '0191', '0fb6', '0fef', " +
            "'05d4', '0257', '0df8', '0101', '015e', '023a', '0e9c', '011e', '020f', '01ce', " +
            "'0479', '03bd', '019c', '025d', '0230', '0251', '01aa', '0522', '0079', '024f', " +
            "'01e4', '0163', '0583', '02e1', '013d', '0154', '0994', '01d8', '0e6f', '01bd', " +
            "'003b', '0be5', '058d', '016e', '0b50', '019a', '0207', '0119', '06ae', '020b', " +
            "'0f55', '0237', '0218', '0241', '01b1', '0190', '0325', '0168', '014c', '0151', " +
            "'015d', '0c54', '01dc', '0161', '073e', '0167', '0c1a', '0107', '022d', '0139', " +
            "'0202', '01d3')"),
        new QueryInfo("/* *//* __QPM Query 4 */ SELECT MAX(unn0) AS max_4 FROM t0 WHERE " +
            "charCol0 IN ('0004', '0004', '0004', '0008', '0001', '0002', '000a', '000a', " +
            "'0001', '0005', '011f', '016e', '01aa', '05a7', '0ccd', '04c7', '0217', '0251', " +
            "'0202', '0156', '0178', '0239', '01f3', '0d7b', '05b3', '0127', '0e0a', '019a', " +
            "'0181', '0277', '032b', '070c', '01bc', '04a1', '0757', '0251', '0821', '019c', " +
            "'04b0', '03f4', '0168', '0b2b', '0525', '0208', '0139', '0243', '0192', '01f3', " +
            "'0127', '0510', '001b', '0171', '03ae', '01ea', '010f', '0b67', '0104', '0269', " +
            "'05d9', '0220', '0234', '01f6', '0501', '0184', '0ae7', '0241', '0245', '0dee', " +
            "'07fe', '0166', '025c', '0160', '0fee', '0dba', '017f', '0a0f', '01c8', '0233', " +
            "'0104', '0a32', '0a05', '01f9', '025c', '0b69', '0d03', '0a21', '016b', '0547', " +
            "'013d', '029d', '01e9', '0130', '01fe', '014d', '016d', '0194', '0acb', '014c', " +
            "'0154', '01a4', '0dd5', '068a', '0afb', '0824', '0228', '021f', '0151', '012b', " +
            "'0cda', '0166', '061b', '0426', '0e53', '084e', '018f', '01ef', '0188', '00a7', " +
            "'0238', '0f3f', '0604', '0486', '0b52', '01ef', '0251', '02cc', '0241', '01e7', " +
            "'0152', '006d', '014a', '0161', '0118', '01fd', '0133', '0152', '0e25', '0088', " +
            "'01e2', '0600', '01f8', '01a8', '0262', '0137', '0132', '0f3f', '06a3', '03f9', " +
            "'0159', '013e', '01f1', '0183', '0259', '0119', '0110', '020a', '025d', '0186', " +
            "'016d', '0efb', '03da', '0269', '021d', '0ace', '01d0', '0631', '07cf', '0261', " +
            "'022c', '0268', '01b2', '00e5', '05e2', '014c', '0611', '0143', '0e06', '0be2', " +
            "'01ee', '01c0', '0937', '0254', '09e0', '01f0', '011e', '089c', '06d1', '0205', " +
            "'0f43', '023c', '01ee', '0202', '0695', '0228', '011d', '0319', '00c2', '0226', " +
            "'0253', '019a', '018d', '0392', '0f80', '01b0', '02bc', '0287', '06be', '012d', " +
            "'0c17', '0144', '01a9', '0243', '038b', '0acf', '018b', '0c2a', '0164', '01ef', " +
            "'0137', '025c', '095b', '01ed', '0127', '00d4', '094f', '01e0', '015e', '0ca4', " +
            "'01e3', '0974', '020a', '01b4', '01a0', '0df1', '0148', '01a3', '0387', '016c', " +
            "'02e7', '0434', '0b1d', '0209', '0205', '0509', '0126', '0183', '011b', '0f63', " +
            "'0232', '026a', '0c67', '0156', '08d9', '0110', '078f', '014e', '0158', '016a', " +
            "'04f3', '0bbc', '019c', '0763', '01ff', '0224', '0227', '0203', '0269', '0178', " +
            "'0228', '0d65', '0160', '02df', '0cab', '0ad5', '01db', '032e', '0110', '08e6', " +
            "'08ea', '0147', '0265', '010b', '04bc', '01de', '0cb8', '0b7e', '02a2', '01fd', " +
            "'0dd9', '050e', '0198', '01e5', '096f', '06b8', '024e', '0c90', '0260', '01e7', " +
            "'05dd', '01f3', '0122', '0180', '077f', '0221', '0143', '04a1', '06fc', '072f', " +
            "'0c60', '0128', '0328', '01cc', '0666', '01a9', '0262', '0129', '028a', '089e', " +
            "'089e', '0258', '0228', '05a6', '05b9', '016c', '018b', '01e9', '0166', '0106', " +
            "'021e', '024b', '0f63', '013a', '01bf', '021e', '021c', '060d', '0211', '0190', " +
            "'0243', '020f', '0e82', '0a92', '09b8', '09b8', '01af', '023f', '0134', '019d', " +
            "'0233', '022d', '01ce', '0fd5', '0325', '01ee', '0c03', '0efb', '0231', '062f', " +
            "'0175', '018f', '0813', '0100', '0169', '0831', '02d7', '01f8', '01ef', '0200', " +
            "'0a71', '016d', '0189', '05cf', '0230', '010f', '01d2', '018e', '01c6', '011b', " +
            "'020f', '0914', '027d', '0154', '0153', '0229', '0233', '03df', '090e', '0159', " +
            "'09b8', '0069', '03ee', '0c3e', '0116', '0e5e', '01cd', '0a43', '00cc', '06d2', " +
            "'030a', '0116', '0231', '0254', '0435', '025c', '0575', '0a20', '01c6', '018c', " +
            "'0149', '013e', '0b38', '0289', '0222', '01b3', '016d', '01e2', '01c9', '0142', " +
            "'07cd', '01a7', '01de', '0121', '025c', '0ff0', '012d', '016c', '0161', '0107', " +
            "'0114', '014d', '0263', '05c1', '012d', '023a', '02d2', '0149', '0170', '0862', " +
            "'010e', '026a', '0655', '0138', '0213', '01e6', '011c', '0141', '0060', '07db', " +
            "'0233', '04e6', '01c1', '014f', '0132', '03b4', '0126', '0a8c', '0d50', '0208', " +
            "'010f', '0792', '021e', '0121', '0bb7', '0f61', '016e', '01da', '0969', '0454', " +
            "'0864', '01d5', '09eb', '02d8', '0131', '024d', '021a', '025d', '0213', '0129', " +
            "'0600', '01f8', '01ec', '0d1b', '0244', '0171', '0b1d', '0114', '019e', '0120', " +
            "'022f', '014f', '01fd', '0225', '063f', '01e8', '01b5', '0d6f', '01c1', '0160', " +
            "'010d', '0162', '0c40', '015e', '015f', '0520', '0ddc', '0198', '0655', '0158', " +
            "'014b', '008e', '0e69', '015b', '012e', '0b84', '09c1', '024f', '018d', '0211', " +
            "'0e01', '06e8', '022c', '017b', '0a1e', '0fa0', '011d', '0206', '01dd', '06f5', " +
            "'01de', '0d46', '01ab', '08c1', '011e', '0d82', '0f33', '0eb6', '0f75', '0eba', " +
            "'0693', '0caa', '0707', '018e', '00ac', '013a', '018d', '01a7', '0143', '0158', " +
            "'0654', '0210', '001d', '0c27', '024f', '0b2c', '01f6', '080e', '01a2', '0b4e', " +
            "'0257', '011e', '071f', '0041', '0196', '0194', '0227', '0156', '0148', '0261', " +
            "'0130', '018e', '0114', '014a', '0164', '047d', '024f', '0189', '01f3', '06c4', " +
            "'0572', '01eb', '0cc1', '0240', '0162', '011e', '04c4', '0177', '0186', '021a', " +
            "'0558', '0cec', '0182', '04f3', '0d77', '05f2', '01f1', '0b4a', '0156', '0192', " +
            "'0bfc', '097a', '0855', '01c6', '01b1', '0f07', '0138', '02ed', '0104', '097b', " +
            "'0b07', '03a5', '0229', '0e14', '0259', '0184', '0e39', '0105', '05d6', '0162', " +
            "'0a39', '063d', '01e6', '0400', '01cf', '000e', '025a', '0267', '0a9b', '084e', " +
            "'01e4', '0a58', '065c', '0238', '0856', '09e7', '016f', '0cc8', '0b8e', '01dd', " +
            "'011a', '0597', '0148', '0c96', '0226', '0100', '0f49', '01d7', '01fd', '0106', " +
            "'0820', '0243', '0d40', '01c6', '01f3', '0b81', '0237', '0fd5', '012d', '0b27', " +
            "'0182', '0c22', '0186', '0117', '0221', '072f', '0ad9', '0159', '01dc', '024d', " +
            "'01f3', '0151', '0112', '0cfb', '0e9e', '0177', '06d2', '011f', '0204', '0177', " +
            "'014d', '012f', '021d', '0200', '0118', '0211', '01f4', '01a3', '04bd', '0146', " +
            "'0258', '068d', '0222', '0953', '019c', '0122', '01d0', '0d8d', '0c15', '04fa', " +
            "'0bfc', '0df6', '0197', '01b1', '0131', '01bf', '0183', '01fd', '0d58', '025e', " +
            "'01e1', '0647', '01ea', '0d62', '03a2', '0208', '0135', '0216', '01c7', '012b', " +
            "'01fa', '0214', '0235', '0bac', '0c1f', '0c9a', '04fd', '05fd', '0253', '0861', " +
            "'03e5', '020c', '06a4', '01b7', '05ad', '016c', '0861', '014b', '0dec', '0102', " +
            "'017d', '013a', '022c', '0149', '018f', '0c24', '01ee', '08d1', '0855', '0128', " +
            "'0196', '02bf', '01b5', '0152', '016c', '04e1', '0ac0', '0e6b', '04a9', '048e', " +
            "'0638', '022f', '04b1', '0217', '0133', '0221', '0216', '05f9', '0ee1', '070c', " +
            "'05ab', '012c', '0cd3', '0322', '0c34', '0052', '01fb', '01cc', '002a', '0dd2', " +
            "'017a', '0110', '02e9', '01da', '0005', '0128', '0224', '012e', '01dc', '0ff4', " +
            "'0155', '017d', '0131', '0858', '0148', '01b8', '040d', '0100', '0185', '012c', " +
            "'0187', '06ea', '053e', '0120', '0e66', '0a9c', '01da', '030d', '0ce7', '025f', " +
            "'0067', '0104', '0115', '0203', '0164', '003c', '011f', '01c1', '0116', '05dd', " +
            "'01c8', '0259', '032e', '00c8', '0224', '0265', '0f6b', '0105', '0915', '0263', " +
            "'025d', '0265', '0163', '016d', '0990', '01b7', '0269', '0a17', '0213', '01b3', " +
            "'014a', '0149', '0192', '01f7', '01d1', '0114', '03af', '032f', '046f', '0250', " +
            "'0109', '01ac', '025e', '073a', '0118', '0b8c', '010b', '0171', '076f', '0e94', " +
            "'00fa', '0408', '053a', '0291', '08a9', '010b', '0260', '0212', '01fa', '019a', " +
            "'0254', '013f', '01b6', '0352', '0772', '021d', '039c', '07b0', '04b7', '0dc9', " +
            "'0fe2', '018b', '0225', '018f', '013c', '04c9', '01a2', '0e7c', '0542', '01aa', " +
            "'014e', '0231', '0ecd', '018a', '01ef', '015c', '08a0', '0257', '0191', '0c7d', " +
            "'015f', '016b', '017f', '0c61', '046b', '021f', '01ad', '09e7', '0161', '0eda', " +
            "'0db4', '01f3', '0142', '011c', '0ce3', '0aa1', '025f', '0a8d', '0e59', '046f', " +
            "'01de', '00c8', '021c', '0528', '01d2', '020a', '07e4', '01c3', '07a3', '01f7', " +
            "'024a', '081b', '015f', '01db', '0224', '079e', '0602', '0237', '0e57', '001b', " +
            "'01f5', '0e86', '012d', '011b', '0184', '0b59', '0fde', '0b78', '0253', '018a', " +
            "'0316', '021c', '0c3a', '0436', '0197', '021c', '0187', '0239', '0953', '0168', " +
            "'01c7', '0235', '0258', '0197', '0257', '0176', '0359', '0546', '0240', '05b4', " +
            "'06c5', '01c2', '0129', '01df', '0a21', '0227', '0865', '0178', '0201', '0e92', " +
            "'018e', '0ebd', '0690', '0136', '0190', '0193', '0b97', '080b', '042d', '0251', " +
            "'028d', '0e9a', '0122', '020a', '0243', '01ee', '014b', '01e2', '0de5', '0ad6', " +
            "'0dc7', '044d', '0a88', '0484', '013c', '0959', '00b2', '0caf', '0eca', '019e', " +
            "'017e', '01e4', '01aa', '0595', '0c2e', '0f5a', '010a', '023c', '0194', '0bc7', " +
            "'01af', '0012', '01b2', '0265', '013f', '0772', '01ac', '0130', '01a7', '0725', " +
            "'022a', '0580', '01cf', '025f', '0ec8', '010e', '0260', '0109', '0a8b', '0132', " +
            "'0e0a', '029b', '0136', '0073', '08e6', '01d4', '0712', '02fd', '015f', '05e8', " +
            "'0126', '023b', '023b', '01ee', '0fdc', '09ee', '031a', '0214', '07ae', '0750', " +
            "'0fd0', '010b', '0f24', '017b', '0133', '01e2', '01d8', '0196', '0458', '08a4', " +
            "'0241', '0a4f', '0ee8', '01c9', '0749', '0257', '0120', '017f', '0226', '01de', " +
            "'013c', '024f', '0de1', '001f', '064a', '0185', '0f9d', '055d', '01fb', '0557', " +
            "'0b88', '0916', '0255', '0b94', '0135', '014b', '015a', '0235', '02ab', '060e', " +
            "'0a5c', '05ba', '01c4', '01c2', '010d', '0a4e', '01dc', '01ee', '0239', '0153', " +
            "'079c', '0135', '0144', '01fc', '0ce8', '01c9', '024f', '036a', '08b8', '0655', " +
            "'0158', '010c', '015d', '0110', '018f', '01b9', '011a', '01c8', '0f41', '0130', " +
            "'0129', '0168', '020c', '0258', '0180', '0940', '08d3', '0107', '048b', '01ee', " +
            "'01c4', '07c4', '0e25', '0160', '0da4', '023c', '0af7', '01e4', '0140', '01d2', " +
            "'07d2', '057d', '01ec', '024d', '07bc', '02d1', '011b', '01d7', '0357', '022e', " +
            "'0263', '0136', '0225', '0076', '0530', '01c3', '000c', '010d', '01e1', '0188', " +
            "'01db', '0185', '0120', '015e', '0103', '01e6', '018e', '04ed', '01b3', '023d', " +
            "'018d', '021f', '02ef', '0756', '0223', '01de', '01b8', '01dd', '016a', '0be6', " +
            "'0332', '00d4', '0b34', '0232', '01c8', '018f', '0204', '05b9', '0167', '0a34', " +
            "'020e', '0183', '012b', '0257', '022c', '01bb', '049a', '016f', '0177', '0217', " +
            "'0178', '01aa', '014f', '016b', '01c3', '0ff4', '05d2', '08cc', '0da3', '07ec', " +
            "'01b7', '03ac', '01b7', '015f', '0189', '01a1', '0185', '0142', '0226', '03f5', " +
            "'01a5', '01d7', '08c1', '0110', '00f2', '0154', '0627', '07bc', '0164', '04bf', " +
            "'0253', '0e9b', '0ef2', '08bb', '03ef', '0af8', '01d4', '0204', '0932', '0dce', " +
            "'01eb', '0172')"),
        new QueryInfo("/* *//* __QPM Query 5 */ SELECT MAX(d3) AS max_5 FROM t3 WHERE " +
            "charCol3 IN ('0004', '0003', '0008', '0003', '0001', '0001', '0005', '0006', " +
            "'0006', '0007', '0236', '0368', '06a0', '0241', '017d', '011b', '0195', '01c7', " +
            "'0196', '0137', '01b4', '0d24', '07e3', '0167', '0234', '01ad', '010a', '0226', " +
            "'0114', '0c34', '0203', '0246', '0de8', '0843', '0222', '013d', '0b9c', '0b6e', " +
            "'0dd7', '072b', '0407', '01bb', '065e', '068b', '0204', '018b', '0166', '022c', " +
            "'00c4', '020d', '0b09', '0106', '0106', '0196', '0206', '029e', '01e3', '014b', " +
            "'0196', '01c5', '0105', '0d87', '0a09', '034f', '022b', '0e28', '0699', '060a', " +
            "'0210', '012d', '0259', '04ef', '0192', '01f8', '0207', '05c2', '01a8', '0191', " +
            "'0138', '0c21', '09d7', '097d', '0120', '07df', '0389', '08c6', '091c', '08db', " +
            "'0661', '01a2', '01c1', '0e19', '0152', '0d4a', '0254', '0d9d', '0160', '01ce', " +
            "'015a', '0126', '013f', '0ec6', '0002', '0249', '018b', '021b', '01b5', '01b7', " +
            "'0819', '0446', '01e6', '01cc', '0115', '0117', '0116', '0baf', '0219', '011a', " +
            "'0ecd', '0d57', '0c15', '0230', '0226', '01c8', '02ea', '0e40', '0183', '01c0', " +
            "'015d', '0117', '0160', '0206', '0d18', '024b', '04fa', '0877', '00e8', '0177', " +
            "'0750', '0200', '00db', '0258', '01b6', '01d3', '0184', '0108', '0147', '0b9a', " +
            "'0191', '0458', '0131', '0133', '0138', '0114', '017a', '0269', '0176', '010a', " +
            "'0171', '0a3e', '0192', '017a', '0247', '021a', '025c', '0189', '018c', '062c', " +
            "'0257', '0218', '024a', '01da', '07b1', '0b28', '0166', '01b2', '01bf', '01eb', " +
            "'01b8', '0188', '022f', '069d', '01c2', '0e15', '0d07', '023a', '04da', '08ef', " +
            "'0d8e', '0070', '0264', '025a', '0263', '0195', '0122', '0249', '0d1e', '02f7', " +
            "'0154', '05d5', '06ad', '05d0', '01ff', '01f0', '07d4', '0ba6', '0541', '0261', " +
            "'025a', '0151', '0253', '01a7', '01a2', '0955', '0e5e', '01e2', '022e', '0210', " +
            "'01ec', '08de', '09be', '0911', '0d6b', '02b2', '0019', '0177', '0609', '022c', " +
            "'0661', '0110', '0132', '0107', '0255', '01dd', '0113', '0241', '02e6', '0208', " +
            "'0206', '01a1', '090f', '0b63', '01f3', '01d6', '0744', '0b8e', '01f9', '0d23', " +
            "'0266', '05f3', '021c', '01a4', '0b96', '0187', '018d', '0235', '06c9', '0dcf', " +
            "'006b', '021d', '04e1', '0120', '0166', '0214', '01ef', '0317', '018c', '0172', " +
            "'08bc', '01d5', '01b6', '020c', '0f64', '01f7', '015f', '03ae', '0113', '0953', " +
            "'0398', '0d0b', '0f4e', '017d', '048d', '01c0', '07f4', '0b7e', '0130', '05a0', " +
            "'0237', '0229', '010e', '01d4', '052e', '021d', '0253', '0119', '0133', '0261', " +
            "'01fa', '0246', '01b1', '0161', '0139', '01dc', '0aa7', '01cf', '0221', '0167', " +
            "'0f42', '01b9', '012a', '0179', '03be', '01e0', '0197', '04ba', '0b12', '0146', " +
            "'0170', '01e2', '024c', '0110', '0140', '017e', '016e', '0205', '021b', '0c28', " +
            "'019d', '01cd', '0193', '0231', '0251', '0404', '080b', '0192', '0249', '0faa', " +
            "'01c6', '0256', '010e', '0ccd', '0223', '09eb', '016b', '095d', '01b1', '0238', " +
            "'000e', '0f17', '0cf5', '01bb', '01d5', '0199', '0238', '0128', '023f', '01c2', " +
            "'01e2', '0258', '016e', '0261', '0215', '02db', '0255', '015a', '010e', '0509', " +
            "'011b', '01d8', '04d1', '0145', '0168', '0925', '06bc', '020e', '0106', '0251', " +
            "'0e9a', '016f', '0b33', '012f', '0208', '0ca2', '011d', '0125', '01ea', '059b', " +
            "'0dc9', '019c', '01c9', '0cd9', '0216', '0175', '01cf', '0268', '01cf', '01ae', " +
            "'010d', '01fc', '0204', '04ad', '0672', '06f9', '021e', '0187', '0d71', '0157', " +
            "'019f', '0212', '0556', '0f76', '01f5', '0155', '0c92', '01d4', '0119', '0177', " +
            "'0133', '01d6', '0217', '023c', '0fc0', '013e', '0163', '016a', '02b1', '020e', " +
            "'0118', '025e', '011a', '01fd', '0256', '0aa0', '014d', '0f40', '0e16', '029b', " +
            "'01ce', '09ef', '0180', '0344', '01d0', '01e2', '03e2', '01bb', '080b', '04fa', " +
            "'0e87', '0206', '092b', '020c', '01c6', '06a7', '0a06', '0153', '0a9f', '0123', " +
            "'0200', '014c', '01cd', '0217', '0657', '09f5', '0f5f', '0100', '01c1', '080b', " +
            "'0129', '025f', '0145', '0197', '010b', '018f', '0e5f', '0107', '0251', '0203', " +
            "'03bb', '0135', '0489', '068e', '0152', '022a', '032b', '0132', '021b', '056f', " +
            "'0673', '05a2', '0205', '0ed6', '018f', '0168', '0256', '0852', '03c0', '0158', " +
            "'0227', '019d', '01f5', '0162', '013e', '0136', '074c', '0180', '0446', '014e', " +
            "'0de3', '01af', '0142', '0261', '0200', '0145', '01c4', '01be', '01b4', '0932', " +
            "'025f', '0141', '0cd7', '013b', '05f3', '010b', '0cfd', '0169', '0b13', '004f', " +
            "'0307', '01b7', '08c4', '0c47', '01f7', '0ede', '0cb4', '0135', '061b', '01ee', " +
            "'01f3', '0239', '01eb', '0d3e', '0ebd', '011f', '0105', '01aa', '0072', '0118', " +
            "'0b45', '025e', '0253', '015a', '0335', '024b', '0207', '013b', '0177', '01fc', " +
            "'019d', '01bd', '0b59', '0125', '01c3', '01a5', '0146', '07d6', '0243', '0235', " +
            "'01a0', '02b8', '025e', '0219', '0d5a', '019a', '02dc', '01f9', '0691', '0195', " +
            "'01da', '0691', '0265', '0200', '0221', '012f', '0126', '0327', '0165', '0390', " +
            "'051c', '037d', '01ab', '05e7', '02cf', '0730', '0d52', '020f', '010e', '0384', " +
            "'0c76', '079a', '0cd8', '07fe', '01c1', '0101', '0122', '0174', '011e', '0208', " +
            "'01c5', '02bb', '0116', '00f1', '016c', '012b', '0267', '0118', '0268', '0120', " +
            "'0e20', '027d', '01d0', '010c', '016f', '024a', '03e9', '01cc', '0199', '0252', " +
            "'024b', '0114', '0177', '091e', '013e', '0122', '0e1b', '024c', '0246', '0217', " +
            "'01cd', '011b', '01cd', '0219', '0204', '023d', '02c7', '0163', '099d', '0900', " +
            "'0269', '08c6', '011d', '0ead', '023f', '01e6', '019b', '025c', '022f', '0190', " +
            "'0159', '0164', '0194', '0139', '012a', '0d9d', '021f', '0191', '0200', '0a5e', " +
            "'0158', '022b', '022f', '0185', '0136', '0eb5', '0191', '0215', '0e1a', '0bea', " +
            "'0bc8', '019e', '0264', '019b', '0145', '0111', '01e9', '028b', '0141', '014b', " +
            "'0764', '014d', '09bf', '0bcf', '0685', '0156', '0665', '01cd', '0246', '01eb', " +
            "'0a56', '01a7', '02b3', '0c18', '0c41', '011d', '014c', '0d26', '022c', '0107', " +
            "'04cf', '017e', '0d2d', '01c1', '02f0', '0c48', '025f', '01b4', '018b', '0210', " +
            "'0226', '011a', '0c2a', '01af', '018f', '032e', '0960', '0129', '0ef6', '024e', " +
            "'01b0', '0d16', '08f0', '021a', '0264', '07a9', '0968', '01c3', '0163', '01d7', " +
            "'033b', '01da', '0f57', '03e9', '0151', '0d44', '0cd7', '0130', '0719', '02fe', " +
            "'070b', '0246', '024b', '0215', '0c1b', '0269', '04df', '091a', '01d1', '04c1', " +
            "'05ca', '0dbe', '0259', '09b5', '04c6', '0023', '015e', '0783', '0ecf', '0b1c', " +
            "'0129', '018c', '0d56', '024f', '01f1', '0234', '0c89', '0132', '0161', '0d96', " +
            "'017a', '0233', '0a4c', '0129', '07f8', '0169', '0edd', '01de', '01a6', '017b', " +
            "'0128', '0831', '01f6', '06cc', '0208', '017f', '0186', '04cf', '0df9', '0eb8', " +
            "'03e8', '011a', '0139', '01a2', '01fa', '0023', '0112', '013e', '015b', '016b', " +
            "'01d8', '0193', '020f', '00c0', '0430', '01f8', '018f', '017d', '06c1', '01c9', " +
            "'0efe', '0c29', '00e4', '019e', '0e7c', '0a5c', '060e', '04a5', '0d2b', '0251', " +
            "'0133', '0253', '0df1', '01f9', '022a', '024a', '0a5b', '0c89', '0141', '0267', " +
            "'01a9', '0219', '0170', '0def', '0a07', '00d0', '0096', '0125', '01a1', '093b', " +
            "'0d41', '00e5', '0419', '01fc', '0c2a', '0123', '08c3', '0188', '01fe', '0128', " +
            "'019a', '0164', '0167', '012d', '0133', '010b', '0385', '0d4c', '0256', '0241', " +
            "'030a', '0207', '0105', '0945', '0121', '0e30', '05a2', '0076', '01e5', '016d', " +
            "'01dd', '0193', '01af', '0542', '025e', '01ff', '0188', '017d', '081f', '0169', " +
            "'0152', '0108', '0223', '023f', '025b', '0198', '01d4', '0816', '09a4', '08ec', " +
            "'051c', '0239', '040f', '0222', '011a', '0148', '013b', '07b9', '0e44', '0236', " +
            "'025d', '0136', '0137', '0113', '0192', '0fe5', '01cd', '013a', '0196', '015a', " +
            "'022d', '0120', '01e5', '0d6d', '0201', '010e', '08c6', '03d9', '023c', '0522', " +
            "'021a', '01a2', '0d3f', '072f', '020b', '0820', '0651', '00c7', '018b', '010b', " +
            "'0266', '01f3', '011b', '01d7', '01ca', '02e6', '0177', '011d', '0169', '021f', " +
            "'01e6', '0b37', '01b6', '025f', '0173', '012b', '023e', '0195', '01c8', '01a4', " +
            "'01b5', '0fc1', '0526', '0e7d', '0222', '07f7', '011b', '0182', '0105', '0266', " +
            "'0201', '01a1', '03ac', '0153', '064a', '01b2', '0239', '01bb', '0a9a', '017a', " +
            "'0c73', '05d9', '014c', '02c7', '0248', '0150', '0148', '0a55', '01c3', '0ef7', " +
            "'0814', '01b5', '0614', '067c', '0118', '025f', '016e', '017e', '041a', '0173', " +
            "'01e1', '0155', '013f', '0205', '0800', '017a', '06f2', '021f', '0ed0', '010d', " +
            "'0175', '0eff', '0e8a', '0b60', '010a', '019c', '01d8', '01c6', '0b0e', '01fe', " +
            "'0237', '0122', '0167', '086c', '01a8', '01c0', '0911', '0102', '0194', '0be6', " +
            "'0ba2', '025c', '021d', '0105', '0afc', '09af', '0254', '019a', '0232', '01b7', " +
            "'0130', '017a', '055a', '018b', '0d5a', '092d', '021b', '06aa', '023d', '03cb', " +
            "'01fb', '0165', '0055', '0a5e', '01fc', '020c', '01c6', '0171', '00e8', '0d26', " +
            "'0210', '01a7', '0f23', '069a', '0a53', '016b', '01ef', '017b', '01f9', '0230', " +
            "'001d', '0460', '0f51', '0193', '0865', '0142', '0144', '01b9', '0065', '020c', " +
            "'015e', '01ea', '0152', '0230', '0156', '09ea', '0205', '0133', '087d', '01f9', " +
            "'0d25', '0061', '012e', '0e13', '03b3', '019e', '01ff', '017f', '0131', '00a2', " +
            "'0d76', '0a6a', '0d6e', '0814', '0184', '0d0c', '0367', '0a9d', '020e', '0eae', " +
            "'01a2', '0473', '0cbf', '05a6', '01ee', '0a38', '0aef', '0192', '01d9', '0126', " +
            "'024d', '0145', '0b62', '07b8', '0e1f', '085c', '018e', '0984', '01d8', '0160', " +
            "'01b9', '0eec', '01c0', '0192', '01b4', '0110', '03ba', '01f4', '01a5', '07f4', " +
            "'0752', '0204', '01cc', '010d', '01d2', '0243', '014e', '030f', '0236', '01df', " +
            "'0264', '0250', '0104', '0005', '018b', '023c', '0160', '0103', '0148', '0228', " +
            "'0153', '0258', '0238', '0259', '06e3', '0197', '0159', '0a35', '0bef', '0137', " +
            "'0893', '01d9', '01e9', '0254', '0136', '01d8', '012b', '016f', '0108', '0248', " +
            "'0f5f', '01c4', '01b8', '0201', '024a', '0d0a', '0007', '0709', '013b', '0248', " +
            "'0ea5', '0acf', '0ddc', '016a', '0200', '022f', '0249', '0120', '0b3e', '016b', " +
            "'01e6', '025b', '022a', '011b', '016a', '0124', '01ba', '0150', '019a', '0e92', " +
            "'0f69', '011c', '0212', '014c', '0266', '0205', '00d2', '019d', '011b', '0b60', " +
            "'00d2', '010a', '025c', '0251', '0265', '0d05', '0230', '0a4d', '0289', '0152', " +
            "'010a', '01d7', '0ed7', '0259', '0145', '0a39', '018c', '0d6b', '01d2', '01af', " +
            "'0206', '0110', '045b', '04ad', '01d6', '018b', '01ab', '01e2', '017e', '07e7', " +
            "'0111', '0d2c')"),
        new QueryInfo("/* *//* __QPM Query 6 */ SELECT MAX(f3) AS max_6 FROM t3 WHERE " +
            "charCol3 IN ('0005', '0002', '0005', '000a', '0009', '0002', '0002', '0008', " +
            "'000a', '000a', '0268', '01f1', '0509', '0193', '0f06', '0ebd', '010b', '0f34', " +
            "'0ded', '0138', '01d5', '01b2', '023d', '0151', '026a', '0187', '010b', '0aea', " +
            "'05f7', '0934', '0494', '01eb', '0fce', '0104', '0129', '0104', '0237', '0117', " +
            "'0105', '0861', '025a', '0223', '01c9', '01fb', '0325', '01d9', '022a', '0248', " +
            "'01b3', '05e0', '0812', '011c', '01d0', '0217', '013b', '0e80', '0136', '01f7', " +
            "'019a', '06ca', '0bd3', '0e95', '024b', '0310', '022a', '022a', '0159', '01cf', " +
            "'0d46', '01c5', '0147', '0206', '019d', '0f92', '03c9', '0131', '0296', '01f3', " +
            "'01e3', '01bb', '0163', '01ed', '0144', '0e32', '02b0', '0876', '01a2', '0118', " +
            "'0269', '014c', '08b2', '0205', '05a0', '0b92', '01a1', '01b6', '01fa', '011b', " +
            "'0247', '023c', '0962', '0475', '0e7d', '0405', '0ccc', '024b', '0ba6', '0124', " +
            "'025c', '0f67', '012a', '01b3', '0161', '0136', '0787', '0167', '05cf', '06f1', " +
            "'01ce', '09b9', '0aae', '0230', '017f', '021c', '0137', '0235', '0178', '0919', " +
            "'0a2e', '01cc', '01ee', '0405', '0ef7', '0106', '0205', '06a7', '0254', '0e2b', " +
            "'0232', '0221', '0e47', '021e', '0252', '0249', '0d80', '01f4', '01ec', '0037', " +
            "'0db6', '08e3', '0125', '0164', '0130', '07f6', '0246', '020a', '0111', '0dcf', " +
            "'012f', '0b74', '024c', '014c', '019d', '0371', '0253', '0240', '0249', '06cf', " +
            "'07eb', '0239', '024f', '01e6', '06ff', '0f08', '023d', '0169', '0d03', '06a1', " +
            "'01e6', '0203', '0a3e', '02b7', '0130', '0194', '0416', '09b2', '052e', '0dd6', " +
            "'0870', '0179', '025f', '011b', '0110', '017d', '01cd', '02dc', '0230', '011b', " +
            "'0177', '0ab3', '0ecd', '03a6', '01e8', '0133', '0163', '01f1', '0148', '01cd', " +
            "'0135', '0a1b', '0e92', '01d8', '0ec1', '0163', '0feb', '0143', '022d', '012e', " +
            "'0219', '0117', '0b92', '0148', '01ca', '01cc', '0532', '09c9', '0dd5', '0120', " +
            "'019f', '010a', '0710', '0131', '013c', '0205', '01af', '01be', '01aa', '0256', " +
            "'0124', '04c9', '0e99', '087e', '0b92', '0267', '0212', '01c0', '0bd3', '04f1', " +
            "'0165', '0200', '0582', '0251', '089e', '01f4', '0116', '0df3', '04af', '0197', " +
            "'0d86', '0159', '023f', '07ea', '0199', '0fe8', '017e', '0112', '019d', '0142', " +
            "'0497', '0ae9', '0421', '024d', '01a8', '012c', '0d7a', '0199', '01f5', '0819', " +
            "'0932', '0b33', '0bd4', '01d0', '07c7', '0566', '0131', '018e', '013a', '0a7d', " +
            "'08ae', '0051', '01a3', '01eb', '00d2', '04a2', '01db', '013d', '0133', '0101', " +
            "'0252', '011a', '01b1', '05ec', '012d', '0122', '0264', '0212', '0a86', '023e', " +
            "'0c39', '04d3', '022d', '04e4', '0241', '0160', '017b', '0212', '012f', '01ed', " +
            "'0260', '0209', '01e1', '011e', '0105', '0734', '0b7b', '01df', '013a', '0190', " +
            "'0210', '0be1', '0188', '018a', '0110', '0133', '01f1', '016f', '0218', '0268', " +
            "'04b8', '0ec6', '01e7', '0204', '0731', '01b7', '026a', '0131', '04ca', '018a', " +
            "'0204', '01fb', '0124', '00c9', '0992', '0f67', '01f6', '0bf5', '025c', '06ec', " +
            "'019f', '019c', '0122', '01bb', '01b7', '07e5', '0850', '035f', '0d81', '021f', " +
            "'08a2', '00ae', '0245', '0da6', '0152', '0210', '0183', '0115', '012a', '0bae', " +
            "'0890', '01bb', '014f', '0c7b', '01a3', '01a6', '0b82', '01ee', '005d', '01a1', " +
            "'04fe', '01b8', '014a', '0203', '013d', '0240', '01c1', '0786', '007c', '07a2', " +
            "'08db', '024c', '0f6f', '05a9', '0c66', '0410', '01f5', '0c97', '01eb', '0207', " +
            "'02e7', '0100', '065a', '020d', '0ac4', '01c3', '0139', '014b', '0eed', '0111', " +
            "'01a8', '017c', '0128', '05d0', '0fa8', '0191', '0dd9', '01a5', '0100', '024c', " +
            "'0ea3', '019c', '0656', '0164', '02bd', '0201', '0240', '0245', '0ae1', '043a', " +
            "'0190', '014f', '0ecb', '0259', '0158', '0a3d', '022b', '019f', '0439', '0243', " +
            "'01e4', '0121', '0725', '09ca', '025e', '0120', '00df', '020a', '0106', '0866', " +
            "'016f', '0233', '0105', '025e', '01c3', '0c93', '012f', '01b8', '020a', '0209', " +
            "'01cf', '015e', '016e', '0247', '0236', '013e', '0afa', '022d', '01eb', '0269', " +
            "'0216', '0162', '02bb', '011b', '018a', '01a8', '019a', '0190', '010e', '07dd', " +
            "'0895', '012d', '0a48', '0122', '0f96', '0173', '0b3e', '0049', '0b72', '0bd6', " +
            "'025b', '0e97', '024d', '015a', '0ee4', '018a', '0251', '01b6', '0152', '094e', " +
            "'0164', '0fdc', '021e', '0a00', '0d6a', '01eb', '0237', '016d', '08b6', '019c', " +
            "'0841', '019f', '0232', '0eda', '01f9', '0169', '01a0', '01b4', '062f', '01ef', " +
            "'0f94', '0167', '0ebb', '0260', '0105', '0a73', '0137', '025c', '0265', '0259', " +
            "'03c3', '01d3', '02c7', '07eb', '0173', '0abc', '00bb', '014e', '0a39', '0216', " +
            "'0222', '0127', '0259', '0204', '0349', '0b11', '014b', '0eae', '0265', '019f', " +
            "'01fb', '0250', '0231', '0aed', '004c', '019e', '0e9f', '0019', '017c', '01c2', " +
            "'0092', '02c6', '013f', '0c5b', '019a', '0f0e', '0211', '0220', '01fa', '0829', " +
            "'01f8', '017c', '014b', '055c', '0228', '0764', '0129', '016f', '09eb', '0218', " +
            "'019f', '09f5', '025e', '0238', '01ba', '01d5', '0238', '01c4', '031c', '0876', " +
            "'0099', '08e3', '0283', '0231', '0211', '0134', '0214', '0771', '01ad', '0b87', " +
            "'021c', '0192', '0162', '01d1', '0114', '01ab', '0199', '0f3d', '0222', '017e', " +
            "'019b', '0313', '0dab', '0166', '0132', '013d', '0246', '0244', '0193', '018a', " +
            "'0d39', '04df', '0246', '0c43', '0267', '0491', '0fd5', '0a4f', '01ee', '0103', " +
            "'0391', '0230', '015f', '0206', '0017', '07e4', '0114', '01da', '084a', '0b82', " +
            "'0990', '0171', '035b', '020a', '058d', '024f', '0266', '0173', '03fc', '01ec', " +
            "'012d', '031d', '0988', '0221', '0c8a', '0241', '0895', '01d6', '0e25', '014a', " +
            "'0112', '020d', '0269', '025a', '011d', '09cd', '0183', '0098', '0239', '0211', " +
            "'0113', '0e31', '0120', '0134', '0169', '0185', '01ec', '0f4c', '021d', '0fd4', " +
            "'0224', '089d', '022e', '0c68', '0253', '06bb', '011d', '018e', '0147', '01e2', " +
            "'0189', '01cc', '0990', '0ef3', '018e', '0794', '021c', '034b', '019e', '025e', " +
            "'01f8', '01cc', '022a', '0213', '0324', '01a9', '0846', '0ee2', '0137', '01d6', " +
            "'0160', '01ba', '015e', '023c', '01dc', '013b', '02ae', '018e', '0e08', '05df', " +
            "'0142', '0ae7', '020f', '054d', '0c9e', '0748', '0166', '01b4', '0220', '0246', " +
            "'0194', '01ed', '0251', '0234', '03e8', '019b', '090d', '0ac1', '0a12', '017f', " +
            "'0243', '01cb', '0483', '030b', '0222', '0583', '0849', '0186', '001b', '0deb', " +
            "'0ffb', '01e1', '01bb', '0f31', '0150', '08a3', '00a6', '01b6', '0c30', '0f7c', " +
            "'0218', '0839', '026a', '023b', '0b3f', '03fd', '0bf8', '01ec', '052a', '0201', " +
            "'01d6', '0af8', '00b7', '0883', '021b', '0e85', '090e', '001a', '0b5e', '0872', " +
            "'0197', '0146', '0692', '0bdd', '0153', '0269', '01c4', '01ca', '0278', '0c99', " +
            "'084f', '0158', '072e', '05f6', '01f7', '015b', '07a9', '090a', '01a6', '021d', " +
            "'0103', '0240', '021a', '017a', '097d', '0230', '06a2', '01f7', '0636', '00a3', " +
            "'0154', '0cb0', '01e5', '021a', '0102', '0256', '01a0', '023f', '011c', '0149', " +
            "'0226', '025a', '0fdd', '068c', '025c', '01d9', '0262', '0f0f', '0ecc', '0248', " +
            "'014d', '01c9', '0711', '0207', '0b1d', '0db8', '0016', '0ad3', '0260', '018d', " +
            "'0185', '023e', '0141', '0177', '0262', '084e', '0265', '0185', '06e3', '065e', " +
            "'0603', '0258', '0523', '013f', '0cf4', '0198', '01d5', '0214', '08be', '0662', " +
            "'0178', '0574', '09cd', '0468', '085b', '0466', '024a', '0e3c', '0209', '021a', " +
            "'013d', '09c6', '0bb0', '0168', '0222', '0108', '022f', '0117', '020a', '0198', " +
            "'0245', '0129', '0f72', '021d', '0bb9', '011e', '0142', '016a', '0761', '047d', " +
            "'0149', '0141', '0211', '018d', '0ac7', '0718', '01af', '022e', '06b8', '0e20', " +
            "'0835', '067e', '01b8', '0339', '08ab', '0100', '06dc', '06e4', '0127', '0252', " +
            "'019e', '01a9', '0f8a', '005a', '01ae', '01d9', '013b', '017d', '020c', '021b', " +
            "'0169', '01e6', '01d8', '0240', '0155', '0170', '01ab', '0e1c', '0c1d', '0107', " +
            "'011d', '0ce2', '0167', '0e8a', '01f6', '013e', '059c', '0bf1', '021e', '0200', " +
            "'010d', '0ecb', '017a', '021f', '0131', '0965', '0243', '09c9', '018f', '0df7', " +
            "'0194', '0af8', '0c0e', '06ef', '01b3', '057f', '0134', '052e', '05ec', '0a77', " +
            "'0ed6', '01e2', '0eb1', '040e', '04a1', '0ccc', '0ce8', '0136', '07c3', '0204', " +
            "'017b', '0165', '022a', '01c5', '01f7', '0133', '0ecd', '0b96', '0e8c', '0180', " +
            "'020e', '0691', '021a', '0185', '018e', '01a2', '012c', '0204', '01f5', '011d', " +
            "'0115', '0145', '0225', '0152', '0629', '0162', '0169', '0791', '0261', '0222', " +
            "'024c', '0624', '014e', '0227', '0157', '022b', '021b', '0c29', '01a7', '012c', " +
            "'01b9', '01b9', '028d', '0240', '04e2', '0289', '0120', '0151', '0255', '01ef', " +
            "'0112', '0206', '06b4', '0ae2', '0229', '0d60', '0c0f', '0100', '0257', '0232', " +
            "'0b19', '0198', '0254', '0239', '0236', '0381', '0aef', '0250', '01ad', '06bc', " +
            "'022f', '035d', '0135', '02ba', '0108', '0151', '0f51', '0fcc', '010e', '045d', " +
            "'0194', '024c', '0e7d', '0fc2', '0be5', '0139', '01b5', '0094', '0364', '0f75', " +
            "'015c', '0198', '05ee', '0b24', '06b3', '0b70', '01bb', '0248', '0d4a', '01a6', " +
            "'0105', '0446', '0f9b', '01bb', '077e', '01a9', '0178', '016d', '0202', '01fa', " +
            "'0234', '0149', '01c4', '012b', '01ed', '0a93', '012b', '0bff', '0214', '01d4', " +
            "'0c80', '088b', '0226', '08d7', '0260', '022a', '0218', '0217', '0b6d', '025c', " +
            "'01a9', '0bfa', '0186', '01dd', '0182', '0165', '00f2', '0d80', '00ed', '0a5f', " +
            "'0b42', '0809', '0b5a', '0251', '01e9', '021b', '0236', '014e', '0241', '01bc', " +
            "'0cc9', '06c6', '0166', '0148', '00ba', '0236', '00e9', '010c', '01ad', '01da', " +
            "'0459', '01bc', '0169', '0172', '0a06', '0e8f', '01ca', '0120', '09cc', '0193', " +
            "'06e9', '0169', '0187', '097c', '0224', '0239', '0a31', '0b54', '0b0f', '0f03', " +
            "'013f', '0236', '03d0', '0239', '0118', '01d2', '0fb2', '0e6c', '02a5', '0acb', " +
            "'0242', '0118', '0135', '0c9d', '022e', '01b4', '0c7b', '0253', '0232', '01b7', " +
            "'0ab1', '0f05', '010d', '08f6', '0140', '07a2', '01ae', '025b', '06fc', '020d', " +
            "'01cd', '0183', '0209', '0252', '0201', '0163', '0263', '01f8', '043d', '0b88', " +
            "'011a', '011d', '0118', '0171', '0363', '0106', '059c', '0fcc', '0173', '0256', " +
            "'0153', '0185', '061e', '024a', '0c2b', '014e', '01cf', '0106', '01b1', '01e9', " +
            "'01b3', '0973', '0140', '0231', '042a', '01e2', '0128', '0175', '0ce1', '0196', " +
            "'0cfc', '0154', '008a', '0996', '013e', '01a3', '0eb8', '0206', '0177', '020f', " +
            "'0138', '025b', '0d2b', '0221', '0cf1', '0253', '037a', '024b', '01b8', '0f89', " +
            "'08e8', '023b')"),
        new QueryInfo("/* *//* __QPM Query 7 */ SELECT MAX(unn3) AS max_7 FROM t3 WHERE " +
            "charCol3 IN ('0007', '0008', '0003', '0001', '0004', '0006', '000a', '0005', " +
            "'0005', '0007', '01f5', '0101', '043a', '022d', '01ea', '0211', '020d', '0113', " +
            "'0340', '0614', '01e8', '052a', '042a', '01f0', '0274', '0c83', '019c', '01d5', " +
            "'014f', '011e', '01d4', '025b', '0367', '0b7c', '0151', '07dd', '010f', '0266', " +
            "'0248', '0096', '024b', '04e1', '0fe9', '048d', '0186', '068a', '01b5', '0166', " +
            "'0178', '0152', '014e', '0268', '0110', '0159', '022b', '084f', '0135', '0195', " +
            "'0240', '02a6', '0150', '017b', '0d8c', '0caa', '0106', '0232', '01df', '0193', " +
            "'0ef7', '0479', '0123', '0e50', '01d7', '0238', '01b2', '0ffb', '010b', '01c0', " +
            "'0ce8', '01b2', '0d65', '023c', '020a', '016c', '0245', '01fc', '010a', '02e3', " +
            "'0415', '0c1b', '0983', '01ff', '01bc', '03a8', '0240', '01a3', '010c', '0179', " +
            "'01c4', '0259', '01eb', '0250', '01f6', '01f0', '01a5', '024a', '0197', '0163', " +
            "'015c', '01f1', '0090', '0264', '0edd', '025b', '0193', '0900', '0176', '0205', " +
            "'0139', '055f', '0246', '025c', '036f', '0239', '021e', '0228', '0146', '0704', " +
            "'018b', '023e', '0c8c', '0104', '01b9', '03ca', '01a5', '0476', '01a6', '01b2', " +
            "'033a', '013d', '01d9', '0273', '0171', '019b', '016a', '01a5', '020c', '0b21', " +
            "'0126', '0133', '0380', '01e9', '01f8', '0154', '01ab', '021b', '01dc', '08bd', " +
            "'06ca', '011f', '08ea', '0220', '0232', '017e', '018b', '029c', '01a6', '0159', " +
            "'011a', '0178', '0f56', '06a4', '0212', '025a', '0107', '0132', '0148', '0181', " +
            "'069e', '01cf', '0e71', '0260', '021b', '01cd', '0b55', '028e', '0b26', '0185', " +
            "'01e6', '01bd', '0113', '0da6', '019b', '0159', '0c5e', '0f55', '0204', '0242', " +
            "'0179', '0185', '025c', '0202', '0119', '017b', '00b4', '010e', '0221', '0caf', " +
            "'0188', '01da', '01e9', '01bf', '01ef', '01e8', '0169', '029e', '01e0', '01df', " +
            "'0c38', '0852', '0e81', '0032', '07f5', '013f', '01c0', '01fd', '01ea', '0e21', " +
            "'054b', '0198', '0170', '0eaa', '00b2', '01fc', '0f95', '022b', '042b', '0e8f', " +
            "'0113', '0103', '0533', '0ad8', '010f', '010c', '0215', '067e', '0e3f', '01e8', " +
            "'01f8', '0254', '018b', '01db', '04b9', '024b', '01ba', '0266', '024e', '0175', " +
            "'017d', '013d', '0cde', '015b', '01a8', '0141', '0253', '014b', '0e49', '06ff', " +
            "'024b', '012a', '0152', '0eb6', '0781', '04ea', '01c9', '01eb', '00a1', '0232', " +
            "'00f4', '0217', '0061', '04e6', '09f5', '01cf', '0121', '0a62', '0ae7', '0119', " +
            "'0642', '0ed3', '0133', '022b', '0203', '0a55', '019c', '0a4f', '01d2', '0230', " +
            "'015b', '0129', '06f3', '0108', '01ef', '0d07', '01a0', '08e0', '01b5', '0214', " +
            "'0625', '0168', '0b08', '02a1', '030f', '058f', '0180', '0764', '0a60', '0127', " +
            "'01fc', '0127', '016e', '0105', '06b2', '01dd', '076d', '0141', '0bab', '061f', " +
            "'0700', '01d8', '0265', '0144', '0222', '0200', '01f1', '0177', '00d0', '01c8', " +
            "'009f', '0165', '0866', '0029', '0171', '0215', '0a6b', '0108', '0e1e', '00c9', " +
            "'014a', '0159', '0235', '015d', '01dc', '0226', '01aa', '0128', '0235', '0173', " +
            "'017f', '01e2', '0135', '0214', '01cd', '015a', '024a', '0211', '0092', '01d1', " +
            "'06ba', '0085', '0263', '0157', '026a', '0189', '0181', '019b', '072a', '0666', " +
            "'0189', '0b60', '0391', '0f47', '01c7', '01a2', '01ec', '0226', '0f57', '0215', " +
            "'0a40', '0a21', '0152', '0205', '0117', '021e', '0218', '0b4b', '01ba', '014c', " +
            "'01ba', '01b4', '0107', '015a', '01d7', '0204', '0188', '03f8', '01e7', '0123', " +
            "'0241', '0b21', '01a7', '017a', '058c', '01b7', '0c77', '014d', '0ac5', '0246', " +
            "'015c', '01c8', '0f8e', '0bf8', '0110', '03a6', '02e6', '0127', '01c9', '0213', " +
            "'017a', '09e6', '012f', '0130', '0221', '0b8f', '024b', '0577', '01d8', '0104', " +
            "'0198', '022c', '0499', '0433', '01f9', '01b6', '0195', '022c', '0cb3', '0189', " +
            "'0226', '0181', '0114', '08a9', '0140', '01e0', '0c7c', '019d', '01d1', '024d', " +
            "'01c3', '01bd', '0166', '024e', '0d9c', '021d', '01fa', '01c3', '0148', '018a', " +
            "'01ff', '033c', '0160', '01e5', '0ee3', '08b7', '01be', '013d', '02c4', '0190', " +
            "'018e', '0126', '0258', '0678', '017d', '0131', '022f', '0114', '024c', '0760', " +
            "'0142', '025c', '018b', '0131', '04be', '0234', '0158', '0127', '0179', '0150', " +
            "'0146', '09b3', '0102', '08f9', '0638', '0ff6', '0d00', '0010', '01ce', '0565', " +
            "'0d8f', '0f17', '017d', '0161', '0ded', '0183', '0105', '0b09', '01cd', '0af3', " +
            "'013e', '01db', '0118', '01e8', '0175', '0b76', '0e46', '01a2', '0231', '0e5e', " +
            "'0a08', '0179', '0350', '0e48', '01c3', '01ac', '0707', '09f9', '0255', '0122', " +
            "'0ca6', '0166', '02e9', '0131', '0106', '024a', '01c8', '0912', '0052', '0137', " +
            "'02b1', '0f9d', '059a', '0269', '0209', '05f9', '013b', '01a4', '076e', '0ccb', " +
            "'0179', '0a7d', '018e', '01cc', '0e8d', '0148', '0241', '0137', '017c', '0d24', " +
            "'0069', '019a', '01c3', '0209', '01b0', '0168', '0117', '0237', '0c23', '021f', " +
            "'0103', '0417', '01d1', '055f', '04ae', '0afd', '00f7', '0131', '0dc6', '019e', " +
            "'0187', '0101', '01f4', '01c8', '025d', '0f92', '0262', '0b0a', '0800', '0d9b', " +
            "'0d44', '0190', '014f', '015c', '0128', '01b7', '0e06', '014a', '022e', '0216', " +
            "'0334', '0584', '010a', '0eaf', '023c', '010f', '021a', '0172', '01ac', '0219', " +
            "'03fd', '01c2', '016e', '012d', '023d', '0101', '0c28', '0249', '083b', '0140', " +
            "'0b46', '0113', '0f39', '0226', '01f7', '022e', '0a93', '070f', '08e0', '025a', " +
            "'0208', '010e', '0310', '0a88', '0114', '01ca', '05f8', '01c1', '0e00', '020a', " +
            "'0231', '0204', '01f9', '01a9', '0246', '0323', '0241', '013c', '0101', '04c9', " +
            "'011e', '0179', '0234', '01b7', '0177', '01bf', '0266', '04e8', '022f', '0263', " +
            "'09d0', '01b0', '0fdd', '0d40', '00bb', '0dc0', '01df', '0158', '05b0', '0a32', " +
            "'0195', '03f7', '0172', '09f3', '022f', '0f3f', '025f', '0255', '02e4', '01e5', " +
            "'05ad', '0a39', '0ddd', '0190', '013b', '01ff', '08b8', '0ad6', '01bb', '0f2a', " +
            "'022c', '000c', '0148', '0239', '01fd', '07fa', '0639', '0538', '045e', '0191', " +
            "'0aac', '08fc', '010c', '0182', '017e', '0834', '0948', '01f1', '01ee', '0567', " +
            "'0184', '0137', '0d47', '0239', '0262', '022a', '0552', '010a', '0103', '0253', " +
            "'0b1c', '06d6', '0214', '0123', '0f58', '0ea9', '010d', '0238', '01ba', '0107', " +
            "'054c', '012a', '011f', '0124', '018b', '024c', '0194', '0c32', '0220', '0180', " +
            "'0155', '01e8', '0233', '0ee3', '01ae', '01d1', '0228', '0190', '0176', '01de', " +
            "'06c7', '09c3', '0acb', '010e', '01fb', '0218', '074c', '01d9', '01a7', '01fd', " +
            "'0340', '0125', '0c88', '01cc', '0389', '019d', '0247', '0219', '0170', '0164', " +
            "'0191', '04a3', '0231', '0a67', '015c', '0e20', '0646', '0534', '0ce8', '024f', " +
            "'0138', '011a', '047b', '0190', '017a', '01e0', '0108', '0184', '010b', '0f30', " +
            "'0223', '023c', '0102', '0e7c', '0c63', '0218', '0246', '01e8', '0f8e', '004d', " +
            "'0412', '0935', '0d5a', '020e', '0524', '0138', '01bf', '07e3', '022b', '0949', " +
            "'0140', '0ab0', '0197', '01ab', '01e7', '019b', '01e9', '0bb0', '05e3', '0252', " +
            "'0181', '0246', '0161', '0151', '05a0', '000c', '018b', '018a', '067d', '04cf', " +
            "'021c', '0159', '0203', '021d', '023b', '0154', '07d0', '0206', '025e', '0158', " +
            "'0140', '0f6f', '01a6', '0170', '02f3', '0e82', '0205', '019b', '01a6', '010d', " +
            "'0204', '0025', '045d', '01d2', '00eb', '00d4', '0b23', '0863', '00e1', '0130', " +
            "'0164', '0366', '0208', '0480', '0123', '0a7a', '0482', '0130', '0236', '0960', " +
            "'0434', '0d77', '0222', '0631', '0183', '0182', '0566', '0111', '01fc', '01d7', " +
            "'012a', '025a', '01d7', '079a', '01ef', '0111', '019d', '0244', '01fc', '0933', " +
            "'0d15', '0841', '021a', '0251', '0266', '0a66', '0bbf', '01cb', '01b5', '0127', " +
            "'0018', '015d', '0108', '0169', '0236', '018d', '0895', '014b', '0144', '025a', " +
            "'021e', '09cf', '015e', '0102', '07a2', '0868', '0177', '017e', '021a', '0227', " +
            "'001e', '0287', '096e', '00cb', '0126', '0239', '014d', '0229', '0de3', '0149', " +
            "'0219', '096d', '07c3', '09b7', '0b15', '0ce2', '01b4', '05f8', '014f', '09db', " +
            "'0122', '013d', '0161', '01e7', '0996', '045c', '0175', '0128', '090d', '06b1', " +
            "'01af', '0257', '0227', '0803', '0218', '020d', '014e', '04df', '08be', '02c0', " +
            "'010f', '0ed9', '0245', '0195', '0980', '0134', '01a0', '07b6', '02ac', '0104', " +
            "'0105', '0800', '024b', '01ac', '013a', '054b', '0252', '024c', '09e5', '0233', " +
            "'01cf', '01fb', '001d', '07a8', '0ac7', '0583', '08e4', '01c0', '0209', '0211', " +
            "'01af', '00f5', '0251', '01f1', '0189', '01f9', '0f20', '0293', '0237', '0161', " +
            "'0e81', '05ad', '01a7', '01fd', '01e7', '0b7c', '0bfb', '0837', '0716', '01aa', " +
            "'011b', '01ae', '015a', '01be', '0209', '014f', '068a', '0dee', '0264', '047e', " +
            "'01dc', '0172', '0a64', '01de', '0151', '0196', '074a', '0111', '011d', '0191', " +
            "'0340', '073b', '0cf9', '0118', '0246', '0154', '01ae', '01a6', '0e87', '09df', " +
            "'0ffe', '0b49', '011c', '0ea0', '01ea', '0110', '0248', '0ab6', '017d', '0241', " +
            "'0749', '01bc', '0681', '010a', '045e', '03ff', '03d2', '0114', '025f', '0155', " +
            "'0134', '0250', '0170', '01e4', '0d68', '0198', '01ed', '0103', '0163', '0a36', " +
            "'0171', '0182', '01cc', '01fa', '012f', '0241', '0fd3', '0b51', '04c7', '0470', " +
            "'017a', '0126', '01c4', '0302', '09af', '0d36', '0121', '092f', '0127', '023c', " +
            "'01ce', '0129', '01bf', '012f', '07e9', '01ec', '0151', '0133', '09be', '0239', " +
            "'017e', '0868', '01e3', '0205', '0c4e', '0b86', '0147', '0546', '0255', '014b', " +
            "'01ea', '01ea', '0202', '0259', '052c', '0784', '0201', '0203', '014c', '0110', " +
            "'01dd', '090c', '0172', '0206', '01f6', '017c', '0732', '01d7', '0652', '0150', " +
            "'0165', '07d2', '0221', '0010', '019e', '0160', '0ec2', '090f', '0831', '01d8', " +
            "'0395', '0689', '0203', '0e73', '0125', '0457', '0205', '096a', '01f4', '016c', " +
            "'0fb0', '050f', '012b', '0ea9', '06b7', '0e51', '0cd9', '0215', '01c4', '0473', " +
            "'06df', '017c', '0126', '01f3', '02ef', '0982', '0161', '0243', '017d', '014e', " +
            "'08ec', '0208', '01b9', '0c09', '0151', '0760', '01ec', '012b', '020d', '02b0', " +
            "'01be', '0ae7', '0251', '018a', '01e5', '0a25', '0170', '0472', '0197', '0c20', " +
            "'07da', '0e51', '04a3', '0228', '0d5e', '071f', '01d0', '01ba', '0117', '07b9', " +
            "'062f', '0c5f', '09b6', '0164', '016b', '022a', '01c9', '021e', '00be', '01d6', " +
            "'06ba', '01fb', '020d', '06fb', '0122', '021a', '03dc', '0157', '0195', '01de', " +
            "'0117', '057f', '0e36', '0149', '01c1', '033a', '0841', '021b', '014f', '048c', " +
            "'0957', '01ce', '09da', '07a9', '0574', '01ce', '01bb', '0ede', '010c', '025b', " +
            "'018a', '046f')"),
        new QueryInfo("/* *//* __QPM Query 8 */ SELECT MAX(b3) AS max_8 FROM t3 WHERE " +
            "charCol3 IN ('0005', '0004', '0005', '0005', '0009', '0005', '0004', '0008', " +
            "'0006', '0006', '0214', '0d44', '01cc', '0f80', '0109', '01b5', '0a05', '0135', " +
            "'045e', '0bf4', '0db5', '01c8', '011c', '012e', '015b', '07b3', '024b', '0137', " +
            "'070d', '0245', '01d1', '01e7', '014b', '00d3', '01a8', '0200', '08f3', '0bb5', " +
            "'06b3', '0710', '038a', '0146', '098c', '0cde', '011b', '01e8', '0211', '01ac', " +
            "'0197', '0a4a', '0cb3', '093c', '0834', '08da', '03d4', '018e', '018f', '011e', " +
            "'01d2', '0145', '041e', '0d3a', '024e', '01d6', '0143', '025e', '06de', '0895', " +
            "'016d', '0237', '0d02', '01a9', '0afc', '00b0', '0261', '0faf', '0ffe', '020c', " +
            "'0ede', '016f', '011b', '0088', '0498', '09ef', '048f', '0b9a', '0f53', '0745', " +
            "'011f', '01ae', '0547', '0122', '0597', '0186', '023e', '09f0', '080f', '05aa', " +
            "'0158', '0241', '0374', '013f', '0e4b', '019d', '01e6', '0201', '0226', '015f', " +
            "'04ff', '0169', '017d', '0103', '01bd', '011d', '0187', '0131', '015e', '0234', " +
            "'0216', '0797', '01cf', '015c', '01dd', '03ff', '013e', '01f6', '0eaa', '093c', " +
            "'01db', '018b', '0139', '0190', '057e', '0185', '0186', '0143', '01c8', '022f', " +
            "'01f9', '06c2', '0177', '0159', '011d', '0114', '006e', '0189', '0cc7', '0129', " +
            "'01a0', '01fb', '0195', '094b', '01aa', '091c', '0101', '0499', '0133', '0264', " +
            "'0110', '0116', '01f0', '03ac', '0157', '0170', '0d3b', '0122', '0110', '0118', " +
            "'01cf', '0e59', '0191', '0683', '0ad3', '0bfb', '012a', '01e9', '0096', '0d39', " +
            "'026a', '0f73', '01ae', '022a', '0358', '01dc', '01af', '0257', '0aba', '01d4', " +
            "'014a', '0321', '01ce', '0178', '0de1', '0317', '0c38', '0565', '011e', '0676', " +
            "'025f', '019e', '01e6', '01ee', '08d9', '06a9', '0102', '0207', '01db', '0190', " +
            "'011a', '0e3d', '010c', '079a', '011b', '0862', '0c3b', '022f', '0e4e', '0f74', " +
            "'01e2', '0240', '010d', '025f', '01ab', '01ea', '0760', '069b', '08b6', '0235', " +
            "'0973', '009f', '0af2', '012c', '0b79', '0d1d', '01b8', '0257', '0333', '0b09', " +
            "'01a0', '0122', '011f', '0b30', '0114', '0247', '0119', '017b', '068e', '066b', " +
            "'0244', '01a8', '0241', '0f09', '0db7', '0178', '0ee3', '0164', '0140', '010c', " +
            "'0255', '0ed5', '0bf8', '036a', '021c', '0175', '01ce', '010e', '0610', '014e', " +
            "'0f62', '0225', '01e1', '0193', '0219', '00e6', '014c', '022b', '01b7', '0de0', " +
            "'0181', '0267', '0190', '058e', '0212', '0b57', '01ff', '015e', '0a4d', '0128', " +
            "'0a67', '020c', '0250', '0c40', '0210', '063f', '0b69', '05fc', '065a', '0215', " +
            "'0253', '020c', '0116', '0159', '01dc', '0122', '021a', '0aa4', '0156', '01cd', " +
            "'0bbe', '0206', '0152', '0eec', '024e', '0115', '063b', '0264', '011a', '01e5', " +
            "'0878', '01f0', '0ded', '0844', '0522', '0db3', '01ad', '0167', '074a', '041d', " +
            "'09ae', '01ec', '0179', '0bc0', '023e', '013c', '018a', '019e', '0219', '0832', " +
            "'03b8', '088c', '052f', '0225', '0558', '01a6', '0120', '094f', '0420', '0870', " +
            "'019f', '01ee', '015b', '0165', '015a', '0b14', '01a6', '01a9', '0e5d', '039a', " +
            "'0ec4', '0507', '0a15', '060b', '0702', '0428', '01d7', '01e1', '0151', '03f8', " +
            "'0ff7', '012e', '0edd', '0100', '08d2', '0ebf', '0213', '0376', '0a51', '07fa', " +
            "'0111', '022f', '016a', '092d', '040a', '05a0', '0c88', '01c5', '0038', '0d57', " +
            "'011e', '0195', '0222', '0128', '0226', '0487', '0074', '016a', '0216', '09b0', " +
            "'01ed', '024d', '014f', '0306', '0a88', '010b', '03e5', '06b1', '0251', '0fa1', " +
            "'0193', '015f', '022b', '0e2b', '0193', '0fc8', '022e', '0248', '0116', '0016', " +
            "'055b', '016f', '0207', '012b', '01e6', '01cf', '01dd', '0196', '0162', '0b95', " +
            "'0b88', '0151', '024c', '01c5', '0100', '025e', '079f', '022f', '0bd6', '04f3', " +
            "'01f2', '01b7', '09c4', '0527', '01ff', '0189', '0691', '0d62', '01a2', '0127', " +
            "'0b46', '0229', '03c9', '0245', '073a', '0197', '0a8e', '0163', '01aa', '015c', " +
            "'0119', '0a00', '0442', '018d', '0110', '012f', '010a', '012c', '0198', '01cf', " +
            "'0145', '013f', '01cb', '016d', '0165', '0122', '0104', '0269', '0106', '0dc6', " +
            "'0fdc', '0e48', '09bb', '0204', '0105', '0150', '0be7', '01e4', '017a', '01c1', " +
            "'01bb', '01e9', '019c', '017d', '06c2', '0753', '023e', '0223', '01df', '0182', " +
            "'0194', '011c', '018c', '05b1', '0230', '0249', '0116', '0176', '020c', '0978', " +
            "'0141', '0162', '0ddd', '0631', '0f2e', '0240', '0144', '01cf', '0152', '03ee', " +
            "'01be', '017b', '012c', '0583', '01d1', '010c', '0189', '0898', '01f6', '0137', " +
            "'0149', '0178', '0233', '014c', '0107', '024f', '0206', '024a', '0438', '0227', " +
            "'0132', '0bb0', '0c1a', '03ab', '08bc', '0929', '0243', '0c4e', '0265', '0199', " +
            "'015c', '01a4', '022d', '0192', '0125', '01ae', '0124', '0d16', '01ca', '0219', " +
            "'01b0', '021a', '0f58', '0b06', '0039', '0b47', '01f2', '02ab', '017f', '01d1', " +
            "'0148', '0136', '0f07', '0d34', '000e', '01c4', '0353', '015a', '045f', '01ba', " +
            "'07db', '021b', '0de4', '0252', '022a', '0185', '0d71', '0224', '0137', '0256', " +
            "'01e8', '017e', '0530', '012e', '025d', '023e', '01f6', '0929', '05c4', '016f', " +
            "'0c28', '0fb0', '0175', '0248', '01c1', '017b', '01c0', '018c', '0181', '0129', " +
            "'01fa', '048a', '06c9', '0ad1', '0234', '0137', '019f', '002f', '0227', '0294', " +
            "'0940', '0854', '08f8', '0a08', '01e4', '01d2', '01c4', '0165', '0ae6', '098b', " +
            "'01cd', '05db', '01b4', '01cb', '0165', '090a', '015b', '0348', '025c', '0d74', " +
            "'0d6e', '0a28', '0238', '0188', '0d78', '0ef2', '07eb', '0599', '0222', '0126', " +
            "'0232', '023b', '041c', '0226', '0112', '0769', '0256', '0165', '099b', '01e4', " +
            "'0259', '0ac3', '005a', '07b7', '04bf', '01cd', '0a5c', '01a1', '0137', '0a14', " +
            "'01be', '0837', '04b9', '0b71', '0257', '014b', '01c8', '0c8d', '013b', '0fcf', " +
            "'013c', '0135', '014f', '01f0', '0259', '0da2', '0e0e', '01d3', '0fd6', '0ead', " +
            "'0204', '0413', '017a', '0b78', '0243', '0255', '06b7', '01c2', '019a', '078b', " +
            "'01e9', '0149', '0152', '0252', '0130', '02ee', '011c', '0167', '0269', '08fe', " +
            "'063d', '0186', '0254', '0255', '0abc', '09f9', '0696', '01ae', '0145', '022d', " +
            "'0230', '01f4', '0384', '02d1', '0f94', '0144', '0c6b', '0ae9', '020e', '0213', " +
            "'01cd', '0a95', '02d9', '0fe7', '025c', '0c8b', '01c9', '09f4', '01a0', '0ef3', " +
            "'01d1', '01c9', '0b6f', '0846', '0722', '0267', '0100', '025a', '0e95', '022e', " +
            "'031d', '0238', '0c86', '0237', '01bf', '0407', '0104', '012d', '0073', '0238', " +
            "'0234', '0eed', '08c4', '0c5d', '0167', '0264', '0164', '073b', '01c4', '0247', " +
            "'0c25', '01af', '016d', '018b', '0235', '0256', '0dca', '021c', '0367', '01f1', " +
            "'0101', '022b', '0d07', '026a', '0eb2', '0ebe', '0165', '0159', '020a', '0e72', " +
            "'01ff', '0253', '0bc2', '0256', '01aa', '021c', '04eb', '0421', '016e', '016d', " +
            "'018b', '0c0b', '09ac', '008d', '09f3', '01cb', '0261', '0806', '025f', '0f34', " +
            "'069a', '01c5', '0122', '0176', '0a74', '0c38', '04ba', '018a', '02ee', '0070', " +
            "'01b7', '0181', '01e0', '0144', '0cc1', '01ff', '0394', '0164', '0221', '01c1', " +
            "'022c', '01b4', '0199', '0136', '0f28', '07b4', '061d', '0144', '013d', '09f7', " +
            "'0d64', '0d40', '0ad9', '0192', '014f', '0197', '0094', '0209', '01c6', '0247', " +
            "'018e', '0252', '0246', '01fc', '0cd0', '0b40', '015e', '0146', '01ee', '01dc', " +
            "'0151', '046f', '01a4', '0231', '016d', '026e', '016c', '023a', '01f7', '0822', " +
            "'01d0', '016d', '0181', '074c', '019a', '0104', '030e', '025e', '0260', '0134', " +
            "'0877', '0122', '01b3', '0242', '03e2', '022f', '0244', '0be0', '010f', '0189', " +
            "'0208', '024c', '090f', '01fb', '0fa3', '066c', '01c4', '0262', '0ce9', '0965', " +
            "'0149', '090b', '01da', '012c', '0218', '0917', '018f', '0140', '037d', '0209', " +
            "'0124', '0151', '018e', '01af', '01be', '0a94', '020e', '021b', '08e7', '0103', " +
            "'010e', '01b3', '01c0', '013e', '0193', '05b3', '01fe', '08ac', '0919', '0474', " +
            "'0290', '0094', '0237', '0197', '0243', '0555', '0242', '01d2', '0142', '01ed', " +
            "'02ad', '010b', '01c5', '010d', '01be', '014a', '0171', '0168', '0202', '0190', " +
            "'008d', '01b9', '020d', '0218', '01d3', '025e', '0112', '01a9', '01e6', '0c0e', " +
            "'011f', '01de', '0155', '075c', '0201', '0100', '019d', '0003', '015c', '0118', " +
            "'017a', '012d', '011b', '061d', '06cb', '014b', '019f', '0260', '0185', '0184', " +
            "'01cb', '0214', '010b', '01b9', '014f', '0ac6', '0a8e', '020c', '010f', '0183', " +
            "'011e', '0258', '0098', '0254', '0245', '012b', '0a43', '0ed9', '021b', '0233', " +
            "'0226', '07de', '01b4', '0101', '011d', '019a', '0d48', '0187', '03df', '01d0', " +
            "'01e9', '014c', '020d', '0232', '0213', '01bd', '0b3e', '0132', '01d0', '00b5', " +
            "'0104', '0132', '0c00', '0d97', '01ab', '0172', '0b19', '0d60', '024a', '0180', " +
            "'06f2', '0eb4', '021b', '0220', '00f8', '02e3', '0f4d', '08d8', '01b5', '08ba', " +
            "'053c', '015f', '03c8', '06ab', '0128', '0219', '01a6', '0902', '0151', '02b0', " +
            "'0fff', '0b63', '0637', '0d1c', '0218', '0eab', '017e', '07d1', '019b', '0920', " +
            "'0196', '0193', '0203', '0229', '017d', '0d8b', '015f', '01fc', '0103', '011f', " +
            "'09d9', '01b8', '015e', '0c0c', '00e3', '012c', '01c3', '0e64', '0a22', '015f', " +
            "'0a1c', '01f8', '0120', '01f0', '0be4', '0077', '04e5', '0a57', '013d', '045c', " +
            "'012c', '0247', '0179', '014d', '0464', '0111', '08fe', '0191', '06e3', '0253', " +
            "'09db', '0127', '024d', '0474', '01c2', '0227', '011c', '0127', '0144', '015a', " +
            "'0165', '0b39', '0156', '01af', '0733', '010e', '0181', '06dc', '01a6', '0740', " +
            "'0245', '01f0', '05b8', '069e', '0b7d', '018e', '0161', '0866', '0c14', '0265', " +
            "'0044', '01d7', '01cc', '01c5', '0cc7', '012c', '0179', '010c', '0bb4', '0225', " +
            "'0182', '0127', '0d78', '0ea1', '01fb', '0254', '0175', '01b7', '0137', '0789', " +
            "'01ca', '0145', '020b', '0b8e', '01a2', '0161', '0126', '025c', '01aa', '015b', " +
            "'013f', '00ef', '0edc', '0dd6', '07ad', '0125', '017e', '0229', '01c8', '01cb', " +
            "'0237', '0139', '01ea', '01eb', '021b', '015b', '0e93', '0259', '0e1d', '0383', " +
            "'0163', '04b4', '06f7', '0ad1', '0107', '0258', '023b', '0170', '0262', '09a6', " +
            "'09c6', '01b2', '0242', '0121', '014b', '043a', '0163', '0185', '0001', '00ce', " +
            "'0200', '01be', '0ad1', '0760', '089d', '01f6', '020b', '0175', '0201', '013f', " +
            "'017b', '0315', '0215', '0a93', '015f', '020c', '01ed', '0a32', '0602', '0149', " +
            "'0115', '0132', '0126', '01ef', '0905', '03a7', '03c7', '0f74', '023b', '01ac', " +
            "'0f5a', '0267', '0fa1', '0ce0', '01d2', '0076', '0942', '011c', '0681', '020e', " +
            "'051b', '00e2', '0c82', '0223', '0159', '019a', '014a', '005c', '01ed', '0191', " +
            "'058a', '0bc6')"),
        new QueryInfo("/* *//* __QPM Query 9 */ SELECT MAX( + a1) AS max_9 FROM t1 WHERE " +
            "charCol1 IN ('0002', '0006', '000a', '0004', '0001', '0006', '0001', '0002', " +
            "'0003', '0005', '0b30', '0bbf', '01d8', '01fe', '0b72', '014c', '05d0', '01bb', " +
            "'0158', '0218', '0a04', '0c39', '010c', '0214', '017c', '01f7', '01b3', '025d', " +
            "'0177', '0945', '018f', '012b', '08d8', '0136', '0150', '0267', '01f1', '0cea', " +
            "'0742', '0fb5', '0216', '012a', '01f9', '0186', '0682', '013e', '0e04', '0615', " +
            "'0145', '08d1', '0b8b', '0841', '013d', '0289', '0069', '01d0', '0e04', '0194', " +
            "'0506', '0b33', '0210', '019c', '0e68', '01fd', '0219', '026a', '0455', '0a08', " +
            "'024d', '0165', '0100', '0227', '01b2', '0146', '0171', '0b78', '05ed', '0116', " +
            "'0817', '0d4f', '014c', '015d', '0151', '0217', '017f', '01e9', '0165', '03cb', " +
            "'0150', '025b', '0a5f', '0250', '010a', '0269', '011b', '012a', '0e91', '084e', " +
            "'094c', '01ff', '01a7', '0263', '01be', '0626', '0879', '0ed8', '018c', '0e15', " +
            "'0c15', '0b86', '0432', '01d4', '0197', '0f51', '01bc', '0397', '0af5', '010d', " +
            "'023c', '0d12', '0141', '01e8', '0118', '013f', '0f2f', '0cfe', '0eec', '0831', " +
            "'01c7', '0004', '01b5', '0253', '0180', '0bd3', '0ec4', '0263', '0229', '0153', " +
            "'0172', '0114', '016c', '0127', '019a', '0463', '0156', '0cd6', '016e', '0240', " +
            "'0262', '01a7', '0122', '0afa', '024a', '020c', '00ae', '01a2', '0226', '01e8', " +
            "'0ff3', '091f', '0b52', '0d5c', '0144', '0251', '0ecc', '0374', '0156', '0106', " +
            "'057f', '0796', '0157', '0e6e', '0175', '01e0', '010b', '0255', '01f7', '01b0', " +
            "'0129', '01d6', '025e', '018c', '0104', '024f', '025c', '0248', '01d2', '018d', " +
            "'0142', '018c', '059d', '01c0', '0237', '01dd', '09d2', '06e7', '023d', '0158', " +
            "'0652', '0056', '0167', '018c', '019a', '0190', '0bed', '0dee', '0f8b', '012a', " +
            "'0dcd', '01ec', '0132', '0efd', '013b', '0d3a', '042c', '0212', '0165', '0168', " +
            "'018d', '023a', '081c', '0253', '01f6', '01bc', '0160', '01f7', '0113', '0239', " +
            "'0101', '01c8', '0143', '0112', '0148', '0151', '00ad', '0d33', '01a5', '0248', " +
            "'0115', '065c', '0241', '06b5', '0190', '023a', '01e8', '0e4b', '062a', '046b', " +
            "'0e1e', '01bd', '0e4d', '00c1', '025b', '080f', '080b', '05fd', '015b', '0240', " +
            "'0245', '01b0', '01ac', '012a', '023f', '015b', '055f', '0246', '019e', '0e4e', " +
            "'0155', '0028', '0daa', '0012', '0ba6', '0208', '07bd', '097a', '0206', '016c', " +
            "'014f', '01ad', '027e', '01fc', '04d9', '0e7d', '025f', '0236', '074e', '0120', " +
            "'01c5', '01f5', '0195', '0a66', '024b', '011d', '0f8e', '01b4', '0245', '0225', " +
            "'0140', '0228', '08b6', '0112', '017c', '01c2', '01ab', '0229', '01fc', '01e0', " +
            "'0761', '017e', '0561', '03cb', '01bb', '0250', '0156', '01c5', '0152', '0215', " +
            "'01c7', '0245', '01e1', '0124', '01fb', '0ab6', '0bff', '0835', '0149', '0f69', " +
            "'001d', '051a', '01db', '01de', '0b53', '0212', '0829', '0248', '0170', '01e3', " +
            "'08bd', '015a', '0147', '0ba8', '0264', '024e', '019c', '02d5', '069a', '0252', " +
            "'0551', '01ab', '01ca', '023d', '0243', '0239', '01a4', '0f03', '01aa', '0e63', " +
            "'0204', '00f9', '03d7', '02a3', '0a38', '01ba', '0205', '05f9', '0ebb', '01b3', " +
            "'0bf5', '07ff', '03a8', '0a4a', '013e', '0123', '01f5', '01b3', '0f32', '0153', " +
            "'0255', '0258', '0269', '0cdb', '0237', '0352', '04f2', '01da', '01ef', '0bb0', " +
            "'0635', '021d', '05e9', '016d', '0249', '015c', '0268', '0194', '056f', '03a4', " +
            "'01ce', '011c', '017a', '0ff4', '017f', '0188', '0702', '02eb', '021a', '0783', " +
            "'0255', '045d', '024b', '01ee', '0176', '0174', '0eed', '017c', '0167', '0270', " +
            "'0151', '0241', '0116', '0e90', '04fe', '0129', '0699', '0133', '09b4', '01b4', " +
            "'01b2', '07fe', '01f0', '03f1', '0222', '0150', '0197', '0268', '01a3', '0973', " +
            "'0566', '016d', '067f', '0645', '01c0', '0155', '01d6', '0e5f', '0170', '0216', " +
            "'0823', '00ae', '0d95', '0ad9', '0090', '0038', '0fc7', '0229', '01c9', '0260', " +
            "'01dd', '01fc', '0121', '0f9e', '025d', '0100', '026a', '0b5d', '02a7', '0169', " +
            "'01b8', '025f', '0261', '017b', '011e', '01aa', '0143', '0166', '01ed', '01ab', " +
            "'09ff', '0200', '01d0', '0be0', '0272', '008c', '03cb', '0134', '0b06', '0890', " +
            "'0d7e', '0213', '010f', '014e', '019b', '0940', '0129', '020f', '0198', '0103', " +
            "'0121', '019e', '0183', '01b4', '022d', '01b9', '0126', '017d', '0915', '06fb', " +
            "'06ed', '0252', '00e5', '00ab', '000f', '091f', '018c', '012b', '06e1', '0210', " +
            "'0104', '018a', '023f', '09b5', '088e', '0ab0', '0240', '023b', '025b', '01c8', " +
            "'03a9', '0109', '015e', '03c1', '0189', '0138', '01f4', '0a1d', '0241', '059d', " +
            "'01e7', '0169', '0c6a', '023b', '01dc', '01ca', '03d8', '0056', '010a', '0a87', " +
            "'0223', '0bbe', '0216', '091a', '0138', '015b', '01a8', '022a', '0053', '037e', " +
            "'09e5', '022a', '01bd', '022d', '03bc', '020b', '01ed', '0186', '0ec2', '0911', " +
            "'0f5e', '0a5e', '01ca', '0168', '02b1', '0d39', '0e6f', '0c11', '0127', '01ea', " +
            "'0231', '0121', '01ea', '012b', '0188', '0171', '0fa5', '01b5', '0c77', '0241', " +
            "'011a', '0199', '0168', '013b', '0236', '0590', '024b', '07db', '0111', '045d', " +
            "'0234', '0245', '01bc', '015c', '0119', '01a8', '0122', '0e1f', '0837', '0125', " +
            "'01ee', '0236', '021b', '0111', '0ce6', '0238', '04ff', '0e36', '014c', '01ca', " +
            "'019b', '011d', '01d3', '0a7a', '021b', '08a1', '01af', '0241', '025e', '01f5', " +
            "'09ff', '01ed', '0543', '0a82', '012d', '0199', '01c7', '0178', '0a2e', '0213', " +
            "'0104', '0127', '0114', '0378', '0b91', '0192', '0132', '0b9f', '00a4', '024b', " +
            "'00b7', '0261', '0138', '0127', '03cd', '005b', '025e', '0cd8', '04fe', '0743', " +
            "'007e', '01de', '0234', '0211', '0190', '01a3', '0b22', '08a8', '0235', '0b54', " +
            "'0160', '024c', '0166', '0456', '0c06', '01fe', '0168', '0587', '0224', '0206', " +
            "'0cd8', '08c3', '012d', '010d', '0147', '0431', '0943', '0234', '020d', '01b8', " +
            "'0cb4', '0215', '022d', '0212', '0261', '024f', '0154', '0146', '0376', '01bd', " +
            "'025e', '0493', '03a2', '0193', '0b2d', '0192', '0221', '0138', '0349', '013c', " +
            "'0135', '01e3', '0bfc', '052e', '0c35', '01e7', '0bf8', '0ae9', '018d', '0199', " +
            "'0f5c', '01dd', '01c1', '0091', '016a', '04c5', '01f1', '024e', '0390', '006d', " +
            "'09cc', '0177', '0722', '0159', '0114', '01dd', '010e', '019e', '066b', '01e6', " +
            "'01d1', '09e3', '0844', '0168', '08e4', '0e27', '0117', '0132', '00d3', '0205', " +
            "'0ba2', '0eea', '0c3a', '0223', '0188', '00ce', '0803', '0105', '0cc1', '01ae', " +
            "'0ceb', '0141', '0176', '0e18', '0145', '01e9', '012e', '025a', '089c', '06a7', " +
            "'01cf', '0e0a', '025f', '013a', '022b', '01fe', '0820', '02b5', '0261', '01c1', " +
            "'01c9', '0262', '002f', '0054', '0791', '0446', '01f1', '0245', '023a', '0e4b', " +
            "'0f47', '0192', '067c', '0c65', '0170', '012f', '0195', '05fb', '015e', '0254', " +
            "'01ce', '0247', '0233', '0c4e', '030e', '0e41', '0174', '0269', '0253', '04ca', " +
            "'01c5', '023e', '01df', '019a', '0173', '0ff8', '0141', '0205', '0760', '0eac', " +
            "'09d3', '0237', '020f', '0f00', '0265', '0d67', '0363', '093d', '0982', '0241', " +
            "'010f', '021a', '0202', '01b4', '0b1c', '024d', '01e8', '085f', '0168', '038c', " +
            "'03da', '0058', '0775', '0126', '01fc', '0693', '0169', '018f', '0110', '0e3c', " +
            "'019d', '0139', '0157', '0d2a', '012b', '08d3', '0224', '0234', '0198', '0217', " +
            "'019f', '0180', '06ad', '01c7', '0066', '01aa', '0352', '0642', '0d06', '01b8', " +
            "'084e', '01f1', '0c76', '0264', '044f', '0521', '010f', '013c', '06f7', '02a0', " +
            "'0256', '0690', '0a7a', '0119', '0194', '0967', '01aa', '016f', '015d', '0233', " +
            "'017f', '0182', '011b', '010a', '0206', '024e', '01da', '0232', '021b', '0176', " +
            "'0246', '024d', '011d', '0c58', '016a', '08a6', '0119', '08f8', '0c73', '01f9', " +
            "'0194', '023c', '08e7', '0155', '0d33', '0bb2', '01e9', '03c4', '0d1e', '0194', " +
            "'0731', '0156', '0152', '02fb', '016c', '00c9', '0194', '0103', '019c', '0110', " +
            "'023b', '01f4', '0118', '0250', '0237', '0972', '0587', '01ff', '010c', '0cda', " +
            "'0244', '09fb', '0195', '010f', '0cab', '022f', '019a', '044e', '0178', '011e', " +
            "'0813', '074b', '03b8', '027a', '022e', '020e', '0236', '0145', '01dd', '01a2', " +
            "'024b', '017e', '0661', '011f', '09dd', '0230', '0161', '0d7e', '01a6', '0f12', " +
            "'0110', '01dc', '01cc', '0245', '0c03', '01e2', '0833', '0143', '0ea4', '0d51', " +
            "'0eea', '0171', '016d', '0144', '016a', '0b7d', '0006', '0aaa', '0246', '0503', " +
            "'01ac', '013d', '0b04', '0103', '024c', '0242', '01f5', '029f', '0155', '00b2', " +
            "'01cd', '01e5', '0bb8', '0162', '0b81', '01bc', '023f', '04c1', '0103', '0b49', " +
            "'0ecf', '0b82', '05c2', '0d83', '01d1', '023a', '0175', '022d', '0c9b', '0144', " +
            "'012e', '0148', '025a', '0aab', '016c', '0962', '00cd', '01ea', '0ce4', '022d', " +
            "'0143', '016f', '0b0f', '0d1a', '02eb', '040f', '0c6d', '0167', '022a', '0265', " +
            "'06ef', '09cf', '0107', '006f', '01b8', '0160', '0147', '00d6', '01b0', '09d8', " +
            "'0969', '0108', '013c', '0252', '0159', '0131', '018e', '01fa', '04d5', '065a', " +
            "'0258', '0110', '0230', '012c', '01bc', '0145', '088b', '009c', '0202', '0262', " +
            "'00fe', '0227', '008b', '0146', '0153', '0617', '023a', '078d', '011c', '0c78', " +
            "'0220', '01a1', '012e', '020a', '01f2', '0142', '087c', '05c7', '017d', '0161', " +
            "'0705', '023c', '0233', '0229', '022b', '0148', '0224', '014d', '0263', '0312', " +
            "'02da', '0849', '0512', '0845', '0239', '021a', '049a', '023e', '0200', '0c5c', " +
            "'0bc9', '0152', '0194', '048a', '01b9', '0ac5', '08f5', '0129', '01ac', '02cc', " +
            "'0200', '01f0', '097b', '078a', '01fe', '016c', '020e', '012e', '01a8', '0230', " +
            "'0177', '0187', '020a', '0182', '0152', '017f', '0168', '0255', '0d77', '017a', " +
            "'0ef8', '06ae', '0133', '0415', '0179', '0200', '0144', '0349', '016b', '0f24', " +
            "'022a', '0e25', '0239', '0b60', '022d', '01a9', '0215', '0deb', '01fe', '0b15', " +
            "'0211', '0207', '0ab9', '017c', '01eb', '0a76', '024f', '06d7', '04b3', '020c', " +
            "'01c2', '0222', '024d', '0c1a', '01e5', '0a54', '0bbb', '0142', '01b4', '018b', " +
            "'0126', '0d44', '0677', '0680', '06ae', '0319', '01d2', '00f8', '0a24', '0257', " +
            "'0264', '0b65', '0246', '0157', '04b7', '0b22', '0122', '0245', '01d8', '06a6', " +
            "'0109', '0e85', '0e19', '01a6', '01d5', '0274', '0b3c', '01f8', '023c', '0165', " +
            "'06f7', '05b9', '0127', '0dd7', '0100', '014e', '0350', '0234', '0178', '0a47', " +
            "'0225', '0af5', '0106', '0259', '07e9', '01dc', '03b4', '057b', '013b', '023d', " +
            "'0156', '017d', '09f3', '018f', '0256', '0cef', '0231', '0878', '019f', '028d', " +
            "'06cc', '0d2a', '023c', '0a28', '0c56', '01a3', '012a', '015d', '01bf', '0241', " +
            "'01f0', '0007')"),
        new QueryInfo("/* *//* __QPM Query 10 */ SELECT MAX(d3 + c3) AS max_10 FROM t3 " +
            "WHERE charCol3 IN ('0001', '0004', '0003', '0001', '0008', '0006', '000a', " +
            "'0001', '0003', '0008', '015e', '0f7f', '0529', '02da', '01e2', '0bd9', '020a', " +
            "'019f', '0d9d', '03a4', '011a', '08cb', '07f3', '0181', '014a', '012b', '0752', " +
            "'01ef', '091f', '0ae5', '04bc', '055f', '0248', '0215', '01db', '0195', '0212', " +
            "'01a6', '01e4', '01d9', '0b58', '0148', '0326', '0741', '0199', '0264', '01f8', " +
            "'0127', '06d1', '025a', '018a', '01f0', '01b4', '0e54', '0212', '09bb', '011e', " +
            "'0244', '016c', '0138', '0404', '0b77', '01bd', '0215', '07e0', '0241', '014c', " +
            "'011f', '01e7', '0218', '0235', '0242', '01ef', '06a4', '012f', '019b', '06a0', " +
            "'025f', '015c', '0ec3', '0254', '0575', '015b', '0869', '012b', '01b5', '0258', " +
            "'0269', '0bb3', '0232', '076c', '0170', '024d', '0575', '0f21', '06ec', '01c1', " +
            "'015b', '0197', '04b2', '0b2b', '01bd', '0ab5', '01e5', '03c0', '044b', '0d11', " +
            "'0220', '020a', '0846', '0114', '0210', '01f9', '01a7', '0326', '07a9', '0110', " +
            "'0211', '010b', '016c', '06e2', '017f', '01f0', '020e', '0c60', '0100', '045c', " +
            "'0867', '019b', '01f4', '0196', '010e', '0242', '0159', '0125', '0fb6', '012b', " +
            "'047c', '06fd', '01df', '0187', '0239', '04b3', '02ab', '0a81', '0114', '0908', " +
            "'01e5', '0291', '0461', '0173', '020b', '0257', '0255', '0de7', '050d', '008c', " +
            "'0ea1', '0260', '0ad3', '037b', '0129', '07aa', '01e1', '0edb', '0109', '0bdd', " +
            "'0202', '0259', '0c2f', '0248', '01cc', '021c', '0202', '0160', '0122', '0d1d', " +
            "'01a8', '01a7', '0264', '0120', '023d', '01c6', '01ad', '01cd', '0dda', '0236', " +
            "'012f', '0176', '03ed', '016c', '01bb', '0f3e', '01ab', '0165', '0ed0', '08c8', " +
            "'0f7d', '01ee', '012a', '01b3', '0d81', '0ac4', '01d8', '01af', '0167', '0147', " +
            "'01b5', '01c7', '0244', '018e', '0747', '03a8', '021f', '0035', '0e49', '0367', " +
            "'0105', '01c6', '01d9', '0a01', '0dd3', '0202', '01cb', '01ac', '01d3', '067e', " +
            "'03dc', '06ad', '012e', '0166', '01a6', '01fa', '0168', '01ac', '010f', '01d9', " +
            "'020f', '0f49', '051e', '0164', '01f5', '0120', '00f6', '0110', '01c7', '0178', " +
            "'01fa', '0620', '01d9', '0401', '04e9', '013a', '0149', '021b', '011b', '06eb', " +
            "'0189', '0eb3', '0140', '01b4', '0216', '0fd6', '0254', '0205', '0527', '0d07', " +
            "'024c', '023a', '0d7c', '04e8', '0171', '01ab', '0197', '01a5', '017f', '01ba', " +
            "'0209', '0b0e', '0124', '0226', '01b2', '0c14', '061d', '023c', '0142', '01bf', " +
            "'0261', '018f', '019c', '0811', '01f1', '0209', '0e95', '0135', '0150', '016f', " +
            "'0189', '0fea', '0f73', '019c', '0e61', '01a7', '0ec9', '0f2d', '0143', '0ccb', " +
            "'01b3', '0c10', '0a9b', '017b', '0d17', '054d', '0259', '01d9', '0208', '011f', " +
            "'0514', '0824', '014b', '0121', '0255', '01ec', '0205', '0218', '010a', '022d', " +
            "'0b3e', '0265', '01b7', '0227', '0f92', '0193', '02b2', '0204', '01f5', '0233', " +
            "'011e', '0b75', '01bb', '0a9e', '0150', '0a59', '01aa', '0131', '03af', '021c', " +
            "'0679', '0f8a', '0215', '0615', '0c61', '0610', '0473', '0650', '01e8', '0174', " +
            "'01c9', '0169', '095b', '0223', '0beb', '01ae', '0f4e', '016d', '046b', '01fe', " +
            "'0d81', '0c7c', '080e', '0cd8', '01c8', '0161', '0242', '01df', '0b30', '063e', " +
            "'013c', '05a3', '018a', '0015', '012f', '0824', '0202', '0101', '01ce', '016d', " +
            "'0c70', '09bd', '0123', '0211', '010e', '0ba2', '0203', '01bd', '01bd', '04d2', " +
            "'0cbb', '0187', '0195', '0050', '0148', '0263', '01dd', '0128', '01ea', '01f5', " +
            "'020a', '0033', '024a', '00c0', '020b', '0684', '0952', '021e', '0a28', '0163', " +
            "'01f8', '011e', '0225', '06d0', '0c81', '0121', '0e01', '01a8', '010b', '01b1', " +
            "'011b', '0101', '0cdf', '0754', '016a', '023d', '034d', '016c', '01bc', '048a', " +
            "'0c7d', '0ee5', '0117', '0a9a', '018d', '01a6', '023e', '0361', '0221', '0143', " +
            "'0e9f', '0a6a', '020f', '0968', '0182', '0208', '0e6a', '01e3', '0815', '0498', " +
            "'0253', '0207', '0261', '01b2', '0229', '0dd1', '0146', '0401', '022b', '01d4', " +
            "'0048', '05ae', '05a4', '0118', '01f6', '061e', '0de6', '0241', '01a0', '010d', " +
            "'0715', '024d', '021a', '0cb1', '0203', '078b', '0896', '01dd', '02fa', '0121', " +
            "'022f', '0267', '014b', '0175', '022d', '0b01', '0241', '0131', '0202', '013d', " +
            "'0c53', '0ffc', '027a', '0f42', '0182', '0172', '0145', '096b', '011b', '050f', " +
            "'04d3', '0252', '01e3', '01d2', '0101', '0396', '012c', '0784', '038f', '01a2', " +
            "'0132', '0140', '0234', '046c', '09fe', '0047', '05de', '0ad6', '020a', '0d25', " +
            "'021a', '003a', '0ec2', '0183', '0765', '0dc4', '0254', '0128', '0174', '0115', " +
            "'0155', '0fa4', '017c', '0528', '014d', '03e8', '0191', '0db7', '01ff', '069d', " +
            "'011a', '025e', '01b4', '0974', '01c7', '013e', '0ef6', '0240', '0472', '01db', " +
            "'015e', '0149', '020f', '0184', '0877', '013b', '00bb', '01ba', '0164', '0259', " +
            "'018b', '0077', '016b', '0340', '03ad', '0224', '01bb', '0ceb', '019e', '0166', " +
            "'0211', '0190', '0376', '01e7', '0b64', '0197', '0d39', '021d', '01ce', '0893', " +
            "'01ea', '01db', '05fa', '068f', '0153', '01ee', '01a3', '01dc', '0d87', '0427', " +
            "'0163', '022a', '01fa', '016c', '0394', '0138', '0d9f', '016f', '0120', '0119', " +
            "'04b4', '024c', '01b4', '014f', '016d', '060e', '0141', '0f50', '01df', '0a2b', " +
            "'0ab2', '0190', '04b9', '06fb', '0244', '0240', '012f', '01f7', '0611', '0e1e', " +
            "'0447', '09e2', '0636', '0989', '0f9a', '015e', '01ce', '0103', '0286', '01a9', " +
            "'0734', '01af', '0237', '0d15', '01d7', '0754', '084c', '0202', '0208', '0686', " +
            "'0512', '023f', '0236', '0236', '01c1', '0146', '0e80', '020b', '01b3', '0e92', " +
            "'03d4', '012a', '0154', '0843', '0fc2', '0108', '0df7', '057c', '03af', '0252', " +
            "'022d', '025d', '01f0', '01a4', '01ca', '01ad', '0228', '0226', '0a7c', '0262', " +
            "'0a27', '0214', '06c8', '0206', '0f18', '0065', '0258', '05e7', '0262', '0d4f', " +
            "'0cea', '04a6', '0534', '01ad', '017b', '01f9', '06d7', '0c4e', '0e9e', '0269', " +
            "'0227', '0b46', '0194', '0181', '017e', '0138', '0af0', '01cd', '0e24', '04ed', " +
            "'01d6', '022d', '0ebf', '025d', '0705', '0165', '0136', '0125', '0c2c', '0183', " +
            "'013f', '0c8d', '0faa', '013d', '00cf', '01db', '0215', '01d5', '0130', '0153', " +
            "'093d', '0236', '075c', '025b', '0ba2', '0198', '0214', '014d', '01dd', '017d', " +
            "'0390', '005a', '015e', '0206', '01fe', '01ef', '01c0', '0116', '0225', '0232', " +
            "'073d', '0bde', '09d0', '019d', '01fd', '020f', '020e', '01af', '05db', '0173', " +
            "'0260', '022d', '0a7f', '0b34', '0202', '01d2', '0a44', '0250', '0139', '0d35', " +
            "'023b', '08ab', '0801', '0859', '0203', '0fdb', '0214', '063c', '0205', '01eb', " +
            "'013a', '01f2', '0bc0', '0244', '0135', '0195', '033b', '0dc1', '01b0', '0251', " +
            "'02a2', '015e', '0221', '0d5f', '0175', '011f', '0898', '01da', '0193', '03b3', " +
            "'0519', '01e1', '0107', '015d', '03af', '015c', '0918', '01e5', '024b', '024d', " +
            "'03d2', '01fd', '093b', '05b4', '0103', '070b', '04c3', '0130', '01ae', '0535', " +
            "'05c1', '0190', '021c', '0150', '0244', '0160', '08a1', '018c', '0bd9', '007f', " +
            "'0238', '0edd', '0129', '021d', '021c', '0f55', '0248', '0a7f', '0184', '0c1c', " +
            "'0228', '05aa', '01df', '015c', '0524', '025f', '0185', '0173', '0100', '07e7', " +
            "'01bc', '020c', '0245', '0e9d', '0108', '0052', '01c0', '0142', '01fe', '01eb', " +
            "'0210', '01ef', '0166', '0694', '043e', '05b3', '01db', '0e76', '073d', '0199', " +
            "'01a4', '0b46', '024f', '015e', '018f', '0e75', '01ed', '0247', '01c6', '0232', " +
            "'056d', '01b7', '0229', '01cb', '0b9e', '0afc', '0d74', '0e6a', '0216', '0b32', " +
            "'0610', '01dc', '07a4', '0240', '0f88', '0e0f', '0a51', '01ed', '0c87', '0af2', " +
            "'0af7', '0797', '014e', '0143', '0208', '0d23', '01f5', '0212', '0df1', '066f', " +
            "'02b2', '0269', '029a', '0131', '018a', '022c', '01f2', '059d', '01c6', '0d61', " +
            "'034d', '04cb', '04f9', '01fc', '0153', '0f9b', '0208', '01af', '0c55', '0121', " +
            "'03bc', '0f5d', '0542', '0160', '00b0', '056b', '0572', '03c3', '0158', '0234', " +
            "'026a', '05a0', '0306', '01c6', '0221', '009e', '0177', '016d', '0263', '01d4', " +
            "'01c5', '0d69', '031a', '01f8', '0104', '0bbf', '0bad', '01c4', '0148', '023a', " +
            "'025f', '0109', '0535', '0173', '0378', '0e19', '06f0', '021b', '01be', '01db', " +
            "'013c', '0247', '0258', '00b3', '0258', '01bf', '04e7', '012e', '01db', '0269', " +
            "'0269', '021d', '01f8', '0194', '01f1', '01e7', '015d', '0139', '0cc4', '076a', " +
            "'01c7', '0027', '056d', '0081', '0832', '01c7', '0721', '01c7', '024f', '0dfe', " +
            "'04b7', '020b', '0201', '0deb', '0b7b', '07bf', '0166', '01e5', '0a4d', '05af', " +
            "'0212', '012f', '01a2', '0c74', '0572', '0381', '01eb', '0268', '0966', '021f', " +
            "'01cb', '01e1', '01ee', '086a', '013c', '0102', '020d', '043a', '0246', '099b', " +
            "'0243', '021d', '0229', '0f6f', '023f', '06fe', '01d7', '025c', '0183', '0165', " +
            "'0978', '01a0', '01ae', '016a', '0110', '0e98', '02bf', '0eb7', '0d42', '01cf', " +
            "'019f', '0529', '0127', '01e1', '0186', '0c1c', '06d4', '01b9', '0248', '0956', " +
            "'0b3a', '09aa', '06a2', '0c9d', '0104', '0258', '02b4', '01eb', '016e', '0758', " +
            "'0164', '0226', '022c', '0111', '079e', '01f6', '018d', '010c', '04f6', '01e5', " +
            "'0da9', '0ae9', '022f', '0253', '01e1', '01eb', '0eb5', '0880', '0052', '016e', " +
            "'0baa', '0220', '022b', '010b', '0178', '0145', '0206', '011c', '04ba', '01e7', " +
            "'026a', '03c8', '0330', '022a', '01a0', '022f', '07b2', '0230', '05ca', '01d2', " +
            "'012d', '024d', '0212', '0267', '0228', '01c3', '002b', '0140', '0152', '0d21', " +
            "'00fe', '03dc', '024a', '0266', '0366', '09e9', '0111', '0489', '058f', '015c', " +
            "'01bd', '098c', '021b', '0163', '0e77', '0dc4', '094d', '01da', '0690', '01ff', " +
            "'0101', '013c', '0193', '0141', '0116', '0c4a', '0cc8', '01f5', '0122', '0b21', " +
            "'0101', '0130', '0251', '08a6', '0193', '0c30', '0207', '020d', '025f', '06da', " +
            "'08ec', '0250', '0bb7', '0773', '01ce', '0170', '0246', '012c', '07a8', '0205', " +
            "'03b7', '0213', '0071', '0d6d', '01b1', '018f', '0e41', '02df', '002f', '01a6', " +
            "'0224', '085b', '01a3', '0104', '0129', '0d2a', '0245', '0eec', '0167', '0364', " +
            "'0122', '02c9', '0212', '0b14', '0254', '01db', '0164', '0d0f', '020a', '0313', " +
            "'01df', '0f1a', '0215', '0527', '0e7c', '024b', '01db', '0ed5', '0194', '0d82', " +
            "'018f', '098d', '020e', '00b1', '01ee', '0234', '013b', '031e', '0257', '01c1', " +
            "'0923', '018f', '0114', '0165', '0105', '0a58', '01ec', '0f51', '0814', '0195', " +
            "'0160', '00a4', '0192', '08aa', '0230', '0190', '021a', '0114', '012c', '01e0', " +
            "'01f0', '0231', '011b', '07da', '0917', '0f46', '0d14', '0199', '0191', '0179', " +
            "'01fd', '01e3', '024b')")
        };
    }

  public QueryInfo[] getJoinQueryInfo() {
    return new QueryInfo[] {
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t0 t3) */ /* __QPM Query 1 */ SELECT" +
            " COUNT(*) AS cnt_1 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND b0 = 27"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t0 t3) */ /* __QPM Query 1 */ SELECT" +
        " COUNT(*) AS cnt_1 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND b0 = 27"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t0 t3) */ /* __QPM Query 1 */ SELECT" +
            " COUNT(*) AS cnt_1 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND b0 = 27"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t0 t3) */ /* __QPM Query 1 */ SELECT" +
            " COUNT(*) AS cnt_1 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND b0 = 27"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t0 t3) */ /* __QPM Query 1 */ SELECT" +
            " COUNT(*) AS cnt_1 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND b0 = 27"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t0 t3) */ /* __QPM Query 1 */ SELECT" +
            " COUNT(*) AS cnt_1 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND b0 = 27"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t1 t0) */ /* __QPM Query 2 */ SELECT" +
            " COUNT(*) AS cnt_2 FROM t1, t0 WHERE unn1 = unn0 AND e1 <= 98"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t1 t0) */ /* __QPM Query 2 */ SELECT" +
            " COUNT(*) AS cnt_2 FROM t1, t0 WHERE unn1 = unn0 AND e1 <= 98"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t1 t0) */ /* __QPM Query 2 */ SELECT" +
            " COUNT(*) AS cnt_2 FROM t1, t0 WHERE unn1 = unn0 AND e1 <= 98"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t1 t0) */ /* __QPM Query 2 */ SELECT" +
            " COUNT(*) AS cnt_2 FROM t1, t0 WHERE unn1 = unn0 AND e1 <= 98"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t1 t0) */ /* __QPM Query 2 */ SELECT" +
            " COUNT(*) AS cnt_2 FROM t1, t0 WHERE unn1 = unn0 AND e1 <= 98"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t1 t0) */ /* __QPM Query 2 */ SELECT" +
            " COUNT(*) AS cnt_2 FROM t1, t0 WHERE unn1 = unn0 AND e1 <= 98"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t3 t1) */ /* __QPM Query 3 */ SELECT" +
            " COUNT(*) AS cnt_3 FROM t3, t1 WHERE b3 = c1 AND pk3_0 = unn1 AND b3 >= 32"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t3 t1) */ /* __QPM Query 3 */ SELECT" +
            " COUNT(*) AS cnt_3 FROM t3, t1 WHERE b3 = c1 AND pk3_0 = unn1 AND b3 >= 32"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t3 t1) */ /* __QPM Query 3 */ SELECT" +
            " COUNT(*) AS cnt_3 FROM t3, t1 WHERE b3 = c1 AND pk3_0 = unn1 AND b3 >= 32"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t3 t1) */ /* __QPM Query 3 */ SELECT" +
            " COUNT(*) AS cnt_3 FROM t3, t1 WHERE b3 = c1 AND pk3_0 = unn1 AND b3 >= 32"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t3 t1) */ /* __QPM Query 3 */ SELECT" +
            " COUNT(*) AS cnt_3 FROM t3, t1 WHERE b3 = c1 AND pk3_0 = unn1 AND b3 >= 32"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t3 t1) */ /* __QPM Query 3 */ SELECT" +
            " COUNT(*) AS cnt_3 FROM t3, t1 WHERE b3 = c1 AND pk3_0 = unn1 AND b3 >= 32"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t3 t1) */ /* __QPM Query 4 */ SELECT" +
            " COUNT(*) AS cnt_4 FROM t3, t1 WHERE unn3 = f1"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t3 t1) */ /* __QPM Query 4 */ SELECT" +
            " COUNT(*) AS cnt_4 FROM t3, t1 WHERE unn3 = f1"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t3 t1) */ /* __QPM Query 4 */ SELECT" +
            " COUNT(*) AS cnt_4 FROM t3, t1 WHERE unn3 = f1"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t3 t1) */ /* __QPM Query 4 */ SELECT" +
            " COUNT(*) AS cnt_4 FROM t3, t1 WHERE unn3 = f1"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t3 t1) */ /* __QPM Query 4 */ SELECT" +
            " COUNT(*) AS cnt_4 FROM t3, t1 WHERE unn3 = f1"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t3 t1) */ /* __QPM Query 4 */ SELECT" +
            " COUNT(*) AS cnt_4 FROM t3, t1 WHERE unn3 = f1"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t2 t0) */ /* __QPM Query 5 */ SELECT" +
            " COUNT(*) AS cnt_5 FROM t2, t0 WHERE unn2 = d0 AND pk2_0 > 48 AND pk2_1 <= 92"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t2 t0) */ /* __QPM Query 5 */ SELECT" +
            " COUNT(*) AS cnt_5 FROM t2, t0 WHERE unn2 = d0 AND pk2_0 > 48 AND pk2_1 <= 92"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t2 t0) */ /* __QPM Query 5 */ SELECT" +
            " COUNT(*) AS cnt_5 FROM t2, t0 WHERE unn2 = d0 AND pk2_0 > 48 AND pk2_1 <= 92"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t2 t0) */ /* __QPM Query 5 */ SELECT" +
            " COUNT(*) AS cnt_5 FROM t2, t0 WHERE unn2 = d0 AND pk2_0 > 48 AND pk2_1 <= 92"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t2 t0) */ /* __QPM Query 5 */ SELECT" +
            " COUNT(*) AS cnt_5 FROM t2, t0 WHERE unn2 = d0 AND pk2_0 > 48 AND pk2_1 <= 92"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t2 t0) */ /* __QPM Query 5 */ SELECT" +
            " COUNT(*) AS cnt_5 FROM t2, t0 WHERE unn2 = d0 AND pk2_0 > 48 AND pk2_1 <= 92"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 6 */ SELECT" +
            " COUNT(*) AS cnt_6 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "pk2_0 < 78 AND pk2_1 > 43"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 6 */ SELECT" +
            " COUNT(*) AS cnt_6 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "pk2_0 < 78 AND pk2_1 > 43"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 6 */ SELECT" +
            " COUNT(*) AS cnt_6 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "pk2_0 < 78 AND pk2_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 6 */ SELECT" +
            " COUNT(*) AS cnt_6 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "pk2_0 < 78 AND pk2_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 6 */ SELECT" +
            " COUNT(*) AS cnt_6 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "pk2_0 < 78 AND pk2_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 6 */ SELECT" +
            " COUNT(*) AS cnt_6 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "pk2_0 < 78 AND pk2_1 > 43"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t0 t2) */ /* __QPM Query 7 */ SELECT" +
            " COUNT(*) AS cnt_7 FROM t0, t2 WHERE unn0 = unn2 AND pk0_0 > 58 AND pk0_1 < 67"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t0 t2) */ /* __QPM Query 7 */ SELECT" +
            " COUNT(*) AS cnt_7 FROM t0, t2 WHERE unn0 = unn2 AND pk0_0 > 58 AND pk0_1 < 67"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t0 t2) */ /* __QPM Query 7 */ SELECT" +
            " COUNT(*) AS cnt_7 FROM t0, t2 WHERE unn0 = unn2 AND pk0_0 > 58 AND pk0_1 < 67"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t0 t2) */ /* __QPM Query 7 */ SELECT" +
            " COUNT(*) AS cnt_7 FROM t0, t2 WHERE unn0 = unn2 AND pk0_0 > 58 AND pk0_1 < 67"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t0 t2) */ /* __QPM Query 7 */ SELECT" +
            " COUNT(*) AS cnt_7 FROM t0, t2 WHERE unn0 = unn2 AND pk0_0 > 58 AND pk0_1 < 67"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t0 t2) */ /* __QPM Query 7 */ SELECT" +
            " COUNT(*) AS cnt_7 FROM t0, t2 WHERE unn0 = unn2 AND pk0_0 > 58 AND pk0_1 < 67"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t1 t2) */ /* __QPM Query 8 */ SELECT" +
            " COUNT(*) AS cnt_8 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 > pk2_1 AND f1 > 7"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t1 t2) */ /* __QPM Query 8 */ SELECT" +
            " COUNT(*) AS cnt_8 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 > pk2_1 AND f1 > 7"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t1 t2) */ /* __QPM Query 8 */ SELECT" +
            " COUNT(*) AS cnt_8 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 > pk2_1 AND f1 > 7"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t1 t2) */ /* __QPM Query 8 */ SELECT" +
            " COUNT(*) AS cnt_8 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 > pk2_1 AND f1 > 7"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t1 t2) */ /* __QPM Query 8 */ SELECT" +
            " COUNT(*) AS cnt_8 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 > pk2_1 AND f1 > 7"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t1 t2) */ /* __QPM Query 8 */ SELECT" +
            " COUNT(*) AS cnt_8 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 > pk2_1 AND f1 > 7"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 9 */ SELECT" +
            " COUNT(*) AS cnt_9 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND f2 >= 26"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 9 */ SELECT" +
            " COUNT(*) AS cnt_9 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND f2 >= 26"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 9 */ SELECT" +
            " COUNT(*) AS cnt_9 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND f2 >= 26"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 9 */ SELECT" +
            " COUNT(*) AS cnt_9 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND f2 >= 26"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 9 */ SELECT" +
            " COUNT(*) AS cnt_9 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND f2 >= 26"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 9 */ SELECT" +
            " COUNT(*) AS cnt_9 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND f2 >= 26"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 10 */ SELECT" +
            " COUNT(*) AS cnt_10 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 10 */ SELECT" +
            " COUNT(*) AS cnt_10 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 10 */ SELECT" +
            " COUNT(*) AS cnt_10 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 10 */ SELECT" +
            " COUNT(*) AS cnt_10 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 10 */ SELECT" +
            " COUNT(*) AS cnt_10 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 10 */ SELECT" +
            " COUNT(*) AS cnt_10 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t1 t0) */ /* __QPM Query 11 */ SELECT" +
            " COUNT(*) AS cnt_11 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t1 t0) */ /* __QPM Query 11 */ SELECT" +
            " COUNT(*) AS cnt_11 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t1 t0) */ /* __QPM Query 11 */ SELECT" +
            " COUNT(*) AS cnt_11 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t1 t0) */ /* __QPM Query 11 */ SELECT" +
            " COUNT(*) AS cnt_11 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t1 t0) */ /* __QPM Query 11 */ SELECT" +
            " COUNT(*) AS cnt_11 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t1 t0) */ /* __QPM Query 11 */ SELECT" +
            " COUNT(*) AS cnt_11 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t1 t0) */ /* __QPM Query 12 */ SELECT" +
            " COUNT(*) AS cnt_12 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND " +
            "pk1_0 < 77 AND pk1_1 >= 28"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t1 t0) */ /* __QPM Query 12 */ SELECT" +
            " COUNT(*) AS cnt_12 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND " +
            "pk1_0 < 77 AND pk1_1 >= 28"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t1 t0) */ /* __QPM Query 12 */ SELECT" +
            " COUNT(*) AS cnt_12 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND " +
            "pk1_0 < 77 AND pk1_1 >= 28"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t1 t0) */ /* __QPM Query 12 */ SELECT" +
            " COUNT(*) AS cnt_12 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND " +
            "pk1_0 < 77 AND pk1_1 >= 28"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t1 t0) */ /* __QPM Query 12 */ SELECT" +
            " COUNT(*) AS cnt_12 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND " +
            "pk1_0 < 77 AND pk1_1 >= 28"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t1 t0) */ /* __QPM Query 12 */ SELECT" +
            " COUNT(*) AS cnt_12 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND " +
            "pk1_0 < 77 AND pk1_1 >= 28"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 13 */ SELECT" +
            " COUNT(*) AS cnt_13 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 " +
            "AND pk0_1 <= 98"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 13 */ SELECT" +
            " COUNT(*) AS cnt_13 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 " +
            "AND pk0_1 <= 98"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 13 */ SELECT" +
            " COUNT(*) AS cnt_13 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_1 <= 98"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 13 */ SELECT" +
            " COUNT(*) AS cnt_13 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_1 <= 98"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 13 */ SELECT" +
            " COUNT(*) AS cnt_13 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_1 <= 98"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 13 */ SELECT" +
            " COUNT(*) AS cnt_13 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_1 <= 98"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t3 t1) */ /* __QPM Query 14 */ SELECT" +
            " COUNT(*) AS cnt_14 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND pk1_0 > 5"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t3 t1) */ /* __QPM Query 14 */ SELECT" +
            " COUNT(*) AS cnt_14 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND pk1_0 > 5"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t3 t1) */ /* __QPM Query 14 */ SELECT" +
            " COUNT(*) AS cnt_14 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND pk1_0 > 5"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t3 t1) */ /* __QPM Query 14 */ SELECT" +
            " COUNT(*) AS cnt_14 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND pk1_0 > 5"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t3 t1) */ /* __QPM Query 14 */ SELECT" +
            " COUNT(*) AS cnt_14 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND pk1_0 > 5"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t3 t1) */ /* __QPM Query 14 */ SELECT" +
            " COUNT(*) AS cnt_14 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND pk1_0 > 5"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 15 */ SELECT" +
            " COUNT(*) AS cnt_15 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 < 62 AND pk3_1 <= 69"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 15 */ SELECT" +
            " COUNT(*) AS cnt_15 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 < 62 AND pk3_1 <= 69"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 15 */ SELECT" +
            " COUNT(*) AS cnt_15 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 < 62 AND pk3_1 <= 69"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 15 */ SELECT" +
            " COUNT(*) AS cnt_15 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 < 62 AND pk3_1 <= 69"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 15 */ SELECT" +
            " COUNT(*) AS cnt_15 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 < 62 AND pk3_1 <= 69"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 15 */ SELECT" +
            " COUNT(*) AS cnt_15 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 < 62 AND pk3_1 <= 69"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 16 */ SELECT" +
            " COUNT(*) AS cnt_16 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_0 < 60 AND pk0_1 > 21"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 16 */ SELECT" +
            " COUNT(*) AS cnt_16 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_0 < 60 AND pk0_1 > 21"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 16 */ SELECT" +
            " COUNT(*) AS cnt_16 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_0 < 60 AND pk0_1 > 21"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 16 */ SELECT" +
            " COUNT(*) AS cnt_16 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_0 < 60 AND pk0_1 > 21"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 16 */ SELECT" +
            " COUNT(*) AS cnt_16 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_0 < 60 AND pk0_1 > 21"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 16 */ SELECT" +
            " COUNT(*) AS cnt_16 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND " +
            "pk0_0 < 60 AND pk0_1 > 21"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 17 */ SELECT" +
            " COUNT(*) AS cnt_17 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn2 < 50"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 17 */ SELECT" +
            " COUNT(*) AS cnt_17 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn2 < 50"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 17 */ SELECT" +
            " COUNT(*) AS cnt_17 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn2 < 50"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 17 */ SELECT" +
            " COUNT(*) AS cnt_17 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn2 < 50"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 17 */ SELECT" +
            " COUNT(*) AS cnt_17 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn2 < 50"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 17 */ SELECT" +
            " COUNT(*) AS cnt_17 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn2 < 50"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 18 */ SELECT" +
            " COUNT(*) AS cnt_18 FROM t0, t1 WHERE unn0 = unn1 AND pk1_0 > 48 AND pk1_1 <= 78"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 18 */ SELECT" +
            " COUNT(*) AS cnt_18 FROM t0, t1 WHERE unn0 = unn1 AND pk1_0 > 48 AND pk1_1 <= 78"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 18 */ SELECT" +
            " COUNT(*) AS cnt_18 FROM t0, t1 WHERE unn0 = unn1 AND pk1_0 > 48 AND pk1_1 <= 78"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 18 */ SELECT" +
            " COUNT(*) AS cnt_18 FROM t0, t1 WHERE unn0 = unn1 AND pk1_0 > 48 AND pk1_1 <= 78"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 18 */ SELECT" +
            " COUNT(*) AS cnt_18 FROM t0, t1 WHERE unn0 = unn1 AND pk1_0 > 48 AND pk1_1 <= 78"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 18 */ SELECT" +
            " COUNT(*) AS cnt_18 FROM t0, t1 WHERE unn0 = unn1 AND pk1_0 > 48 AND pk1_1 <= 78"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t3 t1) */ /* __QPM Query 19 */ SELECT" +
            " COUNT(*) AS cnt_19 FROM t3, t1 WHERE d3 = f1 AND unn3 > 20"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t3 t1) */ /* __QPM Query 19 */ SELECT" +
            " COUNT(*) AS cnt_19 FROM t3, t1 WHERE d3 = f1 AND unn3 > 20"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t3 t1) */ /* __QPM Query 19 */ SELECT" +
            " COUNT(*) AS cnt_19 FROM t3, t1 WHERE d3 = f1 AND unn3 > 20"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t3 t1) */ /* __QPM Query 19 */ SELECT" +
            " COUNT(*) AS cnt_19 FROM t3, t1 WHERE d3 = f1 AND unn3 > 20"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t3 t1) */ /* __QPM Query 19 */ SELECT" +
            " COUNT(*) AS cnt_19 FROM t3, t1 WHERE d3 = f1 AND unn3 > 20"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t3 t1) */ /* __QPM Query 19 */ SELECT" +
            " COUNT(*) AS cnt_19 FROM t3, t1 WHERE d3 = f1 AND unn3 > 20"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 20 */ SELECT" +
            " COUNT(*) AS cnt_20 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 20 */ SELECT" +
            " COUNT(*) AS cnt_20 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 20 */ SELECT" +
            " COUNT(*) AS cnt_20 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 20 */ SELECT" +
            " COUNT(*) AS cnt_20 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 20 */ SELECT" +
            " COUNT(*) AS cnt_20 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 20 */ SELECT" +
            " COUNT(*) AS cnt_20 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 21 */ SELECT" +
            " COUNT(*) AS cnt_21 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND unn3 < 76"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 21 */ SELECT" +
            " COUNT(*) AS cnt_21 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND unn3 < 76"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 21 */ SELECT" +
            " COUNT(*) AS cnt_21 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND unn3 < 76"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 21 */ SELECT" +
            " COUNT(*) AS cnt_21 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND unn3 < 76"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 21 */ SELECT" +
            " COUNT(*) AS cnt_21 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND unn3 < 76"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 21 */ SELECT" +
            " COUNT(*) AS cnt_21 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND unn3 < 76"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t1 t3) */ /* __QPM Query 22 */ SELECT" +
            " COUNT(*) AS cnt_22 FROM t1, t3 WHERE unn1 = unn3 AND d1 > 24"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t1 t3) */ /* __QPM Query 22 */ SELECT" +
            " COUNT(*) AS cnt_22 FROM t1, t3 WHERE unn1 = unn3 AND d1 > 24"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t1 t3) */ /* __QPM Query 22 */ SELECT" +
            " COUNT(*) AS cnt_22 FROM t1, t3 WHERE unn1 = unn3 AND d1 > 24"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t1 t3) */ /* __QPM Query 22 */ SELECT" +
            " COUNT(*) AS cnt_22 FROM t1, t3 WHERE unn1 = unn3 AND d1 > 24"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t1 t3) */ /* __QPM Query 22 */ SELECT" +
            " COUNT(*) AS cnt_22 FROM t1, t3 WHERE unn1 = unn3 AND d1 > 24"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t1 t3) */ /* __QPM Query 22 */ SELECT" +
            " COUNT(*) AS cnt_22 FROM t1, t3 WHERE unn1 = unn3 AND d1 > 24"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t0 t3) */ /* __QPM Query 23 */ SELECT" +
            " COUNT(*) AS cnt_23 FROM t0, t3 WHERE f0 = f3 AND unn0 = pk3_0 AND unn3 < 82"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t0 t3) */ /* __QPM Query 23 */ SELECT" +
            " COUNT(*) AS cnt_23 FROM t0, t3 WHERE f0 = f3 AND unn0 = pk3_0 AND unn3 < 82"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t0 t3) */ /* __QPM Query 23 */ SELECT" +
            " COUNT(*) AS cnt_23 FROM t0, t3 WHERE f0 = f3 AND unn0 = pk3_0 AND unn3 < 82"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t0 t3) */ /* __QPM Query 23 */ SELECT" +
            " COUNT(*) AS cnt_23 FROM t0, t3 WHERE f0 = f3 AND unn0 = pk3_0 AND unn3 < 82"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t0 t3) */ /* __QPM Query 23 */ SELECT" +
            " COUNT(*) AS cnt_23 FROM t0, t3 WHERE f0 = f3 AND unn0 = pk3_0 AND unn3 < 82"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t0 t3) */ /* __QPM Query 23 */ SELECT" +
            " COUNT(*) AS cnt_23 FROM t0, t3 WHERE f0 = f3 AND unn0 = pk3_0 AND unn3 < 82"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t2 t0) */ /* __QPM Query 24 */ SELECT" +
            " COUNT(*) AS cnt_24 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t2 t0) */ /* __QPM Query 24 */ SELECT" +
            " COUNT(*) AS cnt_24 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t2 t0) */ /* __QPM Query 24 */ SELECT" +
            " COUNT(*) AS cnt_24 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t2 t0) */ /* __QPM Query 24 */ SELECT" +
            " COUNT(*) AS cnt_24 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t2 t0) */ /* __QPM Query 24 */ SELECT" +
            " COUNT(*) AS cnt_24 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t2 t0) */ /* __QPM Query 24 */ SELECT" +
            " COUNT(*) AS cnt_24 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 25 */ SELECT" +
            " COUNT(*) AS cnt_25 FROM t3, t2 WHERE b3 = unn2 AND pk3_0 >= 7 AND pk3_1 > 6"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 25 */ SELECT" +
            " COUNT(*) AS cnt_25 FROM t3, t2 WHERE b3 = unn2 AND pk3_0 >= 7 AND pk3_1 > 6"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 25 */ SELECT" +
            " COUNT(*) AS cnt_25 FROM t3, t2 WHERE b3 = unn2 AND pk3_0 >= 7 AND pk3_1 > 6"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 25 */ SELECT" +
            " COUNT(*) AS cnt_25 FROM t3, t2 WHERE b3 = unn2 AND pk3_0 >= 7 AND pk3_1 > 6"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 25 */ SELECT" +
            " COUNT(*) AS cnt_25 FROM t3, t2 WHERE b3 = unn2 AND pk3_0 >= 7 AND pk3_1 > 6"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 25 */ SELECT" +
            " COUNT(*) AS cnt_25 FROM t3, t2 WHERE b3 = unn2 AND pk3_0 >= 7 AND pk3_1 > 6"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 26 */ SELECT" +
            " COUNT(*) AS cnt_26 FROM t3, t2 WHERE charCol3 = charCol2 AND a3 = e2"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 26 */ SELECT" +
            " COUNT(*) AS cnt_26 FROM t3, t2 WHERE charCol3 = charCol2 AND a3 = e2"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 26 */ SELECT" +
            " COUNT(*) AS cnt_26 FROM t3, t2 WHERE charCol3 = charCol2 AND a3 = e2"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 26 */ SELECT" +
            " COUNT(*) AS cnt_26 FROM t3, t2 WHERE charCol3 = charCol2 AND a3 = e2"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 26 */ SELECT" +
            " COUNT(*) AS cnt_26 FROM t3, t2 WHERE charCol3 = charCol2 AND a3 = e2"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 26 */ SELECT" +
            " COUNT(*) AS cnt_26 FROM t3, t2 WHERE charCol3 = charCol2 AND a3 = e2"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 27 */ SELECT" +
            " COUNT(*) AS cnt_27 FROM t3, t0 WHERE d3 = e0 AND e0 >= 32"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 27 */ SELECT" +
            " COUNT(*) AS cnt_27 FROM t3, t0 WHERE d3 = e0 AND e0 >= 32"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 27 */ SELECT" +
            " COUNT(*) AS cnt_27 FROM t3, t0 WHERE d3 = e0 AND e0 >= 32"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 27 */ SELECT" +
            " COUNT(*) AS cnt_27 FROM t3, t0 WHERE d3 = e0 AND e0 >= 32"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 27 */ SELECT" +
            " COUNT(*) AS cnt_27 FROM t3, t0 WHERE d3 = e0 AND e0 >= 32"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 27 */ SELECT" +
            " COUNT(*) AS cnt_27 FROM t3, t0 WHERE d3 = e0 AND e0 >= 32"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t2 t0) */ /* __QPM Query 28 */ SELECT" +
            " COUNT(*) AS cnt_28 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 < pk0_1"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t2 t0) */ /* __QPM Query 28 */ SELECT" +
            " COUNT(*) AS cnt_28 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 < pk0_1"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t2 t0) */ /* __QPM Query 28 */ SELECT" +
            " COUNT(*) AS cnt_28 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 < pk0_1"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t2 t0) */ /* __QPM Query 28 */ SELECT" +
            " COUNT(*) AS cnt_28 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 < pk0_1"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t2 t0) */ /* __QPM Query 28 */ SELECT" +
            " COUNT(*) AS cnt_28 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 < pk0_1"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t2 t0) */ /* __QPM Query 28 */ SELECT" +
            " COUNT(*) AS cnt_28 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 < pk0_1"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 29 */ SELECT" +
            " COUNT(*) AS cnt_29 FROM t0, t1 WHERE unn0 = unn1"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 29 */ SELECT" +
            " COUNT(*) AS cnt_29 FROM t0, t1 WHERE unn0 = unn1"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 29 */ SELECT" +
            " COUNT(*) AS cnt_29 FROM t0, t1 WHERE unn0 = unn1"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 29 */ SELECT" +
            " COUNT(*) AS cnt_29 FROM t0, t1 WHERE unn0 = unn1"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 29 */ SELECT" +
            " COUNT(*) AS cnt_29 FROM t0, t1 WHERE unn0 = unn1"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 29 */ SELECT" +
            " COUNT(*) AS cnt_29 FROM t0, t1 WHERE unn0 = unn1"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t1 t2) */ /* __QPM Query 30 */ SELECT" +
            " COUNT(*) AS cnt_30 FROM t1, t2 WHERE c1 = pk2_1 AND d2 <= 74"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t1 t2) */ /* __QPM Query 30 */ SELECT" +
            " COUNT(*) AS cnt_30 FROM t1, t2 WHERE c1 = pk2_1 AND d2 <= 74"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t1 t2) */ /* __QPM Query 30 */ SELECT" +
            " COUNT(*) AS cnt_30 FROM t1, t2 WHERE c1 = pk2_1 AND d2 <= 74"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t1 t2) */ /* __QPM Query 30 */ SELECT" +
            " COUNT(*) AS cnt_30 FROM t1, t2 WHERE c1 = pk2_1 AND d2 <= 74"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t1 t2) */ /* __QPM Query 30 */ SELECT" +
            " COUNT(*) AS cnt_30 FROM t1, t2 WHERE c1 = pk2_1 AND d2 <= 74"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t1 t2) */ /* __QPM Query 30 */ SELECT" +
            " COUNT(*) AS cnt_30 FROM t1, t2 WHERE c1 = pk2_1 AND d2 <= 74"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 31 */ SELECT" +
            " COUNT(*) AS cnt_31 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1 AND unn1 > 27"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 31 */ SELECT" +
            " COUNT(*) AS cnt_31 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1 AND unn1 > 27"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 31 */ SELECT" +
            " COUNT(*) AS cnt_31 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1 AND unn1 > 27"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 31 */ SELECT" +
            " COUNT(*) AS cnt_31 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1 AND unn1 > 27"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 31 */ SELECT" +
            " COUNT(*) AS cnt_31 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1 AND unn1 > 27"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 31 */ SELECT" +
            " COUNT(*) AS cnt_31 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 = pk1_1 AND unn1 > 27"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 32 */ SELECT" +
            " COUNT(*) AS cnt_32 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 >= 47"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 32 */ SELECT" +
            " COUNT(*) AS cnt_32 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 >= 47"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 32 */ SELECT" +
            " COUNT(*) AS cnt_32 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 >= 47"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 32 */ SELECT" +
            " COUNT(*) AS cnt_32 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 >= 47"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 32 */ SELECT" +
            " COUNT(*) AS cnt_32 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 >= 47"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 32 */ SELECT" +
            " COUNT(*) AS cnt_32 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 >= 47"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t1 t2) */ /* __QPM Query 33 */ SELECT" +
            " COUNT(*) AS cnt_33 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 <= pk2_1 AND " +
            "pk2_0 < 66"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t1 t2) */ /* __QPM Query 33 */ SELECT" +
            " COUNT(*) AS cnt_33 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 <= pk2_1 AND " +
            "pk2_0 < 66"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t1 t2) */ /* __QPM Query 33 */ SELECT" +
            " COUNT(*) AS cnt_33 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 <= pk2_1 AND " +
            "pk2_0 < 66"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t1 t2) */ /* __QPM Query 33 */ SELECT" +
            " COUNT(*) AS cnt_33 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 <= pk2_1 AND " +
            "pk2_0 < 66"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t1 t2) */ /* __QPM Query 33 */ SELECT" +
            " COUNT(*) AS cnt_33 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 <= pk2_1 AND " +
            "pk2_0 < 66"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t1 t2) */ /* __QPM Query 33 */ SELECT" +
            " COUNT(*) AS cnt_33 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 <= pk2_1 AND " +
            "pk2_0 < 66"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 34 */ SELECT" +
            " COUNT(*) AS cnt_34 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 >= pk3_1 AND " +
            "unn3 >= 22"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 34 */ SELECT" +
            " COUNT(*) AS cnt_34 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 >= pk3_1 AND " +
            "unn3 >= 22"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 34 */ SELECT" +
            " COUNT(*) AS cnt_34 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 >= pk3_1 AND " +
            "unn3 >= 22"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 34 */ SELECT" +
            " COUNT(*) AS cnt_34 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 >= pk3_1 AND " +
            "unn3 >= 22"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 34 */ SELECT" +
            " COUNT(*) AS cnt_34 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 >= pk3_1 AND " +
            "unn3 >= 22"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 34 */ SELECT" +
            " COUNT(*) AS cnt_34 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 >= pk3_1 AND " +
            "unn3 >= 22"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 35 */ SELECT" +
            " COUNT(*) AS cnt_35 FROM t0, t1 WHERE e0 = unn1 AND unn1 < 83"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 35 */ SELECT" +
            " COUNT(*) AS cnt_35 FROM t0, t1 WHERE e0 = unn1 AND unn1 < 83"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 35 */ SELECT" +
            " COUNT(*) AS cnt_35 FROM t0, t1 WHERE e0 = unn1 AND unn1 < 83"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 35 */ SELECT" +
            " COUNT(*) AS cnt_35 FROM t0, t1 WHERE e0 = unn1 AND unn1 < 83"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 35 */ SELECT" +
            " COUNT(*) AS cnt_35 FROM t0, t1 WHERE e0 = unn1 AND unn1 < 83"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 35 */ SELECT" +
            " COUNT(*) AS cnt_35 FROM t0, t1 WHERE e0 = unn1 AND unn1 < 83"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t1 t3) */ /* __QPM Query 36 */ SELECT" +
            " COUNT(*) AS cnt_36 FROM t1, t3 WHERE pk1_1 = b3 AND d3 > 12"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t1 t3) */ /* __QPM Query 36 */ SELECT" +
            " COUNT(*) AS cnt_36 FROM t1, t3 WHERE pk1_1 = b3 AND d3 > 12"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t1 t3) */ /* __QPM Query 36 */ SELECT" +
            " COUNT(*) AS cnt_36 FROM t1, t3 WHERE pk1_1 = b3 AND d3 > 12"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t1 t3) */ /* __QPM Query 36 */ SELECT" +
            " COUNT(*) AS cnt_36 FROM t1, t3 WHERE pk1_1 = b3 AND d3 > 12"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t1 t3) */ /* __QPM Query 36 */ SELECT" +
            " COUNT(*) AS cnt_36 FROM t1, t3 WHERE pk1_1 = b3 AND d3 > 12"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t1 t3) */ /* __QPM Query 36 */ SELECT" +
            " COUNT(*) AS cnt_36 FROM t1, t3 WHERE pk1_1 = b3 AND d3 > 12"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t1 t3) */ /* __QPM Query 37 */ SELECT" +
            " COUNT(*) AS cnt_37 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 < pk3_1"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t1 t3) */ /* __QPM Query 37 */ SELECT" +
            " COUNT(*) AS cnt_37 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 < pk3_1"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t1 t3) */ /* __QPM Query 37 */ SELECT" +
            " COUNT(*) AS cnt_37 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 < pk3_1"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t1 t3) */ /* __QPM Query 37 */ SELECT" +
            " COUNT(*) AS cnt_37 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 < pk3_1"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t1 t3) */ /* __QPM Query 37 */ SELECT" +
            " COUNT(*) AS cnt_37 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 < pk3_1"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t1 t3) */ /* __QPM Query 37 */ SELECT" +
            " COUNT(*) AS cnt_37 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 < pk3_1"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 38 */ SELECT" +
            " COUNT(*) AS cnt_38 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND d3 < 89"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 38 */ SELECT" +
            " COUNT(*) AS cnt_38 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND d3 < 89"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 38 */ SELECT" +
            " COUNT(*) AS cnt_38 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND d3 < 89"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 38 */ SELECT" +
            " COUNT(*) AS cnt_38 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND d3 < 89"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 38 */ SELECT" +
            " COUNT(*) AS cnt_38 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND d3 < 89"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 38 */ SELECT" +
            " COUNT(*) AS cnt_38 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND d3 < 89"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 39 */ SELECT" +
            " COUNT(*) AS cnt_39 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND unn3 = 72"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 39 */ SELECT" +
            " COUNT(*) AS cnt_39 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND unn3 = 72"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 39 */ SELECT" +
            " COUNT(*) AS cnt_39 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND unn3 = 72"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 39 */ SELECT" +
            " COUNT(*) AS cnt_39 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND unn3 = 72"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 39 */ SELECT" +
            " COUNT(*) AS cnt_39 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND unn3 = 72"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 39 */ SELECT" +
            " COUNT(*) AS cnt_39 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND unn3 = 72"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 40 */ SELECT" +
            " COUNT(*) AS cnt_40 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 40 */ SELECT" +
            " COUNT(*) AS cnt_40 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 40 */ SELECT" +
            " COUNT(*) AS cnt_40 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 40 */ SELECT" +
            " COUNT(*) AS cnt_40 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 40 */ SELECT" +
            " COUNT(*) AS cnt_40 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 40 */ SELECT" +
            " COUNT(*) AS cnt_40 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t0 t2) */ /* __QPM Query 41 */ SELECT" +
            " COUNT(*) AS cnt_41 FROM t0, t2 WHERE unn0 = unn2 AND unn0 < 60"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t0 t2) */ /* __QPM Query 41 */ SELECT" +
            " COUNT(*) AS cnt_41 FROM t0, t2 WHERE unn0 = unn2 AND unn0 < 60"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t0 t2) */ /* __QPM Query 41 */ SELECT" +
            " COUNT(*) AS cnt_41 FROM t0, t2 WHERE unn0 = unn2 AND unn0 < 60"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t0 t2) */ /* __QPM Query 41 */ SELECT" +
            " COUNT(*) AS cnt_41 FROM t0, t2 WHERE unn0 = unn2 AND unn0 < 60"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t0 t2) */ /* __QPM Query 41 */ SELECT" +
            " COUNT(*) AS cnt_41 FROM t0, t2 WHERE unn0 = unn2 AND unn0 < 60"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t0 t2) */ /* __QPM Query 41 */ SELECT" +
            " COUNT(*) AS cnt_41 FROM t0, t2 WHERE unn0 = unn2 AND unn0 < 60"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t3 t1) */ /* __QPM Query 42 */ SELECT" +
            " COUNT(*) AS cnt_42 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND unn3 >= 36"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t3 t1) */ /* __QPM Query 42 */ SELECT" +
            " COUNT(*) AS cnt_42 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND unn3 >= 36"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t3 t1) */ /* __QPM Query 42 */ SELECT" +
            " COUNT(*) AS cnt_42 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND unn3 >= 36"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t3 t1) */ /* __QPM Query 42 */ SELECT" +
            " COUNT(*) AS cnt_42 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND unn3 >= 36"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t3 t1) */ /* __QPM Query 42 */ SELECT" +
            " COUNT(*) AS cnt_42 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND unn3 >= 36"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t3 t1) */ /* __QPM Query 42 */ SELECT" +
            " COUNT(*) AS cnt_42 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1 AND unn3 >= 36"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 43 */ SELECT" +
            " COUNT(*) AS cnt_43 FROM t0, t1 WHERE unn0 = unn1 AND pk0_0 >= 35"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 43 */ SELECT" +
            " COUNT(*) AS cnt_43 FROM t0, t1 WHERE unn0 = unn1 AND pk0_0 >= 35"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 43 */ SELECT" +
            " COUNT(*) AS cnt_43 FROM t0, t1 WHERE unn0 = unn1 AND pk0_0 >= 35"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 43 */ SELECT" +
            " COUNT(*) AS cnt_43 FROM t0, t1 WHERE unn0 = unn1 AND pk0_0 >= 35"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 43 */ SELECT" +
            " COUNT(*) AS cnt_43 FROM t0, t1 WHERE unn0 = unn1 AND pk0_0 >= 35"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 43 */ SELECT" +
            " COUNT(*) AS cnt_43 FROM t0, t1 WHERE unn0 = unn1 AND pk0_0 >= 35"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t2 t0) */ /* __QPM Query 44 */ SELECT" +
            " COUNT(*) AS cnt_44 FROM t2, t0 WHERE unn2 = unn0 AND f2 <= 89"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t2 t0) */ /* __QPM Query 44 */ SELECT" +
            " COUNT(*) AS cnt_44 FROM t2, t0 WHERE unn2 = unn0 AND f2 <= 89"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t2 t0) */ /* __QPM Query 44 */ SELECT" +
            " COUNT(*) AS cnt_44 FROM t2, t0 WHERE unn2 = unn0 AND f2 <= 89"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t2 t0) */ /* __QPM Query 44 */ SELECT" +
            " COUNT(*) AS cnt_44 FROM t2, t0 WHERE unn2 = unn0 AND f2 <= 89"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t2 t0) */ /* __QPM Query 44 */ SELECT" +
            " COUNT(*) AS cnt_44 FROM t2, t0 WHERE unn2 = unn0 AND f2 <= 89"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t2 t0) */ /* __QPM Query 44 */ SELECT" +
            " COUNT(*) AS cnt_44 FROM t2, t0 WHERE unn2 = unn0 AND f2 <= 89"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 45 */ SELECT" +
            " COUNT(*) AS cnt_45 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 = 16"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 45 */ SELECT" +
            " COUNT(*) AS cnt_45 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 = 16"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 45 */ SELECT" +
            " COUNT(*) AS cnt_45 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 = 16"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 45 */ SELECT" +
            " COUNT(*) AS cnt_45 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 = 16"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 45 */ SELECT" +
            " COUNT(*) AS cnt_45 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 = 16"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 45 */ SELECT" +
            " COUNT(*) AS cnt_45 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 = 16"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t2 t0) */ /* __QPM Query 46 */ SELECT" +
            " COUNT(*) AS cnt_46 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1 " +
            "AND pk0_0 = 49 AND pk0_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t2 t0) */ /* __QPM Query 46 */ SELECT" +
            " COUNT(*) AS cnt_46 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1 AND " +
            "pk0_0 = 49 AND pk0_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t2 t0) */ /* __QPM Query 46 */ SELECT" +
            " COUNT(*) AS cnt_46 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1 AND " +
            "pk0_0 = 49 AND pk0_1 > 43"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t2 t0) */ /* __QPM Query 46 */ SELECT" +
            " COUNT(*) AS cnt_46 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1 AND " +
            "pk0_0 = 49 AND pk0_1 > 43"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t2 t0) */ /* __QPM Query 46 */ SELECT" +
            " COUNT(*) AS cnt_46 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1 AND " +
            "pk0_0 = 49 AND pk0_1 > 43"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t2 t0) */ /* __QPM Query 46 */ SELECT" +
            " COUNT(*) AS cnt_46 FROM t2, t0 WHERE pk2_0 = pk0_0 AND pk2_1 = pk0_1 AND " +
            "pk0_0 = 49 AND pk0_1 > 43"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 47 */ SELECT" +
            " COUNT(*) AS cnt_47 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 < 86"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 47 */ SELECT" +
            " COUNT(*) AS cnt_47 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 < 86"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 47 */ SELECT" +
            " COUNT(*) AS cnt_47 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 < 86"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 47 */ SELECT" +
            " COUNT(*) AS cnt_47 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 < 86"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 47 */ SELECT" +
            " COUNT(*) AS cnt_47 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 < 86"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 47 */ SELECT" +
            " COUNT(*) AS cnt_47 FROM t3, t0 WHERE pk3_0 = pk0_0 AND pk3_1 = pk0_1 AND e0 < 86"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 48 */ SELECT" +
            " COUNT(*) AS cnt_48 FROM t3, t0 WHERE c3 = unn0 AND d3 = c0 AND pk0_0 < 72 AND " +
            "pk0_1 = 41"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 48 */ SELECT" +
            " COUNT(*) AS cnt_48 FROM t3, t0 WHERE c3 = unn0 AND d3 = c0 AND pk0_0 < 72 AND " +
            "pk0_1 = 41"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 48 */ SELECT" +
            " COUNT(*) AS cnt_48 FROM t3, t0 WHERE c3 = unn0 AND d3 = c0 AND pk0_0 < 72 AND " +
            "pk0_1 = 41"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 48 */ SELECT" +
            " COUNT(*) AS cnt_48 FROM t3, t0 WHERE c3 = unn0 AND d3 = c0 AND pk0_0 < 72 AND " +
            "pk0_1 = 41"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 48 */ SELECT" +
            " COUNT(*) AS cnt_48 FROM t3, t0 WHERE c3 = unn0 AND d3 = c0 AND pk0_0 < 72 AND " +
            "pk0_1 = 41"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 48 */ SELECT" +
            " COUNT(*) AS cnt_48 FROM t3, t0 WHERE c3 = unn0 AND d3 = c0 AND pk0_0 < 72 AND " +
            "pk0_1 = 41"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t0 t2) */ /* __QPM Query 49 */ SELECT" +
            " COUNT(*) AS cnt_49 FROM t0, t2 WHERE unn0 = unn2"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t0 t2) */ /* __QPM Query 49 */ SELECT" +
            " COUNT(*) AS cnt_49 FROM t0, t2 WHERE unn0 = unn2"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t0 t2) */ /* __QPM Query 49 */ SELECT" +
            " COUNT(*) AS cnt_49 FROM t0, t2 WHERE unn0 = unn2"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t0 t2) */ /* __QPM Query 49 */ SELECT" +
            " COUNT(*) AS cnt_49 FROM t0, t2 WHERE unn0 = unn2"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t0 t2) */ /* __QPM Query 49 */ SELECT" +
            " COUNT(*) AS cnt_49 FROM t0, t2 WHERE unn0 = unn2"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t0 t2) */ /* __QPM Query 49 */ SELECT" +
            " COUNT(*) AS cnt_49 FROM t0, t2 WHERE unn0 = unn2"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t3 t0) */ /* __QPM Query 50 */ SELECT" +
            " COUNT(*) AS cnt_50 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 = 26"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t3 t0) */ /* __QPM Query 50 */ SELECT" +
            " COUNT(*) AS cnt_50 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 = 26"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t3 t0) */ /* __QPM Query 50 */ SELECT" +
            " COUNT(*) AS cnt_50 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 = 26"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t3 t0) */ /* __QPM Query 50 */ SELECT" +
            " COUNT(*) AS cnt_50 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 = 26"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t3 t0) */ /* __QPM Query 50 */ SELECT" +
            " COUNT(*) AS cnt_50 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 = 26"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t3 t0) */ /* __QPM Query 50 */ SELECT" +
            " COUNT(*) AS cnt_50 FROM t3, t0 WHERE unn3 = unn0 AND pk3_1 = 26"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 51 */ SELECT" +
            " COUNT(*) AS cnt_51 FROM t3, t2 WHERE unn3 = unn2 AND unn3 <= 97"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 51 */ SELECT" +
            " COUNT(*) AS cnt_51 FROM t3, t2 WHERE unn3 = unn2 AND unn3 <= 97"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 51 */ SELECT" +
            " COUNT(*) AS cnt_51 FROM t3, t2 WHERE unn3 = unn2 AND unn3 <= 97"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 51 */ SELECT" +
            " COUNT(*) AS cnt_51 FROM t3, t2 WHERE unn3 = unn2 AND unn3 <= 97"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 51 */ SELECT" +
            " COUNT(*) AS cnt_51 FROM t3, t2 WHERE unn3 = unn2 AND unn3 <= 97"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 51 */ SELECT" +
            " COUNT(*) AS cnt_51 FROM t3, t2 WHERE unn3 = unn2 AND unn3 <= 97"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t3 t1) */ /* __QPM Query 52 */ SELECT" +
            " COUNT(*) AS cnt_52 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t3 t1) */ /* __QPM Query 52 */ SELECT" +
            " COUNT(*) AS cnt_52 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t3 t1) */ /* __QPM Query 52 */ SELECT" +
            " COUNT(*) AS cnt_52 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t3 t1) */ /* __QPM Query 52 */ SELECT" +
            " COUNT(*) AS cnt_52 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t3 t1) */ /* __QPM Query 52 */ SELECT" +
            " COUNT(*) AS cnt_52 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t3 t1) */ /* __QPM Query 52 */ SELECT" +
            " COUNT(*) AS cnt_52 FROM t3, t1 WHERE pk3_0 = pk1_0 AND pk3_1 = pk1_1"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t1 t0) */ /* __QPM Query 53 */ SELECT" +
            " COUNT(*) AS cnt_53 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND f1 >= 22"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t1 t0) */ /* __QPM Query 53 */ SELECT" +
            " COUNT(*) AS cnt_53 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND f1 >= 22"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t1 t0) */ /* __QPM Query 53 */ SELECT" +
            " COUNT(*) AS cnt_53 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND f1 >= 22"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t1 t0) */ /* __QPM Query 53 */ SELECT" +
            " COUNT(*) AS cnt_53 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND f1 >= 22"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t1 t0) */ /* __QPM Query 53 */ SELECT" +
            " COUNT(*) AS cnt_53 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND f1 >= 22"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t1 t0) */ /* __QPM Query 53 */ SELECT" +
            " COUNT(*) AS cnt_53 FROM t1, t0 WHERE pk1_0 = pk0_0 AND pk1_1 >= pk0_1 AND f1 >= 22"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t1 t3) */ /* __QPM Query 54 */ SELECT" +
            " COUNT(*) AS cnt_54 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "unn3 >= 44"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t1 t3) */ /* __QPM Query 54 */ SELECT" +
            " COUNT(*) AS cnt_54 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "unn3 >= 44"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t1 t3) */ /* __QPM Query 54 */ SELECT" +
            " COUNT(*) AS cnt_54 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "unn3 >= 44"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t1 t3) */ /* __QPM Query 54 */ SELECT" +
            " COUNT(*) AS cnt_54 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "unn3 >= 44"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t1 t3) */ /* __QPM Query 54 */ SELECT" +
            " COUNT(*) AS cnt_54 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "unn3 >= 44"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t1 t3) */ /* __QPM Query 54 */ SELECT" +
            " COUNT(*) AS cnt_54 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "unn3 >= 44"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t0 t3) */ /* __QPM Query 55 */ SELECT" +
            " COUNT(*) AS cnt_55 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND " +
            "unn3 <= 71"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t0 t3) */ /* __QPM Query 55 */ SELECT" +
            " COUNT(*) AS cnt_55 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND " +
            "unn3 <= 71"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t0 t3) */ /* __QPM Query 55 */ SELECT" +
            " COUNT(*) AS cnt_55 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND " +
            "unn3 <= 71"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t0 t3) */ /* __QPM Query 55 */ SELECT" +
            " COUNT(*) AS cnt_55 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND " +
            "unn3 <= 71"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t0 t3) */ /* __QPM Query 55 */ SELECT" +
            " COUNT(*) AS cnt_55 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND " +
            "unn3 <= 71"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t0 t3) */ /* __QPM Query 55 */ SELECT" +
            " COUNT(*) AS cnt_55 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND " +
            "unn3 <= 71"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t1 t2) */ /* __QPM Query 56 */ SELECT" +
            " COUNT(*) AS cnt_56 FROM t1, t2 WHERE unn1 = unn2 AND pk2_0 > 10 AND pk2_1 < 52"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t1 t2) */ /* __QPM Query 56 */ SELECT" +
            " COUNT(*) AS cnt_56 FROM t1, t2 WHERE unn1 = unn2 AND pk2_0 > 10 AND pk2_1 < 52"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t1 t2) */ /* __QPM Query 56 */ SELECT" +
            " COUNT(*) AS cnt_56 FROM t1, t2 WHERE unn1 = unn2 AND pk2_0 > 10 AND pk2_1 < 52"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t1 t2) */ /* __QPM Query 56 */ SELECT" +
            " COUNT(*) AS cnt_56 FROM t1, t2 WHERE unn1 = unn2 AND pk2_0 > 10 AND pk2_1 < 52"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t1 t2) */ /* __QPM Query 56 */ SELECT" +
            " COUNT(*) AS cnt_56 FROM t1, t2 WHERE unn1 = unn2 AND pk2_0 > 10 AND pk2_1 < 52"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t1 t2) */ /* __QPM Query 56 */ SELECT" +
            " COUNT(*) AS cnt_56 FROM t1, t2 WHERE unn1 = unn2 AND pk2_0 > 10 AND pk2_1 < 52"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 57 */ SELECT" +
            " COUNT(*) AS cnt_57 FROM t0, t1 WHERE d0 = pk1_0 AND e0 = pk1_1 AND pk1_1 <= 70"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 57 */ SELECT" +
            " COUNT(*) AS cnt_57 FROM t0, t1 WHERE d0 = pk1_0 AND e0 = pk1_1 AND pk1_1 <= 70"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 57 */ SELECT" +
            " COUNT(*) AS cnt_57 FROM t0, t1 WHERE d0 = pk1_0 AND e0 = pk1_1 AND pk1_1 <= 70"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 57 */ SELECT" +
            " COUNT(*) AS cnt_57 FROM t0, t1 WHERE d0 = pk1_0 AND e0 = pk1_1 AND pk1_1 <= 70"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 57 */ SELECT" +
            " COUNT(*) AS cnt_57 FROM t0, t1 WHERE d0 = pk1_0 AND e0 = pk1_1 AND pk1_1 <= 70"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 57 */ SELECT" +
            " COUNT(*) AS cnt_57 FROM t0, t1 WHERE d0 = pk1_0 AND e0 = pk1_1 AND pk1_1 <= 70"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t2 t0) */ /* __QPM Query 58 */ SELECT" +
            " COUNT(*) AS cnt_58 FROM t2, t0 WHERE pk2_1 = pk0_0"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t2 t0) */ /* __QPM Query 58 */ SELECT" +
            " COUNT(*) AS cnt_58 FROM t2, t0 WHERE pk2_1 = pk0_0"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t2 t0) */ /* __QPM Query 58 */ SELECT" +
            " COUNT(*) AS cnt_58 FROM t2, t0 WHERE pk2_1 = pk0_0"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t2 t0) */ /* __QPM Query 58 */ SELECT" +
            " COUNT(*) AS cnt_58 FROM t2, t0 WHERE pk2_1 = pk0_0"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t2 t0) */ /* __QPM Query 58 */ SELECT" +
            " COUNT(*) AS cnt_58 FROM t2, t0 WHERE pk2_1 = pk0_0"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t2 t0) */ /* __QPM Query 58 */ SELECT" +
            " COUNT(*) AS cnt_58 FROM t2, t0 WHERE pk2_1 = pk0_0"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t2 t0) */ /* __QPM Query 59 */ SELECT" +
            " COUNT(*) AS cnt_59 FROM t2, t0 WHERE b2 = a0 AND f2 = pk0_1 AND unn0 <= 93"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t2 t0) */ /* __QPM Query 59 */ SELECT" +
            " COUNT(*) AS cnt_59 FROM t2, t0 WHERE b2 = a0 AND f2 = pk0_1 AND unn0 <= 93"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t2 t0) */ /* __QPM Query 59 */ SELECT" +
            " COUNT(*) AS cnt_59 FROM t2, t0 WHERE b2 = a0 AND f2 = pk0_1 AND unn0 <= 93"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t2 t0) */ /* __QPM Query 59 */ SELECT" +
            " COUNT(*) AS cnt_59 FROM t2, t0 WHERE b2 = a0 AND f2 = pk0_1 AND unn0 <= 93"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t2 t0) */ /* __QPM Query 59 */ SELECT" +
            " COUNT(*) AS cnt_59 FROM t2, t0 WHERE b2 = a0 AND f2 = pk0_1 AND unn0 <= 93"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t2 t0) */ /* __QPM Query 59 */ SELECT" +
            " COUNT(*) AS cnt_59 FROM t2, t0 WHERE b2 = a0 AND f2 = pk0_1 AND unn0 <= 93"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t1 t3) */ /* __QPM Query 60 */ SELECT" +
            " COUNT(*) AS cnt_60 FROM t1, t3 WHERE unn1 = unn3"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t1 t3) */ /* __QPM Query 60 */ SELECT" +
            " COUNT(*) AS cnt_60 FROM t1, t3 WHERE unn1 = unn3"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t1 t3) */ /* __QPM Query 60 */ SELECT" +
            " COUNT(*) AS cnt_60 FROM t1, t3 WHERE unn1 = unn3"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t1 t3) */ /* __QPM Query 60 */ SELECT" +
            " COUNT(*) AS cnt_60 FROM t1, t3 WHERE unn1 = unn3"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t1 t3) */ /* __QPM Query 60 */ SELECT" +
            " COUNT(*) AS cnt_60 FROM t1, t3 WHERE unn1 = unn3"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t1 t3) */ /* __QPM Query 60 */ SELECT" +
            " COUNT(*) AS cnt_60 FROM t1, t3 WHERE unn1 = unn3"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t2 t1) */ /* __QPM Query 61 */ SELECT" +
            " COUNT(*) AS cnt_61 FROM t2, t1 WHERE pk2_0 = pk1_0 AND pk2_1 > pk1_1"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t2 t1) */ /* __QPM Query 61 */ SELECT" +
            " COUNT(*) AS cnt_61 FROM t2, t1 WHERE pk2_0 = pk1_0 AND pk2_1 > pk1_1"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t2 t1) */ /* __QPM Query 61 */ SELECT" +
            " COUNT(*) AS cnt_61 FROM t2, t1 WHERE pk2_0 = pk1_0 AND pk2_1 > pk1_1"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t2 t1) */ /* __QPM Query 61 */ SELECT" +
            " COUNT(*) AS cnt_61 FROM t2, t1 WHERE pk2_0 = pk1_0 AND pk2_1 > pk1_1"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t2 t1) */ /* __QPM Query 61 */ SELECT" +
            " COUNT(*) AS cnt_61 FROM t2, t1 WHERE pk2_0 = pk1_0 AND pk2_1 > pk1_1"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t2 t1) */ /* __QPM Query 61 */ SELECT" +
            " COUNT(*) AS cnt_61 FROM t2, t1 WHERE pk2_0 = pk1_0 AND pk2_1 > pk1_1"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t0 t3) */ /* __QPM Query 62 */ SELECT" +
            " COUNT(*) AS cnt_62 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND unn3 = 93"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t0 t3) */ /* __QPM Query 62 */ SELECT" +
            " COUNT(*) AS cnt_62 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND unn3 = 93"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t0 t3) */ /* __QPM Query 62 */ SELECT" +
            " COUNT(*) AS cnt_62 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND unn3 = 93"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t0 t3) */ /* __QPM Query 62 */ SELECT" +
            " COUNT(*) AS cnt_62 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND unn3 = 93"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t0 t3) */ /* __QPM Query 62 */ SELECT" +
            " COUNT(*) AS cnt_62 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND unn3 = 93"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t0 t3) */ /* __QPM Query 62 */ SELECT" +
            " COUNT(*) AS cnt_62 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1 AND unn3 = 93"),
        new QueryInfo("/*+ Leading((t1 t3))  MergeJoin(t1 t3) */ /* __QPM Query 63 */ SELECT" +
            " COUNT(*) AS cnt_63 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t1 t3))  HashJoin(t1 t3) */ /* __QPM Query 63 */ SELECT" +
            " COUNT(*) AS cnt_63 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t1 t3))  NestLoop(t1 t3) */ /* __QPM Query 63 */ SELECT" +
            " COUNT(*) AS cnt_63 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t3 t1))  MergeJoin(t1 t3) */ /* __QPM Query 63 */ SELECT" +
            " COUNT(*) AS cnt_63 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t3 t1))  HashJoin(t1 t3) */ /* __QPM Query 63 */ SELECT" +
            " COUNT(*) AS cnt_63 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t3 t1))  NestLoop(t1 t3) */ /* __QPM Query 63 */ SELECT" +
            " COUNT(*) AS cnt_63 FROM t1, t3 WHERE pk1_0 = pk3_0 AND pk1_1 = pk3_1 AND " +
            "pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t0 t2) */ /* __QPM Query 64 */ SELECT" +
            " COUNT(*) AS cnt_64 FROM t0, t2 WHERE unn0 = unn2 AND unn2 <= 70"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t0 t2) */ /* __QPM Query 64 */ SELECT" +
            " COUNT(*) AS cnt_64 FROM t0, t2 WHERE unn0 = unn2 AND unn2 <= 70"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t0 t2) */ /* __QPM Query 64 */ SELECT" +
            " COUNT(*) AS cnt_64 FROM t0, t2 WHERE unn0 = unn2 AND unn2 <= 70"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t0 t2) */ /* __QPM Query 64 */ SELECT" +
            " COUNT(*) AS cnt_64 FROM t0, t2 WHERE unn0 = unn2 AND unn2 <= 70"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t0 t2) */ /* __QPM Query 64 */ SELECT" +
            " COUNT(*) AS cnt_64 FROM t0, t2 WHERE unn0 = unn2 AND unn2 <= 70"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t0 t2) */ /* __QPM Query 64 */ SELECT" +
            " COUNT(*) AS cnt_64 FROM t0, t2 WHERE unn0 = unn2 AND unn2 <= 70"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 65 */ SELECT" +
            " COUNT(*) AS cnt_65 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 = 29 AND pk3_1 >= 44"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 65 */ SELECT" +
            " COUNT(*) AS cnt_65 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 = 29 AND pk3_1 >= 44"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 65 */ SELECT" +
            " COUNT(*) AS cnt_65 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 = 29 AND pk3_1 >= 44"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 65 */ SELECT" +
            " COUNT(*) AS cnt_65 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 = 29 AND pk3_1 >= 44"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 65 */ SELECT" +
            " COUNT(*) AS cnt_65 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 = 29 AND pk3_1 >= 44"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 65 */ SELECT" +
            " COUNT(*) AS cnt_65 FROM t2, t3 WHERE unn2 = unn3 AND pk3_0 = 29 AND pk3_1 >= 44"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 66 */ SELECT" +
            " COUNT(*) AS cnt_66 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk2_0 <= 97"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 66 */ SELECT" +
            " COUNT(*) AS cnt_66 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk2_0 <= 97"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 66 */ SELECT" +
            " COUNT(*) AS cnt_66 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk2_0 <= 97"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 66 */ SELECT" +
            " COUNT(*) AS cnt_66 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk2_0 <= 97"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 66 */ SELECT" +
            " COUNT(*) AS cnt_66 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk2_0 <= 97"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 66 */ SELECT" +
            " COUNT(*) AS cnt_66 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk2_0 <= 97"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 67 */ SELECT" +
            " COUNT(*) AS cnt_67 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "unn3 <= 57"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 67 */ SELECT" +
            " COUNT(*) AS cnt_67 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "unn3 <= 57"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 67 */ SELECT" +
            " COUNT(*) AS cnt_67 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "unn3 <= 57"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 67 */ SELECT" +
            " COUNT(*) AS cnt_67 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "unn3 <= 57"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 67 */ SELECT" +
            " COUNT(*) AS cnt_67 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "unn3 <= 57"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 67 */ SELECT" +
            " COUNT(*) AS cnt_67 FROM t3, t2 WHERE pk3_0 = pk2_0 AND pk3_1 = pk2_1 AND " +
            "unn3 <= 57"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t1 t2) */ /* __QPM Query 68 */ SELECT" +
            " COUNT(*) AS cnt_68 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 >= pk2_1"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t1 t2) */ /* __QPM Query 68 */ SELECT" +
            " COUNT(*) AS cnt_68 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 >= pk2_1"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t1 t2) */ /* __QPM Query 68 */ SELECT" +
            " COUNT(*) AS cnt_68 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 >= pk2_1"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t1 t2) */ /* __QPM Query 68 */ SELECT" +
            " COUNT(*) AS cnt_68 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 >= pk2_1"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t1 t2) */ /* __QPM Query 68 */ SELECT" +
            " COUNT(*) AS cnt_68 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 >= pk2_1"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t1 t2) */ /* __QPM Query 68 */ SELECT" +
            " COUNT(*) AS cnt_68 FROM t1, t2 WHERE pk1_0 = pk2_0 AND pk1_1 >= pk2_1"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 69 */ SELECT" +
            " COUNT(*) AS cnt_69 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND " +
            "pk2_0 < 73 AND pk2_1 <= 84"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 69 */ SELECT" +
            " COUNT(*) AS cnt_69 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND " +
            "pk2_0 < 73 AND pk2_1 <= 84"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 69 */ SELECT" +
            " COUNT(*) AS cnt_69 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND " +
            "pk2_0 < 73 AND pk2_1 <= 84"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 69 */ SELECT" +
            " COUNT(*) AS cnt_69 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND " +
            "pk2_0 < 73 AND pk2_1 <= 84"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 69 */ SELECT" +
            " COUNT(*) AS cnt_69 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND " +
            "pk2_0 < 73 AND pk2_1 <= 84"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 69 */ SELECT" +
            " COUNT(*) AS cnt_69 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND " +
            "pk2_0 < 73 AND pk2_1 <= 84"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 70 */ SELECT" +
            " COUNT(*) AS cnt_70 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn3 < 60"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 70 */ SELECT" +
            " COUNT(*) AS cnt_70 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn3 < 60"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 70 */ SELECT" +
            " COUNT(*) AS cnt_70 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn3 < 60"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 70 */ SELECT" +
            " COUNT(*) AS cnt_70 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn3 < 60"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 70 */ SELECT" +
            " COUNT(*) AS cnt_70 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn3 < 60"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 70 */ SELECT" +
            " COUNT(*) AS cnt_70 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND unn3 < 60"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 71 */ SELECT" +
            " COUNT(*) AS cnt_71 FROM t3, t2 WHERE e3 = unn2 AND f3 = b2 AND d3 > 37"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 71 */ SELECT" +
            " COUNT(*) AS cnt_71 FROM t3, t2 WHERE e3 = unn2 AND f3 = b2 AND d3 > 37"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 71 */ SELECT" +
            " COUNT(*) AS cnt_71 FROM t3, t2 WHERE e3 = unn2 AND f3 = b2 AND d3 > 37"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 71 */ SELECT" +
            " COUNT(*) AS cnt_71 FROM t3, t2 WHERE e3 = unn2 AND f3 = b2 AND d3 > 37"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 71 */ SELECT" +
            " COUNT(*) AS cnt_71 FROM t3, t2 WHERE e3 = unn2 AND f3 = b2 AND d3 > 37"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 71 */ SELECT" +
            " COUNT(*) AS cnt_71 FROM t3, t2 WHERE e3 = unn2 AND f3 = b2 AND d3 > 37"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 72 */ SELECT" +
            " COUNT(*) AS cnt_72 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk3_0 >= 24 AND pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 72 */ SELECT" +
            " COUNT(*) AS cnt_72 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk3_0 >= 24 AND pk3_1 = 16"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 72 */ SELECT" +
            " COUNT(*) AS cnt_72 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk3_0 >= 24 AND pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 72 */ SELECT" +
            " COUNT(*) AS cnt_72 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk3_0 >= 24 AND pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 72 */ SELECT" +
            " COUNT(*) AS cnt_72 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk3_0 >= 24 AND pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 72 */ SELECT" +
            " COUNT(*) AS cnt_72 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "pk3_0 >= 24 AND pk3_1 > 16"),
        new QueryInfo("/*+ Leading((t0 t1))  MergeJoin(t0 t1) */ /* __QPM Query 73 */ SELECT" +
            " COUNT(*) AS cnt_73 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 > pk1_1 AND unn1 >= 44"),
        new QueryInfo("/*+ Leading((t0 t1))  HashJoin(t0 t1) */ /* __QPM Query 73 */ SELECT" +
            " COUNT(*) AS cnt_73 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 > pk1_1 AND unn1 >= 44"),
        new QueryInfo("/*+ Leading((t0 t1))  NestLoop(t0 t1) */ /* __QPM Query 73 */ SELECT" +
            " COUNT(*) AS cnt_73 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 > pk1_1 AND unn1 >= 44"),
        new QueryInfo("/*+ Leading((t1 t0))  MergeJoin(t0 t1) */ /* __QPM Query 73 */ SELECT" +
            " COUNT(*) AS cnt_73 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 > pk1_1 AND unn1 >= 44"),
        new QueryInfo("/*+ Leading((t1 t0))  HashJoin(t0 t1) */ /* __QPM Query 73 */ SELECT" +
            " COUNT(*) AS cnt_73 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 > pk1_1 AND unn1 >= 44"),
        new QueryInfo("/*+ Leading((t1 t0))  NestLoop(t0 t1) */ /* __QPM Query 73 */ SELECT" +
            " COUNT(*) AS cnt_73 FROM t0, t1 WHERE pk0_0 = pk1_0 AND pk0_1 > pk1_1 AND unn1 >= 44"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 74 */ SELECT" +
            " COUNT(*) AS cnt_74 FROM t2, t3 WHERE unn2 = unn3 AND pk2_0 <= 83 AND pk2_1 = 24"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 74 */ SELECT" +
            " COUNT(*) AS cnt_74 FROM t2, t3 WHERE unn2 = unn3 AND pk2_0 <= 83 AND pk2_1 = 24"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 74 */ SELECT" +
            " COUNT(*) AS cnt_74 FROM t2, t3 WHERE unn2 = unn3 AND pk2_0 <= 83 AND pk2_1 = 24"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 74 */ SELECT" +
            " COUNT(*) AS cnt_74 FROM t2, t3 WHERE unn2 = unn3 AND pk2_0 <= 83 AND pk2_1 = 24"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 74 */ SELECT" +
            " COUNT(*) AS cnt_74 FROM t2, t3 WHERE unn2 = unn3 AND pk2_0 <= 83 AND pk2_1 = 24"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 74 */ SELECT" +
            " COUNT(*) AS cnt_74 FROM t2, t3 WHERE unn2 = unn3 AND pk2_0 <= 83 AND pk2_1 = 24"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t2 t1) */ /* __QPM Query 75 */ SELECT" +
            " COUNT(*) AS cnt_75 FROM t2, t1 WHERE unn2 = unn1 AND b1 = 34"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t2 t1) */ /* __QPM Query 75 */ SELECT" +
            " COUNT(*) AS cnt_75 FROM t2, t1 WHERE unn2 = unn1 AND b1 = 34"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t2 t1) */ /* __QPM Query 75 */ SELECT" +
            " COUNT(*) AS cnt_75 FROM t2, t1 WHERE unn2 = unn1 AND b1 = 34"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t2 t1) */ /* __QPM Query 75 */ SELECT" +
            " COUNT(*) AS cnt_75 FROM t2, t1 WHERE unn2 = unn1 AND b1 = 34"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t2 t1) */ /* __QPM Query 75 */ SELECT" +
            " COUNT(*) AS cnt_75 FROM t2, t1 WHERE unn2 = unn1 AND b1 = 34"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t2 t1) */ /* __QPM Query 75 */ SELECT" +
            " COUNT(*) AS cnt_75 FROM t2, t1 WHERE unn2 = unn1 AND b1 = 34"),
        new QueryInfo("/*+ Leading((t2 t1))  MergeJoin(t2 t1) */ /* __QPM Query 76 */ SELECT" +
            " COUNT(*) AS cnt_76 FROM t2, t1 WHERE a2 = f1"),
        new QueryInfo("/*+ Leading((t2 t1))  HashJoin(t2 t1) */ /* __QPM Query 76 */ SELECT" +
            " COUNT(*) AS cnt_76 FROM t2, t1 WHERE a2 = f1"),
        new QueryInfo("/*+ Leading((t2 t1))  NestLoop(t2 t1) */ /* __QPM Query 76 */ SELECT" +
            " COUNT(*) AS cnt_76 FROM t2, t1 WHERE a2 = f1"),
        new QueryInfo("/*+ Leading((t1 t2))  MergeJoin(t2 t1) */ /* __QPM Query 76 */ SELECT" +
            " COUNT(*) AS cnt_76 FROM t2, t1 WHERE a2 = f1"),
        new QueryInfo("/*+ Leading((t1 t2))  HashJoin(t2 t1) */ /* __QPM Query 76 */ SELECT" +
            " COUNT(*) AS cnt_76 FROM t2, t1 WHERE a2 = f1"),
        new QueryInfo("/*+ Leading((t1 t2))  NestLoop(t2 t1) */ /* __QPM Query 76 */ SELECT" +
            " COUNT(*) AS cnt_76 FROM t2, t1 WHERE a2 = f1"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t0 t2) */ /* __QPM Query 77 */ SELECT" +
            " COUNT(*) AS cnt_77 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 < pk2_1 AND " +
            "pk0_0 < 58 AND pk0_1 <= 65"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t0 t2) */ /* __QPM Query 77 */ SELECT" +
            " COUNT(*) AS cnt_77 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 < pk2_1 AND " +
            "pk0_0 < 58 AND pk0_1 <= 65"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t0 t2) */ /* __QPM Query 77 */ SELECT" +
            " COUNT(*) AS cnt_77 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 < pk2_1 AND " +
            "pk0_0 < 58 AND pk0_1 <= 65"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t0 t2) */ /* __QPM Query 77 */ SELECT" +
            " COUNT(*) AS cnt_77 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 < pk2_1 AND " +
            "pk0_0 < 58 AND pk0_1 <= 65"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t0 t2) */ /* __QPM Query 77 */ SELECT" +
            " COUNT(*) AS cnt_77 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 < pk2_1 AND " +
            "pk0_0 < 58 AND pk0_1 <= 65"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t0 t2) */ /* __QPM Query 77 */ SELECT" +
            " COUNT(*) AS cnt_77 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 < pk2_1 AND " +
            "pk0_0 < 58 AND pk0_1 <= 65"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 78 */ SELECT" +
            " COUNT(*) AS cnt_78 FROM t2, t3 WHERE unn2 = unn3 AND e3 = 62"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 78 */ SELECT" +
            " COUNT(*) AS cnt_78 FROM t2, t3 WHERE unn2 = unn3 AND e3 = 62"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 78 */ SELECT" +
            " COUNT(*) AS cnt_78 FROM t2, t3 WHERE unn2 = unn3 AND e3 = 62"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 78 */ SELECT" +
            " COUNT(*) AS cnt_78 FROM t2, t3 WHERE unn2 = unn3 AND e3 = 62"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 78 */ SELECT" +
            " COUNT(*) AS cnt_78 FROM t2, t3 WHERE unn2 = unn3 AND e3 = 62"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 78 */ SELECT" +
            " COUNT(*) AS cnt_78 FROM t2, t3 WHERE unn2 = unn3 AND e3 = 62"),
        new QueryInfo("/*+ Leading((t0 t3))  MergeJoin(t0 t3) */ /* __QPM Query 79 */ SELECT" +
            " COUNT(*) AS cnt_79 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t0 t3))  HashJoin(t0 t3) */ /* __QPM Query 79 */ SELECT" +
            " COUNT(*) AS cnt_79 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t0 t3))  NestLoop(t0 t3) */ /* __QPM Query 79 */ SELECT" +
            " COUNT(*) AS cnt_79 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t3 t0))  MergeJoin(t0 t3) */ /* __QPM Query 79 */ SELECT" +
            " COUNT(*) AS cnt_79 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t3 t0))  HashJoin(t0 t3) */ /* __QPM Query 79 */ SELECT" +
            " COUNT(*) AS cnt_79 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t3 t0))  NestLoop(t0 t3) */ /* __QPM Query 79 */ SELECT" +
            " COUNT(*) AS cnt_79 FROM t0, t3 WHERE pk0_0 = pk3_0 AND pk0_1 = pk3_1"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 80 */ SELECT" +
            " COUNT(*) AS cnt_80 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "unn2 <= 91"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 80 */ SELECT" +
            " COUNT(*) AS cnt_80 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "unn2 <= 91"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t2 t3) */ /* __QPM Query 80 */ SELECT" +
            " COUNT(*) AS cnt_80 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "unn2 <= 91"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t2 t3) */ /* __QPM Query 80 */ SELECT" +
            " COUNT(*) AS cnt_80 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 " +
            "AND unn2 <= 91"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t2 t3) */ /* __QPM Query 80 */ SELECT" +
            " COUNT(*) AS cnt_80 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "unn2 <= 91"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t2 t3) */ /* __QPM Query 80 */ SELECT" +
            " COUNT(*) AS cnt_80 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 <= pk3_1 AND " +
            "unn2 <= 91"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t0 t2) */ /* __QPM Query 81 */ SELECT" +
            " COUNT(*) AS cnt_81 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1 AND " +
            "pk2_1 > 27"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t0 t2) */ /* __QPM Query 81 */ SELECT" +
            " COUNT(*) AS cnt_81 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1 AND " +
            "pk2_1 > 27"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t0 t2) */ /* __QPM Query 81 */ SELECT" +
            " COUNT(*) AS cnt_81 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1 AND " +
            "pk2_1 > 27"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t0 t2) */ /* __QPM Query 81 */ SELECT" +
            " COUNT(*) AS cnt_81 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1 AND " +
            "pk2_1 > 27"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t0 t2) */ /* __QPM Query 81 */ SELECT" +
            " COUNT(*) AS cnt_81 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1 AND " +
            "pk2_1 > 27"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t0 t2) */ /* __QPM Query 81 */ SELECT" +
            " COUNT(*) AS cnt_81 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1 AND " +
            "pk2_1 > 27"),
        new QueryInfo("/*+ Leading((t0 t2))  MergeJoin(t0 t2) */ /* __QPM Query 82 */ SELECT" +
            " COUNT(*) AS cnt_82 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1"),
        new QueryInfo("/*+ Leading((t0 t2))  HashJoin(t0 t2) */ /* __QPM Query 82 */ SELECT" +
            " COUNT(*) AS cnt_82 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1"),
        new QueryInfo("/*+ Leading((t0 t2))  NestLoop(t0 t2) */ /* __QPM Query 82 */ SELECT" +
            " COUNT(*) AS cnt_82 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1"),
        new QueryInfo("/*+ Leading((t2 t0))  MergeJoin(t0 t2) */ /* __QPM Query 82 */ SELECT" +
            " COUNT(*) AS cnt_82 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1"),
        new QueryInfo("/*+ Leading((t2 t0))  HashJoin(t0 t2) */ /* __QPM Query 82 */ SELECT" +
            " COUNT(*) AS cnt_82 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1"),
        new QueryInfo("/*+ Leading((t2 t0))  NestLoop(t0 t2) */ /* __QPM Query 82 */ SELECT" +
            " COUNT(*) AS cnt_82 FROM t0, t2 WHERE pk0_0 = pk2_0 AND pk0_1 <= pk2_1"),
        new QueryInfo("/*+ Leading((t3 t2))  MergeJoin(t3 t2) */ /* __QPM Query 83 */ SELECT" +
            " COUNT(*) AS cnt_83 FROM t3, t2 WHERE unn3 = unn2 AND pk3_1 > 43"),
        new QueryInfo("/*+ Leading((t3 t2))  HashJoin(t3 t2) */ /* __QPM Query 83 */ SELECT" +
            " COUNT(*) AS cnt_83 FROM t3, t2 WHERE unn3 = unn2 AND pk3_1 > 43"),
        new QueryInfo("/*+ Leading((t3 t2))  NestLoop(t3 t2) */ /* __QPM Query 83 */ SELECT" +
            " COUNT(*) AS cnt_83 FROM t3, t2 WHERE unn3 = unn2 AND pk3_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t3 t2) */ /* __QPM Query 83 */ SELECT" +
            " COUNT(*) AS cnt_83 FROM t3, t2 WHERE unn3 = unn2 AND pk3_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t3 t2) */ /* __QPM Query 83 */ SELECT" +
            " COUNT(*) AS cnt_83 FROM t3, t2 WHERE unn3 = unn2 AND pk3_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t3))  NestLoop(t3 t2) */ /* __QPM Query 83 */ SELECT" +
            " COUNT(*) AS cnt_83 FROM t3, t2 WHERE unn3 = unn2 AND pk3_1 > 43"),
        new QueryInfo("/*+ Leading((t2 t3))  MergeJoin(t2 t3) */ /* __QPM Query 84 */ SELECT" +
            " COUNT(*) AS cnt_84 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND c3 >= 26"),
        new QueryInfo("/*+ Leading((t2 t3))  HashJoin(t2 t3) */ /* __QPM Query 84 */ SELECT" +
            " COUNT(*) AS cnt_84 FROM t2, t3 WHERE pk2_0 = pk3_0 AND pk2_1 = pk3_1 AND c3 >= 26")
    };
  }
}
