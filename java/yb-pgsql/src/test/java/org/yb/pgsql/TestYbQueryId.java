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

import java.sql.Statement;
import java.util.Collections;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;
import static org.yb.AssertionWrappers.*;
import static org.yb.pgsql.ExplainAnalyzeUtils.getExplainQueryId;

/**
 * Runs tests for tests query id stability, including queries
 * with comments, hints, and different spacing.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestYbQueryId extends BasePgSQLTest {

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  /**
   * Try combinations of GUCs related to hinting to make sure query ids
   * are stable across values. Each query should get the same query id
   * regardless of GUC values, or the presence/absence comments or hints.
   *
   * This test runs the set of queries twice. The first run is with
   * hint table caching on (default) and the second with hint table caching off.
   * On both runs we collect the query ids for the queries. On the second run
   * we assert the second run query id should be the same as that seen in
   * the first run for queries we expect query id stability for across
   * cluster destruction/creation.
   *
   * @throws Exception
   */
  @Test
  public void testYbQueryId() throws Exception {
    final String[] queryArr = {"SELECT * FROM t1, t2 WHERE a1=a2 AND a1<5",
      "/*+ SeqScan(t2) */ SELECT * FROM t1, t2 WHERE a1=a2 AND a2>2",
      "SELECT * FROM t1, t2 WHERE a1=a2 AND a2>2 /*+ MergeJoin(t1 t2) */",
      "select * from information_schema.tables",
      "select   *       from                information_schema.tables   ",
      "/*+ set(yb_enable_batchednl false) */ select * from information_schema.columns",
      "select * from information_schema.columns /*+ set(yb_enable_batchednl false) */"};

    long[][] queryIdArr = {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}};

    for (int runId = 0; runId < 2; ++runId) {
      Statement stmt = connection.createStatement();
      stmt.execute("create table t1(a1 int, primary key(a1 asc))");
      stmt.execute("create table t2(a2 int, primary key(a2 desc))");
      stmt.execute("create extension if not exists pg_hint_plan");

      int queryIndex = 0;
      for (String query : queryArr) {
        try (Statement stmt1 = connection.createStatement()) {
          stmt1.execute("RESET ALL");

          long queryId1 = getExplainQueryId(stmt1, query);
          queryIdArr[runId][queryIndex] = queryId1;

          stmt1.execute("SET pg_hint_plan.enable_hint = TRUE");
          long queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.hints_anywhere TO ON");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.enable_hint_table = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET yb_enable_base_scans_cost_model = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET yb_enable_optimizer_statistics = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.yb_use_query_id_for_hinting = TRUE");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          /*
          * Place comment in front of the query.
          */
          query = "/* comment */ " + query;
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("SET pg_hint_plan.hints_anywhere TO ON");
          queryId2 = getExplainQueryId(stmt1, query);
          assertEquals(queryId1, queryId2);

          stmt1.execute("RESET ALL");
          stmt1.close();
        }

        ++queryIndex;
      }

      // Queries 3 and 4 are the same except for spaces. Queries 5 and 6
      // are the same also except for hint placement.
      assertEquals(queryIdArr[runId][3], queryIdArr[runId][4]);
      assertEquals(queryIdArr[runId][5], queryIdArr[runId][6]);

      if (runId == 1) {
        // Queries 3, 4, 5, and 6 should have stable query ids across runs
        // since they go against tables whose internal ids should not vary
        // across cluster restart.
        assertEquals(queryIdArr[0][3], queryIdArr[1][3]);
        assertEquals(queryIdArr[0][4], queryIdArr[1][4]);
        assertEquals(queryIdArr[0][5], queryIdArr[1][5]);
        assertEquals(queryIdArr[0][6], queryIdArr[1][6]);
      } else {
        // Set up the server flags for restart for the second run.
        Map<String, String> flagMap = super.getTServerFlags();
        appendToYsqlPgConf(flagMap, "pg_hint_plan.yb_enable_hint_table_cache=off");
        restartClusterWithFlags(Collections.emptyMap(), flagMap);
      }
    }
  }
}
