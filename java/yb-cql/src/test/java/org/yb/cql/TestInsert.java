// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.QueryValidationException;
import org.junit.Test;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;


public class TestInsert extends TestBase {

  @Test
  public void testSimpleInsert() throws Exception {
    LOG.info("TEST SIMPLE INSERT - Start");

    // Setup table and insert 100 rows.
    SetupTable("test_insert", 100);

    LOG.info("TEST SIMPLE INSERT - End");
  }

  @Test
  public void testInsertWithTTL() throws Exception {
    String tableName = "test_insert_with_ttl";
    CreateTable(tableName);

    // Now insert with ttl.
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1) VALUES(%d, 'h%d', %d, 'r%d', %d) USING TTL 1000;",
      tableName, 1, 2, 3, 4, 5);
    session.execute(insert_stmt);

    insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v2) VALUES(%d, 'h%d', %d, 'r%d', 'v%d') USING TTL 2000;",
      tableName, 1, 2, 3, 4, 6);
    session.execute(insert_stmt);

    String select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
      + "  WHERE h1 = 1 AND h2 = 'h2';", tableName);

    // Verify row is present.
    Row row = RunSelect(tableName, select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertEquals(5, row.getInt(4));
    assertEquals("v6", row.getString(5));

    // Now verify v1 expires.
    Thread.sleep(1100);
    row = RunSelect(tableName, select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertTrue(row.isNull(4));
    assertEquals("v6", row.getString(5));

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

  private void runInvalidStmt(String stmt) {
    try {
      session.execute(stmt);
      fail(String.format("Statement did not fail: %s", stmt));
    } catch (QueryValidationException qv) {
      LOG.info("Expected exception", qv);
    }
  }

  private void runInvalidInsertWithTTL(String tableName, String ttl) {
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1) VALUES(%d, 'h%d', %d, 'r%d', %d) USING TTL %s;",
      tableName, 1, 2, 3, 4, 5, ttl);
    runInvalidStmt(insert_stmt);
  }

  @Test
  public void testInvalidInsertWithTTL() throws Exception {
    String tableName = "test_insert_with_invalid_ttl";
    CreateTable(tableName);

    runInvalidInsertWithTTL(tableName, "1000.1");
    runInvalidInsertWithTTL(tableName, "abcxyz");
    runInvalidInsertWithTTL(tableName, "-1");
    runInvalidInsertWithTTL(tableName, "0x80");
    runInvalidInsertWithTTL(tableName, "true");
    runInvalidInsertWithTTL(tableName, "false");
  }

  // This test currently fails, will enable it once we support TTL resets.
  //@Test
  public void testResetTTL() throws Exception {
    // Setting TTL to 0 should reset the TTL.
    String tableName = "test_reset_ttl";
    CreateTable(tableName);

    // Insert two rows with TTL 1000
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d') USING TTL " +
        "1000;",
      tableName, 1, 2, 3, 4, 5, 6);
    session.execute(insert_stmt);

    insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d') USING TTL " +
        "1000;",
      tableName, 10, 20, 30, 40, 50, 60);
    session.execute(insert_stmt);

    // Set TTL to 0 for one row.
    insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d') USING TTL " +
        "0;",
      tableName, 10, 20, 30, 40, 50, 60);
    session.execute(insert_stmt);

    // Verify one row exists and the other doesn't.
    String select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
      + "  WHERE h1 = 1 AND h2 = 'h2';", tableName);

    Thread.sleep(1100);

    // Verify row has expired.
    Row row = RunSelect(tableName, select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertTrue(row.isNull(4));
    assertTrue(row.isNull(5));

    // Verify row with TTL reset survives.
    select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
      + "  WHERE h1 = 10 AND h2 = 'h20';", tableName);
    row = RunSelect(tableName, select_stmt).next();
    assertEquals(10, row.getInt(0));
    assertEquals("h20", row.getString(1));
    assertEquals(30, row.getInt(2));
    assertEquals("r40", row.getString(3));
    assertEquals(50, row.getInt(4));
    assertEquals("v60", row.getString(5));
  }

}
