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
import java.util.HashMap;
import java.util.Map;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestUpdate extends BaseCQLTest {

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

  @Test
  public void testUpdateDuplicateColumns() throws Exception {
    String tableName = "test_update_duplicate_column";
    createTable(tableName);

    // Insert a row.
    String insert_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d');",
        tableName, 1, 2, 3, 4, 5, 6);
    session.execute(insert_stmt);

    runInvalidStmt(String.format("UPDATE %s SET v1 = 50, v1 = 500 WHERE h1 = 1 and h2 = 'h2' and " +
        "r1 = 3 and r2 = 'r4'", tableName));
    session.execute("create table foo(h int primary key, i int, m map<int,int>)");
    runInvalidStmt("update foo set m[1] = 1, m[2] = 1, m = m - {1} where h = 1;");
    runInvalidStmt("update foo set m[1] = 1, m[2] = 1, m = {1 : 2, 2 : 3} where h = 1;");
    runInvalidStmt("update foo set m->'a'->'q'->'r' = '200', m[1] = 1 WHERE c1 = 1");
    session.execute("update foo set m[1] = 1, m[2] = 1, m[1] = 3, m[2] = 4 where h = 1;");
    Row row = session.execute("select * from foo").one();
    Map expectedMap = new HashMap<>();
    expectedMap.put(1, 3);
    expectedMap.put(2, 4);
    assertEquals(expectedMap, row.getMap("m", Integer.class, Integer.class));
  }
}
