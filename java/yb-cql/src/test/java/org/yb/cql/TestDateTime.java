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

import java.util.*;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.LocalDate;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import org.junit.Test;
import org.junit.runner.RunWith;

import org.yb.YBTestRunner;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

@RunWith(value=YBTestRunner.class)
public class TestDateTime extends BaseCQLTest {

  @Test
  public void testDDL() throws Exception {
    // Verify a table can be created with date and time partition, clustering and regular columns
    // with both ascending and descending clustering orders.
    session.execute("create table test_dt (h0 int, h1 date, h2 time," +
                    " r0 text, r1 date, r2 time, r3 date, r4 time," +
                    " v0 text, v1 date, v2 time, primary key ((h0, h1, h2), r0, r1, r2, r3, r4))" +
                    " with clustering order by (r0 asc, r1 asc, r2 asc, r3 desc, r4 desc);");
  }

  @Test
  public void testDML() throws Exception {
    // Create test table with date and time partition and clustering columns (ascending/descending).
    session.execute("create table test_dt (h0 int, h1 date, h2 time," +
                    " r0 text, r1 date, r2 time, r3 date, r4 time," +
                    " v0 text, v1 date, v2 time, primary key ((h0, h1, h2), r0, r1, r2, r3, r4))" +
                    " with clustering order by (r0 asc, r1 asc, r2 asc, r3 desc, r4 desc);");

    final List<LocalDate> dates = Arrays.asList(LocalDate.fromYearMonthDay(2018, 1, 3),
                                                LocalDate.fromYearMonthDay(2018, 8, 12),
                                                LocalDate.fromYearMonthDay(2018, 12, 27));
    final List<Long> times = Arrays.asList(1421L, 511451341L, 4587789231163L);

    // Insert rows.
    PreparedStatement stmt = session.prepare(
        "insert into test_dt (h0, h1, h2, r0, r1, r2, r3, r4, v0, v1, v2)" +
        " values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    BatchStatement batch = new BatchStatement();
    for (LocalDate r1 : dates) {
      for (long r2 : times) {
        for (LocalDate r3 : dates) {
          for (long r4 : times) {
            batch.add(stmt.bind(
                1, LocalDate.fromYearMonthDay(2018, 2, 14), 1001L,
                "r", r1, r2, r3, r4,
                "v", r1, r2));
          }
        }
      }
    }
    session.execute(batch);

    // Verify date and time values in descending order.
    assertQuery(new SimpleStatement(
        "select r3, r4, v0, v1, v2 from test_dt where h0 = ? and h1 = ? and h2 = ? and" +
        " r0 = ? and r1 < ? and r2 < ?;",
        1, LocalDate.fromYearMonthDay(2018, 2, 14), 1001L,
        "r", LocalDate.fromYearMonthDay(2018, 1, 4), 1422L),
                "Row[2018-12-27, 4587789231163, v, 2018-01-03, 1421]" +
                "Row[2018-12-27, 511451341, v, 2018-01-03, 1421]" +
                "Row[2018-12-27, 1421, v, 2018-01-03, 1421]" +
                "Row[2018-08-12, 4587789231163, v, 2018-01-03, 1421]" +
                "Row[2018-08-12, 511451341, v, 2018-01-03, 1421]" +
                "Row[2018-08-12, 1421, v, 2018-01-03, 1421]" +
                "Row[2018-01-03, 4587789231163, v, 2018-01-03, 1421]" +
                "Row[2018-01-03, 511451341, v, 2018-01-03, 1421]" +
                "Row[2018-01-03, 1421, v, 2018-01-03, 1421]");

    // Verify date and time values in ascending order.
    assertQuery(new SimpleStatement(
        "select r1, r2, v0, v1, v2 from test_dt where h0 = ? and h1 = ? and h2 = ? and" +
        " r0 = ? and r3 = ? and r4 = ?;",
        1, LocalDate.fromYearMonthDay(2018, 2, 14), 1001L,
        "r", LocalDate.fromYearMonthDay(2018, 1, 3), 1421L),
                "Row[2018-01-03, 1421, v, 2018-01-03, 1421]" +
                "Row[2018-01-03, 511451341, v, 2018-01-03, 511451341]" +
                "Row[2018-01-03, 4587789231163, v, 2018-01-03, 4587789231163]" +
                "Row[2018-08-12, 1421, v, 2018-08-12, 1421]" +
                "Row[2018-08-12, 511451341, v, 2018-08-12, 511451341]" +
                "Row[2018-08-12, 4587789231163, v, 2018-08-12, 4587789231163]" +
                "Row[2018-12-27, 1421, v, 2018-12-27, 1421]" +
                "Row[2018-12-27, 511451341, v, 2018-12-27, 511451341]" +
                "Row[2018-12-27, 4587789231163, v, 2018-12-27, 4587789231163]");

    session.execute("update test_dt set v1 = ?, v2 = ? where h0 = ? and h1 = ? and h2 = ? and" +
                    " r0 = ? and r1 = ? and r2 = ? and r3 = ? and r4 = ?;",
                    LocalDate.fromYearMonthDay(2018, 2, 14), 20180214L,
                    1, LocalDate.fromYearMonthDay(2018, 2, 14), 1001L,
                    "r", LocalDate.fromYearMonthDay(2018, 1, 3), 1421L,
                    LocalDate.fromYearMonthDay(2018, 1, 3), 1421L);
    session.execute("delete from test_dt where h0 = ? and h1 = ? and h2 = ? and" +
                    " r0 = ? and r1 = ? and r2 = ? and r3 = ? and r4 = ?;",
                    1, LocalDate.fromYearMonthDay(2018, 2, 14), 1001L,
                    "r", LocalDate.fromYearMonthDay(2018, 1, 3), 1421L,
                    LocalDate.fromYearMonthDay(2018, 8, 12), 1421L);

    // Rows are updated / deleted.
    assertQuery(new SimpleStatement(
        "select r3, r4, v0, v1, v2 from test_dt where h0 = ? and h1 = ? and h2 = ? and" +
        " r0 = ? and r1 < ? and r2 < ?;",
        1, LocalDate.fromYearMonthDay(2018, 2, 14), 1001L,
        "r", LocalDate.fromYearMonthDay(2018, 1, 4), 1422L),
                "Row[2018-12-27, 4587789231163, v, 2018-01-03, 1421]" +
                "Row[2018-12-27, 511451341, v, 2018-01-03, 1421]" +
                "Row[2018-12-27, 1421, v, 2018-01-03, 1421]" +
                "Row[2018-08-12, 4587789231163, v, 2018-01-03, 1421]" +
                "Row[2018-08-12, 511451341, v, 2018-01-03, 1421]" +
                "Row[2018-01-03, 4587789231163, v, 2018-01-03, 1421]" +
                "Row[2018-01-03, 511451341, v, 2018-01-03, 1421]" +
                "Row[2018-01-03, 1421, v, 2018-02-14, 20180214]");

    // Verify min/max aggregate functions on date and time types.
    assertQuery("select min(v1), max(v1), min(v2), max(v2) from test_dt",
                "Row[2018-01-03, 2018-12-27, 1421, 4587789231163]");
  }

  @Test
  public void testLiterals() throws Exception {
    session.execute("create table test_dt (k int primary key, d date, t time);");

    // Test insert with date literals.
    for (String d : Arrays.asList("2018-01-03",
                                  "1999-02-14",
                                  "1960-08-15",
                                  "1517-10-31",
                                  "-4000-01-31",
                                  "-100000-01-01",
                                  "100000-12-31")) {
      session.execute(String.format("insert into test_dt (k, d) values (1, '%s');", d));
      LocalDate date = session.execute("select d from test_dt where k = 1;").one().getDate("d");
      assertEquals(d, date.toString());

      // Test date to string conversion also.
      String s = session.execute("select cast(d as text) from test_dt where k = 1;")
                 .one().getString(0);
      assertEquals(s, date.toString());
    }

    // Test insert with date literal in non-cardinal form.
    Map<String, LocalDate> map = new HashMap<String, LocalDate>() {{
        put("1-01-2", LocalDate.fromYearMonthDay(1, 1, 2));
        put("11-3-04", LocalDate.fromYearMonthDay(11, 3, 4));
        put("111-5-6", LocalDate.fromYearMonthDay(111, 5, 6));
        put("-11-7-19", LocalDate.fromYearMonthDay(-11, 7, 19));
      }};
    for (Map.Entry<String, LocalDate> e : map.entrySet()) {
      session.execute(String.format("insert into test_dt (k, d) values (1, '%s');", e.getKey()));
      LocalDate d = session.execute("select d from test_dt where k = 1;").one().getDate("d");
      assertEquals(e.getValue(), d);
    }

    // Test insert with time literals.
    for (String t : Arrays.asList("01:12:55.010354728",
                                  "11:47:12.",
                                  "23:59:59")) {
      session.execute(String.format("insert into test_dt (k, t) values (1, '%s');", t));
      long tm = session.execute("select t from test_dt where k = 1;").one().getTime("t");
      LocalTime time = LocalTime.parse(t, DateTimeFormatter.ISO_LOCAL_TIME);
      assertEquals(time.toNanoOfDay(), tm);
    }

    // Test invalid date literals.
    for (String d : Arrays.asList("2018-09",
                                  "201a-01-03",
                                  "1999-13-14",
                                  "1960-08-33")) {
      try {
        session.execute(String.format("insert into test_dt (k, d) values (1, '%s');", d));
        fail(String.format("Invalid date literal '%s' not rejected", d));
      } catch (InvalidQueryException e) { /* expected */ }
    }

    // Test invalid time literals.
    for (String t : Arrays.asList("12:00",
                                  "123:45:00",
                                  "23:61:00",
                                  "23:59:61",
                                  "23:59:00 pm")) {
      try {
        session.execute(String.format("insert into test_dt (k, t) values (1, '%s');", t));
        fail(String.format("Invalid time literal '%s' not rejected", t));
      } catch (InvalidQueryException e) { /* expected */ }
    }
  }

  @Test
  public void testIndex() throws Exception {
    // Create table and index on date and time columns, include additional date and time columns.
    session.execute("create table test_dt (k int primary key, d1 date, t1 time, d2 date, t2 time)" +
                    " with transactions = { 'enabled' : true };");
    session.execute("create index test_dt_idx on test_dt (d1, t1) include (d2, t2);");

    // Insert a row with date and time column. Verify it can be selected by the date and time
    // values.
    session.execute("insert into test_dt (k, d1, t1, d2, t2) values" +
                    " (1, '2018-2-14', 1234, '1999-4-11', 4321);");
    assertQuery("select * from test_dt where d1 = '2018-02-14' and t1 = 1234;",
                "Row[1, 2018-02-14, 1234, 1999-04-11, 4321]");

    // Update the date and time column. Verify the row can be selected by the date and time
    // values with the updated values and the original values have been removed from index.
    session.execute("update test_dt set d1 = '2014-12-25', t1 = 2234," +
                    " d2 = '2014-12-31', t2 = 3234 where k = 1;");
    assertQuery("select * from test_dt where d1 = '2014-12-25' and t1 = 2234;",
                "Row[1, 2014-12-25, 2234, 2014-12-31, 3234]");
    assertQuery("select * from test_dt where d1 = '2018-2-14' and t1 = 1234;",
                "");

    // Delete a column and verify the delete.
    session.execute("delete d2 from test_dt where k = 1;");
    assertQuery("select * from test_dt where d1 = '2014-12-25' and t1 = 2234;",
                "Row[1, 2014-12-25, 2234, NULL, 3234]");

    // Delete the row and verify the delete.
    session.execute("delete from test_dt where k = 1;");
    assertQuery("select * from test_dt where d1 = '2014-12-25' and t1 = 2234;",
                "");
  }
}
