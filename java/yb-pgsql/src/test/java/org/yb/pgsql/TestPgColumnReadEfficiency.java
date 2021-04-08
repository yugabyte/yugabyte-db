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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertGreaterThan;

import com.google.common.collect.Streams;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import java.util.stream.Stream;

@RunWith(YBTestRunnerNonTsanOnly.class)
public class TestPgColumnReadEfficiency extends BasePgSQLTest {

  @Test
  public void testScans() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t(" +
        "h INT, r INT, v1 INT, v2 INT, v3 INT, t1 text, t2 text, t3 text, t4 text, t5 text, " +
        "PRIMARY KEY(h, r ASC))");
      stmt.execute("CREATE INDEX ON t(v1 ASC, v2, v3, t1, t2, t3, t4, t5)");

      String longTextPrefix = Streams.concat(IntStream.range(0, 100).mapToObj(i -> "long"),
                                             Stream.of("_prefix")).collect(Collectors.joining());
      stmt.execute(String.format(
        "INSERT INTO t SELECT 1, s, 1, s, s, '%1$s_1', '%1$s_2', '%1$s_3', '%1$s_4', '%1$s_5'" +
          "FROM generate_series(1, 30000) AS s",
        longTextPrefix));
      stmt.execute("SELECT COUNT(*) FROM t WHERE h = 1");
      // The following functions compare median execution time of 'fat' and 'thin' queries.
      // Where 'fat' queries contain multiple heavy text columns and 'thin' contain single
      // lightweight int column.
      // Median execution time in milliseconds on local PC for 'fat' / 'thin' queries:
      //                                  debug          release
      // - checkIndexScan            565.1 / 252.9    414.5 / 201.8
      // - checkForeignScan          532.8 / 249.5    395.8 / 187.7
      // - checkIndexOnlyScan        928.8 / 843.8    524.1 / 468.7
      // - checkSecondaryIndexScan  1034.6 / 879.1    731.6 / 499.4
      checkIndexScan(stmt);
      checkForeignScan(stmt);
      checkIndexOnlyScan(stmt);
      checkSecondaryIndexScan(stmt);
    }
  }

  private double getExecutionTime(
      Statement stmt, String query, String expectedScanType) throws SQLException {
    String resultJson = getRowList(
        stmt, "EXPLAIN (ANALYZE true, FORMAT json) " + query).get(0).get(0).toString();
    JsonObject obj = new JsonParser().parse(resultJson).getAsJsonArray().get(0).getAsJsonObject();
    assertEquals(expectedScanType,
                 obj.getAsJsonObject("Plan").get("Node Type").getAsString());
    return obj.get("Execution Time").getAsDouble();
  }

  /** Run 'fat' and 'thin' queries multiple times and compare median execution time. */
  private void checkQuery(Statement stmt,
                          String queryThinColumns,
                          String queryFatColumns,
                          String expectedScanType) throws SQLException {
    List<Double> fatTime = new ArrayList<>();
    List<Double> thinTime = new ArrayList<>();
    int attempts = 30;
    for (int i = 0; i < attempts; ++i) {
      thinTime.add(getExecutionTime(stmt, queryThinColumns, expectedScanType));
      fatTime.add(getExecutionTime(stmt, queryFatColumns, expectedScanType));
    }
    Comparator<Double> comparator = Comparator.naturalOrder();
    Collections.sort(thinTime, comparator);
    Collections.sort(fatTime, comparator);
    int medianIndex = attempts / 2;
    double fatTimeMedian = fatTime.get(medianIndex);
    double thinTimeMedian = thinTime.get(medianIndex);
    assertGreaterThan(
        String.format(
            "Expected median time %f of '%s' to be greater than median time %f of '%s'",
            fatTimeMedian, queryFatColumns, thinTimeMedian, queryThinColumns),
        fatTimeMedian, thinTimeMedian);
  }

  private void checkIndexScan(Statement stmt) throws SQLException {
    String pattern = "SELECT %s FROM t WHERE h = 1";
    checkQuery(
        stmt, String.format(pattern, "v1"), String.format(pattern, "*"), "Index Scan");
  }

  private void checkForeignScan(Statement stmt) throws SQLException {
    String pattern = "SELECT %s FROM t";
    checkQuery(
        stmt, String.format(pattern, "v1"), String.format(pattern, "*"), "Seq Scan");
  }

  private void checkIndexOnlyScan(Statement stmt) throws SQLException {
    String pattern = "SELECT %s FROM t WHERE v1 = 1";
    checkQuery(stmt,
               String.format(pattern, "v2"),
               String.format(pattern, "v1, v2, v3, t1, t2, t3, t4, t5"),
               "Index Only Scan");
  }

  private void checkSecondaryIndexScan(Statement stmt) throws SQLException {
    String pattern = "SELECT %s FROM t WHERE v1 = 1";
    checkQuery(stmt, String.format(pattern, "h"), String.format(pattern, "*"), "Index Scan");
  }
}
