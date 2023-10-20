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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Statement;
import java.util.List;
import java.util.Map;
import java.util.Arrays;

import com.google.common.collect.Range;
import org.yb.util.json.Checkers;
import java.lang.Math;

import static org.yb.pgsql.ExplainAnalyzeUtils.checkReadRequests;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_SEQ_SCAN;

@RunWith(YBTestRunnerNonTsanOnly.class)
public class TestPgColumnReadEfficiency extends BasePgSQLTest {
  private static final int N_ROWS = 10000;
  private static final Logger LOG = LoggerFactory.getLogger(TestPgColumnReadEfficiency.class);

  final int SELECT_A = 1 << 0;
  final int SELECT_B = 1 << 1;
  final int SELECT_C = 1 << 2;

  private int getColsBmp(String clause) {
    if (clause == "*")
      return SELECT_A | SELECT_B | SELECT_C;
    int res = 0;
    if (clause.contains("a")) {
      res |= SELECT_A;
    }
    if (clause.contains("b")) {
      res |= SELECT_B;
    }
    if (clause.contains("c")) {
      res |= SELECT_C;
    }

    return res;
  }

  private int countCols(int bmp) {
    return ((bmp & SELECT_A) != 0 ? 1 : 0) +
           ((bmp & SELECT_B) != 0 ? 1 : 0) +
           ((bmp & SELECT_C) != 0 ? 1 : 0);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_enable_packed_row", "false");
    return flagMap;
  }

  @Test
  public void testScanRowSize() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t(a INT, b INT, c INT)");
      stmt.execute("CREATE INDEX ON t (a ASC, b ASC, c ASC)");
      stmt.execute(String.format(
          "INSERT INTO t SELECT g, g / 10, g / 100 FROM generate_series(1, %d) g", N_ROWS));
      stmt.execute("SET yb_fetch_row_limit = 0");
      stmt.execute("SET yb_fetch_size_limit = '1kB'");

      final List<String> selects = Arrays.asList("", "a", "b", "c", "a, b", "a, c", "b, c",
                                                 "a, b, c", "*");
      final List<String> wheres = Arrays.asList("", "WHERE a >= 0", "WHERE b >= 0", "WHERE c >= 0",
                                                "WHERE a >= 0 AND b >= 0",
                                                "WHERE a >= 0 AND c >= 0",
                                                "WHERE b >= 0 AND c >= 0",
                                                "WHERE a >= 0 AND b >= 0 AND c >= 0");
      int estimatedRpcsPerCol = 50;
      for (String select : selects) {
        int selectedColsBitmap = getColsBmp(select);
        int nSelectedCols = countCols(selectedColsBitmap);
        for (String where : wheres) {
          int conditionColsBitmap = getColsBmp(where);
          int nConditionCols = countCols(conditionColsBitmap);

          int nTotalCols = countCols(conditionColsBitmap | selectedColsBitmap);

          int iosEstimate = estimatedRpcsPerCol * nTotalCols;
          int ssEstimate = estimatedRpcsPerCol * nSelectedCols / 3;
          final int isEstimate = estimatedRpcsPerCol * 13;

          if (nSelectedCols == 0 && nConditionCols == 0) {
            iosEstimate = estimatedRpcsPerCol * 10;
            ssEstimate = estimatedRpcsPerCol * 6;
          } else if (nConditionCols == 0) {
            ssEstimate = estimatedRpcsPerCol * nSelectedCols;
          } else if (nSelectedCols == 0) {
            // TODO(#18874): selecting no targets should not be bloated
            ssEstimate = estimatedRpcsPerCol * 2;
          }

          testSpecificScanRowSize(stmt, NODE_SEQ_SCAN, select, where, ssEstimate, N_ROWS);
          testSpecificScanRowSize(stmt, NODE_INDEX_ONLY_SCAN, select, where,
                                  iosEstimate, N_ROWS);
          if (nConditionCols > 0)
            testSpecificScanRowSize(stmt, NODE_INDEX_SCAN, select, where, isEstimate, N_ROWS);
        }
      }
    }
  }


  @Test
  public void testScanRowSizeHashCode() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t(a INT, b INT, c INT)");
      stmt.execute(String.format(
          "INSERT INTO t SELECT g, g / 10, g / 100 FROM generate_series(1, %d) g",
          N_ROWS));
      stmt.execute("SET yb_fetch_row_limit = 0");
      stmt.execute("SET yb_fetch_size_limit = '1kB'");

      final List<String> selects = Arrays.asList("", "a", "b", "a, b");

      int estimatedRpcsPerCol = 50;
      String hashcodeCond = "WHERE yb_hash_code(a) < 12345";
      int N_ROWS_MATCHING_COND = 1928;

      // Without an index, we need to fetch every row.
      for (String select : selects) {
        int selectedColsBitmap = getColsBmp(select);
        int nSelectedCols = countCols(selectedColsBitmap);

        int nTotalColsWithCond = countCols(selectedColsBitmap | SELECT_A);

        int ssEstimateWithCond = estimatedRpcsPerCol * nTotalColsWithCond;
        int ssEstimateWithoutCond = estimatedRpcsPerCol * nSelectedCols;
        if (nSelectedCols == 0) {
          ssEstimateWithoutCond = estimatedRpcsPerCol * 6;
        }
        testSpecificScanRowSize(stmt, NODE_SEQ_SCAN, select, hashcodeCond,
                                ssEstimateWithCond, N_ROWS_MATCHING_COND);
        testSpecificScanRowSize(stmt, NODE_SEQ_SCAN, select, "",
                                ssEstimateWithoutCond, N_ROWS);
      }

      stmt.execute("CREATE INDEX on t(a, b)");

      // With an index, we only need to fetch the requested rows.
      // We still need to fetch the condition column as a target for recheck.
      for (String select : selects) {
        int selectedColsBitmap = getColsBmp(select);
        int nSelectedCols = countCols(selectedColsBitmap);

        int nTotalColsWithCond = countCols(selectedColsBitmap | SELECT_A);

        int iosEstimateWithCond = (int) Math.ceil((double) estimatedRpcsPerCol * nTotalColsWithCond
                                   * N_ROWS_MATCHING_COND / N_ROWS);
        int iosEstimateWithoutCond = estimatedRpcsPerCol * nSelectedCols;
        if (nSelectedCols == 0) {
          iosEstimateWithoutCond = estimatedRpcsPerCol * 6;
        }
        testSpecificScanRowSize(stmt, NODE_INDEX_ONLY_SCAN, select, hashcodeCond,
                                iosEstimateWithCond, N_ROWS_MATCHING_COND);
        testSpecificScanRowSize(stmt, NODE_SEQ_SCAN, select, "",
                                iosEstimateWithoutCond, N_ROWS);
      }
    }
  }

  private void testSpecificScanRowSize(Statement stmt, String scanType, String selectedColumns,
        String conditions, long expected, int expectedRows) throws Exception {
    String hint;
    switch(scanType) {
      case NODE_SEQ_SCAN:
        hint = "SeqScan(t)";
        break;
      case NODE_INDEX_ONLY_SCAN:
        hint = "IndexOnlyScan(t)";
        break;
      case NODE_INDEX_SCAN:
        hint = "IndexScan(t)";
        break;
      default:
        throw new Exception(String.format("Unsupported scan type %s", scanType));
    }

    String query = String.format("/*+ %s */ SELECT %s FROM t %s",
                                 hint, selectedColumns, conditions);
    checkReadRequests(stmt, query, scanType,
        Checkers.range(Long.class, Range.closed(expected * 8 / 10, expected * 12 / 10)),
        expectedRows);
  }
}
