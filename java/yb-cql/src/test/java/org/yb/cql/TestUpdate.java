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

import com.datastax.driver.core.Row;

import org.junit.Test;
import org.yb.client.TestUtils;

import java.net.InetAddress;
import java.util.Date;
import java.util.Map;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;

public class TestUpdate extends BaseCQLTest {

  @Test
  public void testUpdateWithTimestamp() throws Exception {
    String tableName = "test_update_with_timestamp";
    createTable(tableName, "timestamp");
    // this includes both string and int inputs
    Map<String, Date> ts_values = generateTimestampMap();
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
      Row row = runSelect(sel_stmt).next();
      assertEquals(1, row.getInt(0));
      assertEquals(2, row.getInt(2));
      assertEquals(3, row.getInt(4));
      assertEquals(date_value, row.getTimestamp(1));
      assertEquals(date_value, row.getTimestamp(3));
      assertEquals(date_value, row.getTimestamp(5));
    }
  }

  @Test
  public void testUpdateWithTTL() throws Exception {
    String tableName = "test_update_with_ttl";
    createTable(tableName);

    // Insert a row.
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
      tableName, 1, 2, 3, 4, 5, 6);
    session.execute(insert_stmt);

    // Update v1 with a TTL.
    String update_stmt = String.format(
      "UPDATE %s USING TTL 2 SET v1 = 500 WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
        "r2 = 'r4';",
      tableName);
    session.execute(update_stmt);

    // Update v2 with a TTL.
    update_stmt = String.format(
      "UPDATE %s USING TTL 4 SET v2 = 'v600' WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
        "r2 = 'r4';",
      tableName);
    session.execute(update_stmt);

    String select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s" +
      "  WHERE h1 = 1 AND h2 = 'h2';", tableName);

    // Verify row is present.
    Row row = runSelect(select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertEquals(500, row.getInt(4));
    assertEquals("v600", row.getString(5));

    // Now verify v1 expires.
    TestUtils.waitForTTL(2000L);
    row = runSelect(select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertTrue(row.isNull(4));
    assertEquals("v600", row.getString(5));

    // Now verify v2 expires.
    TestUtils.waitForTTL(2000L);
    row = runSelect(select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertTrue(row.isNull(4));
    assertTrue(row.isNull(5));
  }

  private void runInvalidUpdateWithTimestamp(String tableName, String ts) {
    // testing SET clause
    String upd_stmt1 = String.format(
      "UPDATE %s SET v2 = '%s' WHERE h1 = 1 AND h2 = %s" +
        " AND r1 = 2 AND r2 = %s;", tableName, ts, "0", "0");
    runInvalidStmt(upd_stmt1);

    // testing WHERE clause
    String upd_stmt2 = String.format(
      "UPDATE %s SET v2 = %s WHERE h1 = 1 AND h2 = '%s'" +
        " AND r1 = 2 AND r2 = %s;", tableName, "0", ts, "0");
    runInvalidStmt(upd_stmt2);
  }

  @Test
  public void testInvalidUpdateWithTimestamp() throws Exception {
    String tableName = "test_update_with_invalid_timestamp";
    createTable(tableName, "timestamp");
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

  private String getUpdateStmt(String tableName, long ttl_seconds) {
    return String.format(
      "UPDATE %s USING TTL %d SET v1 = 500 WHERE h1 = 1 AND h2 = 'h2' AND r1 = 3 AND " +
        "r2 = 'r4';", tableName, ttl_seconds);
  }

  private void runInvalidUpdateWithTTL(String tableName, long ttlSeconds) {
    runInvalidStmt(getUpdateStmt(tableName, ttlSeconds));
  }

  private void runValidUpdateWithTTL(String tableName, long ttl_seconds) {
    session.execute(getUpdateStmt(tableName, ttl_seconds));
  }

  @Test
  public void testValidInvalidUpdateWithTTL() throws Exception {
    String tableName = "testValidInvalidUpdateWithTTL";
    createTable(tableName);

    // Insert a row.
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
      tableName, 1, 2, 3, 4, 5, 6);
    session.execute(insert_stmt);

    // Invalid statements.
    runInvalidUpdateWithTTL(tableName, Long.MAX_VALUE);
    runInvalidUpdateWithTTL(tableName, MAX_TTL_SEC + 1);
    runInvalidUpdateWithTTL(tableName, -1);

    // Valid statements.
    runValidUpdateWithTTL(tableName, MAX_TTL_SEC);
    runValidUpdateWithTTL(tableName, 0);
  }

  @Test
  public void testUpdateWithInet() throws Exception {
    String tableName = "testUpdateWithInet";
    createTable(tableName, "inet");

    // Insert a row.
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(1, '1.2.3.4', 2, '1.2.3.4', 3, " +
        "'1.2.3.4');", tableName);
    session.execute(insert_stmt);

    // Update the row.
    String update_stmt = String.format(
      "UPDATE %s SET v2 = '1.2.3.5' WHERE h1 = 1 AND h2 = '1.2.3.4' AND r1 = 2 AND r2 = " +
        "'1.2.3.4';", tableName);
    session.execute(update_stmt);

    // Verify the update worked.
    String select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s" +
      "  WHERE h1 = 1 AND h2 = '1.2.3.4';", tableName);
    Row row = runSelect(select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals(InetAddress.getByName("1.2.3.4"), row.getInet(1));
    assertEquals(2, row.getInt(2));
    assertEquals(InetAddress.getByName("1.2.3.4"), row.getInet(3));
    assertEquals(3, row.getInt(4));
    assertEquals(InetAddress.getByName("1.2.3.5"), row.getInet(5));
  }

  @Test
  public void testUpdateSystemNamespace() throws Exception {
    runInvalidStmt("UPDATE system.peers SET h1 = 1, h2 = '1', r1 = 1, r2 = '1';");
  }
}
