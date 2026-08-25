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

import java.sql.Statement;
import java.util.Arrays;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.util.RequiresLinux;

/**
 * Test pushdown behaviour of different expression from PG to YB layer.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestPgPushdown extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgPushdown.class);

  @Override
  protected Integer getYsqlRequestLimit() {
    // This should be less than number of operations in some tests
    return 7;
  }

  // Disable auto analyze to prevent query plan change.
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_enable_auto_analyze", "false");
    return flags;
  }

  @Test
  public void inequality_oneRangeColumn() throws Exception {
    String tableName = "inequality_predicate_pushdown";

    // Schema:
    //
    // Table "public.inequality_predicate_pushdown"
    //  Column |  Type   | Collation | Nullable | Default
    //  -------+---------+-----------+----------+---------
    //  h      | integer |           | not null |
    //  ri     | integer |           | not null |
    //  vs     | text    |           |          |
    //  Indexes:
    //  "inequality_predicate_pushdown_pkey" PRIMARY KEY, lsm (h HASH, ri)

    // Create table.
    try (Statement stmt = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(h int, ri int, vs text, " +
          "PRIMARY KEY (h, ri ASC))", tableName);
      LOG.info("Execute: " + sql);
      stmt.execute(sql);
      LOG.info("Created: " + tableName);

      // Insert rows.
      int hMax = 2, riMax = 5000;
      for (int h = 0; h < hMax; h++) {
        for (int ri = 0; ri < riMax; ri++) {
          String vsTmp = String.format("value_%d_%d", h, ri);

          stmt.execute(String.format("INSERT INTO %s VALUES (%d, %d, '%s')",
              tableName, h, ri, vsTmp));
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
      assertQueryRuntimeWithRowCount(stmt, query, riMax /* expectedRowCount */, queryRunCount,
          fullScanMaxRuntimeMillis * queryRunCount);

      // Interval scans that should return 100 rows each.

      int riDelta = riMax / 50;
      assertEquals(riDelta, 100);

      int A, B;

      // A < ri.
      A = riMax - riDelta - 1;
      query = String.format("SELECT * FROM %s WHERE h = %d AND ri > %d", tableName, h, A);
      assertQueryRuntimeWithRowCount(stmt, query, riDelta /* expectedRowCount */,
          queryRunCount, intervalScanMaxRuntimeMillis * queryRunCount);

      // ri < B.
      B = riDelta;
      query = String.format("SELECT * FROM %s WHERE h = %d AND ri < %d", tableName, h, B);
      assertQueryRuntimeWithRowCount(stmt, query, riDelta /* expectedRowCount */,
          queryRunCount, intervalScanMaxRuntimeMillis * queryRunCount);

      // A < ri < B.
      A = riMax / 2;
      B = A + riDelta + 1;
      query = String.format("SELECT * FROM %s WHERE h = %d AND ri > %d AND ri < %d", tableName, h,
          A, B);
      assertQueryRuntimeWithRowCount(stmt, query, riDelta /* expectedRowCount */,
          queryRunCount, intervalScanMaxRuntimeMillis * queryRunCount);
    }
  }

  @Test
  public void inequalityAndIn_twoRangeColumns() throws Exception {
    String tableName = "inequality_predicate_pushdown";

    // Schema:
    //
    // Table "public.inequality_predicate_pushdown"
    //  Column |  Type   | Collation | Nullable | Default
    //  -------+---------+-----------+----------+---------
    //  h      | integer |           | not null |
    //  ri     | integer |           | not null |
    //  rs     | text    |           | not null |
    //  vs     | text    |           |          |
    //  Indexes:
    //  "inequality_predicate_pushdown_pkey" PRIMARY KEY, lsm (h HASH, ri, rs DESC)

    // Create table.
    try (Statement statement = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(h int, ri int, rs text, vs text, " +
          "PRIMARY KEY (h, ri ASC, rs DESC))", tableName);
      LOG.info("Execute: " + sql);
      statement.execute(sql);
      LOG.info("Created: " + tableName);

      // Numeric and lexicographic order of [rsBase, rsBase + rsMax) should be the same.
      int rsBase = 100;

      // Insert rows.
      int hMax = 2, riMax = 50, rsMax = 100;
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
      assertQueryRuntimeWithRowCount(statement, query, riMax * rsMax /* expectedRowCount */,
          queryRunCount, fullScanMaxRuntimeMillis * queryRunCount);

      // Interval scans that should return 100 rows each.

      int riDelta, rsDelta, A, B, C, D, expectedRowCount;
      String CTmp, DTmp;

      // Inequality only ---------------------------------------------------------------------------

      // A < ri < B.
      riDelta = 1;
      A = riMax / 2;
      B = A + riDelta + 1;
      query = String.format("SELECT * FROM %s WHERE h = %d AND ri > %d AND ri < %d", tableName, h,
          A, B);
      expectedRowCount = riDelta * rsMax;
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // In only -----------------------------------------------------------------------------------

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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // Inequality AND IN on different columns ----------------------------------------------------

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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // Inequality AND IN on same column ----------------------------------------------------------

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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
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
      assertQueryRuntimeWithRowCount(statement, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);
    }
  }

  @Test
  public void inequality_twoRangeColumns() throws Exception {
    String tableName = "inequality_predicate_pushdown";

    // Schema:
    //
    // Table "public.inequality_predicate_pushdown"
    //  Column |  Type   | Collation | Nullable | Default
    //  -------+---------+-----------+----------+---------
    //  r1     | integer |           | not null |
    //  r2     | integer |           | not null |
    //  vs     | text    |           |          |
    //  Indexes:
    //  "inequality_predicate_pushdown_pkey" PRIMARY KEY, lsm (r1, r2)

    // Create table.
    try (Statement stmt = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(r1 int, r2 int, vs text, " +
          "PRIMARY KEY (r1 ASC, r2 ASC))", tableName);
      LOG.info("Execute: " + sql);
      stmt.execute(sql);
      LOG.info("Created: " + tableName);

      // Insert rows.
      int r1Max = 2, r2Max = 5000;
      for (int r1 = 0; r1 < r1Max; r1++) {
        for (int r2 = 0; r2 < r2Max; r2++) {
          String vsTmp = String.format("value_%d_%d", r1, r2);

          stmt.execute(String.format("INSERT INTO %s VALUES (%d, %d, '%s')",
              tableName, r1, r2, vsTmp));
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

      // Scan range column.

      final int queryRunCount = 5;
      int r1 = r1Max / 2;
      String query = String.format("SELECT * FROM %s WHERE r1 = %d", tableName, r1);
      assertQueryRuntimeWithRowCount(stmt, query, r2Max /* expectedRowCount */, queryRunCount,
          fullScanMaxRuntimeMillis * queryRunCount);

      // Interval scans that should return 100 rows each.

      int r2Delta = r2Max / 50;
      assertEquals(r2Delta, 100);

      int A, B;

      // A < r2.
      A = r2Max - r2Delta - 1;
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND r2 > %d", tableName, r1, A);
      assertQueryRuntimeWithRowCount(stmt, query, r2Delta /* expectedRowCount */, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);

      // r2 < B.
      B = r2Delta;
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND r2 < %d", tableName, r1, B);
      assertQueryRuntimeWithRowCount(stmt, query, r2Delta /* expectedRowCount */, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);

      // A < r2 < B.
      A = r2Max / 2;
      B = A + r2Delta + 1;
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND r2 > %d AND r2 < %d", tableName, r1,
          A, B);
      assertQueryRuntimeWithRowCount(stmt, query, r2Delta /* expectedRowCount */, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
    }
  }

  @Test
  public void inequalityAndIn_allRangeColumns() throws Exception {
    String tableName = "inequality_predicate_pushdown";

    // Schema:
    //
    // Table "public.inequality_predicate_pushdown"
    //  Column |  Type   | Collation | Nullable | Default
    //  -------+---------+-----------+----------+---------
    //  r1     | integer |           | not null |
    //  r2     | integer |           | not null |
    //  rs     | text    |           | not null |
    //  vs     | text    |           |          |
    //  Indexes:
    //  "inequality_predicate_pushdown_pkey" PRIMARY KEY, lsm (r1, r2, rs DESC)

    // Create table.
    try (Statement stmt = connection.createStatement()) {
      String sql = String.format("CREATE TABLE %s(r1 int, r2 int, rs text, vs text, " +
          "PRIMARY KEY (r1, r2 ASC, rs DESC))", tableName);
      LOG.info("Execute: " + sql);
      stmt.execute(sql);
      LOG.info("Created: " + tableName);

      // Numeric and lexicographic order of [rsBase, rsBase + rsMax) should be the same.
      int rsBase = 100;

      // Insert rows.
      int r1Max = 2, r2Max = 50, rsMax = 100;
      for (int r1 = 0; r1 < r1Max; r1++) {
        for (int r2 = 0; r2 < r2Max; r2++) {
          for (int rs = 0; rs < rsMax; rs++) {
            String rsTmp = String.format("range_%d", rsBase + rs);
            String vsTmp = String.format("value_%d_%d_%s", r1, r2, rsTmp);

            stmt.execute(String.format("INSERT INTO %s VALUES (%d, %d, '%s', '%s')",
                tableName, r1, r2, rsTmp, vsTmp));
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

      // Scan range column.

      final int queryRunCount = 5;
      int r1 = r1Max / 2;

      String query = String.format("SELECT * FROM %s WHERE r1 = %d", tableName, r1);
      assertQueryRuntimeWithRowCount(stmt, query, r2Max * rsMax /* expectedRowCount */,
          queryRunCount,
          fullScanMaxRuntimeMillis * queryRunCount);

      // Interval scans that should return 100 rows each.

      int r2Delta, rsDelta, A, B, C, D, expectedRowCount;
      String CTmp, DTmp;

      // Inequality only ---------------------------------------------------------------------------

      // A < r2 < B.
      r2Delta = 1;
      A = r2Max / 2;
      B = A + r2Delta + 1;
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND r2 > %d AND r2 < %d", tableName, r1,
          A, B);
      expectedRowCount = r2Delta * rsMax;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // C < rs < D.
      rsDelta = 2;
      C = rsMax / 2;
      D = C + rsDelta + 1;
      CTmp = String.format("range_%d", rsBase + C);
      DTmp = String.format("range_%d", rsBase + D);
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND rs > '%s' AND rs < '%s'", tableName,
          r1, CTmp, DTmp);
      expectedRowCount = r2Max * rsDelta;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // A < ri < B    AND    C < rs < D.
      r2Delta = 10;
      rsDelta = 10;
      A = r2Max / 2;
      B = A + r2Delta + 1;
      C = rsMax / 2;
      D = C + rsDelta + 1;
      CTmp = String.format("range_%d", rsBase + C);
      DTmp = String.format("range_%d", rsBase + D);
      query = String.format(
          "SELECT * FROM %s WHERE r1 = %d AND r2 > %d AND r2 < %d AND rs > '%s' AND rs < '%s'",
          tableName, r1, A, B, CTmp, DTmp);
      expectedRowCount = r2Delta * rsDelta;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // In only -----------------------------------------------------------------------------------

      // IN scans that should return 100 rows each.

      String AtoB, CtoD;

      // r2 IN {A ... B}.
      r2Delta = 1;
      A = r2Max / 2;
      B = A + r2Delta + 1;
      AtoB = String.format("%d", A + 1);
      for (int i = A + 2; i < B; i++) {
        AtoB += String.format(",%d", i);
      }
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND r2 IN (%s)", tableName, r1, AtoB);
      expectedRowCount = r2Delta * rsMax;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // rs IN {C ... D}.
      rsDelta = 2;
      C = rsMax / 2;
      D = C + rsDelta + 1;
      CtoD = String.format("'range_%d'", rsBase + C + 1);
      for (int i = C + 2; i < D; i++) {
        CtoD += String.format(",'range_%d'", rsBase + i);
      }
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND rs IN (%s)", tableName, r1, CtoD);
      expectedRowCount = r2Max * rsDelta;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // r2 IN {A ... B}    AND    rs IN {C ... D}.
      r2Delta = 10;
      rsDelta = 10;
      A = r2Max / 2;
      B = A + r2Delta + 1;
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
          "SELECT * FROM %s WHERE r1 = %d AND r2 IN (%s) AND rs IN (%s)", tableName, r1, AtoB,
          CtoD);
      expectedRowCount = r2Delta * rsDelta;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // Inequality AND IN on different columns ----------------------------------------------------

      // Scans that should return 100 rows each.

      // A < r2 < B    AND    rs IN {C ... D}.
      r2Delta = 10;
      rsDelta = 10;
      A = r2Max / 2;
      B = A + r2Delta + 1;
      C = rsMax / 2;
      D = C + rsDelta + 1;
      CtoD = String.format("'range_%d'", rsBase + C + 1);
      for (int i = C + 2; i < D; i++) {
        CtoD += String.format(",'range_%d'", rsBase + i);
      }
      query = String.format(
          "SELECT * FROM %s WHERE r1 = %d AND r2 > %d AND r2 < %d AND rs IN (%s)", tableName, r1, A,
          B, CtoD);
      expectedRowCount = r2Delta * rsDelta;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // r2 IN {A ... B}    AND    C < rs < D.
      r2Delta = 10;
      rsDelta = 10;
      A = r2Max / 2;
      B = A + r2Delta + 1;
      AtoB = String.format("%d", A + 1);
      for (int i = A + 2; i < B; i++) {
        AtoB += String.format(",%d", i);
      }
      C = rsMax / 2;
      D = C + rsDelta + 1;
      CTmp = String.format("range_%d", rsBase + C);
      DTmp = String.format("range_%d", rsBase + D);
      query = String.format(
          "SELECT * FROM %s WHERE r1 = %d AND r2 IN (%s) AND rs > '%s' AND rs < '%s'", tableName,
          r1,
          AtoB, CTmp, DTmp);
      expectedRowCount = r2Delta * rsDelta;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);

      // Inequality AND IN on same column ----------------------------------------------------------

      // Scans that should return 100 rows each.

      // A < r2 < B    AND    r2 IN {A ... B}.
      r2Delta = 1;
      A = r2Max / 2;
      B = A + r2Delta + 1;
      AtoB = String.format("%d", A + 1);
      for (int i = A + 2; i < B; i++) {
        AtoB += String.format(",%d", i);
      }
      query = String.format("SELECT * FROM %s WHERE r1 = %d AND r2 > %d AND r2 < %d AND r2 IN (%s)",
          tableName, r1, A, B, AtoB);
      expectedRowCount = r2Delta * rsMax;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
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
          "SELECT * FROM %s WHERE r1 = %d AND rs IN (%s) AND rs > '%s' AND rs < '%s'", tableName,
          r1, CtoD, CTmp, DTmp);
      expectedRowCount = r2Max * rsDelta;
      assertQueryRuntimeWithRowCount(stmt, query, expectedRowCount, queryRunCount,
          intervalScanMaxRuntimeMillis * queryRunCount);
      assertEquals(expectedRowCount, 100);
    }
  }

  /** Ensure pushing down aggregate functions with constant argument. */
  @Test
  public void aggregatesConst() throws Exception {
    new AggregatePushdownTester("COUNT(*)").test();
    new AggregatePushdownTester("COUNT(0)").test();
    new AggregatePushdownTester("COUNT(NULL)").test();

    new AggregatePushdownTester("SUM(2)").test();
    new AggregatePushdownTester("SUM(NULL::int)").test();

    // Postgres optimizes MAX(<const>) or MIN(<const>) so it isn't a real pushdown.

    new AggregatePushdownTester("AVG(1)").test();
    // TODO(#18002): uncomment the following when avg(null) is pushed down.
    /*new AggregatePushdownTester("AVG(NULL::int)").test();*/
  }

  /** Ensure pushing down aggregate functions with variables (columns). */
  @Test
  public void aggregatesVar() throws Exception {
    StringBuilder sb = new StringBuilder();
    for (String agg : Arrays.asList("COUNT", "SUM", "MAX", "MIN", "AVG")) {
      for (String column : Arrays.asList("id", "v")) {
        sb.append(String.format("%s(%s),", agg, column));
      }
    }
    // Pass all aggregates at once.  Make sure to remove the trailing comma.
    new AggregatePushdownTester(sb.substring(0, sb.length() - 1)).test();
  }

  //
  // Helpers
  //

  /**
   * Tests pushdown of aggregate SELECTs statements by analyzing YSQL {@code AggregatePushdowns}
   * metrics.
   * <p>
   * Uses a {@code (id int PRIMARY KEY, v int)} table
   */
  private class AggregatePushdownTester {
    private final String tableName = "aggregate";
    private final String indexName = "aggregate_index";
    private final int numRowsToInsert = 5000;
    private final String optimizedExpr;

    public AggregatePushdownTester(String optimizedExpr) {
      this.optimizedExpr = optimizedExpr;
    }

    public void test() throws Exception {
      try (Statement stmt = connection.createStatement()) {
        stmt.executeUpdate(String.format(
            "CREATE TABLE %s (id int PRIMARY KEY, v int)",
            tableName));
        stmt.executeUpdate(String.format(
            "CREATE INDEX %s ON %s (v ASC, id)",
            indexName, tableName));
        stmt.executeUpdate(String.format(
            "INSERT INTO %s ("
                + "SELECT generate_series, generate_series + 1 FROM generate_series(1, %s)"
                + ");",
            tableName, numRowsToInsert));
        verifyPushdown(stmt, "" /* hint */, null /* quals */);
        verifyPushdown(stmt, String.format("/*+SeqScan(%s)*/", tableName), null /* quals */);
        final String quals = String.format("v > %s", numRowsToInsert / 2);
        verifyPushdown(
            stmt, String.format("/*+IndexOnlyScan(%s %s)*/", tableName, indexName), quals);
        verifyPushdown(stmt, String.format("/*+IndexScan(%s %s)*/", tableName, indexName), quals);
        stmt.executeUpdate(String.format("DROP TABLE %s", tableName));
        waitForTServerHeartbeatIfConnMgrEnabled();
      }
    }

    private void verifyPushdown(Statement stmt, String hint, String quals) throws Exception {
      String query = String.format(
            "%sSELECT %s FROM %s%s",
            hint, optimizedExpr, tableName, (quals != null ? " WHERE " + quals : ""));
      verifyStatementMetric(
          stmt,
          query,
          AGGREGATE_PUSHDOWNS_METRIC,
          1 /* queryMetricDelta */,
          0 /* singleShardTxnMetricDelta */,
          1 /* txnMetricDelta */,
          true /* validStmt */
      );
    }
  }
}
