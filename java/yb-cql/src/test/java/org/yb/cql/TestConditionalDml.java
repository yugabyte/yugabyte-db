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
package org.yb.cql;

import com.datastax.driver.core.*;

import org.junit.Test;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestConditionalDml extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestConditionalDml.class);

  // Assert that the specified row (h1, h2, r1, r2, v1, v2) exists.
  void assertRow(int h1, String h2, int r1, String r2, int v1, String v2) {
    String select_stmt = String.format(
        "SELECT v1, v2 FROM t WHERE h1 = %d AND h2 = '%s' AND r1 = %d AND r2 = '%s';",
        h1, h2, r1, r2);
    ResultSet rs = session.execute(select_stmt);
    Row row = rs.one();
    assertNotNull(row);
    assertEquals(v1, row.getInt("v1"));
    assertEquals(v2, row.getString("v2"));
    assertNull(rs.one());
  }

  @Test
  public void testSimpleDelete() throws Exception {
    LOG.info("TEST SIMPLE DELETE - Start");

    // Setup table
    setupTable("t", 0 /* num_rows */);

    // Insert one row.
    {
      String insert_stmt =
          "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c');";
      ResultSet rs = session.execute(insert_stmt);
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test deleting IF EXISTS a row that does not exist.
    String delete_stmt =
        "DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'c' IF EXISTS;";
    {
      // Expected not applied. Verify "[applied]" column is returned.
      ResultSet rs = session.execute(delete_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertFalse(rs.wasApplied());

      // Expect the row to exist.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test deleting a row that exists on condition only IF EXISTS and v2 <> 'c'
    delete_stmt =
        "DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND v2 <> 'c';";
    {
      // Expected not applied. Verify "[applied]" column is returned.
      ResultSet rs = session.execute(delete_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(6, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.cint(), cols.getType("h1"));
      assertEquals(DataType.varchar(), cols.getType("h2"));
      assertEquals(DataType.cint(), cols.getType("r1"));
      assertEquals(DataType.varchar(), cols.getType("r2"));
      assertEquals(DataType.varchar(), cols.getType("v2"));
      assertFalse(rs.wasApplied());
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(2, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals("c", row.getString("v2"));

      // Expect the row unchanged.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test deleting a row that exists on condition only IF EXISTS and either of 2 values, and
    // using paranthesis.
    delete_stmt =
        "DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND (v1 = 3 OR v1 = 4);";
    {
      // Expected applied.
      ResultSet rs = session.execute(delete_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect v1 deleted.
      String select_stmt =
          "SELECT v1, v2 FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b';";
      rs = session.execute(select_stmt);
      Row row = rs.one();
      assertNull(row);
    }

    // Tear down table
    dropTable("t");

    LOG.info("TEST SIMPLE DELETE - End");
  }

  @Test
  public void testSimpleInsert() throws Exception {
    LOG.info("TEST SIMPLE INSERT - Start");

    // Setup table
    setupTable("t", 0 /* num_rows */);

    // Test inserting IF NOT EXISTS a row that does not exist.
    String insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c') IF NOT EXISTS;";
    {
      // Expected applied. Verify "[applied]" column is returned.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row to exist.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test inserting IF NOT EXISTS same row again that exists now.
    {
      ResultSet rs = session.execute(insert_stmt);
      // Expected not applied.
      assertFalse(rs.wasApplied());
      // Expect the same row to exist.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test inserting same row with different value on condition only IF NOT EXISTS OR v1 <> 3.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') " +
        "IF NOT EXISTS OR v1 <> 3;";
    {
      // Expected not applied. Verify "[applied]" and "v1" (current value 3) are returned.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(6, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.cint(), cols.getType("h1"));
      assertEquals(DataType.varchar(), cols.getType("h2"));
      assertEquals(DataType.cint(), cols.getType("r1"));
      assertEquals(DataType.varchar(), cols.getType("r2"));
      assertEquals(DataType.cint(), cols.getType("v1"));
      assertFalse(rs.wasApplied());
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(2, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(3, row.getInt("v1"));

      // Expect the row to exist.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test inserting same row with different value on condition only IF NOT EXISTS OR v1 < 3.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') " +
        "IF NOT EXISTS OR v1 < 3;";
    {
      // Expected not applied. Verify "[applied]" and "v1" (current value 3) are returned.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(6, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.cint(), cols.getType("h1"));
      assertEquals(DataType.varchar(), cols.getType("h2"));
      assertEquals(DataType.cint(), cols.getType("r1"));
      assertEquals(DataType.varchar(), cols.getType("r2"));
      assertEquals(DataType.cint(), cols.getType("v1"));
      assertFalse(rs.wasApplied());
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(2, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(3, row.getInt("v1"));

      // Expect the row to exist.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test inserting same row with different value again on condition only IF NOT EXISTS OR v1 = 3
    // this time.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') " +
        "IF NOT EXISTS OR v1 = 3;";
    {
      // Expected applied.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertTrue(rs.wasApplied());

      // Expect the row with new value.
      assertRow(1, "a", 2, "b", 4, "d");
    }

    // Test inserting same row with different value again on condition only IF NOT EXISTS OR v1 <= 4
    // this time.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 5, 'e') " +
        "IF NOT EXISTS OR v1 <= 4;";
    {
      // Expected applied.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertTrue(rs.wasApplied());

      // Expect the row with new value.
      assertRow(1, "a", 2, "b", 5, "e");
    }

    // Tear down table
    dropTable("t");

    LOG.info("TEST SIMPLE INSERT - End");
  }

  @Test
  public void testSimpleUpdate() throws Exception {
    LOG.info("TEST SIMPLE UPDATE - Start");

    // Setup table
    setupTable("t", 0 /* num_rows */);

    // Insert one row.
    {
      String insert_stmt =
          "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c');";
      ResultSet rs = session.execute(insert_stmt);
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test updating IF NOT EXISTS a row that exists.
    String update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS;";
    {
      // Expected not applied. Verify "[applied]" column is returned.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(5, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.cint(), cols.getType("h1"));
      assertEquals(DataType.varchar(), cols.getType("h2"));
      assertEquals(DataType.cint(), cols.getType("r1"));
      assertEquals(DataType.varchar(), cols.getType("r2"));
      assertFalse(rs.wasApplied());
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(2, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));

      // Expect the row to exist.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test updating a row that exists on condition only IF NOT EXISTS or v1 != 3 AND v2 != 'c'
    update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS OR v1 != 3 AND v2 != 'c';";
    {
      // Expected not applied. Verify "[applied]", v1 and v2 columns are returned. Note that
      // we don't g
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(7, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.cint(), cols.getType("h1"));
      assertEquals(DataType.varchar(), cols.getType("h2"));
      assertEquals(DataType.cint(), cols.getType("r1"));
      assertEquals(DataType.varchar(), cols.getType("r2"));
      assertEquals(DataType.cint(), cols.getType("v1"));
      assertEquals(DataType.varchar(), cols.getType("v2"));
      assertFalse(rs.wasApplied());
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(2, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(3, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));

      // Expect the row unchanged.
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test updating a row that exists on condition only IF NOT EXISTS or v1 = 3 and v2 = 'c'
    update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS OR v1 = 3 AND v2 = 'c';";
    {
      // Expected applied.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row unchanged.
      assertRow(1, "a", 2, "b", 4, "d");
    }

    // Test updating a row that exists on condition only IF NOT EXISTS or v1 > 4 and v2 = 'c'
    update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS OR v1 > 4 AND v2 = 'c';";
    {
      // Expected not applied.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertFalse(rs.wasApplied());
      Row row = rs.one();
      assertEquals(4, row.getInt("v1"));
      assertEquals("d", row.getString("v2"));

      // Expect the row unchanged.
      assertRow(1, "a", 2, "b", 4, "d");
    }

    // Test updating a row that exists on condition only IF EXISTS and either of 2 values.
    update_stmt =
        "UPDATE t SET v1 = 5, v2 = 'e' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND (v1 = 3 OR v1 = 4);";
    {
      // Expected applied.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row update.
      assertRow(1, "a", 2, "b", 5, "e");
    }

    // Test updating a row that exists on condition only IF EXISTS and >= a value.
    update_stmt =
        "UPDATE t SET v1 = 6, v2 = 'f' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND v1 >= 5;";
    {
      // Expected applied.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row update.
      assertRow(1, "a", 2, "b", 6, "f");
    }

    // Tear down table
    dropTable("t");

    LOG.info("TEST SIMPLE UPDATE - End");
  }

  @Test
  public void testPrimaryKeyColumnInIfClause() throws Exception {
    LOG.info("TEST PRIMARY KEY COLUMN IN IF CLAUSE - Start");

    // Setup table
    setupTable("t", 0 /* num_rows */);

    // Invalid cases: primary key columns not allowed in if clause.
    runInvalidStmt("UPDATE t SET v1 = 4, v2 = 'd' " +
                   "WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' "+
                   "IF h1 = 1;");
    runInvalidStmt("UPDATE t SET v1 = 4, v2 = 'd' " +
                   "WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' "+
                   "IF r1 = 1;");
    runInvalidStmt("DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' "+
                   "IF h1 = 1;");
    runInvalidStmt("DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' "+
                   "IF r1 = 1;");

    LOG.info("TEST PRIMARY KEY COLUMN IN IF CLAUSE - End");
  }

  @Test
  public void testElseErrorDelete() throws Exception {
    LOG.info("TEST SIMPLE DELETE - Start");

    // Setup table
    setupTable("t", 0 /* num_rows */);

    // Insert one row.
    {
      String insert_stmt =
          "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c');";
      ResultSet rs = session.execute(insert_stmt);
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test deleting IF EXISTS a row that does not exist.
    String delete_stmt =
        "DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'c' IF EXISTS ELSE ERROR;";
    runInvalidQuery(delete_stmt);
    assertRow(1, "a", 2, "b", 3, "c");

    // Test deleting a row that exists on condition only IF EXISTS and v2 <> 'c'
    delete_stmt =
        "DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND v2 <> 'c' ELSE ERROR;";
    runInvalidQuery(delete_stmt);
    assertRow(1, "a", 2, "b", 3, "c");

    // Test deleting a row that exists on condition only IF EXISTS and either of 2 values, and
    // using paranthesis.
    delete_stmt =
        "DELETE FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND (v1 = 3 OR v1 = 4) ELSE ERROR;";
    {
      // Expected applied.
      ResultSet rs = session.execute(delete_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect v1 deleted.
      String select_stmt =
          "SELECT v1, v2 FROM t WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b';";
      rs = session.execute(select_stmt);
      Row row = rs.one();
      assertNull(row);
    }

    // Tear down table
    dropTable("t");

    LOG.info("TEST SIMPLE DELETE - End");
  }

  @Test
  public void testElseErrorInsert() throws Exception {
    LOG.info("TEST SIMPLE INSERT - Start");

    // Setup table
    setupTable("t", 0 /* num_rows */);

    // Test inserting IF NOT EXISTS a row that does not exist.
    String insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c') " +
        "IF NOT EXISTS ELSE ERROR;";
    {
      // Expected applied. Verify "[applied]" column is returned.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row to exist.
      assertRow(1, "a", 2, "b", 3, "c");
    }
    runInvalidQuery(insert_stmt);
    assertRow(1, "a", 2, "b", 3, "c");

    // Test inserting same row with different value on condition only IF NOT EXISTS OR v1 <> 3.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') " +
        "IF NOT EXISTS OR v1 <> 3 ELSE ERROR;";
    runInvalidQuery(insert_stmt);

    // Test inserting same row with different value on condition only IF NOT EXISTS OR v1 < 3.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') " +
        "IF NOT EXISTS OR v1 < 3 ELSE ERROR;";
    runInvalidQuery(insert_stmt);
    assertRow(1, "a", 2, "b", 3, "c");

    // Test inserting same row with different value again on condition only IF NOT EXISTS OR v1 = 3
    // this time.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 4, 'd') " +
        "IF NOT EXISTS OR v1 = 3 ELSE ERROR;";
    {
      // Expected applied.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertTrue(rs.wasApplied());

      // Expect the row with new value.
      assertRow(1, "a", 2, "b", 4, "d");
    }

    // Test inserting same row with different value again on condition only IF NOT EXISTS OR v1 <= 4
    // this time.
    insert_stmt =
        "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 5, 'e') " +
        "IF NOT EXISTS OR v1 <= 4 ELSE ERROR;";
    {
      // Expected applied.
      ResultSet rs = session.execute(insert_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertTrue(rs.wasApplied());

      // Expect the row with new value.
      assertRow(1, "a", 2, "b", 5, "e");
    }

    // Tear down table
    dropTable("t");

    LOG.info("TEST SIMPLE INSERT - End");
  }

  @Test
  public void testElseErrorUpdate() throws Exception {
    LOG.info("TEST SIMPLE UPDATE - Start");

    // Setup table
    setupTable("t", 0 /* num_rows */);

    // Insert one row.
    {
      String insert_stmt =
          "INSERT INTO t (h1, h2, r1, r2, v1, v2) VALUES (1, 'a', 2, 'b', 3, 'c');";
      ResultSet rs = session.execute(insert_stmt);
      assertRow(1, "a", 2, "b", 3, "c");
    }

    // Test updating IF NOT EXISTS a row that exists.
    String update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS ELSE ERROR;";
    runInvalidQuery(update_stmt);

    // Test updating a row that exists on condition only IF NOT EXISTS or v1 != 3 AND v2 != 'c'
    update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS OR v1 != 3 AND v2 != 'c' ELSE ERROR;";
    runInvalidQuery(update_stmt);

    // Test updating a row that exists on condition only IF NOT EXISTS or v1 = 3 and v2 = 'c'
    update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS OR v1 = 3 AND v2 = 'c' ELSE ERROR;";
    {
      // Expected applied.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row unchanged.
      assertRow(1, "a", 2, "b", 4, "d");
    }

    // Test updating a row that exists on condition only IF NOT EXISTS or v1 > 4 and v2 = 'c'
    update_stmt =
        "UPDATE t SET v1 = 4, v2 = 'd' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF NOT EXISTS OR v1 > 4 AND v2 = 'c' ELSE ERROR;";
    runInvalidQuery(update_stmt);

    // Test updating a row that exists on condition only IF EXISTS and either of 2 values.
    update_stmt =
        "UPDATE t SET v1 = 5, v2 = 'e' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND (v1 = 3 OR v1 = 4) ELSE ERROR;";
    {
      // Expected applied.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row update.
      assertRow(1, "a", 2, "b", 5, "e");
    }

    // Test updating a row that exists on condition only IF EXISTS and >= a value.
    update_stmt =
        "UPDATE t SET v1 = 6, v2 = 'f' WHERE h1 = 1 AND h2 = 'a' AND r1 = 2 AND r2 = 'b' " +
        "IF EXISTS AND v1 >= 5 ELSE ERROR;";
    {
      // Expected applied.
      ResultSet rs = session.execute(update_stmt);
      ColumnDefinitions cols = rs.getColumnDefinitions();
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertTrue(rs.wasApplied());

      // Expect the row update.
      assertRow(1, "a", 2, "b", 6, "f");
    }

    // Tear down table
    dropTable("t");

    LOG.info("TEST SIMPLE UPDATE - End");
  }
}
