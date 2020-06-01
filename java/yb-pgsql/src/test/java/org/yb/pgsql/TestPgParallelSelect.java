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

package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgParallelSelect extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgParallelSelect.class);
  private static final int kNumShardsPerTserver = 20;
  private static final int kSlowdownPgsqlAggregateReadMs = 100;
  private static final long maxTotalMillis =
    (long)((3 * kNumShardsPerTserver * kSlowdownPgsqlAggregateReadMs) * 0.9);

  private void verifyAggregatePushdownMetric(Statement statement,
                                             String stmt,
                                             boolean pushdown_expected) throws Exception {
    long elapsedMillis = verifyStatementMetric(statement, stmt, AGGREGATE_PUSHDOWNS_METRIC,
                                               pushdown_expected ? 1 : 0, 1, true);
    assertTrue(
        String.format("Query took %d ms! Expected %d ms at most", elapsedMillis, maxTotalMillis),
        elapsedMillis <= maxTotalMillis);
  }

  @Override
  protected int overridableNumShardsPerTServer() {
    return kNumShardsPerTserver;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("TEST_slowdown_pgsql_aggregate_read_ms",
        Integer.toString(kSlowdownPgsqlAggregateReadMs));
    flagMap.put("ysql_select_parallelism", Integer.toString(3 * kNumShardsPerTserver));

    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("ysql_num_shards_per_tserver", Integer.toString(kNumShardsPerTserver));
    return flagMap;
  }

  @Test
  public void testParallelAggregatePushdowns() throws Exception {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable("aggtest");

      // Pushdown COUNT/MAX/MIN/SUM for INTEGER/FLOAT.
      verifyAggregatePushdownMetric(
          statement, "SELECT COUNT(vi), MAX(vi), MIN(vi), SUM(vi) FROM aggtest", true);
      verifyAggregatePushdownMetric(
          statement, "SELECT COUNT(r), MAX(r), MIN(r), SUM(r) FROM aggtest", true);

      // Pushdown COUNT(*).
      verifyAggregatePushdownMetric(
          statement, "SELECT COUNT(*) FROM aggtest", true);

      // Pushdown for BIGINT COUNT/MAX/MIN.
      verifyAggregatePushdownMetric(
          statement, "SELECT COUNT(h), MAX(h), MIN(h) FROM aggtest", true);

      // Pushdown COUNT/MIN/MAX for text.
      verifyAggregatePushdownMetric(
          statement, "SELECT COUNT(vs), MAX(vs), MIN(vs) FROM aggtest", true);

      // Pushdown shared aggregates.
      verifyAggregatePushdownMetric(
          statement, "SELECT MAX(vi), MAX(vi) + 1 FROM aggtest", true);

      // Create table with NUMERIC/DECIMAL types.
      statement.execute("CREATE TABLE aggtest2 (n numeric, d decimal)");

      // Pushdown COUNT for NUMERIC/DECIMAL types.
      verifyAggregatePushdownMetric(
          statement, "SELECT COUNT(n), COUNT(d) FROM aggtest2", true);
    }
  }
}
