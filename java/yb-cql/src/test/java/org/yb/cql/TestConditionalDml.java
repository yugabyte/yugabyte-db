// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.Cluster;
import com.datastax.driver.core.DataType;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Session;

import java.util.Iterator;

import org.junit.Test;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.yb.cql.TestBase;

public class TestConditionalDml extends TestBase {

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
    SetupTable("t", 0 /* num_rows */);

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
      assertEquals(2, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.varchar(), cols.getType("v2"));
      assertFalse(rs.wasApplied());
      assertEquals("c", rs.one().getString("v2"));

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
    DropTable("t");

    LOG.info("TEST SIMPLE DELETE - End");
  }

  @Test
  public void testSimpleInsert() throws Exception {
    LOG.info("TEST SIMPLE INSERT - Start");

    // Setup table
    SetupTable("t", 0 /* num_rows */);

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
      assertEquals(2, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.cint(), cols.getType("v1"));
      assertFalse(rs.wasApplied());
      assertEquals(3, rs.one().getInt("v1"));

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

    // Tear down table
    DropTable("t");

    LOG.info("TEST SIMPLE INSERT - End");
  }

  @Test
  public void testSimpleUpdate() throws Exception {
    LOG.info("TEST SIMPLE UPDATE - Start");

    // Setup table
    SetupTable("t", 0 /* num_rows */);

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
      assertEquals(1, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertFalse(rs.wasApplied());

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
      assertEquals(3, cols.size());
      assertEquals(DataType.cboolean(), cols.getType("[applied]"));
      assertEquals(DataType.cint(), cols.getType("v1"));
      assertEquals(DataType.varchar(), cols.getType("v2"));
      assertFalse(rs.wasApplied());
      Row row = rs.one();
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

    // Tear down table
    DropTable("t");

    LOG.info("TEST SIMPLE UPDATE - End");
  }
}
