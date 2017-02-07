// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import org.junit.Ignore;
import org.junit.Test;

import java.util.Iterator;

import static junit.framework.TestCase.assertFalse;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestTableTTL extends TestBase {

  private Iterator<Row> execQuery(String tableName, int primaryKey) {
    ResultSet rs = session.execute(String.format("SELECT c1, c2, c3 FROM %s WHERE c1 = %d;",
      tableName, primaryKey));
    return rs.iterator();
  }

  private void assertNoRow(String tableName, int primaryKey) {
    Iterator<Row> iter = execQuery(tableName, primaryKey);
    assertFalse(iter.hasNext());
  }

  private Row getFirstRow(String tableName, int primaryKey) {
    Iterator<Row> iter = execQuery(tableName, primaryKey);
    assertTrue(iter.hasNext());
    return iter.next();
  }

  private void createTable(String tableName, int ttl) {
    session.execute(String.format("CREATE TABLE %s (c1 int, c2 int, c3 int, PRIMARY KEY(c1)) " +
      "WITH default_time_to_live = %d;", tableName, ttl));
  }

  @Test
  public void testSimpleTableTTL() throws Exception {
    String tableName = "testSimpleTableTTL";

    // Create table with TTL.
    createTable(tableName, 1000);

    // Insert a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));

    // Verify row is present.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));

    // Now row should expire.
    Thread.sleep(1050);

    // Verify row has expired.
    assertNoRow(tableName, 1);
  }

  // Will enable this test once we support table ttl being overridden.
  @Ignore @Test
  public void testTableTTLOverride() throws Exception {
    String tableName = "testTableTTLOverride";

    // Create table with TTL.
    createTable(tableName, 1000);

    // Insert a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (2, 3, 4) USING TTL 2000;",
      tableName));

    Thread.sleep(1050);

    // c1=1 should have expired.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertTrue(row.isNull(1));
    assertTrue(row.isNull(2));

    // c1=2 is still alive.
    row = getFirstRow(tableName, 2);
    assertEquals(2, row.getInt(0));
    assertEquals(3, row.getInt(1));
    assertEquals(4, row.getInt(2));

    Thread.sleep(1050);

    // c1 = 2 should have expired.
    row = getFirstRow(tableName, 2);
    assertEquals(2, row.getInt(0));
    assertTrue(row.isNull(1));
    assertTrue(row.isNull(2));

    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (3, 4, 5) USING TTL 500;",
      tableName));
    Thread.sleep(550);
    // c1 = 3 should have expired.
    row = getFirstRow(tableName, 3);
    assertEquals(3, row.getInt(0));
    assertTrue(row.isNull(1));
    assertTrue(row.isNull(2));
  }

  // Will enable this test once we support table ttl being overriden.
  @Ignore @Test
  public void testTableTTLWithTTLZero() throws Exception {
    String tableName = "testTableTTLWithTTLZero";

    // Create table with TTL.
    createTable(tableName, 1000);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 0;",
      tableName));

    Thread.sleep(2050);

    // Row should not have expired.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));
  }

  @Test
  public void testTableTTLZero() throws Exception {
    String tableName = "testTableTTLZero";

    // Create table with TTL.
    createTable(tableName, 0);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 1000;",
      tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (2, 2, 3);",
      tableName));

    Thread.sleep(1050);

    // TTL 1 should have expired.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertTrue(row.isNull(1));
    assertTrue(row.isNull(2));

    // Row with no TTL should survive
    row = getFirstRow(tableName, 2);
    assertEquals(2, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));
  }

  @Test
  public void testTableTTLAndColumnTTL() throws Exception {
    String tableName = "testTableTTLAndColumnTTL";

    // Create table with TTL.
    createTable(tableName, 2000);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 1000;",
      tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (2, 2, 3);",
      tableName));

    Thread.sleep(1050);

    // c1 = 1 should have expired.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertTrue(row.isNull(1));
    assertTrue(row.isNull(2));

    // Row with no TTL should survive
    row = getFirstRow(tableName, 2);
    assertEquals(2, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(3, row.getInt(2));

    Thread.sleep(1000);

    // Row c1 = 2 should now expire, with init marker.
    assertNoRow(tableName, 2);

    // Row c1 = 1, should also expire.
    assertNoRow(tableName, 1);
  }

  @Test
  public void testTableTTLWithDeletes() throws Exception {
    String tableName = "testTableTTLWithDeletes";

    // Create table with TTL.
    createTable(tableName, 2000);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TTL 1000;",
      tableName));
    session.execute(String.format("DELETE FROM %s WHERE c1 = 1;", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 4);", tableName));

    Thread.sleep(1050);

    // Verify row still exists.
    Row row = getFirstRow(tableName, 1);
    assertEquals(1, row.getInt(0));
    assertEquals(2, row.getInt(1));
    assertEquals(4, row.getInt(2));

    Thread.sleep(1050);

    // Now verify row is gone due to table level TTL.
    assertNoRow(tableName, 1);
  }

  @Test
  public void testTableTTLWithOverwrites() throws Exception {
    String tableName = "testTableTTLWithOverwrites";

    // Create table with TTL.
    createTable(tableName, 2000);
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3);",
      tableName));

    Thread.sleep(1050);

    // Overwrite the row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 4);", tableName));

    Thread.sleep(1000);

    // Row should expire since we don't have a new init marker for the latest insert.
    assertNoRow(tableName, 1);
  }
}
