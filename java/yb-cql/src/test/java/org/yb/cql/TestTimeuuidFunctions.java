//
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

import java.util.List;
import java.util.Calendar;
import java.util.UUID;
import java.util.Date;
import java.util.GregorianCalendar;
import java.util.TimeZone;

import com.datastax.driver.core.Row;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.PreparedStatement;
import org.junit.Test;


import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestTimeuuidFunctions extends BaseCQLTest {

  // A grand day! millis at 00:00:00.000 15 Oct 1582.
  public static final long GREGORIAN_CALENDAR_OFFSET = -12219292800000L;

  // This is the time difference expected between the timestamp you get using now()
  // and the timestamp JAVA code returns.
  public static final long TIME_LIMIT = 10000;

  public static final long MILLIS_IN_SEC = 1000;

  // This function gets the UNIX timestamp in milliseconds from the
  // Gregorian timestamp in nanoseconds.
  long getUnixTimestamp(UUID timeuuid) {
    return (timeuuid.timestamp() / 10000) + GREGORIAN_CALENDAR_OFFSET;
  }

  void checkCorrectTimestamp(List<Row> rows) {
    assertEquals(1, rows.size());
    assertFalse(rows.get(0).isNull("v"));
    UUID timeuuid = rows.get(0).getUUID("v");
    Date now = new Date();
    long timeInMillis = now.getTime();
    assertTrue(Math.abs(timeInMillis - getUnixTimestamp(timeuuid)) <= TIME_LIMIT);
  }

  void checkTimestampIsCloseToExpected(String table_name, int key, int year,
                                       int month, int date,
                                       int hourOfDay, int minute, int second) {
    ResultSet rs = session.execute("select v from " + table_name + " where k = " + key + ";");
    Calendar cal = new GregorianCalendar(TimeZone.getTimeZone("UTC"));
    cal.set(year, month, date, hourOfDay, minute, second);
    Date date_ = cal.getTime();
    assertTrue(Math.abs(rs.one().getTimestamp("v").getTime() -
                            date_.getTime()) <= MILLIS_IN_SEC);
  }

  @Test
  public void testNowFunction() throws Exception {

    // This test is to verify that the now() function works
    // and inserts a value of type TIMEUUID.
    session.execute("create table test_now (k varchar primary key, v timeuuid);");
    session.execute("insert into test_now (k, v) values ('a', now());");
    session.execute("insert into test_now (k, v) values ('b', now());");
    session.execute("insert into test_now (k, v) values ('c', now());");

    ResultSet rs = session.execute("select * from test_now;");
    assertEquals(3, rs.all().size());

    checkCorrectTimestamp(session.execute("select v from test_now where k = 'a';").all());
    checkCorrectTimestamp(session.execute("select v from test_now where k = 'b';").all());
    checkCorrectTimestamp(session.execute("select v from test_now where k = 'c';").all());

    List<Row> rowA = session.execute("select v from test_now where k = 'a';").all();
    List<Row> rowC = session.execute("select v from test_now where k = 'c';").all();

    assertTrue(getUnixTimestamp(rowA.get(0).getUUID("v")) <
                   getUnixTimestamp(rowC.get(0).getUUID("v")));

  }

  @Test
  public void testToUnixTimestampFunction() throws Exception {

    // Test that toUnixTimestamp() works.
    session.execute("create table test_tounixtimestamp (k int primary key, v bigint);");
    session.execute("insert into test_tounixtimestamp " +
                        "(k, v) values (1, tounixtimestamp(now()));");
    session.execute("insert into test_tounixtimestamp " +
                        "(k, v) values (2, tounixtimestamp(now()));");
    ResultSet rs = session.execute("select * from test_tounixtimestamp;");
    assertEquals(2, rs.all().size());

    session.execute
        ("insert into test_tounixtimestamp (k, v) values (5, " +
             "tounixtimestamp(8e6e1416-e447-11e7-80c1-9a214cf093ae));");
    session.execute
        ("insert into test_tounixtimestamp (k, v) values (6, " +
             "tounixtimestamp(35ed600c-e448-11e7-80c1-9a214cf093ae));");

    ResultSet rsA = session.execute("select v from test_tounixtimestamp where k = 5;");
    ResultSet rsB = session.execute("select v from test_tounixtimestamp where k = 6;");
    assertEquals(1513638164147L, rsA.one().getLong("v"));
    assertEquals(1513638445161L, rsB.one().getLong("v"));
  }

  @Test
  public void testUnixTimestampOfFunction() throws Exception {

    // Test that unixtimestampof() works.
    session.execute("create table test_unixtimestampof (k int primary key, v bigint);");
    session.execute("insert into test_unixtimestampof " +
                        "(k, v) values (1, unixtimestampof(now()));");
    session.execute("insert into test_unixtimestampof (k, v) " +
                        "values (2, unixtimestampof(now()));");
    ResultSet rs = session.execute("select * from test_unixtimestampof;");
    assertEquals(2, rs.all().size());

    session.execute
        ("insert into test_unixtimestampof (k, v) values " +
             "(5, unixtimestampof(8e6e1416-e447-11e7-80c1-9a214cf093ae));");
    session.execute
        ("insert into test_unixtimestampof (k, v) values " +
             "(6, unixtimestampof(35ed600c-e448-11e7-80c1-9a214cf093ae));");

    ResultSet rsA = session.execute("select v from test_unixtimestampof where k = 5;");
    ResultSet rsB = session.execute("select v from test_unixtimestampof where k = 6;");

    assertEquals(1513638164147L, rsA.one().getLong("v"));
    assertEquals(1513638445161L, rsB.one().getLong("v"));
  }


  @Test
  public void testToTimestampFunction() throws Exception {

    // Test that totimestamp() works.
    session.execute("create table test_totimestamp (k int primary key, v timestamp);");
    session.execute("insert into test_totimestamp " +
                        "(k, v) values (1, totimestamp(now()));");
    session.execute("insert into test_totimestamp " +
                        "(k, v) values (2, totimestamp(now()));");
    ResultSet rs = session.execute("select * from test_totimestamp;");
    assertEquals(2, rs.all().size());

    session.execute
        ("insert into test_totimestamp (k, v) values (10, " +
             "totimestamp(8e6e1416-e447-11e7-80c1-9a214cf093ae));");
    session.execute
        ("insert into test_totimestamp (k, v) values (11, " +
             "totimestamp(80045516-e45b-11e7-80c1-9a214cf093ae));");
    session.execute
        ("insert into test_totimestamp (k, v) values (12, " +
             "totimestamp(4a749d10-e45c-11e7-b1e1-cbbb3feaf02c));");

    checkTimestampIsCloseToExpected("test_totimestamp", 10, 2017,
                                    Calendar.DECEMBER, 18, 23, 2, 44);

    checkTimestampIsCloseToExpected("test_totimestamp", 11, 2017,
                                    Calendar.DECEMBER, 19, 1, 25, 29);

    checkTimestampIsCloseToExpected("test_totimestamp", 12, 2017,
                                    Calendar.DECEMBER, 19, 1, 31, 9);
  }

  @Test
  public void testTimestampToUnixTimestampFunction() throws Exception {

    // Test that tounixtimestamp() works.
    session.execute("create table test_uxtimestamp (k int primary key, v timestamp);");
    session.execute
        ("insert into test_uxtimestamp (k, v) values (10, " +
             "totimestamp(8e6e1416-e447-11e7-80c1-9a214cf093ae));");
    session.execute
        ("insert into test_uxtimestamp (k, v) values (11, " +
             "totimestamp(80045516-e45b-11e7-80c1-9a214cf093ae));");
    session.execute
        ("insert into test_uxtimestamp (k, v) values (12, " +
             "totimestamp(4a749d10-e45c-11e7-b1e1-cbbb3feaf02c));");
    ResultSet rs = session.execute("select * from test_uxtimestamp;");
    assertEquals(3, rs.all().size());

    rs = session.execute("select tounixtimestamp(v) from test_uxtimestamp where k = 10;");
    assertEquals(1513638164147L, rs.one().getLong(0));
    rs = session.execute("select tounixtimestamp(v) from test_uxtimestamp where k = 11;");
    assertEquals(1513646729901L, rs.one().getLong(0));
    rs = session.execute("select tounixtimestamp(v) from test_uxtimestamp where k = 12;");
    assertEquals(1513647069537L, rs.one().getLong(0));
  }

  @Test
  public void testUUIDComparisonFunction() throws Exception {

    // Test that UUIDs are strictly time ordered and are comparable.
    session.execute("create table test_uuid " +
                        "(k int primary key, v timeuuid);");
    for (int i = 1; i <= 10; i++) {
      session.execute("insert into test_uuid (k, v) values" +
                          " (" + i + ", now());");
    }
    ResultSet rs = session.execute("select * from test_uuid where " +
                                       "v <= now();");
    assertEquals(10, rs.all().size());

    rs = session.execute("select v from test_uuid where k = 5;");
    UUID uuid = rs.one().getUUID(0);
    rs = session.execute("select * from test_uuid where v > " + uuid.toString() + ";");
    assertEquals(5, rs.all().size());
  }

  @Test
  public void testTimestampComparisonFunction() throws Exception {

    // Test that timestamps are strictly time ordered and are comparable.
    session.execute("create table test_ts_comparison " +
                        "(k int primary key, v timestamp);");
    for (int i = 1; i <= 10; i++) {
      session.execute("insert into test_ts_comparison" +
                          " (k, v) values" +
                          " (" + i + ", totimestamp(now()));");
    }
    ResultSet rs = session.execute("select * from test_ts_comparison where " +
                                       "v <= totimestamp(now());");
    assertEquals(10, rs.all().size());
  }

  // Expected min and max values for least-significant bits (clock seq and node components).
  private static final long MIN_TIMEUUID_LBS = 0x0000000000000000L;
  private static final long MAX_TIMEUUID_LBS = 0xffffffffffffffffL;

  private void assertTimeuuidComponents(UUID uuid, long unix_ts, long leastSignificantBits) {
    assertEquals(unix_ts, getUnixTimestamp(uuid));
    assertEquals(leastSignificantBits, uuid.getLeastSignificantBits());
  }

  @Test
  public void testMinMaxTimeuuid() throws Exception {
    session.execute("CREATE TABLE test_minmax (h int, r timeuuid, primary key(h, r))");

    // Check min/max for a fixed timestamp (0).
    {
      session.execute("INSERT INTO test_minmax(h, r) VALUES (1, maxTimeuuid(0))");
      session.execute("INSERT INTO test_minmax(h, r) VALUES (1, minTimeuuid(0))");
      List<Row> rows = session.execute("SELECT * FROM test_minmax").all();
      assertEquals(2, rows.size());

      // Expect min uuid to be first.
      assertTimeuuidComponents(rows.get(0).getUUID("r"), 0, MIN_TIMEUUID_LBS);
      assertTimeuuidComponents(rows.get(1).getUUID("r"), 0, MAX_TIMEUUID_LBS);
    }

    // Check min/max for the current timestamp.
    {
      // Insert a row with 'now()' and check the generated (current) timestamp.
      session.execute("INSERT INTO test_minmax(h, r) VALUES (2, now());");
      List<Row> rows = session.execute("SELECT * FROM test_minmax WHERE h = 2").all();
      assertEquals(1, rows.size());
      UUID uuid = rows.get(0).getUUID("r");

      // Insert min/max timeuuid for that exact timestamp.
      long unix_ts = getUnixTimestamp(uuid);
      session.execute("INSERT INTO test_minmax(h, r) VALUES (2, maxTimeuuid(" + unix_ts + "))");
      session.execute("INSERT INTO test_minmax(h, r) VALUES (2, minTimeuuid(" + unix_ts + "))");

      // Also insert a max for the previous timestamp and a min for the next timestamp.
      session.execute(
          "INSERT INTO test_minmax(h, r) VALUES (2, maxTimeuuid(" + (unix_ts - 1) + "))");
      session.execute(
          "INSERT INTO test_minmax(h, r) VALUES (2, minTimeuuid(" + (unix_ts + 1) + "))");

      // Check all rows (values and ordering).
      rows = session.execute("SELECT * FROM test_minmax WHERE h = 2").all();
      assertEquals(5, rows.size());
      assertTimeuuidComponents(rows.get(0).getUUID("r"), unix_ts - 1, MAX_TIMEUUID_LBS);
      assertTimeuuidComponents(rows.get(1).getUUID("r"), unix_ts, MIN_TIMEUUID_LBS);
      assertEquals(uuid, rows.get(2).getUUID("r"));
      assertTimeuuidComponents(rows.get(3).getUUID("r"), unix_ts, MAX_TIMEUUID_LBS);
      assertTimeuuidComponents(rows.get(4).getUUID("r"), unix_ts + 1, MIN_TIMEUUID_LBS);

      // Check partial rows ('<' condition).
      rows = session.execute(
          "SELECT * FROM test_minmax WHERE h = 2 AND r < maxTimeuuid(" + unix_ts + ")").all();
      assertEquals(3, rows.size());
      assertTimeuuidComponents(rows.get(0).getUUID("r"), unix_ts - 1, MAX_TIMEUUID_LBS);
      assertTimeuuidComponents(rows.get(1).getUUID("r"), unix_ts, MIN_TIMEUUID_LBS);
      assertEquals(uuid, rows.get(2).getUUID("r"));

      // Check partial rows ('>=' condition with query which requires filtering -- no hash key).
      rows = session.execute(
          "SELECT * FROM test_minmax WHERE r >= minTimeuuid(" + unix_ts + ")").all();
      assertEquals(4, rows.size());
      assertTimeuuidComponents(rows.get(0).getUUID("r"), unix_ts, MIN_TIMEUUID_LBS);
      assertEquals(uuid, rows.get(1).getUUID("r"));
      assertTimeuuidComponents(rows.get(2).getUUID("r"), unix_ts, MAX_TIMEUUID_LBS);
      assertTimeuuidComponents(rows.get(3).getUUID("r"), unix_ts + 1, MIN_TIMEUUID_LBS);

      // Test aggregate functions: min()/max().
      rows = session.execute("SELECT min(r), max(r) FROM test_minmax WHERE h = 2").all();
      assertEquals(1, rows.size());
      assertTimeuuidComponents(rows.get(0).getUUID(0), unix_ts - 1, MAX_TIMEUUID_LBS);
      assertTimeuuidComponents(rows.get(0).getUUID(1), unix_ts + 1, MIN_TIMEUUID_LBS);
    }
  }

  @Test
  public void testBindForTimestampAndTimeuuid() throws Exception {

    // Test that bound variables work for timeuuid/timestamp functions.
    session.execute("create table test_bind " +
                        "(k int primary key, v1 timestamp, v2 timeuuid, v3 bigint);");
    PreparedStatement preparedStatement = session.prepare
        ("insert into test_bind (k, v1, v2, v3) values " +
             "(?, totimestamp(now()), now(), tounixtimestamp(now()));");
    session.execute(preparedStatement.bind(1));
    session.execute(preparedStatement.bind(2));
    List<Row> rows1 = session.execute("select * from test_bind where k = 1;").all();
    List<Row> rows2 = session.execute("select * from test_bind where k = 2;").all();
    assertNotEquals(rows1.get(0).getTimestamp("v1"), rows2.get(0).getTimestamp("v1"));
    assertNotEquals(rows1.get(0).getUUID("v2"), rows2.get(0).getUUID("v2"));
    assertNotEquals(rows1.get(0).getLong("v3"), rows2.get(0).getLong("v3"));
  }
}
