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

import java.math.BigDecimal;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;
import java.util.stream.Collectors;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import org.junit.runner.RunWith;

import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgSelect extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSelect.class);

  @Test
  public void testWhereClause() throws Exception {
    Set<Row> allRows = setupSimpleTable("test_where");
    try (Statement statement = connection.createStatement()) {
      // Test no where clause -- select all rows.
      String query = "SELECT * FROM test_where";
      try (ResultSet rs = statement.executeQuery(query)) {
        assertEquals(allRows, getRowSet(rs));
      }
      assertFalse(needsPgFiltering(query));

      // Test fixed hash key.
      query = "SELECT * FROM test_where WHERE h = 2";
      try (ResultSet rs = statement.executeQuery(query)) {
        Set<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L))
            .collect(Collectors.toSet());
        assertEquals(10, expectedRows.size());
        assertEquals(expectedRows, getRowSet(rs));
      }
      assertFalse(needsPgFiltering(query));

      // Test fixed primary key.
      query = "SELECT * FROM test_where WHERE h = 2 AND r = 3.5";
      try (ResultSet rs = statement.executeQuery(query)) {
        Set<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L) &&
                row.getDouble(1).equals(3.5))
            .collect(Collectors.toSet());
        assertEquals(1, expectedRows.size());
        assertEquals(expectedRows, getRowSet(rs));
      }
      assertFalse(needsPgFiltering(query));

      // Test fixed range key without fixed hash key.
      query = "SELECT * FROM test_where WHERE r = 6.5";
      try (ResultSet rs = statement.executeQuery(query)) {
        Set<Row> expectedRows = allRows.stream()
            .filter(row -> row.getDouble(1).equals(6.5))
            .collect(Collectors.toSet());
        assertEquals(10, expectedRows.size());
        assertEquals(expectedRows, getRowSet(rs));
      }
      assertTrue(needsPgFiltering(query));

      // Test range scan.
      query = "SELECT * FROM test_where WHERE h = 2 AND r >= 3.5 AND r < 8.5";
      try (ResultSet rs = statement.executeQuery(query)) {
        Set<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L) &&
                row.getDouble(1) >= 3.5 &&
                row.getDouble(1) < 8.5)
            .collect(Collectors.toSet());
        assertEquals(5, expectedRows.size());
        assertEquals(expectedRows, getRowSet(rs));
      }
      assertTrue(needsPgFiltering(query));

      // Test conditions on regular (non-primary-key) columns.
      query = "SELECT * FROM test_where WHERE vi < 14 AND vs != 'v09'";
      try (ResultSet rs = statement.executeQuery(query)) {
        Set<Row> expectedRows = allRows.stream()
            .filter(row -> row.getInt(2) < 14 &&
                !row.getString(3).equals("v09"))
            .collect(Collectors.toSet());
        // 14 options (for hash key) minus [9,'v09'].
        assertEquals(13, expectedRows.size());
        assertEquals(expectedRows, getRowSet(rs));
      }
      assertTrue(needsPgFiltering(query));

      // Test other WHERE operators (IN, OR, LIKE).
      query = "SELECT * FROM test_where WHERE h IN (2,3) OR vs LIKE 'v_2'";
      try (ResultSet rs = statement.executeQuery(query)) {
        Set<Row> expectedRows = allRows.stream()
            .filter(row -> row.getLong(0).equals(2L) ||
                row.getLong(0).equals(3L) ||
                row.getString(3).matches("v.2"))
            .collect(Collectors.toSet());
        // 20 plus 10 options but 2 common ones ('v22' and 'v32').
        assertEquals(28, expectedRows.size());
        assertEquals(expectedRows, getRowSet(rs));
      }
      assertTrue(needsPgFiltering(query));
    }
  }

  @Test
  public void testSelectTargets() throws SQLException {
    Set<Row> allRows = setupSimpleTable("test_target");
    Statement statement = connection.createStatement();

    // Test all columns -- different order.
    try (ResultSet rs = statement.executeQuery("SELECT vs,vi,r,h FROM test_target")) {
      Set<Row> expectedRows = allRows.stream()
          .map(row -> new Row(row.get(3), row.get(2), row.get(1), row.get(0)))
          .collect(Collectors.toSet());
      assertEquals(expectedRows, getRowSet(rs));
    }

    // Test partial columns -- different order.
    try (ResultSet rs = statement.executeQuery("SELECT vs,r FROM test_target")) {
      Set<Row> expectedRows = allRows.stream()
          .map(row -> new Row(row.get(3), row.get(1)))
          .collect(Collectors.toSet());
      assertEquals(expectedRows, getRowSet(rs));
    }

    // Test aggregates.
    assertOneRow("SELECT avg(r) FROM test_target", 5.0D);
    assertOneRow("SELECT count(*) FROM test_target", 100L);

    // Test distinct.
    try (ResultSet rs = statement.executeQuery("SELECT distinct(h) FROM test_target")) {
      Set<Row> expectedRows = allRows.stream()
          .map(row -> new Row(row.get(0)))
          .distinct()
          .collect(Collectors.toSet());
      assertEquals(expectedRows, getRowSet(rs));
    }

    // Test selecting non-existent column.
    runInvalidQuery(statement, "SELECT v FROM test_target");

    // Test mistyped function.
    runInvalidQuery(statement, "SELECT vs * r FROM test_target");
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
          "ON (a.h = b.h and a.r = b.r) WHERE a.h = 1 AND a.r IN (2.5, 3.5)";
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
}
