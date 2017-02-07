// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.Row;

import org.junit.Test;

import java.util.Date;
import java.util.Map;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestUpdate extends TestBase {

  @Test
  public void testUpdateWithTimestamp() throws Exception {
    String tableName = "test_update_with_timestamp";
    CreateTable(tableName, "timestamp");
    // this includes both string and int inputs
    Map<String, Date> ts_values = GenerateTimestampMap();
    for (String key : ts_values.keySet()) {
      Date date_value = ts_values.get(key);
      String ins_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, %s, %d, %s, %d, %s);",
        tableName, 1, key, 2, key, 3, "0");
      session.execute(ins_stmt);
      String upd_stmt = String.format(
        "UPDATE %s SET v2 = %s WHERE h1 = 1 AND h2 = %s" +
          " AND r1 = 2 AND r2 = %s;", tableName, key , key, key);
      session.execute(upd_stmt);
      String sel_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
        + " WHERE h1 = 1 AND h2 = %s;", tableName, key);
      Row row = RunSelect(tableName, sel_stmt).next();
      assertEquals(1, row.getInt(0));
      assertEquals(2, row.getInt(2));
      assertEquals(3, row.getInt(4));
      assertEquals(date_value, row.getTimestamp(1));
      assertEquals(date_value, row.getTimestamp(3));
      assertEquals(date_value, row.getTimestamp(5));
    }
  }

  private void runInvalidUpdateWithTimestamp(String tableName, String ts) {
    // testing SET clause
    String upd_stmt1 = String.format(
      "UPDATE %s SET v2 = '%s' WHERE h1 = 1 AND h2 = %s" +
        " AND r1 = 2 AND r2 = %s;", tableName, ts, "0", "0");
    RunInvalidStmt(upd_stmt1);

    // testing WHERE clause
    String upd_stmt2 = String.format(
      "UPDATE %s SET v2 = '%s' WHERE h1 = 1 AND h2 = %s" +
        " AND r1 = 2 AND r2 = %s;", tableName, "0", ts, "0");
    RunInvalidStmt(upd_stmt2);
  }

  @Test
  public void testInvalidUpdateWithTimestamp() throws Exception {
    String tableName = "test_update_with_invalid_timestamp";
    CreateTable(tableName, "timestamp");
    String ins_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, %s, %d, %s, %d, %s);",
      tableName, 1, "0", 2, "0", 3, "0");
    session.execute(ins_stmt);

    runInvalidUpdateWithTimestamp(tableName, "plainstring");
    runInvalidUpdateWithTimestamp(tableName, "1992:12:11");
    runInvalidUpdateWithTimestamp(tableName, "1992-11");
    runInvalidUpdateWithTimestamp(tableName, "1992-13-12");
    runInvalidUpdateWithTimestamp(tableName, "1992-12-12 14:23:30:31");
    runInvalidUpdateWithTimestamp(tableName, "1992-12-12 14:23:30.12.32");
  }

  @Test
  public void testUpdateWithTTL() throws Exception {
    String tableName = "test_update_with_ttl";
    CreateTable(tableName);

    // Insert a row.
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
      tableName, 1, 2, 3, 4, 5, 6);
    session.execute(insert_stmt);

    // Update v1 with a TTL.
    String update_stmt = String.format(
      "UPDATE %s USING TTL 1000 SET v1 = 500 WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
        "r2 = 'r4';",
      tableName);
    session.execute(update_stmt);

    // Update v2 with a TTL.
    update_stmt = String.format(
      "UPDATE %s USING TTL 2000 SET v2 = 'v600' WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
        "r2 = 'r4';",
      tableName);
    session.execute(update_stmt);

    String select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s" +
      "  WHERE h1 = 1 AND h2 = 'h2';", tableName);

    // Verify row is present.
    Row row = RunSelect(tableName, select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertEquals(500, row.getInt(4));
    assertEquals("v600", row.getString(5));

    // Now verify v1 expires.
    Thread.sleep(1100);
    row = RunSelect(tableName, select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertTrue(row.isNull(4));
    assertEquals("v600", row.getString(5));

    // Now verify v2 expires.
    Thread.sleep(1000);
    row = RunSelect(tableName, select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertTrue(row.isNull(4));
    assertTrue(row.isNull(5));
  }

  private String getUpdateStmt(String tableName, long ttl) {
    return String.format(
      "UPDATE %s USING TTL %d SET v1 = 500 WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
        "r2 = 'r4';", tableName, ttl);
  }

  private void runInvalidUpdateWithTTL(String tableName, long ttl) {
    RunInvalidStmt(getUpdateStmt(tableName, ttl));
  }

  private void runValidUpdateWithTTL(String tableName, long ttl) {
    session.execute(getUpdateStmt(tableName, ttl));
  }

  @Test
  public void testValidInvalidUpdateWithTTL() throws Exception {
    String tableName = "testValidInvalidUpdateWithTTL";
    CreateTable(tableName);

    // Insert a row.
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
      tableName, 1, 2, 3, 4, 5, 6);
    session.execute(insert_stmt);

    // Invalid statements.
    runInvalidUpdateWithTTL(tableName, Long.MAX_VALUE);
    runInvalidUpdateWithTTL(tableName, MAX_TTL + 1);
    runInvalidUpdateWithTTL(tableName, -1);

    // Valid statements.
    runValidUpdateWithTTL(tableName, MAX_TTL);
    runValidUpdateWithTTL(tableName, 0);
  }
}
