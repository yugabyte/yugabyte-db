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

import org.yb.minicluster.RocksDBMetrics;

import org.yb.util.BuildTypeUtil;
import org.yb.YBTestRunner;
import org.yb.util.RegexMatcher;
import org.yb.YBTestRunner;

import java.math.BigDecimal;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgSelect extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSelect.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_enable_packed_row", "false");
    return flagMap;
  }

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
      assertFalse(isIndexScan(statement, query, PRIMARY_KEY));

      // Test fixed hash key.
      query = "SELECT * FROM test_where WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L))
            .collect(Collectors.toList());
        assertEquals(10, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertTrue(isIndexScan(statement, query, PRIMARY_KEY));

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
      assertTrue(isIndexScan(statement, query, PRIMARY_KEY));

      // Test fixed range key without fixed hash key.
      query = "SELECT * FROM test_where WHERE r = 6.5";
      try (ResultSet rs = statement.executeQuery(query)) {
        List<Row> expectedRows = allRows.stream()
            .filter(row -> row.getDouble(1).equals(6.5))
            .collect(Collectors.toList());
        assertEquals(10, expectedRows.size());
        assertEquals(expectedRows, getSortedRowList(rs));
      }
      assertFalse(isIndexScan(statement, query, PRIMARY_KEY));

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
      assertTrue(isIndexScan(statement, query, PRIMARY_KEY));

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
      assertFalse(isIndexScan(statement, query, PRIMARY_KEY));

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
      assertFalse(isIndexScan(statement, query, PRIMARY_KEY));
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
    assertOneRow(statement, "SELECT avg(r) FROM test_target", 5.0D);
    assertOneRow(statement, "SELECT count(*) FROM test_target", 100L);
    assertOneRow(statement, "SELECT count(test_target.*) FROM test_target", 100L);

    // Test distinct.
    try (ResultSet rs = statement.executeQuery("SELECT distinct(h) FROM test_target")) {
      List<Row> expectedRows = allRows.stream()
          .map(row -> new Row(row.get(0)))
          .distinct()
          .collect(Collectors.toList());
      assertEquals(expectedRows, getSortedRowList(rs));
    }

    // Test selecting non-existent column.
    runInvalidQuery(statement, "SELECT v FROM test_target", "column \"v\" does not exist");

    // Test mistyped function.
    runInvalidQuery(statement, "SELECT vs * r FROM test_target", "operator does not exist");

    // Test aggregates from table without primary key.
    statement.execute("CREATE TABLE test_target_no_pkey(v1 int, v2 int)");
    statement.execute("INSERT INTO test_target_no_pkey(v1, v2) VALUES (1,2)");
    statement.execute("INSERT INTO test_target_no_pkey(v1, v2) VALUES (2,3)");
    statement.execute("INSERT INTO test_target_no_pkey(v1, v2) VALUES (3,4)");
    assertOneRow(statement, "SELECT sum(v1) FROM test_target_no_pkey", 6L);
    assertOneRow(statement, "SELECT count(*) FROM test_target_no_pkey", 3L);
    assertOneRow(statement, "SELECT sum(test_target_no_pkey.v2) FROM test_target_no_pkey", 9L);
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
    assertOneRow(statement,
                 "WITH RECURSIVE t(n) AS (" +
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
      assertOneRow(statement, "SELECT * FROM t1_and_t2 WHERE r > 3", 1L, 3.5D, "def", null);
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
      assertOneRow(statement, "SELECT * FROM test_expr", 2L, 3.0D, 4, "abc");

      // Test expressions in SELECT targets.
      assertOneRow(statement,
                   "SELECT h + 1.5, pow(r, 2), vi * h, 7 FROM test_expr WHERE h = 2",
                   new BigDecimal(3.5), 9.0D, 8L, 7);

      // Test expressions in SELECT WHERE clause.
      assertOneRow(statement,
                   "SELECT * FROM test_expr WHERE h + r <= 10 AND substring(vs from 2) = 'bc'",
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
                          pushdown_expected ? 1 : 0, 0, 1, true);
  }

  @Test
  public void testAggregatePushdowns() throws Exception {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable("aggtest");

      // Pushdown COUNT/MAX/MIN/SUM/AVG for INTEGER.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(vi), MAX(vi), MIN(vi), SUM(vi), AVG(vi) FROM aggtest", true);
      // Pushdown COUNT/MAX/MIN/SUM for FLOAT.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(r), MAX(r), MIN(r), SUM(r) FROM aggtest", true);
      // Don't pushdown AVG for FLOAT.
      verifyStatementPushdownMetric(
          statement, "SELECT AVG(r) FROM aggtest", false);

      // Don't pushdown if non-supported aggregate is provided (e.g. BIT_AND, at least for now).
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(vi), BIT_AND(vi) FROM aggtest", false);

      // Pushdown COUNT(*).
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(*) FROM aggtest", true);

      // Pushdown if there's a pushable WHERE condition.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(*) FROM aggtest WHERE h > 0", true);

      // Don't pushdown if there's a not pushable WHERE condition.
      verifyStatementPushdownMetric(
          statement,
          "SELECT COUNT(*) FROM aggtest WHERE CASE h WHEN 42 THEN true ELSE false END",
          false);

      // Pushdown for BIGINT COUNT/MAX/MIN.
      verifyStatementPushdownMetric(
          statement, "SELECT COUNT(h), MAX(h), MIN(h) FROM aggtest", true);

      // Don't pushdown for BIGINT SUM.
      verifyStatementPushdownMetric(
          statement, "SELECT SUM(h) FROM aggtest", false);

      // Don't pushdown for BIGINT AVG.
      verifyStatementPushdownMetric(
          statement, "SELECT AVG(h) FROM aggtest", false);

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

      // Don't pushdown SUM/MAX/MIN/AVG for NUMERIC/DECIMAL types.
      for (String col : Arrays.asList("n", "d")) {
        for (String agg : Arrays.asList("SUM", "MAX", "MIN", "AVG")) {
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

  public void testNullPushdownUtil(String colOrder) throws Exception {
    String createTable = "CREATE TABLE %s(a int, b int, PRIMARY KEY(a %s))";
    String createIndex = "CREATE INDEX ON %s(b %s)";

    try (Statement statement = connection.createStatement()) {
      // We want to test planner under classical PG nodes.
      statement.execute("set yb_bnl_batch_size = 1");
      statement.execute(String.format(createTable, "t1", colOrder));
      statement.execute(String.format(createTable, "t2", colOrder));
      statement.execute("insert into t1 values (1,1), (2,2), (3,3)");
      statement.execute("insert into t2 values (1,1), (2,2), (3,null)");

      //--------------------------------------------------------------------------------------------
      // Test join where one join column is null.

      // Inner join, expect no rows.
      String query = "select * from t2 inner join t1 on t2.b = t1.a where t2.a = 3";
      assertNoRows(statement, query);
      String explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for t1 pkey",
                 explainOutput.contains("Index Cond: (a = 3)"));
      assertTrue("Expect pushdown for t2 pkey",
                 explainOutput.contains("Index Cond: (a = t2.b)"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));

      // Outer join, expect one row.
      query = "select * from t2 full outer join t1 on t2.b = t1.a where t2.a = 3";
      assertOneRow(statement, query, 3, null, null, null);
      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for t1 pkey",
                 explainOutput.contains("Index Cond: (a = 3)"));
      assertTrue("Expect pushdown for t2 pkey",
                 explainOutput.contains("Index Cond: (t2.b = a)"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));

      // -------------------------------------------------------------------------------------------
      // Test IS NULL and IS NOT NULL.

      // Add an index on t1.b that contains null value in its key.
      statement.execute("insert into t1 values (4,null), (5,null)");
      statement.execute(String.format(createIndex, "t1", colOrder));

      // Test IS NULL on pkey column.
      query = "select * from t1 where a IS NULL";
      assertNoRows(statement, query);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IS NULL",
                 explainOutput.contains("Index Cond: (a IS NULL)"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));

      // Test IS NULL on index column.
      query = "select * from t1 where b IS NULL";
      Set<Row> expectedRows = new HashSet<>();
      expectedRows.add(new Row(4, null));
      expectedRows.add(new Row(5, null));
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IS NULL",
                 explainOutput.contains("Index Cond: (b IS NULL)"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));

      // Test IS NOT NULL.
      query = "select * from t1 where b IS NOT NULL";
      expectedRows.clear();
      expectedRows.add(new Row(1, 1));
      expectedRows.add(new Row(2, 2));
      expectedRows.add(new Row(3, 3));
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      if (colOrder.equals("HASH")) {
        assertTrue("Expect SeqScan on t1 when colOrder is HASH",
                  explainOutput.contains("Seq Scan on t1"));
        assertTrue("Expect filter pushdown to DocDB",
                  explainOutput.contains("Remote Filter: (b IS NOT NULL)"));
      }
      else {
        assertTrue("Expect pushdown for IS NOT NULL when colOrder is ASC or DESC",
                  explainOutput.contains("Index Cond: (b IS NOT NULL)"));
        assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));
      }


      // Test IN with NULL (should not match null row because null == null is not true).
      query = "select * from t1 where b IN (NULL, 2, 3)";
      expectedRows.clear();
      expectedRows.add(new Row(2, 2));
      expectedRows.add(new Row(3, 3));
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IN condition",
                 explainOutput.contains("Index Cond: (b = ANY ('{NULL,2,3}'::integer[]))"));
      assertFalse("Expect DocDB to filter fully",
                 explainOutput.contains("Rows Removed by"));

      // Test NOT IN with NULL (should not match anything because v1 != null is never true).
      query = "select * from t1 where b NOT IN (NULL, 2)";
      expectedRows.clear();
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect no pushdown for NOT IN condition",
                 explainOutput.contains("Filter: (b <> ALL ('{NULL,2}'::integer[]))"));
      assertTrue("Expect YSQL-level filtering",
                 explainOutput.contains("Rows Removed by Filter: 5"));

      // Test BETWEEN.
      query = "select * from t1 where b between 1 and 3";
      expectedRows.clear();
      expectedRows.add(new Row(1, 1));
      expectedRows.add(new Row(2, 2));
      expectedRows.add(new Row(3, 3));
      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertRowSet(statement, query, expectedRows);

      if (colOrder.equals("HASH")) {
        assertTrue("Expect SeqScan on t1 when colOrder is HASH",
                  explainOutput.contains("Seq Scan on t1"));
        assertTrue("Expect filter pushdown to DocDB",
                  explainOutput.contains("Remote Filter: ((b >= 1) AND (b <= 3))"));
      } else {
        assertTrue("Expect pushdown for BETWEEN condition on ASC/DESC",
                   explainOutput.contains("Index Cond: ((b >= 1) AND (b <= 3))"));
        assertFalse("Expect no YSQL-level filtering for ASC/DESC",
                    explainOutput.contains("Rows Removed by"));
      }

      // Test BETWEEN with NULL.
      query = "select * from t1 where b BETWEEN 1 AND NULL";
      expectedRows.clear();
      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertRowSet(statement, query, expectedRows);
      assertTrue("YSQL will auto-eval condition to false",
                 explainOutput.contains("One-Time Filter: false"));
      assertFalse("Expect no YSQL-level filtering",
                  explainOutput.contains("Rows Removed by"));

      //--------------------------------------------------------------------------------------------
      // Test join where one join column is null *and* the other table has null rows for it.

      // Inner join (on t1.b this time), expect no rows.
      query = "select * from t2 inner join t1 on t2.b = t1.b where t2.a = 3";
      assertNoRows(statement, query);
      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for t1 pkey",
                 explainOutput.contains("Index Cond: (a = 3)"));
      assertTrue("Expect pushdown for t2 pkey",
                 explainOutput.contains("Index Cond: (b = t2.b)"));
      assertFalse("Expect not to filter any rows by Index Recheck",
                 explainOutput.contains("Rows Removed by Index Recheck"));

      // Outer join (on t1.b this time), expect one row.
      query = "select * from t2 full outer join t1 on t2.b = t1.b where t2.a = 3";
      assertOneRow(statement, query, 3, null, null, null);
      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for t1 pkey",
                 explainOutput.contains("Index Cond: (a = 3)"));
      assertTrue("Expect pushdown for t2 pkey",
                 explainOutput.contains("Index Cond: (t2.b = b)"));
      assertFalse("Expect not to filter any rows by Index Recheck",
                 explainOutput.contains("Rows Removed by Index Recheck"));

      statement.execute("DROP TABLE t1");
      statement.execute("DROP TABLE t2");
    }
  }

  @Test
  public void testNullPushdown() throws Exception {
    testNullPushdownUtil("ASC");
    testNullPushdownUtil("HASH");
    testNullPushdownUtil("DESC");
  }

  @Test
  public void testMulticolumnNullPushdown() throws Exception {
    try (Statement statement = connection.createStatement()) {

      statement.execute("CREATE TABLE test(h int, r int, vh1 int, vh2 int, vr1 int, vr2 int)");
      statement.execute("CREATE INDEX on test((vh1, vh2) HASH, vr1 ASC, vr2 ASC)");
      statement.execute("INSERT INTO test values (1,1,1,1,1,1)");
      statement.execute("INSERT INTO test values (2,2,null,2,2,2)");
      statement.execute("INSERT INTO test values (3,3,3,null,3,3)");
      statement.execute("INSERT INTO test values (4,4,4,4,null,4)");
      statement.execute("INSERT INTO test values (5,5,5,5,5,null)");
      statement.execute("INSERT INTO test values (6,6,null,null,6,6)");
      statement.execute("INSERT INTO test values (7,7,7,7,null,null)");
      statement.execute("INSERT INTO test values (8,8,null,8,8,null)");
      statement.execute("INSERT INTO test values (9,9,null,null,null,null)");
      statement.execute("INSERT INTO test values (10,10,10,10,10,10)");

      Set<Row> allRows = new HashSet<>();
      allRows.add(new Row(1, 1, 1, 1, 1, 1));
      allRows.add(new Row(2, 2, null, 2, 2, 2));
      allRows.add(new Row(3, 3, 3, null, 3, 3));
      allRows.add(new Row(4, 4, 4, 4, null, 4));
      allRows.add(new Row(5, 5, 5, 5, 5, null));
      allRows.add(new Row(6, 6, null, null, 6, 6));
      allRows.add(new Row(7, 7, 7, 7, null, null));
      allRows.add(new Row(8, 8, null, 8, 8, null));
      allRows.add(new Row(9, 9, null, null, null, null));
      allRows.add(new Row(10, 10, 10, 10, 10, 10));

      // Test null conditions on both hash columns.
      String query = "SELECT * FROM test WHERE vh1 IS NULL AND vh2 IS NULL";
      Set<Row> expectedRows = allRows.stream()
                                     .filter(r -> r.get(2) == null && r.get(3) == null)
                                     .collect(Collectors.toSet());
      assertRowSet(statement, query, expectedRows);

      String explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IS NULL" + explainOutput,
                 explainOutput.contains("Index Cond: ((vh1 IS NULL) AND (vh2 IS NULL))"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));

      // Test null conditions on all hash+range columns.
      query = "SELECT * FROM test WHERE vh1 IS NULL AND vh2 IS NULL" +
              " AND vr1 IS NULL and vr2 IS NULL";
      expectedRows = allRows.stream()
                                       .filter(r -> r.get(2) == null && r.get(3) == null &&
                                               r.get(4) == null && r.get(5) == null)
                                       .collect(Collectors.toSet());
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IS NULL" + explainOutput,
                 explainOutput.contains("Index Cond: ((vh1 IS NULL) AND (vh2 IS NULL)" +
                                                " AND (vr1 IS NULL) AND (vr2 IS NULL))"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));

      // Test null/value condition mix columns.
      query = "SELECT * FROM test WHERE vh1 IS NULL AND vh2 = 8" +
              " AND vr1 = 8 and vr2 IS NULL";
      expectedRows = allRows.stream()
                              .filter(r -> r.get(2) == null && Objects.equals(r.get(3), 8) &&
                                      Objects.equals(r.get(3), 8) && r.get(5) == null)
                              .collect(Collectors.toSet());
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IS NULL" + explainOutput,
                 explainOutput.contains("Index Cond: ((vh1 IS NULL) AND (vh2 = 8)" +
                                                " AND (vr1 = 8) AND (vr2 IS NULL))"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));

      // Test partly set hash key (should not push down).
      query = "SELECT * FROM test WHERE vh1 IS NULL AND vr1 IS NULL and vr2 IS NULL";
      expectedRows = allRows.stream()
                              .filter(r -> r.get(2) == null &&
                                      r.get(4) == null && r.get(5) == null)
                              .collect(Collectors.toSet());
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IS NULL" + explainOutput,
                 explainOutput.contains("Remote Filter: ((vh1 IS NULL) AND (vr1 IS NULL) " +
                                                "AND (vr2 IS NULL))"));

      // Test hash key + partly set range key (should push down).
      query = "SELECT * FROM test WHERE vh1 IS NULL AND vh2 IS NULL" +
              " AND vr1 IS NULL";
      expectedRows = allRows.stream()
                              .filter(r -> r.get(2) == null && r.get(3) == null &&
                                      r.get(4) == null)
                              .collect(Collectors.toSet());
      assertRowSet(statement, query, expectedRows);

      explainOutput = getExplainAnalyzeOutput(statement, query);
      assertTrue("Expect pushdown for IS NULL" + explainOutput,
                 explainOutput.contains("Index Cond: ((vh1 IS NULL) AND (vh2 IS NULL)" +
                                                " AND (vr1 IS NULL))"));
      assertFalse("Expect DocDB to filter fully",
                  explainOutput.contains("Rows Removed by"));
    }
  }

  private RocksDBMetrics assertFullDocDBFilter(Statement statement,
    String query, String table_name) throws Exception {
    RocksDBMetrics beforeMetrics = getRocksDBMetric(table_name);
    String explainOutput = getExplainAnalyzeOutput(statement, query);
        assertFalse("Expect DocDB to filter fully",
                    explainOutput.contains("Rows Removed by"));
    RocksDBMetrics afterMetrics = getRocksDBMetric(table_name);
    return afterMetrics.subtract(beforeMetrics);
  }

  @Test
  public void testPartialKeyScan() throws Exception {
    String query = "CREATE TABLE sample_table(h INT, r1 INT, r2 INT, r3 INT, "
                    + "v INT, PRIMARY KEY(h HASH, r1 ASC, r2 ASC, r3 DESC))";

    try (Statement statement = connection.createStatement()) {
        statement.execute(query);

        // v has values from 1 to 100000 and the other columns are
        // various digits of v as such
        // h    r1  r2  r3      v
        // 0    0   0   0       0
        // 0    0   0   1       1
        // ...
        // 12   4   9   3      12493
        // ...
        // 100  0   0   0      100000
        query = "INSERT INTO sample_table SELECT i/1000, (i/100)%10, " +
                "(i/10)%10, i%10, i FROM generate_series(1, 100000) i";
        statement.execute(query);

        Set<Row> allRows = new HashSet<>();
        for (int i = 1; i <= 100000; i++) {
          allRows.add(new Row(i/1000, (i/100)%10, (i/10)%10, i%10, i));
        }

        // Select where hash code is specified and one range constraint
        query = "SELECT * FROM sample_table WHERE h = 1 AND r3 < 6";

        Set<Row> expectedRows = allRows.stream()
                                       .filter(r -> r.getInt(0) == 1 &&
                                               r.getInt(3) < 6)
                                       .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // There are 10 * 10 total values for r1 and r2 that we have to look
        // through. For each pair (r1, r2) we iterate through all values of
        // r3 in [0, 6] and then seek to the next pair for (r1, r2). There
        // are 10 * 10 such pairs. There is also an initial seek into the
        // hash key, making the total 10 * 10 + 1 = 101. The actual seeks are
        // as follows:
        // Seek(SubDocKey(DocKey(0x1210, [1], [kLowest]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 0, 6]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 1, 6]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 2, 6]), []))
        // ...
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 0, 6]), []))
        // ...
        // Seek(SubDocKey(DocKey(0x1210, [1], [9, 9, 6]), []))

        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 101);

        // Select where hash code is specified, one range constraint
        // and one option constraint on two separate columns.
        // No constraint is specified for r2.
        query = "SELECT * FROM sample_table WHERE " +
                "h = 1 AND r1 < 2 AND r3 IN (2, 25, 8, 7, 23, 18)";
        Integer[] r3FilterArray = {2, 25, 8, 7, 23, 18};
        Set<Integer> r3Filter = new HashSet<Integer>();
        r3Filter.addAll(Arrays.asList(r3FilterArray));

        expectedRows = allRows.stream()
                              .filter(r -> r.getInt(0) == 1 &&
                                      r.getInt(1) < 2 &&
                                      r3Filter.contains(r.getInt(3)))
                              .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // For each of the 2 * 10 possible pairs of (r1, r2) we seek through
        // 4 values of r3 (8, 7, 2, kHighest). We must have that seek to
        // r3 = kHighest in order to get to the next value of (r1,r2).
        // We also have one initial seek into the hash key, making the total
        // number of seeks 2 * 10 * 4 + 1 = 81
        // Seek(SubDocKey(DocKey(0x1210, [1], [kLowest]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 0, 8]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 0, 7]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 0, 2]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 0, kHighest]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 1, 8]), []))
        // ...
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 9, 2]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 9, kHighest]), []))
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 81);

        // Select where all keys have some sort of discrete constraint
        // on them
        query = "SELECT * FROM sample_table WHERE " +
                "h = 1 AND r1 IN (1,2) AND r2 IN (2,3) " +
                "AND r3 IN (2, 25, 8, 7, 23, 18)";

        expectedRows = allRows.stream()
                              .filter(r -> r.getInt(0) == 1 &&
                                      (r.getInt(1) == 1 ||
                                       r.getInt(1) == 2) &&
                                      (r.getInt(2) == 2 ||
                                       r.getInt(2) == 3) &&
                                      r3Filter.contains(r.getInt(3)))
                              .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // There are 2 possible values for r1 and 2 possible values for r2.
        // There are 3 possible values for r3 (8, 7, 2). Remember that for
        // each value of (r1, r2), we must seek to (r1, r2, 25) to get
        // to the first row that has value of (r1, r2),
        // resulting in 4 total seeks for each (r1, r2).
        // Altogether there are 2 * 2 * 4 = 16 seeks.
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 2, 25]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 2, 8]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 2, 7]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 2, 2]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 3, 25]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 3, 8]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 3, 7]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 3, 2]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 2, 25]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 2, 8]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 2, 7]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 2, 2]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 3, 25]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 3, 8]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 3, 7]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [2, 3, 2]), []))
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 16);


        // Select where two out of three columns have discrete constraints
        // set up while the other one has no restrictions
        query = "SELECT * FROM sample_table WHERE " +
                "h = 1 AND r2 IN (2,3) AND r3 IN (2, 25, 8, 7, 23, 18)";

        expectedRows = allRows.stream()
                              .filter(r -> r.getInt(0) == 1 &&
                                      (r.getInt(2) == 2 ||
                                       r.getInt(2) == 3) &&
                                      r3Filter.contains(r.getInt(3)))
                              .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        metrics = assertFullDocDBFilter(statement, query, "sample_table");

        // For each value of r1, we have two values of r2 to seek through and
        // for each of those we have at most 6 values of r3 to seek through.
        // In reality, we seek through 4 values of r3 for each (r1,r2) for
        // the same reason as the previous test. After we've exhausted all
        // possibilities for (r2,r3) for a given r1, we seek to (r1,kHighest)
        // to seek to the next possible value of r1. Therefore, we seek
        // 4 * 2 + 1 = 9 values for each r1.
        // Note that there are 10 values of r1 to seek through and we do an
        // initial seek into the hash code as usual. So in total, we have
        // 10 * (4 * 2 + 1) + 1 = 10 * 9 + 1 = 91 seeks.
        // Seek(SubDocKey(DocKey(0x1210, [1], [kLowest]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 2, 25]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 2, 8]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 2, 7]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 2, 2]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 3, 25]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 3, 8]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 3, 7]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, 3, 2]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [0, kHighest]), []))
        // Seek(SubDocKey(DocKey(0x1210, [1], [1, 2, 25]), []))
        // ...
        // Seek(SubDocKey(DocKey(0x1210, [1], [9, kHighest]), []))
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 91);

        // Select where we have options for the hash code and discrete
        // filters on two out of three range columns
        query = "SELECT * FROM sample_table WHERE " +
                "h IN (1,5) AND r2 IN (2,3) AND r3 IN (2, 25, 8, 7, 23, 18)";

        expectedRows = allRows.stream()
                              .filter(r -> (r.getInt(0) == 1 ||
                                            r.getInt(0) == 5) &&
                                      (r.getInt(2) == 2 ||
                                       r.getInt(2) == 3) &&
                                      r3Filter.contains(r.getInt(3)))
                              .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // Note that in this case, YSQL sends two batches of requests
        // to DocDB in parallel, one for each hash code option. So this
        // should really just be double the number of seeks as
        // SELECT * FROM sample_table WHERE h = 1 AND r2 IN (2,3)
        // AND r3 IN (2, 25, 8, 7, 23, 18)
        // We have 91 * 2 = 182 seeks
        assertLessThanOrEqualTo(metrics.seekCount, 182);
    }
  }

  @Test
  public void testIndexDistinctRangeScan() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, PRIMARY KEY(r1 ASC, r2 ASC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT 1, i FROM GENERATE_SERIES(1, 10) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 2, i FROM GENERATE_SERIES(1, 10) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 3, i FROM GENERATE_SERIES(1, 10) AS i)";
      statement.execute(query);

      Set<Row> expectedRows = new HashSet<>();
      expectedRows.add(new Row(1));
      expectedRows.add(new Row(2));
      expectedRows.add(new Row(3));
      query = "/*+Set(enable_hashagg false)*/ SELECT DISTINCT r1 FROM t WHERE r1 <= 3";
      assertRowSet(statement, query, expectedRows);

      // With DISTINCT pushed down to DocDB, we only to scan three keys:
      // 1. From kLowest, seek to 1.
      // 2. From 1, seek to 2.
      // 3. From 2, seek to 3.
      RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
      assertEquals(3, metrics.seekCount);
    }
  }

  @Test
  public void testIndexDistinctMulticolumnsScan() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, r3 INT, PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT 1, 1, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 2, 2, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 3, 1, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 3, 2, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 3, 3, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      Set<Row> expectedRows = new HashSet<>();
      expectedRows.add(new Row(1, 1));
      expectedRows.add(new Row(2, 2));
      expectedRows.add(new Row(3, 1));
      expectedRows.add(new Row(3, 2));
      expectedRows.add(new Row(3, 3));
      query = "/*+Set(enable_hashagg false)*/ SELECT DISTINCT r1, r2 FROM t WHERE r1 <= 3";
      assertRowSet(statement, query, expectedRows);

      // We need to do 6 seeks here:
      // 1. Seek from (kLowest, kLowest), found (1, 1).
      // 2. Seek from (1, 1), found (2, 2).
      // 3. Seek from (2, 2), found (3, 1).
      // 4. Seek from (3, 1), found (3, 2).
      // 5. Seek from (3, 2), found (3, 3).
      // 6. Seek from (3, 3), found no more key.
      // Note that we need the last seek, since under the condition r1 <= 3, we don't know whether
      // there are more items to be scanned.
      RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
      assertEquals(6, metrics.seekCount);
    }
  }

  @Test
  public void testIndexDistinctSkipColumnScan() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, r3 INT, r4 INT, " +
                    " PRIMARY KEY(r1 ASC, r2 ASC, r3 ASC, r4 ASC))";

    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT 1, 1, 1, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 2, 2, 2, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 3, 3, 3, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      Set<Row> expectedRows = new HashSet<>();
      expectedRows.add(new Row(1, 1));
      expectedRows.add(new Row(2, 2));
      expectedRows.add(new Row(3, 3));
      query = "/*+Set(enable_hashagg false)*/ SELECT DISTINCT r1, r3 FROM t WHERE r3 <= 3";
      assertRowSet(statement, query, expectedRows);

      RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
      assertEquals(4, metrics.seekCount);
    }
  }

  @Test
  public void testIndexDistinctScanWithNonConsecutiveColumns() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, r3 INT, r4 INT, r5 INT," +
                    " PRIMARY KEY(r1 ASC, r3 ASC, r5 ASC))";

    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT 1, i, i, i, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 2, i, i, i, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT 3, i, i, i, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      Set<Row> expectedRows = new HashSet<>();
      expectedRows.add(new Row(1));
      expectedRows.add(new Row(2));
      expectedRows.add(new Row(3));
      query = "/*+Set(enable_hashagg false)*/ SELECT DISTINCT r1 FROM t WHERE r3 <= 1 AND r5 <= 1";
      assertRowSet(statement, query, expectedRows);

      RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
      assertEquals(4, metrics.seekCount);
    }
  }

  @Test
  public void testDistinctScanHashColumn() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, r3 INT, PRIMARY KEY(r1 HASH, r2 ASC, r3 ASC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT i, i, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      {
        Set<Row> expectedRows = new HashSet<>();
        for (int i = 1; i <= 100; i++) {
          expectedRows.add(new Row(i));
        }

        query = "/*+Set(enable_hashagg false)*/ SELECT DISTINCT r1 FROM t WHERE r1 <= 100";
        assertRowSet(statement, query, expectedRows);

        // Here we do a sequential scan.
        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
        assertEquals(3, metrics.seekCount);
      }

      {
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(100));

        query = "/*+Set(enable_hashagg false)*/ SELECT DISTINCT r1 FROM t WHERE r1 = 100";
        assertRowSet(statement, query, expectedRows);

        // Here we do an index scan.
        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
        assertEquals(1, metrics.seekCount);
      }
    }
  }

  @Test
  public void testDistinctMultiHashColumns() throws Exception {
    String query = "CREATE TABLE t(h1 INT, h2 INT, r INT, PRIMARY KEY((h1, h2) HASH, r ASC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT i, i, i FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      {
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(1, 1));

        query = "/*+Set(enable_hashagg false)*/ " +
                "SELECT DISTINCT h1, h2 FROM t WHERE h1 = 1 AND h2 = 1";
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
        assertEquals(1, metrics.seekCount);
      }
    }
  }

  @Test
  public void testDistinctOnNonPrefixScan() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, r3 INT)";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "CREATE INDEX idx on t(r3 ASC)";
      statement.execute(query);

      query = "INSERT INTO t (SELECT i, 1, 1 FROM GENERATE_SERIES(1, 100) AS i)";
      statement.execute(query);

      {
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(1));

        query = "/*+Set(enable_hashagg false)*/ SELECT DISTINCT r3 FROM t WHERE r3 <= 10";
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "t");
        assertEquals(0, metrics.seekCount);

        metrics = assertFullDocDBFilter(statement, query, "idx");
        assertEquals(1, metrics.seekCount);
      }
    }
  }

  /**
   * DISTINCT pushdown must behave correctly even in the presence of non-index predicates
   * Guard against scenarios where we incorrectly de-duplicate rows that may be filtered out
   * later on
   * Here we consider a query where the predicate is pushed down to DocDB
   */
  @Test
  public void testDistinctRemoteFilter() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT 1, i, i FROM GENERATE_SERIES(1, 1000) AS i)";
      statement.execute(query);

      {
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(1));

        query = "/*+Set(enable_hashagg false)*/ " +
                "SELECT DISTINCT r1 FROM t WHERE v = 500 AND r1 <= 10";
        assertRowSet(statement, query, expectedRows);
      }
    }
  }

  /**
   * Here we consider a query where the predicate is local to postgres
   * and not pushed down to DocDB
   */
  @Test
  public void testDistinctLocalFilter() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, v INT, PRIMARY KEY(r1 ASC, r2 ASC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      query = "INSERT INTO t (SELECT 1, i, i FROM GENERATE_SERIES(1, 1000) AS i)";
      statement.execute(query);

      {
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(1));

        // The join clause is not pushed down to DocDB
        // Manually verified that the condition t1.v + t2.v = 500 is not pushed down
        // by looking at the request to DocDB
        query = "/*+Set(enable_hashagg false)*/ " +
                "SELECT DISTINCT t1.r1 FROM t AS t1, t AS t2 " +
                "WHERE t1.r1 <= 10 AND t2.r1 <= 10 AND t1.v + t2.v = 500";
        assertRowSet(statement, query, expectedRows);
      }
    }
  }

  /**
   * DISTINCT pushdown must behave correctly even in the presence of agg functions
   * Guard against scenarios where we incorrectly de-duplicate rows even before agg
   */
  @Test
  public void testDistinctAgg() throws Exception {
    String query = "CREATE TABLE t(r1 INT, r2 INT, PRIMARY KEY(r1 ASC, r2 ASC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      for (int r1 = 0; r1 < 2; r1++) {
        query = String.format(
          "INSERT INTO t (SELECT %d, i FROM GENERATE_SERIES(1, 1000) AS i)", r1);
        statement.execute(query);
      }

      // We refer to a system column `tableoid` to avoid a postgres optimization that
      // requests the complete tuple from lower layers. This optimization is not
      // necessarily accurate in the presence of remote storage layers such as DocDB
      {
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(1000));

        query = "/*+Set(enable_hashagg false)*/ " +
                "SELECT DISTINCT COUNT(tableoid) FROM t WHERE r1 <= 10 GROUP BY r1";
        assertRowSet(statement, query, expectedRows);
      }

      // Guard against any future changes to the above mentioned "optimization"
      {
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(1000));

        query = "/*+Set(enable_hashagg false)*/ " +
                "SELECT DISTINCT COUNT(r1) FROM t WHERE r1 <= 10 GROUP BY r1";
        assertRowSet(statement, query, expectedRows);
      }
    }
  }

  @Test
  public void testStrictInequalities() throws Exception {
    String query = "CREATE TABLE sample_table(h INT, r1 INT, r2 INT, r3 INT, " +
                   "v INT, PRIMARY KEY(h HASH, r1 ASC, r2 ASC, r3 DESC))";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);

      // v has values from 1 to 100000 and the other columns are
      // various digits of v as such
      // h    r1  r2  r3      v
      // 0    0   0   0       0
      // 0    0   0   1       1
      // ...
      // 12   4   9   3      12493
      // ...
      // 100  0   0   0      100000
      query = "INSERT INTO sample_table SELECT i/1000, (i/100)%10, " +
              "(i/10)%10, i%10, i FROM generate_series(1, 100000) i";
      statement.execute(query);

      Set<Row> allRows = new HashSet<>();
      for (int i = 1; i <= 100000; i++) {
        allRows.add(new Row(i/1000, (i/100)%10, (i/10)%10, i%10, i));
      }

      {
        // Select where hash code is specified and three range constraints
        query = "SELECT * FROM sample_table WHERE h = 1 AND " +
                "r1 IN (1,4,6)AND r2 < 3 AND r3 IN (1,3,5,7)";

        Set<Row> expectedRows = allRows.stream()
                                       .filter(r -> r.getInt(0) == 1 &&
                                               (r.getInt(1) == 1 ||
                                                r.getInt(1) == 4 ||
                                                r.getInt(1) == 6) &&
                                               r.getInt(2) < 3 &&
                                               (r.getInt(3) % 2 == 1) &&
                                               r.getInt(3) < 9 &&
                                               r.getInt(3) > 0)
                                       .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // There are m = 3 values of r1 to look at, n = 3 values of r2 and
        // p = 4 values of r3 to look at. For each (r1,r2) we seek to each
        // value of r3 along with (+Inf) to get to the next value of r2,
        // resulting in p + 1 seeks.
        // For each r1, there are n * (p+1) + 1 seeks. The +1 is needed
        // at the start of an r1 value to determine what r2 value to start
        // with using a seek to (r1, -Inf)
        // So there are m*(n*(p+1) + 1) = 48 seeks
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 48);
      }

      {
        // Select where hash code is specified and three range constraints
        query = "SELECT * FROM sample_table WHERE h = 1 AND "
                + "r1 IN (1,4,6) AND r2 < 5 AND r2 > 1 AND r3 IN (1,3,5,7)";

        Set<Row> expectedRows = allRows.stream()
                                       .filter(r -> r.getInt(0) == 1 &&
                                               (r.getInt(1) == 1 ||
                                                r.getInt(1) == 4 ||
                                                r.getInt(1) == 6) &&
                                               r.getInt(2) < 5 &&
                                               r.getInt(2) > 1 &&
                                               (r.getInt(3) % 2 == 1) &&
                                               r.getInt(3) < 9 &&
                                               r.getInt(3) > 0)
                                       .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // There are m = 3 values of r1 to look at, n = 3 values of r2 and
        // p = 4 values of r3 to look at. For each (r1,r2) we seek to each
        // value of r3 along with (+Inf) to get to the next value of r2,
        // resulting in p + 1 seeks.
        // For each r1, there are n * (p+1) + 1 seeks. The +1 is needed
        // at the start of an r1 value to determine what r2 value to start
        // with using a seek to (r1, 1, +Inf)
        // So there are m*(n*(p+1) + 1) = 48 seeks
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 48);
      }

      {
        // Select where hash code is specified and three range constraints
        query = "SELECT * FROM sample_table WHERE h = 1 AND r1 " +
                "IN (1,4,6) AND r2 < 5 AND r2 >= 1 AND r3 IN (1,3,5,7)";

        Set<Row> expectedRows = allRows.stream()
                                       .filter(r -> r.getInt(0) == 1 &&
                                               (r.getInt(1) == 1 ||
                                                r.getInt(1) == 4 ||
                                                r.getInt(1) == 6) &&
                                               r.getInt(2) < 5 &&
                                               r.getInt(2) >= 1 &&
                                               (r.getInt(3) % 2 == 1) &&
                                               r.getInt(3) < 9 &&
                                               r.getInt(3) > 0)
                                       .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // There are m = 3 values of r1 to look at, n = 4 values of r2 and
        // p = 4 values of r3 to look at. For each (r1,r2) we seek to each
        // value of r3 along with (+Inf) to get to the next value of r2,
        // resulting in p + 1 seeks.
        // For each r1, there are n * (p+1) seeks.
        // So there are m*(n*(p+1)) = 60 seeks
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 60);
      }

      {
        // Select where hash code is specified and three range constraints
        query = "SELECT * FROM sample_table WHERE h = 1 AND " +
                "r1 IN (1,4,6) AND r2 <= 5 AND r2 > 1 AND r3 IN (1,3,5,7)";

        Set<Row> expectedRows = allRows.stream()
                                       .filter(r -> r.getInt(0) == 1 &&
                                               (r.getInt(1) == 1 ||
                                                r.getInt(1) == 4 ||
                                                r.getInt(1) == 6) &&
                                               r.getInt(2) <= 5 &&
                                               r.getInt(2) > 1 &&
                                               (r.getInt(3) % 2 == 1) &&
                                               r.getInt(3) < 9 &&
                                               r.getInt(3) > 0)
                                       .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // There are m = 3 values of r1 to look at, n = 4 values of r2 and
        // p = 4 values of r3 to look at. For each (r1,r2) we seek to each
        // value of r3 along with (+Inf) to get to the next value of r2,
        // resulting in p + 1 seeks in most cases.
        // (Note: This extra + 1 doesn't occur when r2 = 5)
        // For each r1, there are n * (p+1) + 1 - n seeks. The + 1 is needed
        // at the start of an r1 value to determine what r2 value to start
        // with using a seek to (r1, 1, +Inf). The -n is to account for the
        // above note.
        // So there are m*(n*(p+1) + 1 - n) = 60 seeks
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 60);
      }

      {
        // Select where hash code is specified and three range constraints
        query = "SELECT * FROM sample_table WHERE h = 1 AND " +
                "r1 > 1 AND r1 < 6 AND r2 < 5 AND r2 > 1 AND " +
                "r3 > 4 AND r3 <= 8 ORDER BY r1 DESC, r2 DESC, r3 ASC";

        Set<Row> expectedRows = allRows.stream()
                                       .filter(r -> r.getInt(0) == 1 &&
                                               r.getInt(1) > 1 &&
                                               r.getInt(1) < 6 &&
                                               r.getInt(2) > 1 &&
                                               r.getInt(2) < 5 &&
                                               r.getInt(3) > 4 &&
                                               r.getInt(3) <= 8)
                                       .collect(Collectors.toSet());
        assertRowSet(statement, query, expectedRows);

        RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "sample_table");
        // There are m = 4 values of r1 to look at, n = 3 values of r2 and
        // p = 4 values of r3 to look at. For each (r1,r2) we seek to each
        // value of r3 along with (-Inf) to get to the next value of r2,
        // resulting in p + 1 seeks.
        // For each r1, there are n * (p+1) + 2 seeks.
        // The + 2 is needed for two special seeks.
        // One at the start of an r1 value to determine
        // what r2 value to start by using a seek to (r1, 1, +Inf)/
        // Another one at the end of each r1 value to determine the next
        // r1 value via a seek to (r1, -Inf)
        // There is also one seek at the beginning of the entire scan
        // to (6, -Inf) to determine the first r1 value
        // So there are m*(n*(p+1) + 2) + 1 = 69 seeks
        // Each seek during a reverse scan is implemented with two seeks,
        // so in total there are 69 * 2 = 138 seeks.
        // Actual number of seeks could be lower because there is Next instead of Seek optimisation
        // in DocDB (see SeekPossiblyUsingNext).
        assertLessThanOrEqualTo(metrics.seekCount, 138);
      }
    }
  }

  @Test
  public void testIsNotNull() throws Exception {
    String query = "CREATE TABLE sample_table(k INT PRIMARY KEY, v INT, v2 INT)";
    try (Statement statement = connection.createStatement()) {
      statement.execute(query);
      query = "CREATE INDEX sample_table_v_idx ON sample_table(v ASC)";
      statement.execute(query);

      // There are 10 rows with v IS NOT NULL, 100 rows with v IS NULL.
      query = "INSERT INTO sample_table SELECT i, NULL, i + 1000 FROM generate_series(1, 100) i";
      statement.execute(query);
      query = "INSERT INTO sample_table SELECT i, i, i + 1000 FROM generate_series(101, 110) i";
      statement.execute(query);

      query = "SELECT v2 FROM sample_table WHERE v IS NOT NULL";
      Set<Row> expectedRows = new HashSet<>();
      for (int i = 101; i <= 110; ++i) {
        expectedRows.add(new Row(i + 1000));
      }
      assertRowSet(statement, query, expectedRows);

      RocksDBMetrics metrics = assertFullDocDBFilter(statement, query, "sample_table");
      // The index on sample_table means that each IS NOT NULL row can be found by key,
      // but the "SELECT v2" query requires referencing sample_table for each of these
      // rows. Running an experiment without pushdown code, the number of seeks is 113
      // (regardless of whether max_nexts_to_avoid_seek is set to 0, though nextCount
      // does change in that case). With pushdown code, it's 13. Setting the assert to be
      // below 26 (2*13) to allow for some extra seeks to occur without being flaky, but
      // still likely to catch the pushdown failing.
      assertLessThanOrEqualTo(metrics.seekCount, 26);

      // Now do the same thing with a descending index.
      query = "DROP INDEX sample_table_v_idx";
      statement.execute(query);
      query = "CREATE INDEX sample_table_v_idx ON sample_table(v DESC)";
      statement.execute(query);

      query = "SELECT v2 FROM sample_table WHERE v IS NOT NULL";
      assertRowSet(statement, query, expectedRows);

      metrics = assertFullDocDBFilter(statement, query, "sample_table");
      // Same numbers of seeks between code with/without pushdown for a descending index.
      assertLessThanOrEqualTo(metrics.seekCount, 26);

      // To ensure nothing breaks in the future, force an index scan using a hash index.
      query = "DROP INDEX sample_table_v_idx";
      statement.execute(query);
      query = "CREATE INDEX sample_table_v_idx ON sample_table(v HASH)";
      statement.execute(query);

      query = "/*+Set(enable_seqscan false)*/ SELECT v FROM sample_table WHERE v IS NOT NULL";
      expectedRows = new HashSet<>();
      for (int i = 101; i <= 110; ++i) {
        expectedRows.add(new Row(i));
      }
      assertRowSet(statement, query, expectedRows);
      // In practice, only sample_table_v_idx is touched. Its seek count is 3, and its
      // next count is 110. However, this part of the test is only about making sure the
      // results aren't broken, so there is no assertion on performance.
    }
  }

  @Test
  public void testInequalitiesRangePartitioned() throws Exception {
      String query = "CREATE TABLE sample (key int, val int, primary key(key asc) ) " +
                     "SPLIT AT VALUES ((65535), (2000000000), (2100000000) )";
      try (Statement statement = connection.createStatement()) {
        statement.execute(query);

        // Insert stuff into the table
        statement.execute("INSERT INTO sample VALUES(1,1)");
        statement.execute("INSERT INTO sample VALUES(60000,60000)");
        statement.execute("INSERT INTO sample VALUES(120000,120000)");
        statement.execute("INSERT INTO sample VALUES(150000,150000)");
        statement.execute("INSERT INTO sample VALUES(2000000001,2000000001)");
        statement.execute("INSERT INTO sample VALUES(2000000005,2000000005)");


        //     key     |    val
        // ------------+------------        ________
        //           1 |          1         |Tablet|
        //       60000 |      60000         |___1__|
        // ___________________________
        //                                  ________
        //      120000 |     120000         |Tablet|
        //      150000 |     150000         |___2__|
        // ____________________________
        //                                  ________
        //  2000000001 | 2000000001         |Tablet|
        //  2000000005 | 2000000005         |___3__|
        // ____________________________
        //                                  ________
        //             |                    |Tablet|
        //             |                    |___4__|

        // Test 1
        // When the same qualifying conditions that fits within 4 byte integers are passed as int
        // and bigint, both ends up being pushed in to docDB since they are both lesser than 4 byte
        // integer values.
        query = "SELECT * FROM sample WHERE key ";

        // Num requests are 1 as it just searches tablet 1.
        assertTrue(getNumDocdbRequests(statement, query + "< 65534") == 1);

        // Test 2
        assertTrue(getNumDocdbRequests(statement, query + "< 65534::bigint") == 1);

        // Test 3
        // 2147483648 is an actual bigint value. Hence, we end up perfroming a scan on all the
        // tablets as we cannot push an actual bigint value to an integer column. Though the number
        // of rows returned are equal when the qualifying condition is 2147483648 as compared to
        // 20999999999, the former condition ends up scanning all the 4 tablets while the later
        // condition scans just 3 tablets.
        assertTrue(getNumDocdbRequests(statement, query + "< 2147483648") == 4);

        // Test 4
        assertTrue(getNumDocdbRequests(statement, query + "< 2099999999") == 3);
    }
  }

  @Test
  public void testINQueriesRangePartitioned() throws Exception {

      // Creating a table with the same schema that is created for the previous test
      // testInequalitiesRangePartitioned.
      String query = "CREATE TABLE sample (key int, val int, primary key(key asc) ) " +
                     "SPLIT AT VALUES ((65535), (2000000000), (2100000000) )";
      try (Statement statement = connection.createStatement()) {
        statement.execute(query);

        statement.execute("INSERT INTO sample VALUES(1,1)");
        statement.execute("INSERT INTO sample VALUES(60000,60000)");
        statement.execute("INSERT INTO sample VALUES(120000,120000)");
        statement.execute("INSERT INTO sample VALUES(150000,150000)");
        statement.execute("INSERT INTO sample VALUES(2000000001,2000000001)");
        statement.execute("INSERT INTO sample VALUES(2000000005,2000000005)");

        query = "SELECT * FROM sample WHERE key ";

        // Test 5
        // Remove IN queries that contain values that are out of bounds.
        // All the elements present in the IN list are out of the 32 bit integer range. Hence, they
        // should not be pushed down as a part of search array. Subsequently the number of RPCs
        // should be 0.
        assertTrue(
          getNumDocdbRequests(statement, query + "IN (3000000005, 3000000006, 3000000007)") == 0);

        // Test 6
        // Fails with the following error in prior to this diff
        // expected:<[Row[java.lang.Integer::1,java.lang.Integer::1]]> but was:<[]>
        //
        // This happens because the docDB RPC that is sent for the following request tries to push
        // 3000000005 down. However, 3000000005 does not fit into a 4-byte integer and hence it is
        // overflown to -1294967291.
        // DocDB batches IN queries on range keys. It also expects that the list of search keys as a
        // part of the search array in the IN queries are sorted and hence chooses the first element
        // as the lower bound and the last element as the upper bound. In this scenario, -1294967291
        // is the upper bound and 1 is the lower bound. There are no elements between these values
        // and hence docDB returns 0 rows.
        Set<Row> expectedRows = new HashSet<>();
        expectedRows.add(new Row(1, 1));
        assertRowSet(statement, query + "IN (1, 3000000005)", expectedRows);
    }
  }

  @Test
  public void testFilteringUsingIN() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test(h int, r int, v int, PRIMARY KEY (h, r))");

      statement.execute("INSERT INTO test (h,r,v) values (1,1,1)");
      statement.execute("INSERT INTO test (h,r,v) values (1,2,2)");
      statement.execute("INSERT INTO test (h,r,v) values (1,3,3)");
      statement.execute("INSERT INTO test (h,r,v) values (2,1,2)");
      statement.execute("INSERT INTO test (h,r,v) values (2,2,3)");
      statement.execute("INSERT INTO test (h,r,v) values (2,3,1)");
      statement.execute("INSERT INTO test (h,r,v) values (3,1,3)");
      statement.execute("INSERT INTO test (h,r,v) values (3,2,1)");
      statement.execute("INSERT INTO test (h,r,v) values (3,3,2)");

      // Filter by hash & range columns.
      assertQuery(statement, "SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2)",
          new Row(1, 1, 1),
          new Row(1, 2, 2),
          new Row(2, 1, 2),
          new Row(2, 2, 3));
      // Filter by hash & non-key columns.
      assertQuery(statement, "SELECT * FROM test WHERE h IN (1,2) AND v IN (1,2)",
          new Row(1, 1, 1),
          new Row(1, 2, 2),
          new Row(2, 1, 2),
          new Row(2, 3, 1));
      assertQuery(statement, "SELECT * FROM test WHERE h IN (1,2) AND v = 2",
          new Row(1, 2, 2),
          new Row(2, 1, 2));
      // Filter by range & non-key columns.
      assertQuery(statement, "SELECT * FROM test WHERE r IN (1,2) AND v IN (1,2)",
          new Row(1, 1, 1),
          new Row(1, 2, 2),
          new Row(2, 1, 2),
          new Row(3, 2, 1));
      assertQuery(statement, "SELECT * FROM test WHERE r IN (1,2) AND v = 2",
          new Row(1, 2, 2),
          new Row(2, 1, 2));
      // Filter by hash & range & non-key columns.
      assertQuery(statement, "SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2) AND v IN (1,2)",
          new Row(1, 1, 1),
          new Row(1, 2, 2),
          new Row(2, 1, 2));
      assertQuery(statement, "SELECT * FROM test WHERE h IN (1,2) AND r IN (1,2) AND v = 2",
          new Row(1, 2, 2),
          new Row(2, 1, 2));
    }
  }

}
