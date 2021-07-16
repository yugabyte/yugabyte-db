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

import java.util.*;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.SimpleStatement;
import org.junit.Test;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.SyntaxError;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;

import org.junit.rules.ExpectedException;
import org.junit.Rule;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestDelete extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestDelete.class);

  @Rule
  public ExpectedException exception = ExpectedException.none();

  @Test
  public void testDeleteOneColumn() throws Exception {
    LOG.info("TEST CQL DELETE - Start");

    // Setup test table.
    setupTable("test_delete", 2);

    // Select data from the test table.
    String delete_stmt = "DELETE v1 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    session.execute(delete_stmt);
    String select_stmt_1 = "SELECT v1 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0';";
    ResultSet rs = session.execute(select_stmt_1);

    List<Row> rows = rs.all();

    assertEquals(1, rows.size());
    Row row = rows.get(0);
    assertTrue(row.isNull(0));

    String select_stmt_2 = "SELECT v2 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0';";
    rs = session.execute(select_stmt_2);
    rows = rs.all();
    assertEquals(1, rows.size());
    row = rows.get(0);
    assertEquals("v1000",  row.getString(0));
  }

  @Test
  public void testDeleteMultipleColumns() throws Exception {
    LOG.info("TEST CQL DELETE - Start");

    // Setup test table.
    setupTable("test_delete", 2);

    // Select data from the test table.
    String delete_stmt = "DELETE v1, v2 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    session.execute(delete_stmt);
    String select_stmt_1 = "SELECT v1, v2 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0';";
    ResultSet rs = session.execute(select_stmt_1);

    List<Row> rows = rs.all();

    assertEquals(1, rows.size());
    Row row = rows.get(0);
    assertTrue(row.isNull(0));
    assertTrue(row.isNull(1));
  }

  @Test
  public void testStarDeleteSyntaxError() throws Exception {
    LOG.info("TEST CQL SyntaxError DELETE * - Start");

    // Setup test table.
    setupTable("test_delete", 2);
    String delete_stmt = "DELETE * FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    exception.expect(SyntaxError.class);
    session.execute(delete_stmt);
  }

  @Test
  public void testPrimaryDeleteSyntaxError() throws Exception {
    LOG.info("TEST CQL SyntaxError DELETE primary - Start");

    // Setup test table.
    setupTable("test_delete", 2);
    String delete_stmt = "DELETE h1 FROM test_delete" +
                         "  WHERE h1 = 0 AND h2 = 'h0' AND r1 = 100 AND r2 = 'r100';";
    exception.expect(InvalidQueryException.class);
    session.execute(delete_stmt);
  }

  @Test
  public void testRangeDelete() throws Exception {
    LOG.info("TEST CQL range DELETE - Start");


    // Set up the table.
    String tableName = "test_range_delete";
    String createStmt = "CREATE TABLE " + tableName +
        " (h1 int, h2 text, " +
        " r1 int, r2 text, " +
        " s int static, v text, " +
        " primary key((h1, h2), r1, r2));";
    session.execute(createStmt);

    final PreparedStatement insert = session.prepare(
        "INSERT INTO " + tableName + "(h1, h2, r1, r2, s, v) VALUES(" +
            "?, ?, ?, ?, ?, ?);");

    // Insert 100 rows.
    for (int h = 0; h < 10; h++) {
      for (int r = 0; r < 10; r++) {
        session.execute(insert.bind(
            h, "h" + h, r, "r" + r, 1, "v"));
      }
    }

    final PreparedStatement select = session.prepare(
        "SELECT * FROM " + tableName + " WHERE h1 = ? AND h2 = ?");

    final PreparedStatement static_select = session.prepare(
        "SELECT DISTINCT s FROM " + tableName + " WHERE h1 = ? AND h2 = ?");

    final SimpleStatement full_select = new SimpleStatement("SELECT * FROM " + tableName);

    //----------------------------------------------------------------------------------------------
    // Test Valid Statements

    // Test Delete entire hash.
    {
      // Deleting 10 rows (out of 100).
      session.execute("DELETE FROM " + tableName + " WHERE h1 = 0 and h2 = 'h0'");

      // Check row was removed.
      List<Row> rows = session.execute(select.bind(new Integer(0), "h0")).all();
      assertTrue(rows.isEmpty());

      // Check that static column is removed.
      rows = session.execute(static_select.bind(new Integer(0), "h0")).all();
      assertTrue(rows.isEmpty());

      // Check that no other rows are removed.
      rows = session.execute(full_select).all();
      assertEquals(90, rows.size());
    }

    // Test Delete non-existing hash.
    {
      // Deleting 0 rows (out of 90).
      session.execute("DELETE FROM " + tableName + " WHERE h1 = -1 and h2 = 'h-1'");

      // Check that no rows are removed.
      List<Row> rows = session.execute(full_select).all();
      assertEquals(90, rows.size());
    }

    // Test Delete with lower bound.
    {
      // Delete entries 6,7,8,9
      session.execute("DELETE FROM " + tableName + " WHERE h1 = 1 and h2 = 'h1' AND r1 > 5");

      // Check rows.
      Iterator<Row> rows = session.execute(select.bind(new Integer(1), "h1")).iterator();
      for (int r = 0; r <= 5; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      assertFalse(rows.hasNext());
    }

    // Test Delete with upper bound.
    {
      // Delete entries 1,2,3 (due to condition on r2)
      session.execute(
          "DELETE FROM " + tableName + " WHERE h1 = 2 and h2 = 'h2' AND r1 < 5 AND r2 <= 'r3'");

      // Check Rows.
      Iterator<Row> rows = session.execute(select.bind(new Integer(2), "h2")).iterator();
      for (int r = 4; r < 10; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      assertFalse(rows.hasNext());
    }

    // Test delete with both bounds.
    {
      // Delete entries 3,4,5,6,7.
      session.execute(
          "DELETE FROM " + tableName + " WHERE h1 = 3 and h2 = 'h3' " +
              "AND r1 >= 3 AND r1 < 8");

      // Check Rows.
      Iterator<Row> rows = session.execute(select.bind(new Integer(3), "h3")).iterator();
      for (int r = 0; r < 3; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      for (int r = 8; r < 10; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      assertFalse(rows.hasNext());
    }

    // Test delete with partial range key (equality condition).
    {
      // Delete entry 3.
      session.execute(
          "DELETE FROM " + tableName + " WHERE h1 = 4 and h2 = 'h4' " +
              "AND r1 = 3");

      // Check Rows.
      Iterator<Row> rows = session.execute(select.bind(new Integer(4), "h4")).iterator();
      for (int r = 0; r < 3; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      for (int r = 4; r < 10; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      assertFalse(rows.hasNext());
    }

    // Test static column when deleting all rows.
    {
      // Delete all entries.
      session.execute(
          "DELETE FROM " + tableName + " WHERE h1 = 5 and h2 = 'h5' " +
              "AND r1 >= 0 AND r1 <= 10");

      // Check Rows.
      Iterator<Row> rows = session.execute(select.bind(new Integer(5), "h5")).iterator();
      // Omer: Changed this to true. Cassandra actually returns the static column if it
      // was not deleted
      assertTrue(rows.hasNext());

      // Check that static column is not removed.
      rows = session.execute(static_select.bind(new Integer(5), "h5")).iterator();
      assertTrue(rows.hasNext());
      assertEquals(1, rows.next().getInt("s"));
      assertFalse(rows.hasNext());
    }

    // Test range delete with timestamp (in the past).
    {
      // Delete some entries but with old timestamp -- should do nothing.
      session.execute(
          "DELETE FROM " + tableName + " USING TIMESTAMP 1 WHERE h1 = 6 and h2 = 'h6' " +
              "AND r1 >= 0 AND r1 <= 10");

      // Check Rows -- all should be there because timestamp was in the past.
      Iterator<Row> rows = session.execute(select.bind(new Integer(6), "h6")).iterator();
      for (int r = 0; r < 10; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      assertFalse(rows.hasNext());
    }

    // Test range delete with timestamp (in the future).
    {

      // Delete entries 0,1,2,3,4,5,6 using max long as timestamp.
      session.execute(
          "DELETE FROM " + tableName + " USING TIMESTAMP " + Long.MAX_VALUE +
              " WHERE h1 = 7 AND h2 = 'h7' AND r1 >= 0 AND r1 <= 6");

      // Check Rows -- delete should applied because timestamp was in the future.
      Iterator<Row> rows = session.execute(select.bind(new Integer(7), "h7")).iterator();
      for (int r = 7; r < 10; r++) {
        assertTrue(rows.hasNext());
        assertEquals(r, rows.next().getInt("r1"));
      }
      assertFalse(rows.hasNext());
    }

    //----------------------------------------------------------------------------------------------
    // Test Invalid Statements.

    // Range delete with only static columns specified must not have conditions on range.
    runInvalidStmt("DELETE s FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0' AND r1 < 5");

    // Range delete with specified non-static columns is not allowed.
    runInvalidStmt("DELETE v FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0' AND r1 < 5");
    runInvalidStmt("DELETE s,v FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0'");

    // Range delete with IF condition is not allowed.
    runInvalidStmt("DELETE FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0' IF v1 = 0");

    // Hash portion is still required.
    runInvalidStmt("DELETE FROM " + tableName + " WHERE h1 = 0");
    runInvalidStmt("DELETE FROM " + tableName + " WHERE h2 = 'h0'");
    runInvalidStmt("DELETE FROM " + tableName + " WHERE h1 >= 0 AND h2 = 'h0'");

    // Illogical conditions on the range columns are not allowed.
    runInvalidStmt(
        "DELETE FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0' AND r1 < 5 AND r1 = 4");
    runInvalidStmt(
        "DELETE FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0' AND r1 < 5 AND r1 <= 4");
    runInvalidStmt(
        "DELETE FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0' AND r1 > 5 AND r1 > 4");
    runInvalidStmt(
        "DELETE FROM " + tableName + " WHERE h1 = 0 AND h2 = 'h0' AND r2 = 'h5' AND r2 = 'h6'");
  }
}
