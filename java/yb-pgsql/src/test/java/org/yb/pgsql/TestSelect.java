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
public class TestSelect extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgWrapper.class);

  Set<Row> getRowSet(ResultSet rs) throws SQLException {
    Set<Row> rows = new HashSet<>();
    while (rs.next()) {
      Object[] elems = new Object[rs.getMetaData().getColumnCount()];
      for (int i = 0; i < elems.length; i++) {
        elems[i] = rs.getObject(i + 1); // Column index starts from 1.
      }
      rows.add(new Row(elems));
    }
    return rows;
  }

  @Test
  public void testWhereClause() throws Exception {
    Set<Row> allRows = setupSimpleTable("test");
    Statement statement = connection.createStatement();

    // Test no where clause -- select all rows.
    try (ResultSet rs = statement.executeQuery("SELECT * FROM test")) {
      assertEquals(allRows, getRowSet(rs));
    }

    // Test fixed hash key.
    try (ResultSet rs = statement.executeQuery("SELECT * FROM test WHERE h = 2")) {
      Set<Row> expected_rows = allRows.stream()
                                       .filter(row -> row.getLong(0).equals(2L))
                                       .collect(Collectors.toSet());
      assertEquals(10, expected_rows.size());
      assertEquals(expected_rows, getRowSet(rs));
    }

    // Test fixed primary key.
    try (ResultSet rs = statement.executeQuery("SELECT * FROM test WHERE h = 2 AND r = 3.5")) {
      Set<Row> expected_rows = allRows.stream()
                                       .filter(row -> row.getLong(0).equals(2L) &&
                                                      row.getDouble(1).equals(3.5))
                                       .collect(Collectors.toSet());
      assertEquals(1, expected_rows.size());
      assertEquals(expected_rows, getRowSet(rs));
    }

    // Test range scan.
    try (ResultSet rs = statement.executeQuery("SELECT * FROM test " +
                                                   "WHERE h = 2 AND r >= 3.5 AND r < 8.5")) {
      Set<Row> expected_rows = allRows.stream()
                                       .filter(row -> row.getLong(0).equals(2L) &&
                                                      row.getDouble(1) >= 3.5 &&
                                                      row.getDouble(1) < 8.5)
                                       .collect(Collectors.toSet());
      assertEquals(5, expected_rows.size());
      assertEquals(expected_rows, getRowSet(rs));
    }

    // Test conditions on regular (non-primary-key) columns.
    try (ResultSet rs =
             statement.executeQuery("SELECT * FROM test WHERE vi < 14 AND vs != 'v09'")) {
      Set<Row> expected_rows = allRows.stream()
                                       .filter(row -> row.getInt(2) < 14 &&
                                                      !row.getString(3).equals("v09"))
                                       .collect(Collectors.toSet());
      // 14 options (for hash key) minus [9,'v09'].
      assertEquals(13, expected_rows.size());
      assertEquals(expected_rows, getRowSet(rs));
    }

    // Test other WHERE operators (IN, OR, LIKE).
    try (ResultSet rs =
             statement.executeQuery("SELECT * FROM test WHERE h IN (2,3) OR vs LIKE 'v_2'")) {
      Set<Row> expected_rows = allRows.stream()
                                       .filter(row -> row.getLong(0).equals(2L) ||
                                                      row.getLong(0).equals(3L) ||
                                                      row.getString(3).matches("v.2"))
                                       .collect(Collectors.toSet());
      // 20 plus 10 options but 2 common ones ('v22' and 'v32').
      assertEquals(28, expected_rows.size());
      assertEquals(expected_rows, getRowSet(rs));
    }
    statement.close();
  }

  @Test
  public void testSelectTargets() throws SQLException {
    Set<Row> allRows = setupSimpleTable("test");
    Statement statement = connection.createStatement();

    // Test all columns -- different order.
    try (ResultSet rs = statement.executeQuery("SELECT vs,vi,r,h FROM test")) {
      Set<Row> expected_rows = allRows.stream()
          .map(row -> new Row(row.get(3), row.get(2), row.get(1), row.get(0)))
          .collect(Collectors.toSet());
      assertEquals(expected_rows, getRowSet(rs));
    }

    // Test partial columns -- different order.
    try (ResultSet rs = statement.executeQuery("SELECT vs,r FROM test")) {
      Set<Row> expected_rows = allRows.stream()
          .map(row -> new Row(row.get(3), row.get(1)))
          .collect(Collectors.toSet());
      assertEquals(expected_rows, getRowSet(rs));
    }

    // Test aggregates.
    try (ResultSet rs = statement.executeQuery("SELECT avg(r) FROM test")) {
      assertTrue(rs.next());
      assertEquals(5.0, rs.getDouble(1));
      assertFalse(rs.next());
    }

    // Test distinct.
    try (ResultSet rs = statement.executeQuery("SELECT distinct(h) FROM test")) {
      Set<Row> expected_rows = allRows.stream()
          .map(row -> new Row(row.get(0)))
          .distinct()
          .collect(Collectors.toSet());
      assertEquals(expected_rows, getRowSet(rs));
    }
  }


  @Test
  public void testOrderBy() throws Exception {
    setupSimpleTable("test");
    Statement statement = connection.createStatement();

    try (ResultSet rs = statement.executeQuery("SELECT h, r FROM test " +
                                                   "ORDER BY r ASC, h DESC")) {
      for (double r = 0.5; r < 10.5; r += 1) {
        for (long h = 9; h >= 0; h--) {
          assertTrue(rs.next());
          assertEquals(h, rs.getLong("h"));
          assertEquals(r, rs.getDouble("r"));
        }
      }
    }
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

      try (ResultSet rs = statement.executeQuery("SELECT a.h, a.r, a.v as av, b.v as bv " +
                                                "FROM t1 a LEFT JOIN t2 b " +
                                                "ON (a.h = b.h and a.r = b.r) " +
                                                "WHERE a.h = 1 AND a.r IN (2.5, 3.5)")) {

        assertTrue(rs.next());
        assertEquals(1, rs.getLong("h"));
        assertEquals(2.5, rs.getDouble("r"));
        assertEquals("abc", rs.getString("av"));
        assertEquals("foo", rs.getString("bv"));

        assertTrue(rs.next());
        assertEquals(1, rs.getLong("h"));
        assertEquals(3.5, rs.getDouble("r"));
        assertEquals("def", rs.getString("av"));
        assertEquals(null, rs.getString("bv"));
        assertTrue(rs.wasNull());

        assertFalse(rs.next());
      }
    }
  }

  @Test
  public void testExpressions() throws Exception {
    try (Statement statement = connection.createStatement()) {
      createSimpleTable("test");

      // Test expressions in INSERT values.
      statement.execute("INSERT INTO test(h, r, vi, vs) " +
                            "VALUES (floor(1 + 1.5), log(3, 27), ceil(pi()), 'ab' || 'c')");
      try (ResultSet rs = statement.executeQuery("SELECT * FROM test")) {

        assertTrue(rs.next());
        assertEquals(2, rs.getLong("h"));
        assertEquals(3.0, rs.getDouble("r"));
        assertEquals(4, rs.getInt("vi"));
        assertEquals("abc", rs.getString("vs"));

        assertFalse(rs.next());
      }

      // Test expressions in SELECT targets.
      try (ResultSet rs = statement.executeQuery("SELECT h + 1.5, pow(r, 2), vi * h, 7 " +
                                                     "FROM test WHERE h = 2")) {

        assertTrue(rs.next());
        assertEquals(3.5, rs.getDouble(1));
        assertEquals(9.0, rs.getDouble(2));
        assertEquals(8, rs.getLong(3));
        assertEquals(7, rs.getInt(4));
        assertFalse(rs.next());
      }

      // Test expressions in SELECT WHERE clause.
      try (ResultSet rs = statement.executeQuery("SELECT * FROM test WHERE h + r <= 10 AND " +
                                                     "substring(vs from 2) = 'bc'")) {

        assertTrue(rs.next());
        assertEquals(2, rs.getLong("h"));
        assertEquals(3.0, rs.getDouble("r"));
        assertEquals(4, rs.getInt("vi"));
        assertEquals("abc", rs.getString("vs"));

        assertFalse(rs.next());
      }
    }
  }

  private void createSimpleTable(String tableName) throws SQLException {
    try (Statement statement = connection.createStatement()) {
      String sql =
          "CREATE TABLE " + tableName + "(h bigint, r float, vi int, vs text, PRIMARY KEY (h, r))";
      LOG.info("Creating table " + tableName + ", SQL statement: " + sql);
      statement.execute(sql);
      LOG.info("Table creation finished: " + tableName);
    }
  }

  private Set<Row> setupSimpleTable(String tableName) throws SQLException {
    Set<Row> allRows = new HashSet<>();
    try (Statement statement = connection.createStatement()) {
      createSimpleTable(tableName);
      String insertTemplate = "INSERT INTO test(h, r, vi, vs) VALUES (%d, %f, %d, '%s')";

      for (int h = 0; h < 10; h++) {
        for (int r = 0; r < 10; r++) {
          statement.execute(String.format(insertTemplate, h, r + 0.5, h * 10 + r, "v" + h + r));
          allRows.add(new Row(new Long(h),
                              new Double(r + 0.5),
                              new Integer(h * 10 + r),
                              "v" + h + r));
        }
      }
    }
    return allRows;
  }
}
