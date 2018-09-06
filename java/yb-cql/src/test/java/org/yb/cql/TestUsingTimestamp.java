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

import com.datastax.driver.core.DataType;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import org.junit.Test;
import org.yb.client.TestUtils;

import java.math.BigInteger;
import java.util.*;

import static junit.framework.TestCase.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestUsingTimestamp extends BaseCQLTest {

  private void createTimestampTable(String tableName) {
    session.execute(String.format(
        "CREATE TABLE %s (c1 int, c2 int, c3 int, PRIMARY KEY((c1), c2));",
        tableName));
  }

  private ResultSet runSelectStmt(int c1, int c2, String tableName) {
    return session.execute(String.format("SELECT * FROM %s WHERE c1 = %d and c2 = %d",
        tableName, c1, c2));
  }

  private void assertRow(ResultSet result, Integer[] arr) {
    assertRow(result, arr, new Integer[]{});
  }

  private void assertRow(ResultSet result, Integer[] arr, Integer[] collectionArr) {
    List<Row> rows = result.all();
    assertEquals(1, rows.size());
    assertEquals(rows.get(0).getColumnDefinitions().size(),
        arr.length + (collectionArr.length == 0 ? 0 : 1));
    for (int i = 0; i < arr.length; i++) {
      if (arr[i] == null) {
        assertTrue(rows.get(0).isNull(i));
      } else {
        assertEquals(arr[i].intValue(), rows.get(0).getInt(i));
      }
    }

    if (collectionArr.length != 0) {
      DataType dataType = rows.get(0).getColumnDefinitions().getType(arr.length);
      Iterator<Integer> iterator = null;
      switch (dataType.getName()) {
        case SET:
          iterator = rows.get(0).getSet(arr.length, Integer.class).iterator();
          break;
        case LIST:
          iterator = rows.get(0).getList(arr.length, Integer.class).iterator();
          break;
        default:
          fail("Invalid type: " + dataType);
      }

      int index = 0;
      while (iterator.hasNext()) {
        int val = iterator.next();
        assertEquals(collectionArr[index].intValue(), val);
        index++;
      }
    }
  }

  private long getWriteTime(ResultSet result) {
    List<Row> rows = result.all();
    assertEquals(1, rows.size());
    return rows.get(0).getLong(0);
  }

  private void assertWriteTime(ResultSet result, Long writetime) {
    assertEquals(writetime.longValue(), getWriteTime(result));
  }

  @Test
  public void testHighestTimestamp() {
    String tableName = "testBasicOperations";
    createTimestampTable(tableName);

    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TIMESTAMP " +
        "1000", tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 3});

    // Verify lowest timestamp has no effect.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 4) USING TIMESTAMP " +
        "%d", tableName, Long.MIN_VALUE + 1));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 3});

    // Overwrite with higher timestamp.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 4) USING TIMESTAMP " +
        "2000", tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 4});

    // Overwrite with same timestamp, newer value wins.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 100) USING TIMESTAMP" +
        " 2000", tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 100});

    // Negative timestamps are allowed.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, -1) USING " +
        "TIMESTAMP -1", tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 100});

    // Try overwrite with lower timestamp.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 5) USING TIMESTAMP " +
        "500", tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 100});

    // Overwrite with regular statement
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 5)", tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 5});

    // Try to overwrite with very large timestamp.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 6) USING TIMESTAMP " +
        "%d", tableName, Long.MAX_VALUE));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 6});

    // Regular INSERT would still hide a very high timestamp.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 7)", tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 7});
  }

  @Test
  public void testTTLAndTimestamp() throws Exception {
    String tableName = "testTTLAndTimestamp";
    createTimestampTable(tableName);

    // Last value in the list always wins.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TIMESTAMP " +
        "1000 AND TTL 1000 AND TIMESTAMP 2000 and TTL 2", tableName));

    // Verify timestamp.
    assertWriteTime(session.execute(String.format("SELECT writetime(c3) FROM %s WHERE c1 = 1 " +
        "and c2 = 2", tableName)), 2000L);

    TestUtils.waitForTTL(2000L);

    // Row is now expired.
    assertEquals(0, runSelectStmt(1, 2, tableName).all().size());
  }

  @Test
  public void testInvalidTimestamp() throws Exception {
    String tableName = "testInvalidTimestamp";
    createTimestampTable(tableName);

    // Long.MIN_VALUE is not allowed.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) " +
        "USING TIMESTAMP %d", tableName, Long.MIN_VALUE));

    // Need to be within bounds of a 64 bit int.
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) " +
            "USING TIMESTAMP %s", tableName,
        new BigInteger(Long.toString(Long.MAX_VALUE)).add(new BigInteger("1"))));
    runInvalidStmt(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) " +
            "USING TIMESTAMP %s", tableName,
        new BigInteger(Long.toString(Long.MIN_VALUE)).subtract(new BigInteger("1"))));
  }

  @Test
  public void testPrimaryKeyTimestamp() throws Exception {
    String tableName = "testPrimaryKeyTimestamp";
    createTimestampTable(tableName);

    session.execute(String.format("INSERT into %s (c1, c2, c3) values (2, 3, 6) USING " +
        "TIMESTAMP 1000;", tableName));
    // Increase timestamp for primary key.
    session.execute(String.format("INSERT into %s (c1, c2) values (2, 3) USING " +
        "TIMESTAMP 2000;", tableName));

    // The ttl should only affect the column c3.
    session.execute(String.format("INSERT into %s (c1, c2, c3) values (2, 3, 7) USING " +
        "TIMESTAMP 1500 and TTL 2;", tableName));

    assertRow(runSelectStmt(2, 3, tableName), new Integer[]{2, 3, 7});

    TestUtils.waitForTTL(2000L);

    // Column is now expired.
    assertRow(runSelectStmt(2, 3, tableName), new Integer[]{2, 3, null});

    // Now lets try the other way around.
    session.execute(String.format("INSERT into %s (c1, c2, c3) values (20, 30, 60) USING " +
        "TIMESTAMP 1000;", tableName));

    // Higher timestamp for the column.
    session.execute(String.format("UPDATE %s USING TIMESTAMP 2000 SET c3 = 70 WHERE c1 = 20 and " +
        "c2 = 30", tableName));

    assertRow(runSelectStmt(20, 30, tableName), new Integer[]{20, 30, 70});

    // Verify its writetime.
    assertWriteTime(session.execute(String.format("SELECT writetime(c3) FROM %s WHERE c1 = 20 " +
        "and c2 = 30", tableName)), 2000L);

    // Try to overwrite column with a lower timestamp.
    session.execute(String.format("INSERT into %s (c1, c2, c3) values (20, 30, 80) USING " +
        "TIMESTAMP 1500 and TTL 2;", tableName));
    assertRow(runSelectStmt(20, 30, tableName), new Integer[]{20, 30, 70});

    TestUtils.waitForTTL(2000L);

    // Row still exists since the column lives on.
    assertRow(runSelectStmt(20, 30, tableName), new Integer[]{20, 30, 70});

    // Setting the column to null takes out the whole row.
    session.execute(String.format("UPDATE %s SET c3 = null WHERE c1 = 20 and c2 = 30", tableName));
    assertEquals(0, runSelectStmt(20, 30, tableName).all().size());
  }

  @Test
  public void testSet() {
    String tableName = "testSet";
    session.execute(String.format("CREATE TABLE %s (c1 int, c2 int, c3 set<int>, PRIMARY KEY (" +
        "(c1), c2))", tableName));

    session.execute(String.format("INSERT into %s (c1, c2, c3) values (1, 2, {1, 2, 3}) USING" +
        " TIMESTAMP 1000;", tableName));

    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2}, new Integer[]{1, 2, 3});

    // Modifying a collection with using timestamp is not allowed.
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = c3 + " +
        "{4} WHERE c1=1 and c2=2;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = c3 - " +
        "{4} WHERE c1=1 and c2=2;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 =  {4} WHERE c1=1 and c2=2;",
        tableName));
    runInvalidStmt(String.format("INSERT into %s (c1, c2, c3) values (1, 2, c3 + {4}) USING" +
        " TIMESTAMP 2000;", tableName));
    runInvalidStmt(String.format("INSERT into %s (c1, c2, c3) values (1, 2, c3 - {4}) USING" +
        " TIMESTAMP 2000;", tableName));

    // Higher timestamp wins.
    session.execute(String.format("INSERT into %s (c1, c2, c3) values (1, 2, {1, 2, 4}) USING" +
        " TIMESTAMP %d;", tableName, Long.MAX_VALUE));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2}, new Integer[]{1, 2, 4});

    // Insert without timestamp always wins.
    session.execute(String.format("INSERT into %s (c1, c2, c3) values (1, 2, {1, 2, 5})",
        tableName));
    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2}, new Integer[]{1, 2, 5});
  }

  @Test
  public void testList() {
    // Test for list.
    String tableName = "testList";
    session.execute(String.format("CREATE TABLE %s (c1 int, c2 int, c3 list<int>, PRIMARY KEY (" +
        "(c1), c2))", tableName));

    session.execute(String.format("INSERT into %s (c1, c2, c3) values (1, 2, [1, 2, 3]) USING" +
        " TIMESTAMP 1000;", tableName));

    session.execute(String.format("INSERT into %s (c1, c2, c3) values (1, 2, [1, 2, 4]) USING" +
        " TIMESTAMP 2000;", tableName));

    assertRow(runSelectStmt(1, 2, tableName), new Integer[] { 1 , 2 }, new Integer[] { 1, 2, 4});

    // Invalid Statements.
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = c3 + " +
        "[4] WHERE c1=1 and c2=2;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = c3 - " +
        "[4] WHERE c1=1 and c2=2;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = [4] WHERE c1=1 and c2=2;",
        tableName));
    runInvalidStmt(String.format("INSERT into %s (c1, c2, c3) values (1, 2, c3 + [4]) USING" +
        " TIMESTAMP 2000;", tableName));
    runInvalidStmt(String.format("INSERT into %s (c1, c2, c3) values (1, 2, c3 - [4]) USING" +
        " TIMESTAMP 2000;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3[1] = 3 WHERE c1=1 and " +
        "c2=2;", tableName));
    runInvalidStmt(String.format("DELETE c3[1] FROM %s USING TIMESTAMP 500 WHERE c1=1 and" +
        "c2=2;", tableName));
  }

  @Test
  public void testMap() {
    // Test for list.
    String tableName = "testMap";
    session.execute(String.format("CREATE TABLE %s (c1 int, c2 int, c3 map<int, int>, PRIMARY KEY" +
        " ((c1), c2))", tableName));

    session.execute(String.format("INSERT into %s (c1, c2, c3) values (1, 2, {1:1, 2:2, 3:3}) " +
        "USING TIMESTAMP 1000;", tableName));

    session.execute(String.format("INSERT into %s (c1, c2, c3) values (1, 2, {1:1, 2:2, 4:4}) " +
        "USING TIMESTAMP 2000;", tableName));

    List<Row> rows = runSelectStmt(1, 2, tableName).all();
    assertEquals(1, rows.size());
    assertEquals(1, rows.get(0).getInt(0));
    assertEquals(2, rows.get(0).getInt(1));
    assertEquals(1, rows.get(0).getMap(2, Integer.class, Integer.class).get(1).intValue());
    assertEquals(2, rows.get(0).getMap(2, Integer.class, Integer.class).get(2).intValue());
    assertEquals(4, rows.get(0).getMap(2, Integer.class, Integer.class).get(4).intValue());

    // Invalid Statements.
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = c3 + " +
        "{4:4} WHERE c1=1 and c2=2;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = c3 - " +
        "{4:4} WHERE c1=1 and c2=2;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3 = {4:4} WHERE c1=1 and " +
        "c2=2;", tableName));
    runInvalidStmt(String.format("INSERT into %s (c1, c2, c3) values (1, 2, c3 + {4:4}]) USING" +
        " TIMESTAMP 2000;", tableName));
    runInvalidStmt(String.format("INSERT into %s (c1, c2, c3) values (1, 2, c3 - {4:4}) USING" +
        " TIMESTAMP 2000;", tableName));
    runInvalidStmt(String.format("UPDATE %s USING TIMESTAMP 500 SET c3[4] = 3 WHERE c1=1 and " +
        "c2=2;", tableName));
    runInvalidStmt(String.format("DELETE c3[1] FROM %s USING TIMESTAMP 500 WHERE c1=1 and" +
        "c2=2;", tableName));
  }

  @Test
  public void testDeleteAndTTL() throws Exception {
    String tableName = "testDeleteAndTTL";
    createTimestampTable(tableName);

    // Insert with a timestamp, delete and insert at higher timestamp.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (10, 20, 30) USING " +
        "TIMESTAMP 1000", tableName));
    assertRow(runSelectStmt(10, 20, tableName), new Integer[]{10, 20, 30});

    // Now delete.
    session.execute(String.format("DELETE FROM %s WHERE c1 = 10 and c2 = 20", tableName));
    assertEquals(0, runSelectStmt(10, 20, tableName).getAvailableWithoutFetching());

    // Insert at higher timestamp, row still shouldn't be visible.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (10, 20, 30) USING " +
        "TIMESTAMP 2000", tableName));
    assertEquals(0, runSelectStmt(10, 20, tableName).getAvailableWithoutFetching());

    // Insert and delete a row.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3)", tableName));
    // Validate write time.
    long writeTime = getWriteTime(session.execute(String.format("SELECT writetime(c3) FROM %s " +
        "WHERE c1 = 1 and c2 = 2", tableName)));
    assertTrue(System.currentTimeMillis() * 1000 >= writeTime);
    assertTrue(System.currentTimeMillis() * 1000 - writeTime <= 3000000); // Within 3 seconds.

    assertRow(runSelectStmt(1, 2, tableName), new Integer[]{1, 2, 3});

    session.execute(String.format("DELETE FROM %s WHERE c1 = 1 and c2 = 2", tableName));
    assertEquals(0, runSelectStmt(1, 2, tableName).all().size());

    // Now inserts at a lower timestamp don't work.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 4) USING TIMESTAMP " +
        "%d", tableName, writeTime - 1));
    assertEquals(0, runSelectStmt(1, 2, tableName).all().size());

    // Insert a row with TTL
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (10, 20, 30) USING TTL 2",
        tableName));
    writeTime = getWriteTime(session.execute(String.format("SELECT writetime(c3) FROM %s " +
        "WHERE c1 = 10 and c2 = 20", tableName)));
    assertRow(runSelectStmt(10, 20, tableName), new Integer[]{10, 20, 30});

    // Let it expire.
    TestUtils.waitForTTL(2000L);
    assertEquals(0, runSelectStmt(10, 20, tableName).all().size());

    // Now inserts at a lower timestamp don't work.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (10, 20, 40) USING " +
        "TIMESTAMP %d", tableName, writeTime - 1));
    assertEquals(0, runSelectStmt(10, 20, tableName).all().size());
  }

  private void insertColumnsWithDifferentTimestamps(String tableName, int initValue, long initTs,
                                                    int c3Update, int c4Update, int c5Update,
                                                    long c3Ts, long c4Ts, long c5Ts) {
    session.execute(String.format("INSERT INTO %s (c1, c2, c3, c4, c5) values (%d, %d, %d, %d, " +
        "%d) USING TIMESTAMP %d", tableName, initValue, initValue * 2, initValue * 3, initValue
        * 4, initValue * 5, initTs));
    session.execute(String.format("UPDATE %s USING TIMESTAMP %d SET c3 = %d WHERE c1 = %d AND " +
        "c2 = %d", tableName, c3Ts, c3Update, initValue, initValue * 2));
    session.execute(String.format("UPDATE %s USING TIMESTAMP %d SET c4 = %d WHERE c1 = %d AND " +
        "c2 = %d", tableName, c4Ts, c4Update, initValue, initValue * 2));
    session.execute(String.format("UPDATE %s USING TIMESTAMP %d SET c5 = %d WHERE c1 = %d AND " +
        "c2 = %d", tableName, c5Ts, c5Update, initValue, initValue * 2));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = %d and c2 = %d",
        tableName, initValue, initValue * 2)), new Integer[]{initValue, initValue * 2, c3Update,
        c4Update, c5Update});
  }

  private void deleteAndVerifyColumns(String tableName, Integer c1, Integer c2, Integer c3,
                                      Integer c4, Integer c5, long deleteTs) {
    session.execute(String.format("DELETE FROM %s USING TIMESTAMP %d WHERE c1 = %d and c2 = %d ",
        tableName, deleteTs, c1, c2));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = %d and c2 = %d",
        tableName, c1, c2)), new Integer[]{c1, c2, c3, c4, c5});
  }

  @Test
  public void testDeleteUsingTimestamp() throws Exception {
    String tableName = "testDeleteUsingTimestamp";
    createTimestampTable(tableName);

    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TIMESTAMP " +
      "5000", tableName));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)), new Integer[]{1, 2, 3});

    // Delete at lower timestamp.
    session.execute(String.format("DELETE FROM %s USING TIMESTAMP 4000 WHERE c1 = 1 and c2 = 2 ",
      tableName));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)), new Integer[]{1, 2, 3});

    // Delete at higher timestamp.
    session.execute(String.format("DELETE FROM %s USING TIMESTAMP 6000 WHERE c1 = 1 and c2 = 2 ",
      tableName));
    assertEquals(0, session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)).getAvailableWithoutFetching());

    // Regular insert and delete at very high timestamp.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3)", tableName));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)), new Integer[]{1, 2, 3});

    session.execute(String.format("DELETE FROM %s USING TIMESTAMP %d WHERE c1 = 1 and c2 = 2 ",
      tableName, Long.MAX_VALUE));
    assertEquals(0, session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)).getAvailableWithoutFetching());

    // Inserts with lower timestamp don't work, but an insert without a timestamp works.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3) USING TIMESTAMP " +
      "%d", tableName, Long.MAX_VALUE - 1));
    assertEquals(0, session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)).getAvailableWithoutFetching());

    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (1, 2, 3)", tableName));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)), new Integer[]{1, 2, 3});

    // Insert with a very high timestamp, hidden with a regular delete.
    session.execute(String.format("INSERT INTO %s (c1, c2, c3) values (10, 20, 30) USING " +
      "TIMESTAMP %d", tableName, Long.MAX_VALUE));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 10 and c2 = 20",
      tableName)), new Integer[]{10, 20, 30});

    // Delete at lower timestamp than insert will not have any effect.
    session.execute(String.format("DELETE FROM %s USING TIMESTAMP %d WHERE c1 = 1 and c2 = 2 ",
      tableName, Long.MAX_VALUE - 1));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 10 and c2 = 20",
      tableName)), new Integer[]{10, 20, 30});

    // Delete without using timestamp (but effectively at a much lower timestamp), successfully
    // deletes the row.
    session.execute(String.format("DELETE FROM %s WHERE c1 = 10 and c2 = 20 ", tableName));
    assertEquals(0, session.execute(String.format("SELECT * FROM %s WHERE c1 = 10 and c2 = 20",
      tableName)).getAvailableWithoutFetching());

    // Test with a different table schema.
    session.execute(String.format("DROP TABLE %s", tableName));
    session.execute(String.format(
      "CREATE TABLE %s (c1 int, c2 int, c3 int, c4 int, c5 int, PRIMARY KEY((c1), c2));",
      tableName));

    // Different time for each column.
    insertColumnsWithDifferentTimestamps(tableName, 1, 5000, 30, 40, 50, 6000, 7000, 8000);

    // Now try deletes.
    deleteAndVerifyColumns(tableName, 1, 2, 30, 40, 50, 5500);

    // c3 should be deleted.
    deleteAndVerifyColumns(tableName, 1, 2, null, 40, 50, 6500);

    // c4 should be deleted.
    deleteAndVerifyColumns(tableName, 1, 2, null, null, 50, 7500);

    // Everything null, since the liveness column is gone too.
    session.execute(String.format("DELETE FROM %s USING TIMESTAMP 8500 WHERE c1 = 1 and c2 = 2 ",
      tableName));
    assertEquals(0, session.execute(String.format("SELECT * FROM %s WHERE c1 = 1 and c2 = 2",
      tableName)).getAvailableWithoutFetching());

    // Try to delete individual columns and verify the liveness column still exists.
    insertColumnsWithDifferentTimestamps(tableName, 100, 5000, 3000, 4000, 5000, 6000, 7000, 8000);

    // Delete c3.
    session.execute(String.format("DELETE c3 FROM %s USING TIMESTAMP 6500 WHERE c1 = 100 and c2 =" +
        " 200 ", tableName));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 100 and c2 = 200",
        tableName)), new Integer[]{100, 200, null, 4000, 5000});

    // Delete c4.
    session.execute(String.format("DELETE c4 FROM %s USING TIMESTAMP 7500 WHERE c1 = 100 and c2 =" +
        " 200 ", tableName));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 100 and c2 = 200",
        tableName)), new Integer[]{100, 200, null, null, 5000});

    // Delete c5, row still exists due to primary key column.
    session.execute(String.format("DELETE c5 FROM %s USING TIMESTAMP 8500 WHERE c1 = 100 and c2 =" +
        " 200 ", tableName));
    assertRow(session.execute(String.format("SELECT * FROM %s WHERE c1 = 100 and c2 = 200",
        tableName)), new Integer[]{100, 200, null, null, null});

    // USING TTL is not supported with delete.
    runInvalidStmt(String.format("DELETE FROM %s USING TTL 1000 WHERE c1 = 1 and c2 = 2",
      tableName));
  }
}
