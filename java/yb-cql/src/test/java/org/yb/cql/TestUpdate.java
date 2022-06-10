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

import com.datastax.driver.core.PreparedStatement;
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

  @Test
  public void testUpdateWithIgnoreNullJsonbAttributes() throws Exception {
    String tableName = "test_update_with_ignore_null_jsonb_attributes";

    // Testing with multiple json cols to ensure for loops work fine internally.
    // Using multiple json attributes in each json col for the same reason.
    session.execute(
      String.format("create table %s (h int, r int, v1 jsonb, v2 jsonb, " +
        "primary key (h, r)) with transactions = {'enabled' : true};", tableName));

    // Tests summary -
    //   1-4 are for new rows inserted/updated via the UPDATE statement.
    //   5 tries to update all existing attrbutes to null with ignore_null_jsonb_attributes=false
    //      - the row isn't removed
    //   6 tries to add a new row with all json attrs as null with ignore_null_jsonb_attributes=true
    //      - no new row is added
    //   7 same as 6 along with RETURN STATUS AS ROW
    //
    //  [applied] | [message]                                              | h    | r    | v1   | v2
    //  ----------+--------------------------------------------------------+------+------+------+---
    //    False | No update performed as all JSON cols are set to 'null' | null | null | null | null
    //
    //   8 same as 6 along with IF clause
    //  [applied]
    //  ---------
    //      False
    //
    //   9-10 UPDATE statement with both cases of ignore_null_jsonb_attributes on an existing row
    //      which was added using INSERT statement

    // Test 1: Verify 'null's are not written for new json attributes if
    // ignore_null_jsonb_attributes=true.
    session.execute("update " + tableName +
      " set v1->'a' = '1', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = '2', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': true};");
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":1}, {\"b\":2}]");

    // Write 'c' so that we have more than 1 attrs in the cols on which we can perform test 2.
    session.execute("update " + tableName +
      " set v1->'c' = '3', v2->'c' = '3' where h = 1 and r = 1;");

    // Test 2: Verify 'null's don't overwrite existing values if ignore_null_jsonb_attributes=true.
    session.execute("update " + tableName +
      " set v1->'a' = 'null', v1->'b' = '2', v1->'c' = 'null', " +
      " v2->'a' = '1', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': true};");
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":1,\"b\":2,\"c\":3}, {\"a\":1,\"b\":2,\"c\":3}]");

    session.execute("truncate table " + tableName);

    // Test 3: Verify 'null's are written for new json attributes if
    // ignore_null_jsonb_attributes=false.
    session.execute("update " + tableName +
      " set v1->'a' = '1', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = '2', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': false};");
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":1,\"b\":null,\"c\":null}, {\"a\":null,\"b\":2,\"c\":null}]");

    // Write other cols so that we have more than 1 attrs in the cols on which we can perform
    // test 4.
    session.execute("update " + tableName +
      " set v1->'b' = '2', v1->'c' = '3', v2->'a' = '1', v2->'c' = '3' where h = 1 and r = 1;");

    // Test 4: Verify 'null's overwrite existing values if ignore_null_jsonb_attributes=false.
    session.execute("update " + tableName +
      " set v1->'a' = 'null', v1->'b' = '20', v1->'c' = 'null'," +
      " v2->'a' = '10', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': false};");
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":20,\"c\":null}, {\"a\":10,\"b\":null,\"c\":null}]");

    // Test 5: Verify if setting all JSON attrs to 'null' for an existing row doesn't delete the
    // row if ignore_null_jsonb_attributes=false
    session.execute("update " + tableName +
      " set v1->'b' = 'null', v2->'a' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': false};");
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":null,\"c\":null}, {\"a\":null,\"b\":null,\"c\":null}]");

    session.execute("truncate table " + tableName);

    // Test 6: If all json attrs are set to 'null' and the row is absent, a new row won't
    // be added if ignore_null_jsonb_attributes=true.
    session.execute("update " + tableName +
      " set v1->'a' = 'null', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': true};");
    assertQueryRowsUnorderedWithoutDups(String.format("SELECT * FROM %s", tableName));

    // Test 7: same as 6 along with RETURN STATUS AS ROW
    assertQueryRowsUnorderedWithoutDups(
      "update " + tableName +
      " set v1->'a' = 'null', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 returns status as row" +
      " WITH options = {'ignore_null_jsonb_attributes': true};",
      "Row[false, No update performed as all JSON cols are set to 'null', NULL, NULL, NULL, NULL]");

    // Test 8: same as 6 along with IF clause
    assertQueryRowsUnorderedWithoutDups(
      "update " + tableName +
      " set v1->'a' = 'null', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 if exists " +
      " WITH options = {'ignore_null_jsonb_attributes': true};",
      "Row[false]");

    // INSERT a row with jsonb nulls and UPDATE it with and without
    // ignore_null_jsonb_attributes set.
    session.execute("insert into " + tableName +
      "(h, r, v1, v2) values (1, 1, '{\"a\": null, \"b\": 2}', '{\"a\": 1, \"b\": null}');");

    // Test 9: UPDATE with ignore_null_jsonb_attributes=true will ignore new attributes with null
    // and avoid overwriting existing attributes.
    session.execute("update " + tableName +
      " set v1->'a' = 'null', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': true};");
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":2}, {\"a\":1,\"b\":null}]");

    // Test 10: UPDATE with ignore_null_jsonb_attributes=false will add new attributes with null and
    // overwriting existing attributes.
    session.execute("update " + tableName +
      " set v1->'a' = 'null', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null_jsonb_attributes': false};");
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":null,\"c\":null}, {\"a\":null,\"b\":null,\"c\":null}]");

    // Test 11: UPDATE with unrecognized option
    runInvalidStmt("update " + tableName +
      " set v1->'a' = 'null', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options = {'ignore_null': false};", "Unknown options property ");
    runInvalidStmt("update " + tableName +
      " set v1->'a' = 'null', v1->'b' = 'null', v1->'c' = 'null'," +
      " v2->'a' = 'null', v2->'b' = 'null', v2->'c' = 'null'" +
      " where h = 1 and r = 1 WITH options={'ignore_null_jsonb_attributes': true}" +
      " and options={'ignore_null_jsonb_attributes':true};",
      "Duplicate Update Property");
  }

  @Test
  public void testUpdateWithIgnoreNullJsonbAttributesWithPreparedStmt() throws Exception {
    String tableName = "test_update_with_ignore_null_jsonb_attributes";

    // Testing with multiple json cols to ensure for loops work fine internally.
    // Using multiple json attributes in each json col for the same reason.
    session.execute(
      String.format("create table %s (h int, r int, v1 jsonb, v2 jsonb, " +
        "primary key (h, r)) with transactions = {'enabled' : true};", tableName));

    // Tests summary -
    //   1-8 same as in testUpdateWithIgnoreNullJsonbAttributes
    //   9 checks if binding with YCQL null (not the jsonb 'null') results in error

    PreparedStatement statement_true_case = session.prepare(
      "update " + tableName +
      " set v1->'a' = ?, v1->'b' = ?, v1->'c' = ?," +
      " v2->'a' = ?, v2->'b' = ?, v2->'c' = ?" +
      " where h = ? and r = ? WITH options = {'ignore_null_jsonb_attributes': true};");

    PreparedStatement statement_false_case = session.prepare(
      "update " + tableName +
      " set v1->'a' = ?, v1->'b' = ?, v1->'c' = ?," +
      " v2->'a' = ?, v2->'b' = ?, v2->'c' = ?" +
      " where h = ? and r = ? WITH options = {'ignore_null_jsonb_attributes': false};");

    // Test 1: Verify 'null's are not written for new json attributes if
    // ignore_null_jsonb_attributes=true.
    session.execute(statement_true_case.bind(
      "1", "null", "null", // v1
      "null", "2", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":1}, {\"b\":2}]");

    // Write 'c' so that we have more than 1 attrs in the cols on which we can perform test 2.
    session.execute("update " + tableName +
      " set v1->'c' = '3', v2->'c' = '3' where h = 1 and r = 1;");

    // Test 2: Verify 'null's don't overwrite existing values if ignore_null_jsonb_attributes=true.
    session.execute(statement_true_case.bind(
      "null", "2", "null", // v1
      "1", "null", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":1,\"b\":2,\"c\":3}, {\"a\":1,\"b\":2,\"c\":3}]");

    session.execute("truncate table " + tableName);

    // Test 3: Verify 'null's are written for new json attributes if
    // ignore_null_jsonb_attributes=false.
    session.execute(statement_false_case.bind(
      "1", "null", "null", // v1
      "null", "2", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":1,\"b\":null,\"c\":null}, {\"a\":null,\"b\":2,\"c\":null}]");

    // Write other cols so that we have more than 1 attrs in the cols on which we can perform
    // test 4.
    session.execute("update " + tableName +
      " set v1->'b' = '2', v1->'c' = '3', v2->'a' = '1', v2->'c' = '3' where h = 1 and r = 1;");

    // Test 4: Verify 'null's overwrite existing values if ignore_null_jsonb_attributes=false.
    session.execute(statement_false_case.bind(
      "null", "20", "null", // v1
      "10", "null", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":20,\"c\":null}, {\"a\":10,\"b\":null,\"c\":null}]");

    // Test 5: Verify if setting all JSON attrs to 'null' for an existing row doesn't delete the
    // row if ignore_null_jsonb_attributes=false
    session.execute(statement_false_case.bind(
      "null", "null", "null", // v1
      "null", "null", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":null,\"c\":null}, {\"a\":null,\"b\":null,\"c\":null}]");

    session.execute("truncate table " + tableName);

    // Test 6: If all json attrs are set to 'null' and the row is absent, a new row won't
    // be added if ignore_null_jsonb_attributes=true.
    session.execute(statement_true_case.bind(
      "null", "null", "null", // v1
      "null", "null", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(String.format("SELECT * FROM %s", tableName));

    // INSERT a row with jsonb nulls and UPDATE it with and without
    // ignore_null_jsonb_attributes set.
    session.execute("insert into " + tableName +
      "(h, r, v1, v2) values (1, 1, '{\"a\": null, \"b\": 2}', '{\"a\": 1, \"b\": null}');");

    // Test 7: UPDATE with ignore_null_jsonb_attributes=true will ignore new attributes with null
    // and avoid overwriting existing attributes.
    session.execute(statement_true_case.bind(
      "null", "null", "null", // v1
      "null", "null", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":2}, {\"a\":1,\"b\":null}]");

    // Test 8: UPDATE with ignore_null_jsonb_attributes=false will add new attributes with null and
    // overwriting existing attributes.
    session.execute(statement_false_case.bind(
      "null", "null", "null", // v1
      "null", "null", "null", // v2
      1, 1) // h, r
    );
    assertQueryRowsUnorderedWithoutDups(
      String.format("SELECT * FROM %s", tableName),
      "Row[1, 1, {\"a\":null,\"b\":null,\"c\":null}, {\"a\":null,\"b\":null,\"c\":null}]");

    // Test 9: check that binding with non-string literal null i.e., the real null, results in
    // a failure.
    runInvalidStmt(statement_true_case.bind("1", "2", "3", "1", "2", null, 1, 1),
      "Invalid json type");
  }
}
