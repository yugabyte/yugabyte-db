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

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.Row;

import org.junit.Test;

import java.util.*;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestBooleanDataType extends BaseCQLTest {

  @Test
  public void testPrimaryKey() throws Exception {
    // Create test table with boolean hash and clustering columns (ascending/descending).
    session.execute("create table bool_test (h boolean, r1 boolean, r2 boolean, v boolean," +
                    " primary key ((h), r1, r2)) with clustering order by (r1 asc, r2 desc);");

    // Insert rows with all possible combinations of values.
    BatchStatement batch = new BatchStatement();
    PreparedStatement stmt = session.prepare("insert into bool_test (h, r1, r2, v) "+
                                             "values (?, ?, ?, ?);");
    List<Boolean> values = Arrays.asList(false, true);
    for (Boolean h : values) {
      for (Boolean r1 : values) {
        for (Boolean r2 : values) {
          batch.add(stmt.bind(h, r1, r2, r2));
        }
      }
    }
    session.execute(batch);

    // Verify select by a hash key and clustering order.
    assertQuery(new SimpleStatement("select h, r1, r2 from bool_test where h = ?;", true),
                "Row[true, false, true]" +
                "Row[true, false, false]" +
                "Row[true, true, true]" +
                "Row[true, true, false]");

    // Verify select by a hash key and range bound.
    assertQuery(new SimpleStatement("select h, r1, r2 from bool_test where h = ? and r1 > ?;",
                                    true, false),
                "Row[true, true, true]" +
                "Row[true, true, false]");

    // Verify min/max aggregate functions on boolean type.
    assertQuery("select min(v), max(v) from bool_test", "Row[false, true]");
  }

  @Test
  public void testIndex() throws Exception {
    // Create table and index on boolean column.
    session.execute("create table bool_test (k int primary key, v1 int, v2 boolean)" +
                    " with transactions = { 'enabled' : true };");
    session.execute("create index bool_test_idx on bool_test (v2, v1);");

    waitForReadPermsOnAllIndexes("bool_test");

    // Populate rows with alternating true/false.
    final int KEY_COUNT = 10;
    for (int k = 1; k <= KEY_COUNT; k++) {
      session.execute("insert into bool_test (k, v1, v2) values (?, ?, ?);",
                      k, 100 + k, k % 2 == 0);
    }

    // Select all rows that are false.
    assertQuery("select * from bool_test where v2 = false;",
                "Row[1, 101, false]" +
                "Row[3, 103, false]" +
                "Row[5, 105, false]" +
                "Row[7, 107, false]" +
                "Row[9, 109, false]");
  }

  @Test
  public void testCast() throws Exception {
    // Create table.
    session.execute("create table bool_test (k int primary key, b boolean);");

    // Insert boolean values and verify the values casted as text.
    Map<Integer, String> map = new HashMap<Integer, String>();
    map.put(1, "false");
    map.put(2, "false");
    for(Map.Entry<Integer, String> entry : map.entrySet()) {
      session.execute(String.format("insert into bool_test (k, b) values (%d, %s);",
                                    entry.getKey(), entry.getValue()));
      assertEquals(entry.getValue(),
                   session.execute("select cast(b as text) from bool_test where k = ?;",
                                   entry.getKey()).one().getString(0));
    }
  }

  @Test
  public void testComparison() throws Exception {
    // Setup table
    session.execute("create table bool_test(h int, v boolean, primary key(h))");
    session.execute("insert into bool_test(h, v) values (1, true)");
    session.execute("insert into bool_test(h, v) values (2, false)");

    // Check boolean equality: without hash condition.
    Iterator<Row> rows = runSelect("SELECT v FROM bool_test WHERE v = true");
    // Expecting one row, with value 'true'.
    assertTrue(rows.hasNext());
    assertTrue(rows.next().getBool("v"));
    assertFalse(rows.hasNext());

    // Check boolean equality: with hash condition.
    rows = runSelect("SELECT v FROM bool_test WHERE h = 2 AND v = false");
    // Expecting one row, with value 'false'.
    assertTrue(rows.hasNext());
    assertFalse(rows.next().getBool("v"));
    assertFalse(rows.hasNext());

    // Check boolean comparison: less than.
    rows = runSelect("SELECT v FROM bool_test WHERE v < true");
    // Expecting one row, with value 'false'.
    assertTrue(rows.hasNext());
    assertFalse(rows.next().getBool("v"));
    assertFalse(rows.hasNext());

    // Check boolean comparison: greater or equal.
    rows = runSelect("SELECT v FROM bool_test WHERE v >= false");
    // Expecting two rows, 'true' and 'false', in any order.
    assertTrue(rows.hasNext());
    boolean first = rows.next().getBool("v");
    assertTrue(rows.hasNext());
    boolean second = rows.next().getBool("v");
    assertNotEquals(first, second);
    assertFalse(rows.hasNext());
  }
}
