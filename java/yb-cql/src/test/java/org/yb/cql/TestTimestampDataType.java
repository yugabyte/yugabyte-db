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

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import org.junit.Test;

import java.text.SimpleDateFormat;
import java.util.*;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestTimestampDataType extends BaseCQLTest {

  // Utility function for timestamp tests: Generates a comprehensive map from valid date-time inputs
  // to corresponding Date values -- includes both integer and string inputs.
  Map<String, Date> generateTimestampMap() {
    Map<String, Date> ts_values = new HashMap<>();
    Calendar cal = new GregorianCalendar();

    // adding some Integer input values
    cal.setTimeInMillis(631238400000L);
    ts_values.put("631238400000", cal.getTime());
    cal.setTimeInMillis(631238434123L);
    ts_values.put("631238434123", cal.getTime());
    cal.setTimeInMillis(631238445000L);
    ts_values.put("631238445000", cal.getTime());

    // Generate String inputs as combinations of valid components (date/time/frac_seconds/timezone).
    int nr_entries = 7;
    String[] dates = {"'1992-06-04", "'1992-6-4", "'1992-06-4", "'1992-06-4",
                      "'1992-06-4", "'1992-06-4"};
    String[] times_no_sec = {"12:30", "15:30", "9:00", "4:30", "12:30", "21:00", "5:30"};
    String[] times = {"12:30:45", "15:30:45", "9:00:45", "4:30:45", "12:30:45",
                      "21:00:45", "5:30:45"};
    String[] times_frac = {"12:30:45.1", "15:30:45.10", "9:00:45.100", "4:30:45.100",
                           "12:30:45.100", "21:00:45.100", "5:30:45.100"};
    // Timezones correspond one-to-one with times
    //   -- so that the UTC-normalized time is the same
    // Subset of supported TZ formats https://docs.oracle.com/cd/E51711_01/DR/ICU_Time_Zones.html
    // Full database can be found at https://www.iana.org/time-zones
    // We support everything that Cassandra supports, like z/Z, +/-0800, +/-08:30 GMT+/-[0]7:00,
    // and we also support UTC+/-[0]9:30 which Cassandra does not support
    String[] timezones = {" UTC'", "+03:00'", " UTC-03:30'",
                          " PST'", "Z'", "+0830'", " GMT-7:00'"};
    for (String date : dates) {
      cal.setTimeZone(TimeZone.getTimeZone("GMT")); // resetting
      cal.setTimeInMillis(0); // resetting
      cal.set(1992, 5, 4); // Java Date month value starts at 0 not 1
      ts_values.put(date + " UTC'", cal.getTime());

      cal.set(Calendar.HOUR_OF_DAY, 12);
      cal.set(Calendar.MINUTE, 30);
      for (int i = 0; i < nr_entries; i++) {
        String time = times_no_sec[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
      cal.set(Calendar.SECOND, 45);
      for (int i = 0; i < nr_entries; i++) {
        String time = times[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
      cal.set(Calendar.MILLISECOND, 100);
      for (int i = 0; i < nr_entries; i++) {
        String time = times_frac[i] + timezones[i];
        ts_values.put(date + " " + time, cal.getTime());
        ts_values.put(date + "T" + time, cal.getTime());
      }
    }
    return ts_values;
  }

  int getMicrosecondTimestampMetricCount(Map<MiniYBDaemon, Metrics> beforeMetrics)
      throws Exception {
    Map<MiniYBDaemon, Metrics> afterMetrics = getAllMetrics();

    int obtainedMetric = 0;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      obtainedMetric += afterMetrics.get(ts).getCounter("cql_microseconds_timestamps_used").value
          - beforeMetrics.get(ts).getCounter("cql_microseconds_timestamps_used").value;
    }
    return obtainedMetric;
  }

  @Test
  public void testInsert() throws Exception {
    String tableName = "test_insert_with_timestamp";
    createTable(tableName, "timestamp");
    // This includes both string (date format) and integer (millisecond) inputs.
    Map<String, Date> ts_values = generateTimestampMap();
    for (String key : ts_values.keySet()) {
      Date date_value = ts_values.get(key);
      String ins_stmt = String.format(
          "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, %s, %d, %s, %d, %s);",
          tableName, 1, key, 2, key, 3, key);
      session.execute(ins_stmt);
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
  public void testInsertVariableTimezone() throws Exception {
    String tableName = "test";
    String newTimestamp = "\'2019-01-26T00:03:16.059 America/New_York\'";
    session.execute(String.format("CREATE TABLE %s(x int primary key, b timestamp);", tableName));
    String ins_stmt = String.format(
            "INSERT INTO %s(x, b) VALUES(%d, %s);",
            tableName, 1, newTimestamp);
    session.execute(ins_stmt);
    String sel_stmt = String.format("SELECT * FROM %s", tableName);
    String gmtTime = runSelect(sel_stmt).next().getTimestamp(1).toGMTString();
    assertTrue(gmtTime.equals("26 Jan 2019 05:03:16 GMT") ||
               gmtTime.equals("26 Jan 2019 04:03:16 GMT"));
  }

  private void runInvalidInsert(String tableName, String ts) {
    String insert_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, %d, %d, %d, %d, '%s');",
        tableName, 1, 2, 3, 4, 5, ts);
    runInvalidStmt(insert_stmt);
  }

  @Test
  public void testInvalidInsert() throws Exception {
    String tableName = "test_insert_with_invalid_timestamp";
    createTable(tableName, "timestamp");

    runInvalidInsert(tableName, "plainstring");
    runInvalidInsert(tableName, "1992:12:11");
    runInvalidInsert(tableName, "1992-11");
    runInvalidInsert(tableName, "1992-13-12");
    runInvalidInsert(tableName, "1992-12-12 14:23:30:31");
    runInvalidInsert(tableName, "1992-12-12 14:23:30.12.32");
    runInvalidInsert(tableName, "2017-12-21 00:00:01.000+123:30");
    runInvalidInsert(tableName, "2017-12-21 00:00:01.0000");
    runInvalidInsert(tableName, "2017-12-21 00:00:01.000Y");
    runInvalidInsert(tableName, "2017-12-21 00:00:01.000 IIST");
    runInvalidInsert(tableName, "2017-12-21 00:00:01.000-700");
    runInvalidInsert(tableName, "2017-12-21 00:00:01.000 Mars/Olympus");
    runInvalidInsert(tableName, "2017-12-21 00:00:01.000 AMERICA/NEW_YORK");
  }

  @Test
  public void testUpdate() throws Exception {
    String tableName = "test_update_with_timestamp";
    createTable(tableName, "timestamp");
    // This includes both string (date format) and integer (millisecond) inputs.
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
  public void testUpdateVariableTimezone() throws Exception {
    String tableName = "test";
    String Timestamp = "\'2019-01-26T00:03:16.059 America/New_York\'";
    String newTimestamp = "\'2019-01-26T00:03:16.059 America/Los_Angeles\'";
    session.execute(String.format("CREATE TABLE %s(x int primary key, b timestamp);", tableName));
    String ins_stmt = String.format(
            "INSERT INTO %s(x, b) VALUES(%d, %s);",
            tableName, 1, newTimestamp);
    session.execute(ins_stmt);
    String upd_stmt = String.format(
            "UPDATE %s SET b = %s WHERE x = 1;", tableName, newTimestamp);
    session.execute(upd_stmt);
    String sel_stmt = String.format("SELECT * FROM %s", tableName);
    String gmtTime = runSelect(sel_stmt).next().getTimestamp(1).toGMTString();
    assertTrue(gmtTime.equals("26 Jan 2019 08:03:16 GMT") ||
               gmtTime.equals("26 Jan 2019 07:03:16 GMT"));
  }

  private void runInvalidUpdate(String tableName, String ts) {
    // Testing SET clause.
    String upd_stmt1 = String.format(
        "UPDATE %s SET v2 = '%s' WHERE h1 = 1 AND h2 = %s" +
            " AND r1 = 2 AND r2 = %s;", tableName, ts, "0", "0");
    runInvalidStmt(upd_stmt1);

    // Testing WHERE clause.
    String upd_stmt2 = String.format(
        "UPDATE %s SET v2 = %s WHERE h1 = 1 AND h2 = '%s'" +
            " AND r1 = 2 AND r2 = %s;", tableName, "0", ts, "0");
    runInvalidStmt(upd_stmt2);
  }

  @Test
  public void testInvalidUpdate() throws Exception {
    String tableName = "test_update_with_invalid_timestamp";
    createTable(tableName, "timestamp");
    String ins_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, %s, %d, %s, %d, %s);",
        tableName, 1, "0", 2, "0", 3, "0");
    session.execute(ins_stmt);

    runInvalidUpdate(tableName, "plainstring");
    runInvalidUpdate(tableName, "1992:12:11");
    runInvalidUpdate(tableName, "1992-11");
    runInvalidUpdate(tableName, "1992-13-12");
    runInvalidUpdate(tableName, "1992-12-12 14:23:30:31");
    runInvalidUpdate(tableName, "1992-12-12 14:23:30.12.32");
  }

  @Test
  public void testOrdering() throws Exception {

    //----------------------------------------------------------------------------------------------
    // Setting up

    String tableName = "test_select_with_timestamp";
    String create_stmt =
        "CREATE TABLE " + tableName + " (h int, r timestamp, v int, primary key((h), r));";
    session.execute(create_stmt);
    Calendar cal = new GregorianCalendar();
    cal.setTimeZone(TimeZone.getTimeZone("GMT"));
    cal.setTimeInMillis(0); // resetting

    // Min valid (formatted) date in Cassandra.
    cal.set(1900, Calendar.JANUARY, 1, 0, 0, 0);
    Date date1 = cal.getTime();
    String date1_formatted = "'1900-1-1 00:00:00 UTC'";


    cal.set(2017, Calendar.AUGUST, 11, 12, 20, 30);
    Date date2 = cal.getTime();
    String date2_formatted = "'2017-8-11 12:20:30 UTC'";

    // Max valid (formatted) date in Cassandra.
    cal.set(9999, Calendar.DECEMBER, 31, 23, 59, 59);
    Date date3 = cal.getTime();
    String date3_formatted = "'9999-12-31 23:59:59 UTC'";


    // Inserting Data -- fixing values for non-timestamp columns.
    String insert_template =
        "INSERT INTO " + tableName + "(h, r, v) VALUES(1, %s, 1);";

    session.execute(String.format(insert_template, date1_formatted));
    session.execute(String.format(insert_template, date2_formatted));
    session.execute(String.format(insert_template, date3_formatted));

    //----------------------------------------------------------------------------------------------
    // Test timestamp comparisons.

    String select_template = "SELECT h, r, v FROM " + tableName + " WHERE h = 1 AND r %s %s";

    {
      ResultSet rs = session.execute(String.format(select_template, "<=", date2_formatted));
      Iterator<Row> iter = rs.iterator();

      assertTrue(iter.hasNext());
      assertEquals(date1, iter.next().getTimestamp("r"));

      assertTrue(iter.hasNext());
      assertEquals(date2, iter.next().getTimestamp("r"));

      assertFalse(iter.hasNext());
    }

    {
      ResultSet rs = session.execute(String.format(select_template, ">", date1_formatted));
      Iterator<Row> iter = rs.iterator();

      assertTrue(iter.hasNext());
      assertEquals(date2, iter.next().getTimestamp("r"));

      assertTrue(iter.hasNext());
      assertEquals(date3, iter.next().getTimestamp("r"));

      assertFalse(iter.hasNext());
    }

    {
      ResultSet rs = session.execute(String.format(select_template, "<=", Long.MAX_VALUE));
      Iterator<Row> iter = rs.iterator();

      assertTrue(iter.hasNext());
      assertEquals(date1, iter.next().getTimestamp("r"));

      assertTrue(iter.hasNext());
      assertEquals(date2, iter.next().getTimestamp("r"));

      assertTrue(iter.hasNext());
      assertEquals(date3, iter.next().getTimestamp("r"));

      assertFalse(iter.hasNext());
    }

    {
      ResultSet rs = session.execute(String.format(select_template, ">=", -Long.MAX_VALUE));
      Iterator<Row> iter = rs.iterator();

      assertTrue(iter.hasNext());
      assertEquals(date1, iter.next().getTimestamp("r"));

      assertTrue(iter.hasNext());
      assertEquals(date2, iter.next().getTimestamp("r"));

      assertTrue(iter.hasNext());
      assertEquals(date3, iter.next().getTimestamp("r"));

      assertFalse(iter.hasNext());
    }
  }

  private void runInvalidSelectWithTimestamp(String tableName, String ts) {
    String sel_stmt = String.format(
        "SELECT * from %s WHERE h1 = 1 AND h2 = '%s'" +
            " AND r1 = 2 AND r2 = %s;", tableName, ts, "0");
    runInvalidStmt(sel_stmt);
  }

  @Test
  public void testInvalidSelectWithTimestamp() throws Exception {
    String tableName = "test_select_with_invalid_timestamp";
    createTable(tableName, "timestamp");
    String ins_stmt = String.format(
        "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, %s, %d, %s, %d, %s);",
        tableName, 1, "0", 2, "0", 3, "0");
    session.execute(ins_stmt);

    runInvalidSelectWithTimestamp(tableName, "plainstring");
    runInvalidSelectWithTimestamp(tableName, "1992:12:11");
    runInvalidSelectWithTimestamp(tableName, "1992-11");
    runInvalidSelectWithTimestamp(tableName, "1992-13-12");
    runInvalidSelectWithTimestamp(tableName, "1992-12-12 14:23:30:31");
    runInvalidSelectWithTimestamp(tableName, "1992-12-12 14:23:30.12.32");
  }

  @Test
  public void testTimestampLogicGFlag() throws Exception {
    // Testing ICU timezones flag enabled (default).
    String tableName = "test";
    String newTimestamp = "\'2019-01-26T00:03:16.059 America/New_York\'";
    session.execute(String.format("CREATE TABLE %s(x int primary key, b timestamp);", tableName));
    String ins_stmt = String.format(
            "INSERT INTO %s(x, b) VALUES(%d, %s);",
            tableName, 1, newTimestamp);
    session.execute(ins_stmt);
    String sel_stmt = String.format("SELECT * FROM %s", tableName);
    Iterator<Row> rows = runSelect(sel_stmt);
    assertTrue(rows.hasNext());
    destroyMiniCluster();
    // Testing ICU timezones flag disabled.
    createMiniCluster(
        Collections.emptyMap(),
        Collections.singletonMap("use_icu_timezones", "false"));
    setUpCqlClient();
    session.execute(String.format("CREATE TABLE %s(x int primary key, b timestamp);", tableName));
    runInvalidStmt(ins_stmt);
    String[] oldTimestamps = new String[]{"\'2019-01-26T03:33:16.059 UTC\'",
                                          "\'2019-01-26T00:03:16.059 UTC-03:30\'",
                                          "\'2019-01-26T00:03:16.059-03:30\'"};
    for (int i = 1; i <= 3; i++) {
      ins_stmt = String.format(
              "INSERT INTO %s(x, b) VALUES(%d, %s);",
              tableName, i, oldTimestamps[i-1]);
      session.execute(ins_stmt);
    }
    for(Row row: session.execute(sel_stmt)){
      assertEquals("26 Jan 2019 03:33:16 GMT",row.getTimestamp(1).toGMTString());
    }
    destroyMiniCluster();
  }

  @Test
  public void testTimestampMicrosecsInsertSelect() throws Exception {
    destroyMiniCluster();
    createMiniCluster(Collections.emptyMap(),
        Collections.singletonMap("cql_revert_to_partial_microsecond_support", "false"));
    setUpCqlClient();
    String tableName = "test";
    String testTimestamp = "2024-08-26 09:23:38.319213+0000";
    String testTimestampMS = "2024-08-26 09:23:38.319000+0000";
    Map<MiniYBDaemon, Metrics> beforeMetrics = getAllMetrics();

    session.execute(String.format("CREATE TABLE %s(x int primary key, b timestamp);", tableName));
    session.execute(
        String.format("INSERT INTO %s(x, b) VALUES(%d, '%s');", tableName, 1, testTimestamp));

    session.execute(
        String.format("INSERT INTO %s(x, b) VALUES(%d, %s);", tableName, 2, "currenttimestamp()"));

    session.execute(String.format("INSERT INTO %s(x, b) VALUES(%d, %s);", tableName, 3,
        "totimestamp(now())")); /* Previously, totimestamp(now()) worked fine */

    String sel_stmt_init = "SELECT b FROM %s WHERE x=%d";
    SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSSXXX");
    String currenttimestamp_string =
        dateFormat.format(runSelect(sel_stmt_init, tableName, 2).next().getTimestamp(0));
    String totimestamp_now_string =
        dateFormat.format(runSelect(sel_stmt_init, tableName, 3).next().getTimestamp(0));

    String sel_stmt = "SELECT COUNT(*) FROM %s WHERE b='%s'";

    assertQuery(String.format(sel_stmt, tableName, testTimestampMS), "Row[1]");
    assertQuery(String.format(sel_stmt, tableName, currenttimestamp_string), "Row[1]");
    assertQuery(String.format(sel_stmt, tableName, totimestamp_now_string), "Row[1]");

    // only milliseconds timestamps are used when cql_revert_to_partial_microsecond_support = false
    assertEquals(0, getMicrosecondTimestampMetricCount(beforeMetrics));

    destroyMiniCluster();
  }

  @Test
  public void testTimestampPKIndexScan() throws Exception {
    destroyMiniCluster();
    createMiniCluster(Collections.emptyMap(),
        Collections.singletonMap("cql_revert_to_partial_microsecond_support", "false"));
    setUpCqlClient();
    String tableName = "test";
    String testTimestamp = "2024-08-26 09:23:38.319213+0000";
    Map<MiniYBDaemon, Metrics> beforeMetrics = getAllMetrics();

    session.execute(String.format("CREATE TABLE %s(x INT, b TIMESTAMP, v1 INT, v2 INT, PRIMARY "
            + "KEY(x, b)) WITH transactions = {'enabled': 'true'};",
        tableName));
    session.execute(String.format("CREATE INDEX idx1 ON %s(v1);", tableName));
    waitForReadPermsOnAllIndexes(tableName);

    session.execute(String.format("INSERT INTO %s(x, b, v1, v2) VALUES(%d, %s, %d, %d);", tableName,
        1, "currenttimestamp()", 12, 13));
    session.execute(String.format("INSERT INTO %s(x, b, v1, v2) VALUES(%d, %s, %d, %d);", tableName,
        2, "currenttimestamp()", 13, 14));
    session.execute(String.format("INSERT INTO %s(x, b, v1, v2) VALUES(%d, '%s', %d, %d);",
        tableName, 3, testTimestamp, 12, 15));
    session.execute(String.format("INSERT INTO %s(x, b, v1, v2) VALUES(%d, %s, %d, %d);", tableName,
        4, "totimestamp(now())", 12, 16)); /* Previously, totimestamp(now()) worked fine */
    String sel_stmt = String.format("SELECT * FROM %s  WHERE v1=12", tableName);
    assertTrue("Should use index scan",
        session.execute("EXPLAIN " + sel_stmt).all().toString().contains("Index Scan using"));

    int count = 0;
    for (Row row : session.execute(sel_stmt)) {
      count++;
    }
    assertEquals(3, count);

    // only milliseconds timestamps are used when cql_revert_to_partial_microsecond_support = false
    assertEquals(0, getMicrosecondTimestampMetricCount(beforeMetrics));

    destroyMiniCluster();
  }

  @Test
  public void testMicrosecondsMetric() throws Exception {
    destroyMiniCluster();
    createMiniCluster(Collections.emptyMap(),
        Collections.singletonMap("cql_revert_to_partial_microsecond_support",
            "true")); // this is also the default behaviour as of 2024-11-05
    setUpCqlClient();
    String tableName = "test";
    String testTimestamp = "2024-08-26 09:23:38.319213+0000";
    String testTimestampMS = "2024-08-26 09:23:38.319000+0000";
    String testTimestamp2 = "2024-08-26 19:23:38.537281+0000";

    int usTimestampCount = 0;

    Map<MiniYBDaemon, Metrics> beforeMetrics = getAllMetrics();

    session.execute(String.format("CREATE TABLE %s(x int primary key, b timestamp);", tableName));
    session.execute(
        String.format("INSERT INTO %s(x, b) VALUES(%d, '%s');", tableName, 1, testTimestamp));
    usTimestampCount++; // timestamp inserted with microseconds precision.
    assertEquals(usTimestampCount, getMicrosecondTimestampMetricCount(beforeMetrics));

    session.execute(
        String.format("INSERT INTO %s(x, b) VALUES(%d, %s);", tableName, 2, "currenttimestamp()"));
    usTimestampCount++; // inserted with `currenttimestamp` function i.e. microseconds precision.
    assertEquals(usTimestampCount, getMicrosecondTimestampMetricCount(beforeMetrics));

    session.execute(String.format("INSERT INTO %s(x, b) VALUES(%d, %s);", tableName, 3,
        "totimestamp(now())")); // used milliseconds precision hence no updation of metric.
    assertEquals(usTimestampCount, getMicrosecondTimestampMetricCount(beforeMetrics));

    String sel_stmt = "SELECT * FROM %s WHERE b='%s'";

    session.execute(String.format(sel_stmt, tableName, testTimestamp));
    usTimestampCount++; // timestamp used is with microseconds precision.
    assertEquals(usTimestampCount, getMicrosecondTimestampMetricCount(beforeMetrics));

    session.execute(String.format(
        sel_stmt, tableName, testTimestampMS)); // timestamp used is with milliseconds precision.
    assertEquals(usTimestampCount, getMicrosecondTimestampMetricCount(beforeMetrics));

    session.execute(String.format(sel_stmt, tableName, testTimestamp2));
    usTimestampCount++; // timestamp used is with microseconds precision.
    assertEquals(usTimestampCount, getMicrosecondTimestampMetricCount(beforeMetrics));

    destroyMiniCluster();
  }
}
