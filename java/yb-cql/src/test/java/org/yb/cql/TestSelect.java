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

import org.junit.Test;

import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBDaemon;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public class TestSelect extends BaseCQLTest {
  @Test
  public void testSimpleQuery() throws Exception {
    LOG.info("TEST CQL SIMPLE QUERY - Start");

    // Setup test table.
    setupTable("test_select", 10);

    // Select data from the test table.
    String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                         "  WHERE h1 = 7 AND h2 = 'h7' AND r1 = 107;";
    ResultSet rs = session.execute(select_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), 7);
      assertEquals(row.getString(1), "h7");
      assertEquals(row.getInt(2), 107);
      assertEquals(row.getString(3), "r107");
      assertEquals(row.getInt(4), 1007);
      assertEquals(row.getString(5), "v1007");
      row_count++;
    }
    assertEquals(row_count, 1);

    // Insert multiple rows with the same partition key.
    int num_rows = 20;
    int h1_shared = 1111111;
    String h2_shared = "h2_shared_key";
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO test_select(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, 'r%d', %d, 'v%d');",
        h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }

    // Verify multi-row select.
    String multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                                      "  WHERE h1 = %d AND h2 = '%s';",
                                      h1_shared, h2_shared);
    rs = session.execute(multi_stmt);

    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_rows);

    // Test ALLOW FILTERING clause.
    multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                               "  WHERE h1 = %d AND h2 = '%s' ALLOW FILTERING;",
                               h1_shared, h2_shared);
    rs = session.execute(multi_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      if (rs.getAvailableWithoutFetching() == 100 && !rs.isFullyFetched()) {
        rs.fetchMoreResults();
      }

      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_rows);

    LOG.info("TEST CQL SIMPLE QUERY - End");
  }

  @Test
  public void testRangeQuery() throws Exception {
    LOG.info("TEST CQL RANGE QUERY - Start");

    // Setup test table.
    setupTable("test_select", 0);

    // Populate rows.
    {
      String insert_stmt = "INSERT INTO test_select (h1, h2, r1, r2, v1, v2) " +
                           "VALUES (?, ?, ?, ?, ?, ?);";
      PreparedStatement stmt = session.prepare(insert_stmt);
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 3; j++) {
          session.execute(stmt.bind(new Integer(i), "h" + i,
                                    new Integer(j), "r" + j,
                                    new Integer(j), "v" + i + j));
        }
      }
    }

    // Test with ">" and "<".
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 > 1 AND r1 < 3;",
                "Row[1, h1, 2, r2, 2, v12]");

    // Test with mixing ">" and "<=".
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 > 1 AND r1 <= 3;",
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[1, h1, 3, r3, 3, v13]");

    // Test with ">=" and "<=".
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 <= 3;",
                "Row[1, h1, 1, r1, 1, v11]" +
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[1, h1, 3, r3, 3, v13]");

    // Test with ">=" and "<=" on r1 and ">" and "<" on r2.
    assertQuery("SELECT * FROM test_select " +
                "WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 <= 3 AND r2 > 'r1' AND r2 < 'r3';",
                "Row[1, h1, 2, r2, 2, v12]");

    // Test with "=>" and "<=" with partial hash key.
    assertQuery("SELECT * FROM test_select WHERE h1 = 1 AND r1 >= 1 AND r1 <= 3;",
                "Row[1, h1, 1, r1, 1, v11]" +
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[1, h1, 3, r3, 3, v13]");

    // Test with ">" and "<" with no hash key.
    assertQuery("SELECT * FROM test_select WHERE r1 > 1 AND r1 < 3;",
                "Row[1, h1, 2, r2, 2, v12]" +
                "Row[3, h3, 2, r2, 2, v32]" +
                "Row[2, h2, 2, r2, 2, v22]");

    // Invalid range: equal and bound conditions cannot be used together.
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 = 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 > 1 AND r1 = 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 = 1 AND r1 < 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 = 1 AND r1 <= 3;");

    // Invalid range: two lower or two upper bound conditions cannot be used together.
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 >= 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 >= 1 AND r1 > 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 < 1 AND r1 <= 3;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 <= 1 AND r1 <= 3;");

    // Invalid range: not-equal not supported.
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 <> 1;");
    runInvalidStmt("SELECT * FROM test_select WHERE h1 = 1 AND h2 = 'h1' AND r1 != 1;");

    LOG.info("TEST CQL RANGE QUERY - End");
  }

  @Test
  public void testSelectWithLimit() throws Exception {
    LOG.info("TEST CQL LIMIT QUERY - Start");

    // Setup test table.
    setupTable("test_select", 0);

    // Insert multiple rows with the same partition key.
    int num_rows = 20;
    int h1_shared = 1111111;
    int num_limit_rows = 10;
    String h2_shared = "h2_shared_key";
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO test_select(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, 'r%d', %d, 'v%d');",
        h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }

    // Verify multi-row select.
    String multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                                      "  WHERE h1 = %d AND h2 = '%s' LIMIT %d;",
                                      h1_shared, h2_shared, num_limit_rows);
    ResultSet rs = session.execute(multi_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_limit_rows);

    // Test allow filtering.
    multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                               "  WHERE h1 = %d AND h2 = '%s' LIMIT %d ALLOW FILTERING;",
                               h1_shared, h2_shared, num_limit_rows);
    rs = session.execute(multi_stmt);
    row_count = 0;
    iter = rs.iterator();
    while (iter.hasNext()) {
      Row row = iter.next();
      String result = String.format("Result = %d, %s, %d, %s, %d, %s",
                                    row.getInt(0),
                                    row.getString(1),
                                    row.getInt(2),
                                    row.getString(3),
                                    row.getInt(4),
                                    row.getString(5));
      LOG.info(result);

      assertEquals(row.getInt(0), h1_shared);
      assertEquals(row.getString(1), h2_shared);
      assertEquals(row.getInt(2), row_count + 100);
      assertEquals(row.getString(3), String.format("r%d", row_count + 100));
      assertEquals(row.getInt(4), row_count + 1000);
      assertEquals(row.getString(5), String.format("v%d", row_count + 1000));
      row_count++;
    }
    assertEquals(row_count, num_limit_rows);

    LOG.info("TEST CQL LIMIT QUERY - End");
  }

  // testing only range queries, rest are tested by insert/update tests
  @Test
  public void testSelectWithTimestamp() throws Exception {
    String tableName = "test_select_with_timestamp";
    createTable(tableName, "timestamp");
    // testing String input
    String ins_stmt = String.format(
            "INSERT INTO %s(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, '%s', %d, %s);",
            tableName, 1, "1980-8-11 12:20:30 UTC", 2, "1980-8-11 12:20:30 UTC", 3, "0");
    session.execute(ins_stmt);

    // testing <,=,> for String input
    String sel_stmt1 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 > '%s';", tableName,
            "1980-8-11 12:20:30 UTC", "1980-8-11 12:20:30.142 UTC");
    ResultSet rs = session.execute(sel_stmt1);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt2 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 = '%s';", tableName,
            "1980-8-11 12:20:30 UTC", "1980-8-11 12:20:30.142 UTC");
    rs = session.execute(sel_stmt2);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt3 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 < '%s';", tableName,
            "1980-8-11 12:20:30 UTC", "1980-8-11 12:20:30.142 UTC");
    Iterator<Row> iter = session.execute(sel_stmt3).iterator();
    assertTrue(iter.hasNext());
    Row row = iter.next();
    Calendar cal = new GregorianCalendar();
    cal.setTimeZone(TimeZone.getTimeZone("GMT"));
    cal.setTimeInMillis(0); // resetting
    // Java Date month value starts at 0 not 1
    cal.set(1980, 7, 11, 12, 20, 30);
    Date date = cal.getTime();
    assertEquals(1, row.getInt(0));
    assertEquals(date, row.getTimestamp(1));
    assertEquals(2, row.getInt(2));
    assertEquals(date, row.getTimestamp(3));
    assertEquals(3, row.getInt(4));

    // testing <,=,> for Int input
    String sel_stmt4 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 > %s;", tableName,
            "1980-8-11 12:20:30 UTC", "334844431000");
    rs = session.execute(sel_stmt4);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt5 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 = %s;", tableName,
            "1980-8-11 12:20:30 UTC", "334844431000");
    rs = session.execute(sel_stmt5);
    assertFalse(rs.iterator().hasNext());
    String sel_stmt6 = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM %s"
                    + " WHERE h1 = 1 AND h2 = '%s' AND r1 = 2 AND r2 < %s;", tableName,
            "1980-8-11 12:20:30 UTC", "334844431000");
    iter = session.execute(sel_stmt6).iterator();
    assertTrue(iter.hasNext());
    row = iter.next();
    assertEquals(1, row.getInt(0));
    assertEquals(date, row.getTimestamp(1));
    assertEquals(2, row.getInt(2));
    assertEquals(date, row.getTimestamp(3));
    assertEquals(3, row.getInt(4));
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
  public void testLocalTServerCalls() throws Exception {
    // Create test table.
    session.execute("CREATE TABLE test_local (k int PRIMARY KEY, v int);");

    // Get the base metrics of each tserver.
    Map<MiniYBDaemon, IOMetrics> baseMetrics = new HashMap<>();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = new IOMetrics(new Metrics(ts.getLocalhostIP(),
                                                    ts.getCqlWebPort(),
                                                    "server"));
      baseMetrics.put(ts, metrics);
    }

    // Insert rows and select them back.
    final int NUM_KEYS = 100;
    PreparedStatement stmt = session.prepare("INSERT INTO test_local (k, v) VALUES (?, ?);");
    for (int i = 1; i <= NUM_KEYS; i++) {
      session.execute(stmt.bind(Integer.valueOf(i), Integer.valueOf(i + 1)));
    }
    stmt = session.prepare("SELECT v FROM test_local WHERE k = ?");
    for (int i = 1; i <= NUM_KEYS; i++) {
      assertEquals(i + 1, session.execute(stmt.bind(Integer.valueOf(i))).one().getInt("v"));
    }

    // Check the metrics again.
    IOMetrics totalMetrics = new IOMetrics();
    int tsCount = miniCluster.getTabletServers().values().size();
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      IOMetrics metrics = new IOMetrics(new Metrics(ts.getLocalhostIP(),
                                                    ts.getCqlWebPort(),
                                                    "server"))
                          .subtract(baseMetrics.get(ts));
      LOG.info("Metrics of " + ts.toString() + ": " + metrics.toString());
      totalMetrics.add(metrics);
    }

    // Verify there are some local read and write calls.
    assertTrue(totalMetrics.localReadCount > 0);
    assertTrue(totalMetrics.localWriteCount > 0);

    // Verify total number of read / write calls. It is possible to have more calls than the
    // number of keys because some calls may reach step-down leaders and need retries.
    assertTrue(totalMetrics.localReadCount + totalMetrics.remoteReadCount >= NUM_KEYS);
    assertTrue(totalMetrics.localWriteCount + totalMetrics.remoteWriteCount >= NUM_KEYS);
  }

  @Test
  public void testTtlInWhereClauseOfSelect() throws Exception {
    LOG.info("TEST SELECT TTL queries - Start");

    // Setup test table.
    int[] ttls = {
      100,
      100,
      100,
      100,
      100,
      100,
      100,
      100,
      100,
      200
    };
    setupTable("test_ttl", ttls);

    // Select data from the test table.
    String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl" +
                         "  WHERE ttl(v1) > 150";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    assertEquals(1, rows.size());
    Row row = rows.get(0);
    assertEquals(9, row.getInt(0));
    assertEquals("h9", row.getString(1));
    assertEquals(109, row.getInt(2));
    assertEquals("r109", row.getString(3));
    assertEquals(1009, row.getInt(4));
    assertEquals(1009, row.getInt(5));

    String update_stmt = "UPDATE test_ttl USING ttl 300 SET v1 = 1009" +
                         "  WHERE h1 = 9 and h2 = 'h9' and r1 = 109 and r2 = 'r109' ";
    session.execute(update_stmt);
    select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl" +
                  "  WHERE ttl(v1) > 250";

    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(9, row.getInt(0));
    assertEquals("h9", row.getString(1));
    assertEquals(109, row.getInt(2));
    assertEquals("r109", row.getString(3));
    assertEquals(1009, row.getInt(4));
    assertEquals(1009, row.getInt(5));

    select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl" +
                  "  WHERE ttl(v2) > 250";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(0, rows.size());
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlOfCollectionsThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl " +
                         " (h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl" +
                         "  WHERE ttl(v1) < 150";
    session.execute(select_stmt);
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlOfPrimaryThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl " +
                         " (h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl" +
                         "  WHERE ttl(h) < 150";
    session.execute(select_stmt);
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlWrongParametersThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl " +
                         " (h int, v1 int, v2 int, primary key (h));";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, 1, 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl() < 150";
    session.execute(select_stmt);
  }

  @Test
  public void testTtlOfDefault() throws Exception {
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl (h int, v1 list<int>, v2 int, primary key (h)) " +
                         "with default_time_to_live = 100;";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1);";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) <= 100";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    assertEquals(1, rows.size());

    select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) >= 90";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(1, rows.size());

    String insert_stmt_2 = "INSERT INTO test_ttl (h, v1, v2) VALUES(2, [2], 2) using ttl 150;";
    session.execute(insert_stmt_2);
    select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) >= 140";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(1, rows.size());
  }

  @Test
  public void testTtlWhenNoneSpecified() throws Exception {
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl " +
                         " (h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1);";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) > 100;";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    //The number of rows when we query ttl on v2 should be 0, since ttl(v2) isn't defined.
    assertEquals(0, rows.size());

    select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v2) <= 100";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(0, rows.size());
  }

  @Test
  public void testToken() throws Exception {
    LOG.info("TEST TOKEN - Start");
    setupTable("token_test", 10);

    // Testing only basic token call as sanity check here.
    // Main token tests are in YbSqlQuery (C++) and TestBindVariable (Java) tests.
    Iterator<Row> rows = session.execute("SELECT * FROM token_test WHERE " +
            "token(h1, h2) = token(2, 'h2')").iterator();

    assertTrue(rows.hasNext());
    // Checking result.
    Row row = rows.next();
    assertEquals(2, row.getInt(0));
    assertEquals("h2", row.getString(1));
    assertEquals(102, row.getInt(2));
    assertEquals("r102", row.getString(3));
    assertEquals(1002, row.getInt(4));
    assertEquals("v1002", row.getString(5));
    assertFalse(rows.hasNext());

    LOG.info("TEST TOKEN - End");
  }

  @Test
  public void testInKeyword() throws Exception {
    LOG.info("TEST IN KEYWORD - Start");
    setupTable("in_test", 10);

    // Test basic IN condition on hash column.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE h1 IN (1, 3, -1, 7)");
      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(1);
      expected_values.add(3);
      expected_values.add(7);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expected_values.contains(h1));
        expected_values.remove(h1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test basic IN condition on range column.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE " +
                                     "r2 IN ('foo', 'r101','r103','r107')");
      Set<String> expected_values = new HashSet<>();
      expected_values.add("r101");
      expected_values.add("r103");
      expected_values.add("r107");
      // Check rows
      for (Row row : rs) {
        String r2 = row.getString("r2");
        assertTrue(expected_values.contains(r2));
        expected_values.remove(r2);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test basic IN condition on regular column.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE v1 IN (1006, 1002, -1)");
      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(1002);
      expected_values.add(1006);
      // Check rows
      for (Row row : rs) {
        Integer v1 = row.getInt("v1");
        assertTrue(expected_values.contains(v1));
        expected_values.remove(v1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test multiple IN conditions.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE " +
              "h2 IN ('h1', 'h2', 'h7', 'h8') AND v2 in ('v1001', 'v1004', 'v1007')");
      // Since all values are unique we identify rows by the first hash column.
      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(1);
      expected_values.add(7);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expected_values.contains(h1));
        expected_values.remove(h1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test IN condition with single entry.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE h1 IN (4)");

      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(4);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expected_values.contains(h1));
        expected_values.remove(h1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test empty IN condition.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE h1 IN ()");
      assertFalse(rs.iterator().hasNext());

      rs = session.execute("SELECT * FROM in_test WHERE r2 IN ()");
      assertFalse(rs.iterator().hasNext());

      rs = session.execute("SELECT * FROM in_test WHERE v1 IN ()");
      assertFalse(rs.iterator().hasNext());
    }

    // Test NOT IN condition.
    {
      ResultSet rs = session.execute("SELECT * FROM in_test WHERE " +
              "h1 NOT IN (0, 1, 3, -1, 4, 5, 7, -2)");
      Set<Integer> expected_values = new HashSet<>();
      expected_values.add(2);
      expected_values.add(6);
      expected_values.add(8);
      expected_values.add(9);
      // Check rows
      for (Row row : rs) {
        Integer h1 = row.getInt("h1");
        assertTrue(expected_values.contains(h1));
        expected_values.remove(h1);
      }
      assertTrue(expected_values.isEmpty());
    }

    // Test Invalid Statements.

    // Column cannot be restricted by more than one relation if it includes an IN
    runInvalidStmt("SELECT * FROM in_test WHERE h1 IN (1,2) AND h1 = 2");
    runInvalidStmt("SELECT * FROM in_test WHERE r1 IN (1,2) AND r1 < 2");
    runInvalidStmt("SELECT * FROM in_test WHERE v1 >= 2 AND v1 NOT IN (1,2)");
    runInvalidStmt("SELECT * FROM in_test WHERE v1 IN (1,2) AND v1 NOT IN (2,3)");

    // IN tuple elements must be convertible to column type.
    runInvalidStmt("SELECT * FROM in_test WHERE h1 IN (1.2,2.2)");
    runInvalidStmt("SELECT * FROM in_test WHERE h2 NOT IN ('a', 1)");

    LOG.info("TEST IN KEYWORD - End");
  }

  public void testStatementList() throws Exception {
    // Verify handling of empty statements.
    assertEquals(0, session.execute("").all().size());
    assertEquals(0, session.execute(";").all().size());
    assertEquals(0, session.execute("  ;  ;  ").all().size());

    // Verify handling of multi-statement (not supported yet).
    setupTable("test_select", 0);
    runInvalidStmt("SELECT * FROM test_select; SELECT * FROM test_select;");
  }
}
