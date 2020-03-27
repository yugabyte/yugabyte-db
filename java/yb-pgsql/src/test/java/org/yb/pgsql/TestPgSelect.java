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
import org.yb.util.RegexMatcher;

import java.math.BigDecimal;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgSelect extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSelect.class);

  @Test
  public void testWhereClause() throws Exception {
    List<Row> allRows = setupSimpleTable("test_where");
    final String PRIMARY_KEY = "test_where_pkey";
    try (Statement statement = connection.createStatement()) {
      // Test no where clause -- select all rows.
      String query = "SELECT * FROM test_where";
      try (ResultSet rs = statement.executeQuery(query)) {
        assertEquals(allRows, getSortedRowList(rs));
      }
      assertFalse(useIndex(query, PRIMARY_KEY));

      // Test fixed hash key.
      query = "SELECT * FROM test_where WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L))
            .collect(Collectors.toList());
        assertEquals(10, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertTrue(useIndex(query, PRIMARY_KEY));

      // Test fixed primary key.
      query = "SELECT * FROM test_where WHERE h = 2 AND r = 3.5";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L) &&
                row.getDouble(1).equals(3.5))
            .collect(Collectors.toList());
        assertEquals(1, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertTrue(useIndex(query, PRIMARY_KEY));

      // Test fixed range key without fixed hash key.
      query = "SELECT * FROM test_where WHERE r = 6.5";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getDouble(1).equals(6.5))
            .collect(Collectors.toList());
        assertEquals(10, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertFalse(useIndex(query, PRIMARY_KEY));

      // Test range scan.
      query = "SELECT * FROM test_where WHERE h = 2 AND r >= 3.5 AND r < 8.5";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L) &&
                row.getDouble(1) >= 3.5 &&
                row.getDouble(1) < 8.5)
            .collect(Collectors.toList());
        assertEquals(5, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertTrue(useIndex(query, PRIMARY_KEY));

      // Test conditions on regular (non-primary-key) columns.
      query = "SELECT * FROM test_where WHERE vi < 14 AND vs != 'v09'";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getInt(2) < 14 &&
                !row.getString(3).equals("v09"))
            .collect(Collectors.toList());
        // 14 options (for hash key) minus [9,'v09'].
        assertEquals(13, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertFalse(useIndex(query, PRIMARY_KEY));

      // Test other WHERE operators (IN, OR, LIKE).
      query = "SELECT * FROM test_where WHERE h = 2 OR h = 3 OR vs LIKE 'v_2'";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L) ||
                row.getLong(0).equals(3L) ||
                row.getString(3).matches("v.2"))
            .collect(Collectors.toList());
        // 20 plus 10 options but 2 common ones ('v22' and 'v32').
        assertEquals(28, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertFalse(useIndex(query, PRIMARY_KEY));
    }
  }

  @Test
  public void testSelectTargets() throws SQLException {
    List<Row> allRows = setupSimpleTable("test_target");
    Statement statement = connection.createStatement();

    // Test all columns -- different order.
    try (ResultSet rs = statement.executeQuery("SELECT vs,vi,r,h FROM test_target")) {
      List<Row> expectedRows = allRows.stream()
          .map(row -> new Row(row.get(3), row.get(2), row.get(1), row.get(0)))
          .collect(Collectors.toList());
      assertEquals(expectedRows, getSortedRowList(rs));
    }

    // Test partial columns -- different order.
    try (ResultSet rs = statement.executeQuery("SELECT vs,r FROM test_target")) {
      List<Row> expectedRows = allRows.stream()
          .map(row -> new Row(row.get(3), row.get(1)))
          .collect(Collectors.toList());
      assertEquals(expectedRows, getSortedRowList(rs));
    }

    // Test aggregates.
    assertOneRow("SELECT avg(r) FROM test_target", 5.0D);
    assertOneRow("SELECT count(*) FROM test_target", 100L);
    assertOneRow("SELECT count(test_target.*) FROM test_target", 100L);

    // Test distinct.
    try (ResultSet rs = statement.executeQuery("SELECT distinct(h) FROM test_target")) {
      List<Row> expectedRows = allRows.stream()
          .map(row -> new Row(row.get(0)))
          .distinct()
          .collect(Collectors.toList());
      assertEquals(expectedRows, getSortedRowList(rs));
    }

    // Test selecting non-existent column.
    runInvalidQuery(statement, "SELECT v FROM test_target");

    // Test mistyped function.
    runInvalidQuery(statement, "SELECT vs * r FROM test_target");

    // Test aggregates from table without primary key.
    statement.execute("CREATE TABLE test_target_no_pkey(v1 int, v2 int)");
    statement.execute("INSERT INTO test_target_no_pkey(v1, v2) VALUES (1,2)");
    statement.execute("INSERT INTO test_target_no_pkey(v1, v2) VALUES (2,3)");
    statement.execute("INSERT INTO test_target_no_pkey(v1, v2) VALUES (3,4)");
    assertOneRow("SELECT sum(v1) FROM test_target_no_pkey", 6L);
    assertOneRow("SELECT count(*) FROM test_target_no_pkey", 3L);
    assertOneRow("SELECT sum(test_target_no_pkey.v2) FROM test_target_no_pkey", 9L);
  }

  @Test
  public void testComplexSelect() throws Exception {
    setupSimpleTable("test_clauses");
    Statement statement = connection.createStatement();

    // Test ORDER BY, OFFSET, and LIMIT clauses
    try (ResultSet rs = statement.executeQuery("SELECT h, r FROM test_clauses" +
                                                   " ORDER BY r ASC, h DESC LIMIT 27 OFFSET 17")) {
      int count = 0;
      int start = 17; // offset.
      int end = start + 27; // offset + limit.
      for (double r = 0.5; r < 10.5 && count < end; r += 1) {
        for (long h = 9; h >= 0 && count < end; h--) {
          if (count >= start) {
            assertTrue(rs.next());
            assertEquals(h, rs.getLong("h"));
            assertEquals(r, rs.getDouble("r"));
          }
          count++;
        }
      }
      assertFalse(rs.next());
    }

    // Test WITH clause (with RECURSIVE modifier).
    assertOneRow("WITH RECURSIVE t(n) AS (" +
                     "    VALUES (1)" +
                     "  UNION ALL" +
                     "    SELECT n+1 FROM t WHERE n < 100" +
                     ")" +
                     "SELECT sum(n) FROM t",
                 5050L);
  }

  @Test
  public void testJoins() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE t1(h bigint, r float, v text, PRIMARY KEY (h, r))");
      statement.execute("CREATE TABLE t2(h bigint, r float, v text, PRIMARY KEY (h, r))");

      statement.execute("INSERT INTO t1(h, r, v) VALUES (1, 2.5, 'abc')");
      statement.execute("INSERT INTO t1(h, r, v) VALUES (1, 3.5, 'def')");
      statement.execute("INSERT INTO t1(h, r, v) VALUES (1, 4.5, 'xyz')");

      statement.execute("INSERT INTO t2(h, r, v) VALUES (1, 2.5, 'foo')");
      statement.execute("INSERT INTO t2(h, r, v) VALUES (1, 4.5, 'bar')");

      // Test simple join.
      String joinStmt = "SELECT a.h, a.r, b.h, b.r, a.v as av, b.v as bv " +
          "FROM t1 a JOIN t2 b ON (a.h = b.h and a.r = b.r)";
      try (ResultSet rs = statement.executeQuery(joinStmt)) {
        assertNextRow(rs, 1L, 2.5D, 1L, 2.5D, "abc", "foo");
        assertNextRow(rs, 1L, 4.5D, 1L, 4.5D, "xyz", "bar");
        assertFalse(rs.next());
      }

      // Test join with WHERE clause.
      joinStmt = "SELECT a.h, a.r, a.v as av, b.v as bv FROM t1 a LEFT JOIN t2 b " +
          "ON (a.h = b.h and a.r = b.r) WHERE a.h = 1 AND (a.r = 2.5 OR a.r = 3.5)";
      try (ResultSet rs = statement.executeQuery(joinStmt)) {
        assertNextRow(rs, 1L, 2.5D, "abc", "foo");
        assertNextRow(rs, 1L, 3.5D, "def", null);
        assertFalse(rs.next());
      }

      // Test views from join.
      statement.execute("CREATE VIEW t1_and_t2 AS " + joinStmt);
      assertOneRow("SELECT * FROM t1_and_t2 WHERE r > 3", 1L, 3.5D, "def", null);
    }
  }

  /**
   * Regression test for #1827.
   */
  @Test
  public void testJoinWithArraySearch() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table(id int, name varchar, PRIMARY KEY (id))");
      statement.execute("CREATE TABLE join_table(id int, tid int, PRIMARY KEY (id))");

      statement.execute("INSERT INTO test_table VALUES (0, 'name 1')");
      statement.execute("INSERT INTO test_table VALUES (1, 'name 2')");
      statement.execute("INSERT INTO test_table VALUES (2, 'name 3')");

      statement.execute("INSERT INTO join_table VALUES (0, 0)");
      statement.execute("INSERT INTO join_table VALUES (1, 0)");
      statement.execute("INSERT INTO join_table VALUES (2, 1)");
      statement.execute("INSERT INTO join_table VALUES (3, 1)");
      statement.execute("INSERT INTO join_table VALUES (4, 2)");
      statement.execute("INSERT INTO join_table VALUES (5, 2)");

      assertQuery(statement, "SELECT tt.name, jt.id FROM test_table tt" +
              " INNER JOIN join_table jt ON tt.id = jt.tid" +
              " WHERE tt.id IN (0, 1)" +
              " ORDER BY jt.id",
          new Row("name 1", 0),
          new Row("name 1", 1),
          new Row("name 2", 2),
          new Row("name 2", 3));
    }
  }

  @Test
  public void testExpressions() throws Exception {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable("test_expr");

      // Insert a sample row: Row[2, 3.0, 4, 'abc'].
      statement.execute("INSERT INTO test_expr(h, r, vi, vs) VALUES (2, 3.0, 4, 'abc')");
      assertOneRow("SELECT * FROM test_expr", 2L, 3.0D, 4, "abc");

      // Test expressions in SELECT targets.
      assertOneRow("SELECT h + 1.5, pow(r, 2), vi * h, 7 FROM test_expr WHERE h = 2",
                   new BigDecimal(3.5), 9.0D, 8L, 7);

      // Test expressions in SELECT WHERE clause.
      assertOneRow("SELECT * FROM test_expr WHERE h + r <= 10 AND substring(vs from 2) = 'bc'",
                   2L, 3.0D, 4, "abc");
    }
  }

  @Test
  public void testPgsqlVersion() throws Exception {
    try (Statement statement = connection.createStatement()) {
      try (ResultSet rs = statement.executeQuery("SELECT version();")) {
          assertTrue(rs.next());
          assertThat(String.valueOf(rs.getArray(1)),
                     RegexMatcher.matchesRegex("PostgreSQL.*-YB-.*"));
          assertFalse(rs.next());
      }
      try (ResultSet rs = statement.executeQuery("show server_version;")) {
        assertTrue(rs.next());
        assertThat(String.valueOf(rs.getArray(1)),
                RegexMatcher.matchesRegex(".*-YB-.*"));
        assertFalse(rs.next());
      }
    }
  }

  private void verifyStatementPushdownMetric(Statement statement,
                                             String stmt,
                                             boolean pushdown_expected) throws Exception {
    verifyStatementMetric(statement, stmt, AGGREGATE_PUSHDOWNS_METRIC,
                          pushdown_expected ? 1 : 0, 1, true);
  }

  @Test
  public void testAggregatePushdowns() throws Exception {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable("aggtest");

      // Pushdown COUNT/MAX/MIN/SUM for INTEGER/FLOAT.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(vi), MAX(vi), MIN(vi), SUM(vi) FROM aggtest", true);
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(r), MAX(r), MIN(r), SUM(r) FROM aggtest", true);

      // Don't pushdown if non-supported aggregate is provided (e.g. AVG, at least for now).
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(vi), AVG(vi) FROM aggtest", false);

      // Pushdown COUNT(*).
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(*) FROM aggtest", true);

      // Don't pushdown if there's a WHERE condition.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(*) FROM aggtest WHERE h > 0", false);

      // Pushdown for BIGINT COUNT/MAX/MIN.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(h), MAX(h), MIN(h) FROM aggtest", true);

      // Don't pushdown for BIGINT SUM.
      verifyStatementPushdownMetric(
          statement, "SELECT SUM(h) FROM aggtest", false);

      // Pushdown COUNT/MIN/MAX for text.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(vs), MAX(vs), MIN(vs) FROM aggtest", true);

      // Pushdown shared aggregates.
      verifyStatementPushdownMetric(
          statement, "SELECT MAX(vi), MAX(vi) + 1 FROM aggtest", true);

      // Don't pushdown complicated expression in aggregate.
      verifyStatementPushdownMetric(
          statement, "SELECT MAX(vi + 1) FROM aggtest", false);

      // Don't pushdown window functions.
      verifyStatementPushdownMetric(
          statement, "SELECT h, COUNT(h) OVER (PARTITION BY h) FROM aggtest", false);

      // Don't pushdown if DISTINCT present.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(DISTINCT vi) FROM aggtest", false);

      // Create table with NUMERIC/DECIMAL types.
      statement.execute("CREATE TABLE aggtest2 (n numeric, d decimal)");

      // Pushdown COUNT for NUMERIC/DECIMAL types.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(n), COUNT(d) FROM aggtest2", true);

      // Don't pushdown SUM/MAX/MIN for NUMERIC/DECIMAL types.
      for (String col : Arrays.asList("n", "d")) {
        for (String agg : Arrays.asList("SUM", "MAX", "MIN")) {
          verifyStatementPushdownMetric(
              statement, "SELECT " + agg + "(" + col + ") FROM aggtest2", false);
        }
      }
    }
  }

  @Test
  public void testReverseScanMultiRangeCol() throws Exception {
    try (Statement statement = connection.createStatement()) {

      statement.execute("CREATE TABLE test_reverse_scan_multicol (h int, r1 int, r2 int, r3 int," +
                              " PRIMARY KEY (h, r1, r2, r3))");
      String insert_stmt = "INSERT INTO test_reverse_scan_multicol VALUES (1, %d, %d, %d)";

      for (int r1 = 1; r1 <= 5; r1++) {
        for (int r2 = 1; r2 <= 5; r2++) {
          for (int r3 = 1; r3 <= 5; r3++) {
            statement.execute(String.format(insert_stmt, r1, r2, r3));
          }
        }
      }

      // Test reverse scan with prefix bounds: r1[2, 4], r2(1,4).
      String select_stmt = "SELECT * FROM test_reverse_scan_multicol WHERE h = 1" +
                                          "AND r1 >= 2 AND r1 <= 4 AND r2 > 1 and r2 < 4" +
                                          "ORDER BY r1 DESC, r2 DESC, r3 DESC";
      ResultSet rs = statement.executeQuery(select_stmt);

      for (int r1 = 4; r1 >= 2; r1--) {
        for (int r2 = 3; r2 > 1; r2--) {
          for (int r3 = 5; r3 >= 1; r3--) {
            assertTrue(rs.next());
            assertEquals(r1, rs.getInt("r1"));
            assertEquals(r2, rs.getInt("r2"));
            assertEquals(r3, rs.getInt("r3"));
          }
        }
      }
      assertFalse(rs.next());

      // Test reverse scan with non-prefix bounds and LIMIT: r1[2, 4], r3[2, 3].
      // Total 3 * 5 * 2 = 30 rows but set LIMIT to 25.
      select_stmt = "SELECT * FROM test_reverse_scan_multicol WHERE h = 1" +
              "AND r1 >= 2 AND r1 <= 4 AND r3 > 1 and r3 < 4" +
              "ORDER BY r1 DESC, r2 DESC, r3 DESC LIMIT 25";
      rs = statement.executeQuery(select_stmt);

      int idx = 0;
      for (int r1 = 4; r1 >= 2 && idx < 25; r1--) {
        for (int r2 = 5; r2 >= 1 && idx < 25; r2--) {
          for (int r3 = 3; r3 > 1 && idx < 25; r3--) {
            assertTrue(rs.next());
            assertEquals(r1, rs.getInt("r1"));
            assertEquals(r2, rs.getInt("r2"));
            assertEquals(r3, rs.getInt("r3"));
            idx++;
          }
        }
      }
      assertFalse(rs.next());
    }
  }
}
