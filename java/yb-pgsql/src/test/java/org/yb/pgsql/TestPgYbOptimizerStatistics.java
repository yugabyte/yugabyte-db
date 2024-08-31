// Copyright (c) YugaByte, Inc.
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

// Tests cases covering yb_enable_optimizer_statistics flag

package org.yb.pgsql;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;

import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;
import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;

import java.sql.Statement;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.json.Checker;

@RunWith(value=YBTestRunner.class)
public class TestPgYbOptimizerStatistics extends BasePgSQLTest {
  // If tuple count in pg_class is 0, either because table is empty or ANALYZE
  // has not yet been called, then CBO in YB assumes that the table has 1000
  // rows by default. This is needed to avoid poor query plan choices.
  private final long DEFAULT_TUPLE_COUNT = 1000;

  private TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false);
  }

  public void testExplain(String query, Checker checker) throws Exception {
    try (Statement stmt = connection.createStatement()) {
      ExplainAnalyzeUtils.testExplain(stmt, query, checker);
    }
  }

  private void helperCheckSeqScanPlanRowsEstimate(
      String query, long expectation) throws Exception {
      try (Statement stmt = connection.createStatement()) {
        testExplain(
            query,
            makeTopLevelBuilder()
                .storageReadRequests(Checkers.greater(0))
                .storageWriteRequests(Checkers.equal(0))
                .plan(makePlanBuilder()
                    .nodeType(NODE_SEQ_SCAN)
                    .planRows(Checkers.equal(expectation))
                    .build())
                .build());
    }
  }

  @Test
  public void testDefaultTupleCount() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE TABLE TEST (k1 int, v1 int)"));

      // Expect 1000 rows estimated when table is empty, regardless of whether
      // `yb_enable_optimizer_statistics` is TRUE or FALSE and ANALYZE is called
      // or not.
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=OFF"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          DEFAULT_TUPLE_COUNT);
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=ON"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          DEFAULT_TUPLE_COUNT);

      stmt.execute(String.format("ANALYZE TEST"));
      // ANALYZEd empty table shouldn't look like a 1000 row table.
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=OFF"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          1);
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=ON"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          1);

      // With 100 rows in table, and ANALYZE not yet called, estimated rows
      // should still be 1000.
      stmt.execute("INSERT INTO TEST (SELECT s, s FROM generate_series(1, 100) s)");
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=OFF"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          1);
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=ON"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          1);

      // After ANALYZE is called, the estimated rows should be 100.
      stmt.execute(String.format("ANALYZE TEST"));
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=OFF"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          100);
      stmt.execute(String.format("SET yb_enable_optimizer_statistics=ON"));
      helperCheckSeqScanPlanRowsEstimate(String.format(
          "/*+ SeqScan(TEST) */ SELECT * FROM TEST"),
          100);

      stmt.execute(String.format("DROP TABLE TEST"));
    }
  }
}
