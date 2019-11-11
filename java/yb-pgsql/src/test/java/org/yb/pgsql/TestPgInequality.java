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

import java.math.BigDecimal;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.List;
import java.util.stream.Collectors;

import static org.yb.AssertionWrappers.*;

import java.io.File;
import java.sql.*;
import java.util.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgInequality extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgInequality.class);

  private void createTable(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      createTable(statement, tableName);
    }
  }

  private void createTable(Statement statement, String tableName) throws SQLException {
    // Schema:
    //
    //      Table "public.test_inequality"
    //   Column |  Type   | Collation | Nullable | Default
    //  --------+---------+-----------+----------+---------
    //   h      | text    |           | not null |
    //   r1     | integer |           | not null |
    //   r2     | text    |           | not null |
    //   val    | text    |           |          |
    //  Indexes:
    //      "test_inequality_pkey" PRIMARY KEY, lsm (h HASH, r1, r2 DESC)
    //

    String sql = "CREATE TABLE IF NOT EXISTS " + tableName +
      "(h text, r1 integer, r2 text, val text, PRIMARY KEY(h, r1 ASC, r2 DESC))";
    LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
    statement.execute(sql);
    LOG.info("Table creation finished: " + tableName);
  }

  private List<Row> setupTable(String tableName) throws SQLException {
    List<Row> allRows = new ArrayList<>();
    try (Statement statement = connection.createStatement()) {
      createTable(tableName);
      String insertTemplate = "INSERT INTO %s(h, r1, r2, val) VALUES ('%s', %d, '%s', '%s')";

      // NOTE: val = h-r1-r2

      for (int h = 0; h < 10; h++) {
        for (int r1 = 0; r1 < 5; r1++) {
          for (char r2 = 'z'; r2 >= 's'; r2--) {
            statement.execute(String.format(insertTemplate, tableName, "" + h, r1, "" + r2,
                  h + "-" + r1 + "-" + r2));
            allRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }
      }
    }

    // Sort inserted rows and return.
    Collections.sort(allRows);
    return allRows;
  }

  @Test
  public void testWhereClauseInequality() throws Exception {
    String tableName = "test_inequality";

    List<Row> allRows = setupTable(tableName);
    try (Statement statement = connection.createStatement()) {
      // Test no where clause -- select all rows.
      String query = "SELECT val FROM " + tableName;
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> sortedRs = getSortedRowList(rs);
        assertEquals(allRows, sortedRs);
      }

      int h = 1;

      // Test where clause with inequality: A1 < r1 < {B1, B2}.

      int A = 2, B1 = 4, B2 = 5;
      query = "SELECT val FROM " + tableName + " WHERE h = '" + h + "' and r1 > " + A +
        " and r1 < " + B1 + " and r1 < " + B2;
      try (ResultSet rs = statement.executeQuery(query)) {
        // Check the result.
        List<Row> someRows = new ArrayList<>();
        for (int r1 = A + 1; r1 < B1 && r1 < B2; r1++) {
          for (char r2 = 'z'; r2 >= 's'; r2--) {
            someRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }

        Collections.sort(someRows);
        assertEquals(someRows, getSortedRowList(rs));
      }

      // Test where clause with inequality: {C1, C2} < r2 < D1.

      char C1 = 's', C2 = 't', D = 'z';
      query = "SELECT val FROM " + tableName + " WHERE h = '" + h + "' and r2 > '" + C1 +
        "' and r2 > '" + C2 + "' and r2 < '" + D + "'";
      try (ResultSet rs = statement.executeQuery(query)) {
        // Check the result.
        List<Row> someRows = new ArrayList<>();
        for (int r1 = 0; r1 < 5; r1++) {
          for (char r2 = (char)(D - 1); r2 > C1 && r2 > C2; r2 = (char)(r2 - 1)) {
            someRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }

        Collections.sort(someRows);
        assertEquals(someRows, getSortedRowList(rs));
      }

      // Test where clause with inequality: A1 < r1 < {B1, B2} \and {C1, C2} < r2 < D.

      query = "SELECT val FROM " + tableName + " WHERE h = '" + h + "' and r1 > " + A +
        " and r1 < " + B1 + " and r1 < " + B2 + " and r2 > '" + C1 + "' and r2 > '" + C2 +
        "' and r2 < '" + D + "'";
      try (ResultSet rs = statement.executeQuery(query)) {
        // Check the result.
        List<Row> someRows = new ArrayList<>();
        for (int r1 = A + 1; r1 < B1 && r1 < B2; r1++) {
          for (char r2 = (char)(D - 1); r2 > C1 && r2 > C2; r2 = (char)(r2 - 1)) {
            someRows.add(new Row(h + "-" + r1 + "-" + r2));
          }
        }

        Collections.sort(someRows);
        assertEquals(someRows, getSortedRowList(rs));
      }
    }
  }

  @Test
  public void testInequalityPredicatePushdownOneRangeColumnPerformance() throws Exception {
    String tableName = "test_inequality_predicate_pushdown_performance";

    // Schema:
    //
    // Table "public.test_inequality_predicate_pushdown_performance"
    //  Column |  Type   | Collation | Nullable | Default
    //  --------+---------+-----------+----------+---------
    //  h      | integer |           | not null |
    //  ri     | integer |           | not null |
    //  vs     | text    |           |          |
    //  Indexes:
    //  "test_inequality_predicate_pushdown_performance_pkey" PRIMARY KEY, lsm (h HASH, ri)

    // Create table.
    try (Statement statement = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(h int, ri int, vs text, " +
                                 "PRIMARY KEY (h, ri ASC))", tableName);
      LOG.info("Execute: " + sql);
      statement.execute(sql);
      LOG.info("Created: " + tableName);
    }

    // Insert rows.
    int hMax = 2, riMax = 5000;
    try (Statement statement = connection.createStatement()) {
      for (int h = 0; h < hMax; h++) {
        for (int ri = 0; ri < riMax; ri++) {
          String vsTmp = String.format("value_%d_%d", h, ri);

          String stmt = String.format("INSERT INTO %s VALUES (%d, %d, '%s')",
              tableName, h, ri, vsTmp);
          statement.execute(stmt);
        }
      }
    }

    // Choose the maximum runtimes of the SELECT statements.

    int fullScanReleaseRuntime = 400;
    int fullScanDebugRuntime = 500;
    int fullScanDebugRuntimeWithMargin = 10 * fullScanDebugRuntime;
    int fullScanMaxRuntimeMillis = getPerfMaxRuntime(fullScanReleaseRuntime,
        fullScanDebugRuntime, fullScanDebugRuntimeWithMargin, fullScanDebugRuntimeWithMargin,
        fullScanDebugRuntimeWithMargin);

    int intervalScanReleaseRuntime = 200;
    int intervalScanDebugRuntime = 250;
    int intervalScanDebugRuntimeWithMargin = 10 * intervalScanDebugRuntime;
    int intervalScanMaxRuntimeMillis = getPerfMaxRuntime(intervalScanReleaseRuntime,
        intervalScanDebugRuntime, intervalScanDebugRuntimeWithMargin,
        intervalScanDebugRuntimeWithMargin, intervalScanDebugRuntimeWithMargin);

    // Full scan.

    final int queryRunCount = 5;
    int h = hMax / 2;
    String query = String.format("SELECT * FROM %s WHERE h = %d", tableName, h);
    timeQueryWithRowCount(query, riMax /* expectedRowCount */, fullScanMaxRuntimeMillis,
                          queryRunCount);

    // Interval scans that should return 100 rows each.

    int riDelta = riMax / 50;
    assertEquals(riDelta, 100);

    int A, B;

    // A < ri.
    A = riMax - riDelta - 1;
    query = String.format("SELECT * FROM %s WHERE h = %d AND ri > %d", tableName, h, A);
    timeQueryWithRowCount(query, riDelta /* expectedRowCount */, intervalScanMaxRuntimeMillis,
                          queryRunCount);

    // ri < B.
    B = riDelta;
    query = String.format("SELECT * FROM %s WHERE h = %d AND ri < %d", tableName, h, B);
    timeQueryWithRowCount(query, riDelta  /* expectedRowCount */, intervalScanMaxRuntimeMillis,
                          queryRunCount);

    // A < ri < B.
    A = riMax / 2;
    B = A + riDelta + 1;
    query = String.format("SELECT * FROM %s WHERE h = %d AND ri > %d AND ri < %d", tableName, h,
        A, B);
    timeQueryWithRowCount(query, riDelta /* expectedRowCount */, intervalScanMaxRuntimeMillis,
                          queryRunCount);
  }

  @Test
  public void testInequalityPredicatePushdownTwoRangeColumnsPerformance() throws Exception {
    String tableName = "test_inequality_predicate_pushdown_performance";

    // Schema:
    //
    // Table "public.test_inequality_predicate_pushdown_performance"
    //  Column |  Type   | Collation | Nullable | Default
    //  --------+---------+-----------+----------+---------
    //  h      | integer |           | not null |
    //  ri     | integer |           | not null |
    //  rs     | text    |           | not null |
    //  vs     | text    |           |          |
    //  Indexes:
    //  "test_inequality_predicate_pushdown_performance_pkey" PRIMARY KEY, lsm (h HASH, ri, rs DESC)

    // Create table.
    try (Statement statement = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(h int, ri int, rs text, vs text, " +
                                 "PRIMARY KEY (h, ri ASC, rs DESC))", tableName);
      LOG.info("Execute: " + sql);
      statement.execute(sql);
      LOG.info("Created: " + tableName);
    }

    // Numeric and lexicographic order of [rsBase, rsBase + rsMax) should be the same.
    int rsBase = 100;

    // Insert rows.
    int hMax = 2, riMax = 50, rsMax = 100;
    try (Statement statement = connection.createStatement()) {
      for (int h = 0; h < hMax; h++) {
        for (int ri = 0; ri < riMax; ri++) {
          for (int rs = 0; rs < rsMax; rs++) {
            String rsTmp = String.format("range_%d", rsBase + rs);
            String vsTmp = String.format("value_%d_%d_%s", h, ri, rsTmp);

            String stmt = String.format("INSERT INTO %s VALUES (%d, %d, '%s', '%s')",
                tableName, h, ri, rsTmp, vsTmp);
            statement.execute(stmt);
          }
        }
      }
    }

    // Choose the maximum runtimes of the SELECT statements.

    int fullScanReleaseRuntime = 500;
    int fullScanDebugRuntime = 600;
    int fullScanDebugRuntimeWithMargin = 10 * fullScanDebugRuntime;
    int fullScanMaxRuntimeMillis = getPerfMaxRuntime(fullScanReleaseRuntime,
        fullScanDebugRuntime, fullScanDebugRuntimeWithMargin, fullScanDebugRuntimeWithMargin,
        fullScanDebugRuntimeWithMargin);

    int intervalScanReleaseRuntime = 350;
    int intervalScanDebugRuntime = 400;
    int intervalScanDebugRuntimeWithMargin = 10 * intervalScanDebugRuntime;
    int intervalScanMaxRuntimeMillis = getPerfMaxRuntime(intervalScanReleaseRuntime,
        intervalScanDebugRuntime, intervalScanDebugRuntimeWithMargin,
        intervalScanDebugRuntimeWithMargin, intervalScanDebugRuntimeWithMargin);

    // Full scan.

    final int queryRunCount = 5;
    int h = hMax / 2;

    String query = String.format("SELECT * FROM %s WHERE h = %d", tableName, h);
    timeQueryWithRowCount(query, riMax * rsMax /* expectedRowCount */, fullScanMaxRuntimeMillis,
                          queryRunCount);

    // Interval scans that should return 100 rows each.

    int riDelta, rsDelta, A, B, C, D, expectedRowCount;
    String CTmp, DTmp;

    // Inequality only -----------------------------------------------------------------------------

    // A < ri < B.
    riDelta = 1;
    A = riMax / 2;
    B = A + riDelta + 1;
    query = String.format("SELECT * FROM %s WHERE h = %d AND ri > %d AND ri < %d", tableName, h,
        A, B);
    expectedRowCount = riDelta * rsMax;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // C < rs < D.
    rsDelta = 2;
    C = rsMax / 2;
    D = C + rsDelta + 1;
    CTmp = String.format("range_%d", rsBase + C);
    DTmp = String.format("range_%d", rsBase + D);
    query = String.format("SELECT * FROM %s WHERE h = %d AND rs > '%s' AND rs < '%s'", tableName,
        h, CTmp, DTmp);
    expectedRowCount = riMax * rsDelta;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // A < ri < B    AND    C < rs < D.
    riDelta = 10;
    rsDelta = 10;
    A = riMax / 2;
    B = A + riDelta + 1;
    C = rsMax / 2;
    D = C + rsDelta + 1;
    CTmp = String.format("range_%d", rsBase + C);
    DTmp = String.format("range_%d", rsBase + D);
    query = String.format(
        "SELECT * FROM %s WHERE h = %d AND ri > %d AND ri < %d AND rs > '%s' AND rs < '%s'",
        tableName, h, A, B, CTmp, DTmp);
    expectedRowCount = riDelta * rsDelta;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // In only -------------------------------------------------------------------------------------

    // IN scans that should return 100 rows each.

    String AtoB, CtoD;

    // ri IN {A ... B}.
    riDelta = 1;
    A = riMax / 2;
    B = A + riDelta + 1;
    AtoB = String.format("%d", A + 1);
    for (int i = A + 2; i < B; i++) {
      AtoB += String.format(",%d", i);
    }
    query = String.format("SELECT * FROM %s WHERE h = %d AND ri IN (%s)", tableName, h, AtoB);
    expectedRowCount = riDelta * rsMax;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // rs IN {C ... D}.
    rsDelta = 2;
    C = rsMax / 2;
    D = C + rsDelta + 1;
    CtoD = String.format("'range_%d'", rsBase + C + 1);
    for (int i = C + 2; i < D; i++) {
      CtoD += String.format(",'range_%d'", rsBase + i);
    }
    query = String.format("SELECT * FROM %s WHERE h = %d AND rs IN (%s)", tableName, h, CtoD);
    expectedRowCount = riMax * rsDelta;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // ri IN {A ... B}    AND    rs IN {C ... D}.
    riDelta = 10;
    rsDelta = 10;
    A = riMax / 2;
    B = A + riDelta + 1;
    AtoB = String.format("%d", A + 1);
    for (int i = A + 2; i < B; i++) {
      AtoB += String.format(",%d", i);
    }
    C = rsMax / 2;
    D = C + rsDelta + 1;
    CtoD = String.format("'range_%d'", rsBase + C + 1);
    for (int i = C + 2; i < D; i++) {
      CtoD += String.format(",'range_%d'", rsBase + i);
    }
    query = String.format(
        "SELECT * FROM %s WHERE h = %d AND ri IN (%s) AND rs IN (%s)", tableName, h, AtoB, CtoD);
    expectedRowCount = riDelta * rsDelta;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // Inequality AND IN on different columns ------------------------------------------------------

    // Scans that should return 100 rows each.

    // A < ri < B    AND    rs IN {C ... D}.
    riDelta = 10;
    rsDelta = 10;
    A = riMax / 2;
    B = A + riDelta + 1;
    C = rsMax / 2;
    D = C + rsDelta + 1;
    CtoD = String.format("'range_%d'", rsBase + C + 1);
    for (int i = C + 2; i < D; i++) {
      CtoD += String.format(",'range_%d'", rsBase + i);
    }
    query = String.format(
        "SELECT * FROM %s WHERE h = %d AND ri > %d AND ri < %d AND rs IN (%s)", tableName, h, A,
        B, CtoD);
    expectedRowCount = riDelta * rsDelta;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // ri IN {A ... B}    AND    C < rs < D.
    riDelta = 10;
    rsDelta = 10;
    A = riMax / 2;
    B = A + riDelta + 1;
    AtoB = String.format("%d", A + 1);
    for (int i = A + 2; i < B; i++) {
      AtoB += String.format(",%d", i);
    }
    C = rsMax / 2;
    D = C + rsDelta + 1;
    CTmp = String.format("range_%d", rsBase + C);
    DTmp = String.format("range_%d", rsBase + D);
    query = String.format(
        "SELECT * FROM %s WHERE h = %d AND ri IN (%s) AND rs > '%s' AND rs < '%s'", tableName, h,
        AtoB, CTmp, DTmp);
    expectedRowCount = riDelta * rsDelta;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // Inequality AND IN on same column ------------------------------------------------------------

    // Scans that should return 100 rows each.

    // A < ri < B    AND    ri IN {A ... B}.
    riDelta = 1;
    A = riMax / 2;
    B = A + riDelta + 1;
    AtoB = String.format("%d", A + 1);
    for (int i = A + 2; i < B; i++) {
      AtoB += String.format(",%d", i);
    }
    query = String.format("SELECT * FROM %s WHERE h = %d AND ri > %d AND ri < %d AND ri IN (%s)",
        tableName, h, A, B, AtoB);
    expectedRowCount = riDelta * rsMax;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);

    // C < rs < D    AND    rs IN {C ... D}.
    rsDelta = 2;
    C = rsMax / 2;
    D = C + rsDelta + 1;
    CTmp = String.format("range_%d", rsBase + C);
    DTmp = String.format("range_%d", rsBase + D);
    CtoD = String.format("'range_%d'", rsBase + C + 1);
    for (int i = C + 2; i < D; i++) {
      CtoD += String.format(",'range_%d'", rsBase + i);
    }
    query = String.format(
        "SELECT * FROM %s WHERE h = %d AND rs IN (%s) AND rs > '%s' AND rs < '%s'", tableName,
        h, CtoD, CTmp, DTmp);
    expectedRowCount = riMax * rsDelta;
    timeQueryWithRowCount(query, expectedRowCount, intervalScanMaxRuntimeMillis,
                          queryRunCount);
    assertEquals(expectedRowCount, 100);
  }
}
