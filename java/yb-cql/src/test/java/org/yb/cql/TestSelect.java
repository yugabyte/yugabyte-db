// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.util.*;

import org.junit.Test;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public class TestSelect extends TestBase {
  @Test
  public void testSimpleQuery() throws Exception {
    LOG.info("TEST CQL SIMPLE QUERY - Start");

    // Setup test table.
    SetupTable("test_select", 10);

    // Select data from the test table.
    String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                         "  WHERE h1 = 7 AND h2 = 'h7' AND r1 = 107;";
    ResultSet rs = session.execute(select_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), 7);
      assertEquals(row.getString(1), "h7");
      assertEquals(row.getInt(2), 107);
      assertEquals(row.getString(3), "r107");
      assertEquals(row.getInt(4), 1007);
      assertEquals(row.getString(5), "v1007");
      row_count++;
    }
    assertEquals(row_count, 1);

    // Insert multiple rows with the same partition key.
    int num_rows = 20;
    int h1_shared = 1111111;
    String h2_shared = "h2_shared_key";
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO test_select(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, 'r%d', %d, 'v%d');",
        h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }

    // Verify multi-row select.
    String multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                                      "  WHERE h1 = %d AND h2 = '%s';",
                                      h1_shared, h2_shared);
    rs = session.execute(multi_stmt);

    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_rows);

    // Test ALLOW FILTERING clause.
    multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                               "  WHERE h1 = %d AND h2 = '%s' ALLOW FILTERING;",
                               h1_shared, h2_shared);
    rs = session.execute(multi_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_rows);

    LOG.info("TEST CQL SIMPLE QUERY - End");
  }

  @Test
  public void testSelectWithLimit() throws Exception {
    LOG.info("TEST CQL LIMIT QUERY - Start");

    // Setup test table.
    SetupTable("test_select", 0);

    // Insert multiple rows with the same partition key.
    int num_rows = 20;
    int h1_shared = 1111111;
    int num_limit_rows = 10;
    String h2_shared = "h2_shared_key";
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO test_select(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, 'r%d', %d, 'v%d');",
        h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }

    // Verify multi-row select.
    String multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                                      "  WHERE h1 = %d AND h2 = '%s' LIMIT %d;",
                                      h1_shared, h2_shared, num_limit_rows);
    ResultSet rs = session.execute(multi_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_limit_rows);

    // Test allow filtering.
    multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                               "  WHERE h1 = %d AND h2 = '%s' LIMIT %d ALLOW FILTERING;",
                               h1_shared, h2_shared, num_limit_rows);
    rs = session.execute(multi_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_limit_rows);

    LOG.info("TEST CQL LIMIT QUERY - End");
  }

  // testing only range queries, rest are tested by insert/update tests
  @Test
  public void testSelectWithTimestamp() throws Exception {
    String tableName = "test_select_with_timestamp";
    CreateTable(tableName, "timestamp");
    // testing String input
    String ins_stmt = String.format(
            "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, '%s', %d, %s);",
            tableName, 1, "1980-8-11 12:20:30 UTC", 2, "1980-8-11 12:20:30 UTC", 3, "0");
    session.execute(ins_stmt);

    // testing <,=,> for String input
    String sel_stmt1 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 > '%s';", tableName,
            "1980-8-11 12:20:30 UTC", "1980-8-11 12:20:30.142 UTC");
    ResultSet rs = session.execute(sel_stmt1);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt2 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 = '%s';", tableName,
            "1980-8-11 12:20:30 UTC", "1980-8-11 12:20:30.142 UTC");
    rs = session.execute(sel_stmt2);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt3 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 < '%s';", tableName,
            "1980-8-11 12:20:30 UTC", "1980-8-11 12:20:30.142 UTC");
    Iterator<Row> iter = session.execute(sel_stmt3).iterator();
    assertTrue(iter.hasNext());
    Row row = iter.next();
    Calendar cal = new GregorianCalendar();
    cal.setTimeZone(TimeZone.getTimeZone("GMT"));
    cal.setTimeInMillis(0); // resetting
    // Java Date month value starts at 0 not 1
    cal.set(1980, 7, 11, 12, 20, 30);
    Date date = cal.getTime();
    assertEquals(1, row.getInt(0));
    assertEquals(date, row.getTimestamp(1));
    assertEquals(2, row.getInt(2));
    assertEquals(date, row.getTimestamp(3));
    assertEquals(3, row.getInt(4));

    // testing <,=,> for Int input
    String sel_stmt4 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 > %s;", tableName,
            "1980-8-11 12:20:30 UTC", "334844431000");
    rs = session.execute(sel_stmt4);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt5 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 = %s;", tableName,
            "1980-8-11 12:20:30 UTC", "334844431000");
    rs = session.execute(sel_stmt5);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt6 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 < %s;", tableName,
            "1980-8-11 12:20:30 UTC", "334844431000");
    iter = session.execute(sel_stmt6).iterator();
    assertTrue(iter.hasNext());
    row = iter.next();
    assertEquals(1, row.getInt(0));
    assertEquals(date, row.getTimestamp(1));
    assertEquals(2, row.getInt(2));
    assertEquals(date, row.getTimestamp(3));
    assertEquals(3, row.getInt(4));
  }

  private void runInvalidSelectWithTimestamp(String tableName, String ts) {
    String sel_stmt = String.format(
            "SELECT * from %s WHERE h1 = 1 AND h2 = %s" +
                    " AND r1 = 2 AND r2 = %s;", tableName, ts, "0");
    RunInvalidStmt(sel_stmt);
  }

  @Test
  public void testInvalidSelectWithTimestamp() throws Exception {
    String tableName = "test_select_with_invalid_timestamp";
    CreateTable(tableName, "timestamp");
    String ins_stmt = String.format(
            "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, %s, %d, %s, %d, %s);",
            tableName, 1, "0", 2, "0", 3, "0");
    session.execute(ins_stmt);

    runInvalidSelectWithTimestamp(tableName, "plainstring");
    runInvalidSelectWithTimestamp(tableName, "1992:12:11");
    runInvalidSelectWithTimestamp(tableName, "1992-11");
    runInvalidSelectWithTimestamp(tableName, "1992-13-12");
    runInvalidSelectWithTimestamp(tableName, "1992-12-12 14:23:30:31");
    runInvalidSelectWithTimestamp(tableName, "1992-12-12 14:23:30.12.32");
  }
}
