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

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.INDEX_SCAN_DIRECTION_ARBITRARY;
import static org.yb.pgsql.ExplainAnalyzeUtils.INDEX_SCAN_DIRECTION_BACKWARD;
import static org.yb.pgsql.ExplainAnalyzeUtils.INDEX_SCAN_DIRECTION_FORWARD;
import static org.yb.pgsql.ExplainAnalyzeUtils.makePlanBuilder;
import static org.yb.pgsql.ExplainAnalyzeUtils.makeTopLevelBuilder;
import static org.yb.pgsql.ExplainAnalyzeUtils.testExplainNoTiming;

import java.sql.Statement;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.util.json.Checkers;

@RunWith(value=YBTestRunner.class)
public class TestPgSequentialSelect extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequentialSelect.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    // Limit parallelism so even parallel requests are sent one by one, so it is easy to count RPCs
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_select_parallelism", "1");

    return flagMap;
  }

  private void testRangeScanHelper(
      Statement stmt, String query,
      String table_name, String index_name, String scan_direction,
      long expected_table_rpcs, long expected_index_rpcs) throws Exception {
    try {
      PlanCheckerBuilder scan_builder = makePlanBuilder()
          .nodeType(NODE_INDEX_SCAN)
          .scanDirection(scan_direction)
          .relationName(table_name)
          .indexName(index_name);
      // Optional checkers
      if (expected_table_rpcs > 0) {
        scan_builder = scan_builder.storageTableReadRequests(Checkers.equal(expected_table_rpcs));
      }
      if (expected_index_rpcs > 0) {
        scan_builder = scan_builder.storageIndexReadRequests(Checkers.equal(expected_index_rpcs));
      }
      testExplainNoTiming(stmt,
          String.format(query, table_name),
          makeTopLevelBuilder()
              .plan(scan_builder.build())
              .storageReadRequests(Checkers.equal(expected_table_rpcs + expected_index_rpcs))
              .build());
    }
    catch (AssertionError e) {
      LOG.info("Failed Query: " + query, e);
      throw e;
    }
  }

  @Test
  public void testRangeIndexScanOnHashTable() throws Exception {
    // Setup
    try (Statement statement = connection.createStatement()) {
      statement.execute(
          "CREATE TABLE range_hash_pk(k int, i1 int, i2 int, v text, primary key(k hash))" +
          " SPLIT INTO 10 TABLETS");
      statement.execute(
          "CREATE INDEX ON range_hash_pk(i1 desc)" +
          " SPLIT AT VALUES ((9), (8), (7), (6), (5), (4), (3), (2), (1))");
      statement.execute(
          "CREATE INDEX ON range_hash_pk(i2 asc)" +
          " SPLIT AT VALUES ((200), (400), (600), (800), (1000), (1200), (1400), (1600), (1800))");
      statement.execute(
          "INSERT INTO range_hash_pk SELECT i, i % 10, (1000 - i) * 2 + 1, 'Value ' || i::text" +
          " FROM generate_series(1, 1000) i");
      String table_name = "range_hash_pk";
      String index_name = "range_hash_pk_i1_idx";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4)",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7)",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 6);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1 ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4) ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7) ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 6);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1 ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4) ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7) ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 6);

      index_name = "range_hash_pk_i2_idx";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 = 550",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 0, 0);

      statement.execute("DROP TABLE range_hash_pk");
    }
  }

  @Test
  public void testRangeIndexScanOnAscTable() throws Exception {
    // Setup
    try (Statement statement = connection.createStatement()) {
      statement.execute(
          "CREATE TABLE range_asc_pk(k int, i1 int, i2 int, v text, primary key(k asc))" +
          " SPLIT AT VALUES ((100), (200), (300), (400), (500), (600), (700), (800), (900))");
      statement.execute(
          "CREATE INDEX ON range_asc_pk(i1 asc)" +
          " SPLIT AT VALUES ((1), (2), (3), (4), (5), (6), (7), (8), (9))");
      statement.execute(
          "CREATE INDEX ON range_asc_pk(i2 desc)" +
          " SPLIT AT VALUES ((1800), (1600), (1400), (1200), (1000), (800), (600), (400), (200))");
      statement.execute(
          "INSERT INTO range_asc_pk SELECT i, i % 10, (1000 - i) * 2 + 1, 'Value ' || i::text" +
          " FROM generate_series(1, 1000) i");

      String table_name = "range_asc_pk";
      String index_name = "range_asc_pk_pkey";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k = 550",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 90 AND k < 210",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 190 AND k < 210",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 790 AND k < 910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 210",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 90 AND k < 210 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 190 AND k < 210 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 790 AND k < 910 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 910 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 210 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 90 AND k < 210 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 190 AND k < 210 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 790 AND k < 910 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 910 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 210 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 0, 0);

      index_name = "range_asc_pk_i1_idx";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4)",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7)",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 6);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1 ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4) ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7) ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 6);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1 ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4) ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7) ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 6);

      index_name = "range_asc_pk_i2_idx";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 = 550",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 0, 0);

      statement.execute("DROP TABLE range_asc_pk");
    }
  }

  @Test
  public void testRangeIndexScanOnDescTable() throws Exception {
    // Setup
    try (Statement statement = connection.createStatement()) {
      statement.execute(
          "CREATE TABLE range_desc_pk(k int, i1 int, i2 int, v text, primary key(k desc))" +
          " SPLIT AT VALUES ((900), (800), (700), (600), (500), (400), (300), (200), (100))");
      statement.execute(
          "CREATE INDEX ON range_desc_pk(i1 asc)" +
          " SPLIT AT VALUES ((1), (2), (3), (4), (5), (6), (7), (8), (9))");
      statement.execute(
          "CREATE INDEX ON range_desc_pk(i2 desc)" +
          " SPLIT AT VALUES ((1800), (1600), (1400), (1200), (1000), (800), (600), (400), (200))");
      statement.execute(
          "INSERT INTO range_desc_pk SELECT i, i % 10, (1000 - i) * 2 + 1, 'Value ' || i::text" +
          " FROM generate_series(1, 1000) i");

      String table_name = "range_desc_pk";
      String index_name = "range_desc_pk_pkey";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k = 550",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 90 AND k < 210",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 190 AND k < 210",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 790 AND k < 910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 210",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 90 AND k < 210 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 190 AND k < 210 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 790 AND k < 910 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 910 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 210 ORDER BY k ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 90 AND k < 210 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 190 AND k < 210 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 790 AND k < 910 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 910 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE k > 890 AND k < 210 ORDER BY k DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 0, 0);

      index_name = "range_desc_pk_i1_idx";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4)",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7)",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 6);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1 ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4) ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7) ORDER BY i1 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 6);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 = 1 ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 1, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (1, 4) ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 4);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i1 in (5, 2, 7) ORDER BY i1 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 6);

      index_name = "range_desc_pk_i2_idx";
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 = 550",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 1);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410",
          table_name, index_name, INDEX_SCAN_DIRECTION_ARBITRARY, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410 ORDER BY i2 ASC",
          table_name, index_name, INDEX_SCAN_DIRECTION_BACKWARD, 0, 0);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 190 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 390 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1590 AND i2 < 1910 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 3, 3);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 1910 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 2, 2);
      testRangeScanHelper(
          statement, "SELECT * from %s WHERE i2 > 1790 AND i2 < 410 ORDER BY i2 DESC",
          table_name, index_name, INDEX_SCAN_DIRECTION_FORWARD, 0, 0);

      statement.execute("DROP TABLE range_desc_pk");
    }
  }

}
