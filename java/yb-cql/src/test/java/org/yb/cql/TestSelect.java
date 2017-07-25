// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.net.URL;
import java.net.URLConnection;
import java.util.*;

import org.junit.Test;

import org.yb.minicluster.MiniYBDaemon;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

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
    CreateTable(tableName, "timestamp");
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
    CreateTable(tableName, "timestamp");
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

  private int getTotalCount(JsonObject metric) {
    return metric.get("total_count").getAsInt();
  }

  @Test
  public void testLocalTServerCalls() throws Exception {
    // Insert 100 rows and select them back.
    final int NUM_KEYS = 100;
    session.execute("CREATE TABLE test_local (k int PRIMARY KEY, v int);");
    PreparedStatement stmt = session.prepare("INSERT INTO test_local (k, v) VALUES (?, ?);");
    for (int i = 1; i <= 100; i++) {
      session.execute(stmt.bind(Integer.valueOf(i), Integer.valueOf(i + 1)));
    }
    stmt = session.prepare("SELECT v FROM test_local WHERE k = ?");
    for (int i = 1; i <= 100; i++) {
      assertEquals(i + 1, session.execute(stmt.bind(Integer.valueOf(i))).one().getInt("v"));
    }

    // Check the metrics.
    int totalLocalReadCalls = 0;
    int totalRemoteReadCalls = 0;
    int totalLocalWriteCalls = 0;
    int totalRemoteWriteCalls = 0;
    for (MiniYBDaemon ts : miniCluster.getTabletServers().values()) {
      URL metrics = new URL(String.format("http://%s:%d/metrics",
                                          ts.getLocalhostIP(), ts.getCqlWebPort()));
      URLConnection yc = metrics.openConnection();
      JsonParser parser = new JsonParser();
      JsonElement tree = parser.parse(new Scanner(yc.getInputStream()).useDelimiter("\\A").next());
      JsonObject obj = tree.getAsJsonArray().get(0).getAsJsonObject();
      for (JsonElement elem : obj.getAsJsonArray("metrics")) {
        JsonObject metric = elem.getAsJsonObject();
        String metric_name = metric.get("name").getAsString();
        if (metric_name.equals("handler_latency_yb_client_read_local")) {
          totalLocalReadCalls += getTotalCount(metric);
        } else if (metric_name.equals("handler_latency_yb_client_write_local")) {
          totalLocalWriteCalls += getTotalCount(metric);
        } else if (metric_name.equals("handler_latency_yb_client_read_remote")) {
          totalRemoteReadCalls += getTotalCount(metric);
        } else if (metric_name.equals("handler_latency_yb_client_write_remote")) {
          totalRemoteWriteCalls += getTotalCount(metric);
        }
      }
    }

    // Verify there are some local calls and some remote calls for reads and writes.
    assertTrue(totalLocalReadCalls > 0);
    assertTrue(totalRemoteReadCalls > 0);
    assertTrue(totalLocalWriteCalls > 0);
    assertTrue(totalRemoteWriteCalls > 0);

    // Verify total number of read / write calls. It is possible to have more calls than the
    // number of keys because some calls may reach step-down leaders and need retries.
    assertTrue(totalLocalReadCalls + totalRemoteReadCalls >= NUM_KEYS);
    assertTrue(totalLocalWriteCalls + totalRemoteWriteCalls >= NUM_KEYS);
  }
}
