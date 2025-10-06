// Copyright (c) YugabyteDB, Inc.
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

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.TransportException;
import org.apache.commons.lang3.RandomStringUtils;
import org.junit.Test;
import org.yb.client.TestUtils;

import java.net.InetAddress;
import java.text.SimpleDateFormat;
import java.util.*;

import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestInsertValues extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestInsertValues.class);

  @Override
  public int getTestMethodTimeoutSec() {
    // No need to adjust for TSAN vs. non-TSAN here, it will be done automatically.
    return 240;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    // testLargeInsert needs more memory than the default of 5%.
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("read_buffer_memory_limit", "-100");
    return flagMap;
  }

  @Test
  public void testSimpleInsert() throws Exception {
    LOG.info("TEST INSERT VALUES - Start");

    // Setup table and insert 100 rows.
    setupTable("test_insert", 100);

    LOG.info("TEST INSERT VALUES - End");
  }

  @Test
  public void testLargeInsert() throws Exception {
    final int STRING_SIZE = 64 * 1024 * 1024;
    final String errorMessage = "is longer than max value size supported";
    String tableName = "test_large_insert";
    String create_stmt = String.format(
        "CREATE TABLE %s (h int PRIMARY KEY, c varchar);", tableName);
    String exceptionString = null;
    session.execute(create_stmt);
    String ins_stmt = "INSERT INTO test_large_insert (h, c) VALUES (1, ?);";
    String value = RandomStringUtils.randomAscii(STRING_SIZE);
    session.execute(ins_stmt, value);
    String sel_stmt = String.format("SELECT h, c FROM %s"
        + " WHERE h = 1 ;", tableName);
    Row row = runSelect(sel_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals(value, row.getString(1));
    try {
      session.execute(ins_stmt, value+"Too much");
    }
    catch (Exception e) {
      exceptionString = e.getCause().getMessage();
    }
    assertTrue(exceptionString.contains(errorMessage));
  }

  @Test
  public void testInsertWithLargeNumbers() throws Exception {
    String tableName = "test_insert_with_large_numbers";
    session.execute(String.format("CREATE TABLE %s (c1 bigint, c2 double, PRIMARY KEY(c1))",
      tableName));

    // Invalid
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2) values (9223372036854775808, 1)",
      tableName));
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2) values (1, " +
      "9223372036854775808e9223372036854775808)", tableName));
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2) values (1, " +
      "-9223372036854775808e9223372036854775808)", tableName));
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2) values (-9223372036854775809, 1)",
      tableName));

    // Valid
    session.execute(String.format("INSERT INTO %s (c1, c2) values (9223372036854775807, 1)",
      tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2) values (-9223372036854775808, 1)",
      tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2) values (1, " +
      "1.7e308)", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2) values (1, " +
      "-1.7e308)", tableName));
  }

  @Test
  public void testInsertWithTTL() throws Exception {
    String tableName = "test_insert_with_ttl";
    createTable(tableName);

    // Now insert with ttl.
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1) VALUES(%d, 'h%d', %d, 'r%d', %d) USING TTL 2;",
      tableName, 1, 2, 3, 4, 5);
    session.execute(insert_stmt);

    insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v2) VALUES(%d, 'h%d', %d, 'r%d', 'v%d') USING TTL 4;",
      tableName, 1, 2, 3, 4, 6);
    session.execute(insert_stmt);

    String select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
      + "  WHERE h1 = 1 AND h2 = 'h2';", tableName);

    // Verify row is present.
    Row row = runSelect(select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertEquals(5, row.getInt(4));
    assertEquals("v6", row.getString(5));

    // Now verify v1 expires.
    TestUtils.waitForTTL(2000L);
    row = runSelect(select_stmt).next();
    assertEquals(1, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(3, row.getInt(2));
    assertEquals("r4", row.getString(3));
    assertTrue(row.isNull(4));
    assertEquals("v6", row.getString(5));

    // Now verify v2 expires.
    TestUtils.waitForTTL(2000L);
    assertNoRow(select_stmt);
  }

  private String getInsertStmt(String tableName, String ttlSeconds) {
    return String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1) VALUES(%d, 'h%d', %d, 'r%d', %d) USING TTL %s;",
      tableName, 1, 2, 3, 4, 5, ttlSeconds);
  }

  private void runInvalidInsertWithTTL(String tableName, String ttlSeconds) {
    runInvalidStmt(getInsertStmt(tableName, ttlSeconds));
  }

  private void runValidInsertWithTTL(String tableName, String ttlSeconds) {
    session.execute(getInsertStmt(tableName, ttlSeconds));

    // Verify TTL.
    List<Row> rows = session.execute(String.format("SELECT ttl(v1) FROM %s", tableName)).all();
    assertEquals(1, rows.size());
    // TTL should be atleast within 10 seconds of what we set it to.
    assertTrue(Math.abs(Long.valueOf(ttlSeconds).longValue() - rows.get(0).getLong(0)) <= 10);
  }

  @Test
  public void testInvalidInsertWithTTL() throws Exception {
    String tableName = "test_insert_with_invalid_ttl";
    createTable(tableName);

    runValidInsertWithTTL(tableName, "1000");
    runValidInsertWithTTL(tableName, "0");
    runValidInsertWithTTL(tableName, String.valueOf(MAX_TTL_SEC - 1));
    runValidInsertWithTTL(tableName, String.valueOf(MAX_TTL_SEC));
    // Test ttl() function with no ttl.
    session.execute(String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1) VALUES(%d, 'h%d', %d, 'r%d', %d);", tableName,
      1, 2, 3, 4, 5));
    List<Row> rows = session.execute(String.format("SELECT ttl(v1) FROM %s", tableName)).all();
    assertEquals(1, rows.size());
    assertEquals(0, rows.get(0).getLong(0));

    runInvalidInsertWithTTL(tableName, String.valueOf(MAX_TTL_SEC + 1));
    runInvalidInsertWithTTL(tableName, String.valueOf(Long.MAX_VALUE));
    runInvalidInsertWithTTL(tableName, "1000.1");
    runInvalidInsertWithTTL(tableName, "abcxyz");
    runInvalidInsertWithTTL(tableName, "-1");
    runInvalidInsertWithTTL(tableName, "0x80");
    runInvalidInsertWithTTL(tableName, "true");
    runInvalidInsertWithTTL(tableName, "false");
  }

  @Test
  public void testResetTTL() throws Exception {
    // Setting TTL to 0 should reset the TTL.
    String tableName = "test_reset_ttl";
    createTable(tableName);

    // Insert two rows with TTL 1000
    String insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d') USING TTL " +
        "2;",
      tableName, 1, 2, 3, 4, 5, 6);
    session.execute(insert_stmt);

    insert_stmt = String.format(
      "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, 'h%d', %d, 'r%d', %d, 'v%d') USING TTL " +
        "2;",
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

    TestUtils.waitForTTL(2000L);

    // Verify row has expired.
    assertNoRow(select_stmt);

    // Verify row with TTL reset survives.
    select_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
      + "  WHERE h1 = 10 AND h2 = 'h20';", tableName);
    Row row = runSelect(select_stmt).next();
    assertEquals(10, row.getInt(0));
    assertEquals("h20", row.getString(1));
    assertEquals(30, row.getInt(2));
    assertEquals("r40", row.getString(3));
    assertEquals(50, row.getInt(4));
    assertEquals("v60", row.getString(5));
  }

  @Test
  public void testInsertWithInet() throws Exception {
    String tableName = "table_with_inet";
    session.execute(String.format("CREATE TABLE %s (c1 inet, c2 inet, c3 int, c4 " +
      "inet, c5 inet, PRIMARY KEY(c1, c2, c3));", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values ('1.2.3.4', " +
      "'fe80::2978:9018:b288:3f6c', 1, 'fe80::9929:23c3:8309:c29f', '10.10.10.10');", tableName));
    ResultSet rs = session.execute(String.format("SELECT c1, c2, c3, c4, c5 FROM %s WHERE c1 = " +
      "'1.2.3.4' AND c2 = 'fe80::2978:9018:b288:3f6c' AND c3 = 1;", tableName));
    Row row = rs.one();

    assertEquals(InetAddress.getByName("1.2.3.4"), row.getInet("c1"));
    assertEquals(InetAddress.getByName("fe80::2978:9018:b288:3f6c"), row.getInet("c2"));
    assertEquals(1, row.getInt("c3"));
    assertEquals(InetAddress.getByName("fe80::9929:23c3:8309:c29f"), row.getInet("c4"));
    assertEquals(InetAddress.getByName("10.10.10.10"), row.getInet("c5"));
    assertNull(rs.one());

    // Try blobs and int.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values ('0xff', " +
        "'4294967295', 1, '0xffffffff', '291913250');", tableName));
    row = session.execute(String.format("SELECT c1, c2, c3, c4, c5 FROM %s WHERE c1 = '0xff';",
      tableName)).one();
    assertEquals(InetAddress.getByName("0.0.0.255"), row.getInet("c1"));
    assertEquals(InetAddress.getByName("255.255.255.255"), row.getInet("c2"));
    assertEquals(1, row.getInt("c3"));
    assertEquals(InetAddress.getByName("255.255.255.255"), row.getInet("c4"));
    assertEquals(InetAddress.getByName("17.102.62.34"), row.getInet("c5"));

    // Now try a bunch of invalid inserts.
    // 1.2.3.400 invalid IPv4
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values ('1.2.3.400', " +
      "'fe80::2978:9018:b288:3f6c', 1, 'fe80::9929:23c3:8309:c29f', '10.10.10.10');", tableName));
    // fe80::2978:9018:b288:3z6c invalid IPv6
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values ('1.2.3.4', " +
      "'fe80::2978:9018:b288:3z6c', 1, 'fe80::9929:23c3:8309:c29f', '10.10.10.10');", tableName));
    // Invalid types.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (1, " +
      "'fe80::2978:9018:b288:3f6c', 1, 'fe80::9929:23c3:8309:c29f', '10.10.10.10');", tableName));
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (3.1, " +
      "'fe80::2978:9018:b288:3f6c', 1, 'fe80::9929:23c3:8309:c29f', '10.10.10.10');", tableName));
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (true, " +
      "'fe80::2978:9018:b288:3f6c', 1, 'fe80::9929:23c3:8309:c29f', '10.10.10.10');", tableName));
  }

  @Test
  public void testInsertWithUuid() throws Exception {
    String tableName = "table_with_uuid";
    session.execute(String.format("CREATE TABLE %s (c1 uuid, c2 uuid, c3 int, c4 " +
      "uuid, c5 uuid, PRIMARY KEY(c3));", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-4173-bced-0eba570d969e, " +
      "c57c4b82-ef52-2073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));
    // TODO: Test for UUID in where clause after literals.
    ResultSet rs = session.execute(
      "SELECT c1, c2, c3, c4, c5 FROM table_with_uuid WHERE c3 = ?",
      1);
    Row row = rs.one();

    assertEquals(UUID.fromString("467c4b82-ef22-4173-bced-0eba570d969e"), row.getUUID("c1"));
    assertEquals(UUID.fromString("c57c4b82-ef52-2073-aced-0eba570d969e"), row.getUUID("c2"));
    assertEquals(1, row.getInt("c3"));
    assertEquals(UUID.fromString("157c4b82-ff32-1073-bced-0eba570d969e"), row.getUUID("c4"));
    assertEquals(UUID.fromString("b57c4b82-ef52-1173-bced-0eba570d969e"), row.getUUID("c5"));
    assertNull(rs.one());

    // Now try a bunch of invalid inserts.
    // Short UUIDs for c1.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "67c4b82-ef22-4173-bced-0eba570d969e, " +
      "c57c4b82-ef52-2073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-e22-4173-bced-0eba570d969e, " +
      "c57c4b82-ef52-2073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-413-bced-0eba570d969e, " +
      "c57c4b82-ef52-2073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-4173-bced-0eba570d96, " +
      "c57c4b82-ef52-2073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    // Invalid type for c1.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "2, " +
      "c57c4b82-ef52-2073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    // Invalid UUID for c2.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-4173-bced-0eba570d969e, " +
      "X57c4b82-ef52-2073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));
  }


  @Test
  public void testInsertWithTimeUuid() throws Exception {
    String tableName = "table_with_timeuuid";
    session.execute(String.format("CREATE TABLE %s (c1 timeuuid, c2 timeuuid, c3 int, " +
      "c4 timeuuid, c5 timeuuid, PRIMARY KEY(c3));", tableName));
    session.execute(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-1173-bced-0eba570d969e, " +
      "c57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));
    // TODO: Test for UUID in where clause after literals.
    ResultSet rs = session.execute(
      "SELECT c1, c2, c3, c4, c5 FROM table_with_timeuuid WHERE c3 = ?",
      1);
    Row row = rs.one();

    assertEquals(UUID.fromString("467c4b82-ef22-1173-bced-0eba570d969e"), row.getUUID("c1"));
    assertEquals(UUID.fromString("c57c4b82-ef52-1073-aced-0eba570d969e"), row.getUUID("c2"));
    assertEquals(1, row.getInt("c3"));
    assertEquals(UUID.fromString("157c4b82-ff32-1073-bced-0eba570d969e"), row.getUUID("c4"));
    assertEquals(UUID.fromString("b57c4b82-ef52-1173-bced-0eba570d969e"), row.getUUID("c5"));
    assertNull(rs.one());

    // Now try a bunch of invalid inserts.
    // Short TimeUUIDs for c1.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "67c4b82-ef22-1173-bced-0eba570d969e, " +
      "c57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-e22-1173-bced-0eba570d969e, " +
      "c57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-113-bced-0eba570d969e, " +
      "c57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-1173-bced-0eba570d96, " +
      "c57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    // Invalid type for c1.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "2, " +
      "c57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    // Invalid UUID for c2.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-1173-bced-0eba570d969e, " +
      "X57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-1073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));

    // Valid UUID for c1, invalid TIMEUUID for c4.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (" +
      "467c4b82-ef22-1173-bced-0eba570d969e, " +
      "c57c4b82-ef52-1073-aced-0eba570d969e, 1, " +
      "157c4b82-ff32-4073-bced-0eba570d969e, " +
      "b57c4b82-ef52-1173-bced-0eba570d969e);", tableName));
  }

  @Test
  public void testInsertIntoSystemNamespace() throws Exception {
    runInvalidStmt("INSERT INTO system.peers (h1, h2, r1, r2) VALUES (1, '1', 1, '1');");
    runInvalidStmt("DELETE FROM system.peers WHERE h1 = 1 AND h2 = '1' AND r1 = 1 AND r2 = '1';");
  }

  @Test
  public void testInsertIntoRecreatedTable() throws Exception {
    // Create table with a TIMESTAMP column and insert a value.
    SimpleDateFormat df = new SimpleDateFormat("yyyy-MM-dd HH:mm:ssZ");
    df.setTimeZone(TimeZone.getTimeZone("+0000"));
    Date timestamp = df.parse("2017-01-01 00:00:00+0000");
    session.execute("CREATE TABLE t (h int PRIMARY KEY, c timestamp);");
    session.execute(String.format("INSERT INTO t (h, c) VALUES (1, '%s');", df.format(timestamp)));

    // Verify the timestamp is inserted.
    Row row = runSelect("SELECT * FROM t;").next();
    assertEquals(1, row.getInt(0));
    assertEquals(df.format(timestamp), df.format(row.getTimestamp(1)));

    // Drop and create a new table of the same name but different column datatype. Insert a value.
    session.execute("DROP TABLE t;");
    session.execute("CREATE TABLE t (h int PRIMARY KEY, c varchar);");
    session.execute("INSERT INTO t (h, c) VALUES (1, 'hello');");

    // Verify the value is inserted.
    assertQuery("SELECT * FROM t;", "Row[1, hello]");
  }

  @Test
  public void testInsertNewlineCharacter() throws Exception {
    // Create table with a TIMESTAMP column and insert a value.
    session.execute("CREATE TABLE tab (t text PRIMARY KEY);");
    session.execute(String.format("INSERT INTO tab (t) VALUES ('\n');"));

    // Verify the value.
    Row row = runSelect("SELECT * FROM tab;").next();
    assertEquals("\n", row.getString(0));
  }

  @Test
  public void testInsertUnset() throws Exception {
    session.execute("create table t (k int primary key, v1 int, v2 int);");

    // First insert a key value
    String insertPos = "insert into t (k, v1, v2) values (?, ?, ?);";
    String insertNamed = "insert into t (k, v1, v2) values (:k, :v1, :v2);";
    PreparedStatement insertPosStmt = session.prepare(insertPos);
    session.execute(insertPosStmt.bind(1, 1, 1));
    session.execute(insertPosStmt.bind(2, 2, 2));
    session.execute(insertPosStmt.bind(3, 3, 3));
    session.execute(insertPosStmt.bind(4, 4, 4));

    // Then, insert the same PK with an unset value using positional binding
    BoundStatement bstmt = insertPosStmt.bind().setInt(0, 1);
    bstmt = bstmt.setInt(1, 100);
    bstmt.unset(2);
    session.execute(bstmt);

    // Test that the originally inserted value remains unchanged
    assertQuery("select k, v1, v2 from t where k = 1;", "Row[1, 100, 1]");

    // Now, insert the same PK with an unset value using named binding
    PreparedStatement insertNamedStmt = session.prepare(insertNamed);
    BoundStatement bstmt1 = insertNamedStmt.bind().setInt("k", 2);
    bstmt1 = bstmt1.setInt("v1", 200);
    bstmt1.unset("v2");
    session.execute(bstmt1);

    // Test that the originally inserted value remains unchanged
    assertQuery("select k, v1, v2 from t where k = 2;", "Row[2, 200, 2]");

    // test if unset works within BatchStatement as well
    BatchStatement batch = new BatchStatement();
    BoundStatement bstmt2 = insertPosStmt.bind(3, 3, 3);
    batch.add(bstmt2);
    BoundStatement bstmt3 = insertPosStmt.bind().setInt(0, 4);
    bstmt3.unset(1);
    bstmt3 = bstmt3.setInt(2, 400);
    batch.add(bstmt3);
    session.execute(batch);

    // Test that the originally inserted value remains unchanged
    assertQuery("select k, v1, v2 from t where k in (3, 4);", "Row[3, 3, 3]Row[4, 4, 400]");
  }

  protected static enum Bind { BY_POS, BY_NAME };

  protected void testBindExec(
      PreparedStatement preparedStmt, Bind bindMode, int colIdx, int v1Value) throws Exception {
    final String[] columnNames = new String[] {"h1", "h2", "r1", "r2", "v1", "v2"};
    LOG.info("TEST: Unset column " + colIdx + " (" + columnNames[colIdx] + ") bind " + bindMode);
    BoundStatement bstmt = preparedStmt.bind();

    for (int i = 0; i < 6; ++i) {
      if (bindMode == Bind.BY_POS) {
        if (i == colIdx) {
          // Unset value using binding by position.
          bstmt.unset(colIdx);
        } else {
          switch (i) {
            case 4: bstmt.setInt(4, v1Value); break; // v1
            case 5: bstmt.setInt(5, 200); break; // v2
            default: bstmt.setInt(i, i + 1); // PK = {1, 2, 3, 4}
          }
        }
      } else { // bindMode == Bind.BY_NAME
        if (i == colIdx) {
          // Unset value using binding by name.
          bstmt.unset(columnNames[colIdx]);
        } else {
          switch (i) {
            case 4: bstmt.setInt(columnNames[4], v1Value); break; // v1
            case 5: bstmt.setInt(columnNames[5], 200); break; // v2
            default: bstmt.setInt(columnNames[i], i + 1); // PK = {1, 2, 3, 4}
          }
        }
      }
    }

    if (colIdx < 4) { // PK
      try {
        session.execute(bstmt);
        fail("Query did not fail with null primary key column");
      } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
        LOG.info("Expected exception", e);
        final String nullPKError = "Null Argument for Primary Key";
        assertTrue("Error message '" + e.getMessage() + "' should contain '" + nullPKError + "'",
            e.getMessage().contains(nullPKError));
      }
    } else {
      session.execute(bstmt); // Should be successful for non-PK column.
    }
  }

  @Test
  public void testInsertUnsetMultiKey() throws Exception {
    session.execute("create table t (h1 int, h2 int, r1 int, r2 int, v1 int, v2 int, " +
                    "primary key((h1, h2), r1, r2))");

    String insertPos = "insert into t (h1, h2, r1, r2, v1, v2) values (?, ?, ?, ?, ?, ?);";
    PreparedStatement insertPosStmt = session.prepare(insertPos);
    String insertNamed =
        "insert into t (h1, h2, r1, r2, v1, v2) values (:h1, :h2, :r1, :r2, :v1, :v2);";
    PreparedStatement insertNamedStmt = session.prepare(insertNamed);
    // Insert a key value: (1, 2, 3, 4) -> (5, NULL).
    session.execute(insertPosStmt.bind(1, 2, 3, 4, 5));

    // Test hash key column h2 - expected error.
    testBindExec(insertPosStmt, Bind.BY_POS, 1, 100);
    testBindExec(insertNamedStmt, Bind.BY_NAME, 1, 100);

    // Test range key column r2 - expected error.
    testBindExec(insertPosStmt, Bind.BY_POS, 3, 100);
    testBindExec(insertNamedStmt, Bind.BY_NAME, 3, 100);

    // Test that the originally inserted value remains unchanged.
    assertQuery("select * from t", "Row[1, 2, 3, 4, 5, NULL]");

    // Set v2 = 6.
    session.execute(insertPosStmt.bind(1, 2, 3, 4, 5, 6));

    // Test non-key column v2. Ensure v2 is unchanged.
    testBindExec(insertPosStmt, Bind.BY_POS, 5, 300);
    assertQuery("select * from t", "Row[1, 2, 3, 4, 300, 6]");
    testBindExec(insertNamedStmt, Bind.BY_NAME, 5, 400);
    assertQuery("select * from t", "Row[1, 2, 3, 4, 400, 6]");
  }
}
