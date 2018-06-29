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

import com.datastax.driver.core.SimpleStatement;
import com.yugabyte.driver.core.TableSplitMetadata;
import com.yugabyte.driver.core.policies.PartitionAwarePolicy;
import org.junit.Test;

import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.RocksDBMetrics;

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
          h1_shared, h2_shared, idx + 100, idx + 100, idx + 1000, idx + 1000);
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

  private void assertQueryWithPageSize(String query, String expected, int pageSize) {
    SimpleStatement stmt = new SimpleStatement(query);
    stmt.setFetchSize(pageSize);
    assertQuery(stmt, expected);
  }

  private void testScansWithOffset(int pageSize) {
    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 0",
        "Row[5, 5, 5]" +
        "Row[1, 1, 1]" +
        "Row[6, 6, 6]" +
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 5 OFFSET 3",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 3 LIMIT 5",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 10 OFFSET 3",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 5 OFFSET 4",
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 8",
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 6",
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 3 OFFSET 3",
        "Row[7, 7, 7]" +
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 9 OFFSET 9",
        "", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 >= 5 LIMIT 9 OFFSET 0",
        "Row[5, 5, 5]" +
        "Row[6, 6, 6]" +
        "Row[7, 7, 7]" +
        "Row[8, 8, 8]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 >= 5 LIMIT 2 OFFSET 1",
        "Row[6, 6, 6]" +
        "Row[7, 7, 7]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 < 4 LIMIT 9 OFFSET 0",
        "Row[1, 1, 1]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 < 4 LIMIT 2 OFFSET 1",
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 < 4 LIMIT 4 OFFSET 1",
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[3, 3, 3]", pageSize);

    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 4",
        "Row[4, 4, 4]" +
        "Row[0, 0, 0]" +
        "Row[2, 2, 2]" +
        "Row[8, 8, 8]" +
        "Row[3, 3, 3]", pageSize);
  }

  @Test
  public void testSelectWithOffset() throws Exception {
    session.execute("CREATE TABLE test_offset (h1 int, r1 int, c1 int, PRIMARY KEY(h1, r1))");

    // Test single shard offset and limits.
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 1, 1)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 2, 2)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 3, 3)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 4, 4)");
    session.execute("INSERT INTO test_offset (h1, r1, c1) VALUES (1, 5, 5)");

    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 2 OFFSET 3",
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 3 LIMIT 2",
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset LIMIT 2 OFFSET 4", "Row[1, 5, 5]",
        Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset OFFSET 2",
        "Row[1, 3, 3]" +
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);

    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE h1 = 1 ORDER BY r1 DESC LIMIT 2 OFFSET 3",
        "Row[1, 2, 2]" +
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE h1 = 1 ORDER BY r1 DESC LIMIT 2 OFFSET 4",
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE h1 = 1 ORDER BY r1 DESC OFFSET 2",
        "Row[1, 3, 3]" +
        "Row[1, 2, 2]" +
        "Row[1, 1, 1]", Integer.MAX_VALUE);

    // Offset applies only to matching rows.
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 >= 2 LIMIT 2 OFFSET 2",
        "Row[1, 4, 4]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) LIMIT 2 OFFSET 1",
        "Row[1, 3, 3]" +
        "Row[1, 5, 5]", Integer.MAX_VALUE);
    assertQueryWithPageSize("SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) LIMIT 2 OFFSET 2",
        "Row[1, 5, 5]", Integer.MAX_VALUE);

    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE c1 <= 4 AND h1 = 1 ORDER BY r1 DESC LIMIT 2 OFFSET 2",
        "Row[1, 2, 2]" +
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) AND h1 = 1 ORDER BY r1 DESC LIMIT 2 " +
        "OFFSET 1",
        "Row[1, 3, 3]" +
        "Row[1, 1, 1]", Integer.MAX_VALUE);
    assertQueryWithPageSize(
        "SELECT * FROM test_offset WHERE c1 IN (1, 3, 5) AND h1 = 1 ORDER BY r1 DESC LIMIT 2 " +
        "OFFSET 2",
        "Row[1, 1, 1]", Integer.MAX_VALUE);

    // Test multi-shard offset and limits.
    // Delete and re-create the table first.
    session.execute("DROP TABLE test_offset");
    session.execute("CREATE TABLE test_offset (h1 int, r1 int, c1 int, PRIMARY KEY(h1, r1))");

    int totalShards = MiniYBCluster.DEFAULT_NUM_SHARDS_PER_TSERVER * MiniYBCluster
        .DEFAULT_NUM_TSERVERS;
    for (int i = 0; i < totalShards; i++) {
      // 1 row per tablet (roughly).
      session.execute(String.format("INSERT INTO test_offset (h1, r1, c1) VALUES (%d, %d, %d)",
          i, i, i));
    }

    testScansWithOffset(Integer.MAX_VALUE);
    for (int i = 0; i <= totalShards; i++) {
      testScansWithOffset(i);
    }

    // Test Invalid offsets.
    runInvalidStmt("SELECT * FROM test_offset OFFSET -1");
    runInvalidStmt(String.format("SELECT * FROM test_offset OFFSET %d",
        (long)Integer.MAX_VALUE + 1));
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
    String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl WHERE ttl(v1) > 150";
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
    select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl WHERE ttl(v1) > 250";

    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(9, row.getInt(0));
    assertEquals("h9", row.getString(1));
    assertEquals(109, row.getInt(2));
    assertEquals("r109", row.getString(3));
    assertEquals(1009, row.getInt(4));
    assertEquals(1009, row.getInt(5));

    select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_ttl WHERE ttl(v2) > 250";
    rs = session.execute(select_stmt);
    rows = rs.all();
    assertEquals(0, rows.size());
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlOfCollectionsThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl(h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);

    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(v1) < 150";
    session.execute(select_stmt);
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlOfPrimaryThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl(h int, v1 list<int>, v2 int, primary key (h));";
    session.execute(create_stmt);
    String insert_stmt = "INSERT INTO test_ttl (h, v1, v2) VALUES(1, [1], 1) using ttl 100;";
    session.execute(insert_stmt);

    String select_stmt = "SELECT h, v1, v2 FROM test_ttl WHERE ttl(h) < 150";
    session.execute(select_stmt);
  }

  @Test(expected=InvalidQueryException.class)
  public void testTtlWrongParametersThrowsError() throws Exception {
    int []ttls = {100};
    LOG.info("CREATE TABLE test_ttl");
    String create_stmt = "CREATE TABLE test_ttl(h int, v1 int, v2 int, primary key (h));";
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
      Iterator<Row> rows = session.execute("SELECT * FROM in_test WHERE " +
              "h1 IN (3, -1, 1, 7, 1) AND h2 in ('h7', 'h3', 'h1', 'h2')").iterator();

      // Check rows: expecting no duplicates and ascending order.
      assertTrue(rows.hasNext());
      assertEquals(1, rows.next().getInt("h1"));
      assertTrue(rows.hasNext());
      assertEquals(3, rows.next().getInt("h1"));
      assertTrue(rows.hasNext());
      assertEquals(7, rows.next().getInt("h1"));
      assertFalse(rows.hasNext());
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

  @Test
  public void testStatementList() throws Exception {
    // Verify handling of empty statements.
    assertEquals(0, session.execute("").all().size());
    assertEquals(0, session.execute(";").all().size());
    assertEquals(0, session.execute("  ;  ;  ").all().size());

    // Verify handling of multi-statement (not supported yet).
    setupTable("test_select", 0);
    runInvalidStmt("SELECT * FROM test_select; SELECT * FROM test_select;");
  }

  // Execute query, assert results and return RocksDB metrics.
  private RocksDBMetrics assertPartialRangeSpec(String tableName, String query, Set<String> rows)
      throws Exception {
    RocksDBMetrics beforeMetrics = getRocksDBMetric(tableName);
    LOG.info(tableName + " metric before: " + beforeMetrics);
    assertQuery(query, rows);
    RocksDBMetrics afterMetrics = getRocksDBMetric(tableName);
    LOG.info(tableName + " metric after: " + afterMetrics);
    return afterMetrics.subtract(afterMetrics);
  }

  @Test
  public void testPartialRangeSpec() throws Exception {
    {
      // Create test table and populate data.
      session.execute("CREATE TABLE test_range (h INT, r1 TEXT, r2 INT, c INT, " +
                      "PRIMARY KEY ((h), r1, r2));");
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 5; j++) {
          for (int k = 1; k <= 3; k++) {
            session.execute("INSERT INTO test_range (h, r1, r2, c) VALUES (?, ?, ?, ?);",
                            Integer.valueOf(i), "r" + j, Integer.valueOf(k), Integer.valueOf(k));
          }
        }
      }

      // Specify only r1 range in SELECT. Verify result.
      String query = "SELECT * FROM test_range WHERE h = 2 AND r1 >= 'r2' AND r1 <= 'r3';";
      Set<String> rows = new HashSet<>(Arrays.asList("Row[2, r2, 1, 1]",
                                                     "Row[2, r2, 2, 2]",
                                                     "Row[2, r2, 3, 3]",
                                                     "Row[2, r3, 1, 1]",
                                                     "Row[2, r3, 2, 2]",
                                                     "Row[2, r3, 3, 3]"));
      RocksDBMetrics metrics1 = assertPartialRangeSpec("test_range", query, rows);

      // Insert some more rows
      for (int i = 1; i <= 3; i++) {
        for (int j = 6; j <= 10; j++) {
          for (int k = 1; k <= 3; k++) {
            session.execute("INSERT INTO test_range (h, r1, r2, c) VALUES (?, ?, ?, ?);",
                            Integer.valueOf(i), "r" + j, Integer.valueOf(k), Integer.valueOf(k));
          }
        }
      }

      // Specify only r1 range in SELECT again. Verify result.
      RocksDBMetrics metrics2 = assertPartialRangeSpec("test_range", query, rows);

      // Verify that the seek/next metrics is the same despite more rows in the range.
      assertEquals(metrics1, metrics2);

      session.execute("DROP TABLE test_range;");
    }

    {
      // Create test table and populate data.
      session.execute("CREATE TABLE test_range (h INT, r1 INT, r2 TEXT, r3 INT, c INT, " +
                      "PRIMARY KEY ((h), r1, r2, r3));");
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 5; j++) {
          for (int k = 1; k <= 3; k++) {
            for (int l = 1; l <= 5; l++) {
              session.execute("INSERT INTO test_range (h, r1, r2, r3, c) VALUES (?, ?, ?, ?, ?);",
                              Integer.valueOf(i),
                              Integer.valueOf(j),
                              "r" + k,
                              Integer.valueOf(l),
                              Integer.valueOf(l));
            }
          }
        }
      }

      // Specify only r1 and r3 ranges in SELECT. Verify result.
      String query = "SELECT * FROM test_range WHERE " +
                     "h = 2 AND r1 >= 2 AND r1 <= 3 AND r3 >= 4 and r3 <= 5;";
      Set<String> rows = new HashSet<>(Arrays.asList("Row[2, 2, r1, 4, 4]",
                                                     "Row[2, 2, r1, 5, 5]",
                                                     "Row[2, 2, r2, 4, 4]",
                                                     "Row[2, 2, r2, 5, 5]",
                                                     "Row[2, 2, r3, 4, 4]",
                                                     "Row[2, 2, r3, 5, 5]",
                                                     "Row[2, 3, r1, 4, 4]",
                                                     "Row[2, 3, r1, 5, 5]",
                                                     "Row[2, 3, r2, 4, 4]",
                                                     "Row[2, 3, r2, 5, 5]",
                                                     "Row[2, 3, r3, 4, 4]",
                                                     "Row[2, 3, r3, 5, 5]"));
      RocksDBMetrics metrics1 = assertPartialRangeSpec("test_range", query, rows);

      // Insert some more rows
      for (int i = 1; i <= 3; i++) {
        for (int j = 6; j <= 10; j++) {
          for (int k = 1; k <= 3; k++) {
            for (int l = 1; l <= 5; l++) {
              session.execute("INSERT INTO test_range (h, r1, r2, r3, c) VALUES (?, ?, ?, ?, ?);",
                              Integer.valueOf(i),
                              Integer.valueOf(j),
                              "r" + k,
                              Integer.valueOf(l),
                              Integer.valueOf(l));
            }
          }
        }
      }

      // Specify only r1 range in SELECT again. Verify result.
      RocksDBMetrics metrics2 = assertPartialRangeSpec("test_range", query, rows);

      // Verify that the seek/next metrics is the same despite more rows in the range.
      assertEquals(metrics1, metrics2);

      session.execute("DROP TABLE test_range;");
    }
  }

  // This test is to check that SELECT expression is supported. Currently, only TTL and WRITETIME
  // are available.  We use TTL() function here.
  public void testSelectTtl() throws Exception {
    LOG.info("TEST SELECT TTL - Start");

    // Setup test table.
    int[] ttls = {
      100,
      200,
      300,
      400,
      500,
      600,
      700,
      800,
      900,
    };
    setupTable("test_ttl", ttls);
    Thread.sleep(1000);

    // Select data from the test table.
    String select_stmt = "SELECT ttl(v1) FROM test_ttl;";
    ResultSet rs = session.execute(select_stmt);
    List<Row> rows = rs.all();
    assertEquals(ttls.length, rows.size());

    for (int i = 0; i < rows.size(); i++) {
      Row row = rows.get(i);
      LOG.info("Selected TTL value is " + row.getLong(0));

      // Because ORDER BY is not yet supported, we cannot assert row by row.
      assertTrue(999 >= row.getLong(0));
    }
  }

  @Test
  public void testInvalidSelectQuery() throws Exception {
    session.execute("CREATE TABLE t (a int, primary key (a));");
    runInvalidStmt("SELECT FROM t;");
    runInvalidStmt("SELECT t;");
  }

  @Test
  public void testQualifiedColumnReference() throws Exception {

    setupTable("test_select", 0);

    // Verify qualified name for column reference is disallowed.
    runInvalidStmt("SELECT t.h1 FROM test_select;");
    runInvalidStmt("INSERT INTO test_select (t.h1) VALUES (1);");
    runInvalidStmt("UPDATE test_select SET t.h1 = 1;");
    runInvalidStmt("DELETE t.h1 FROM test_select;");
  }

  @Test
  public void testScanLimitsWithToken() throws Exception {
    // The main test for 'token' scans is ql-query-test.cc/TestScanWithBounds which checks that the
    // correct results are returned.
    // Therefore, it ensures that we are hitting all the right tablets.
    // However, hitting extra tablets (outside the scan range) never yields any new results, so that
    // test would not catch redundant tablet reads.
    //
    // This test only checks that the expected number of partitions (tablets) are hit when doing
    // table scans with upper/lower bounds via 'token'.
    // Since we know all the needed ones are hit (based on ql-query-test.cc/TestScanWithBounds),
    // this ensures we are not doing redundant reads.
    // TODO (Mihnea) Find a way to integrate these two tests into one.

    session.execute("CREATE TABLE test_token_limits(h int primary key, v int);");

    // Wait to ensure the partitions metadata was updated.
    // Schema change should trigger a refresh but playing it safe in case debouncing will delay it.
    Thread.sleep(2 * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);

    // Get the number of partitions of the source table.
    TableSplitMetadata tableSplitMetadata =
        cluster.getMetadata().getTableSplitMetadata(DEFAULT_TEST_KEYSPACE, "test_token_limits");
    Integer[] keys = tableSplitMetadata.getPartitionMap().navigableKeySet().toArray(new Integer[0]);
    // Need at least 3 partitions for this test to make sense -- current default for tests is 9.
    assertTrue(keys.length >= 3);

    PreparedStatement select =
        session.prepare("SELECT * FROM test_token_limits where token(h) >= ? and token(h) < ?");

    // Scan [first, last) partitions interval -- should hit all partitions except last.
    {
      // Get the initial metrics.
      Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();
      session.execute(select.bind(PartitionAwarePolicy.YBToCqlHashCode(keys[0]),
                                  PartitionAwarePolicy.YBToCqlHashCode(keys[keys.length - 1])));
      // Check the metrics again.
      IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);
      // Check all but one partitions were hit.
      assertEquals(keys.length - 1, totalMetrics.readCount());
    }

    // Scan [first, second) partitions interval -- should hit just first partition.
    {
      // Get the initial metrics.
      Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();

      // Execute query.
      session.execute(select.bind(PartitionAwarePolicy.YBToCqlHashCode(keys[0]),
                                  PartitionAwarePolicy.YBToCqlHashCode(keys[1])));
      // Check the metrics again.
      IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);
      // Check only one partition was hit.
      assertEquals(1, totalMetrics.readCount());
    }

    // Scan [second-to-last, last) partitions interval -- should hit just second-to-last partition.
    {
      // Get the initial metrics.
      Map<MiniYBDaemon, IOMetrics> initialMetrics = getTSMetrics();
      // Execute query.
      session.execute(select.bind(PartitionAwarePolicy.YBToCqlHashCode(keys[keys.length - 2]),
                                  PartitionAwarePolicy.YBToCqlHashCode(keys[keys.length - 1])));
      // Get the metrics again.
      IOMetrics totalMetrics = getCombinedMetrics(initialMetrics);
      // Check only one partition was hit.
      assertEquals(1, totalMetrics.readCount());
    }
  }

  private void selectAndVerify(String query, String result)  {
    assertEquals(result, session.execute(query).one().getString(0));
  }

  private void selectAndVerify(String query, int result)  {
    assertEquals(result, session.execute(query).one().getInt(0));
  }

  private void selectAndVerify(String query, short result)  {
    assertEquals(result, session.execute(query).one().getShort(0));
  }

  private void selectAndVerify(String query, long result)  {
    assertEquals(result, session.execute(query).one().getLong(0));
  }

  private void selectAndVerify(String query, float result)  {
    assertEquals(result, session.execute(query).one().getFloat(0), 1e-13);
  }

  private void selectAndVerify(String query, double result)  {
    assertEquals(result, session.execute(query).one().getDouble(0), 1e-13);
  }

  @Test
  public void testIntegerBounds() throws Exception {
    session.execute("CREATE TABLE test_int_bounds(h int primary key, " +
        "t tinyint, s smallint, i int, b bigint)");

    String insertStmt = "INSERT INTO test_int_bounds(h, %s) VALUES (1, %s)";

    // Test upper bounds.
    session.execute(String.format(insertStmt, "t", "127"));
    session.execute(String.format(insertStmt, "s", "32767"));
    session.execute(String.format(insertStmt, "i", "2147483647"));
    session.execute(String.format(insertStmt, "b", "9223372036854775807"));
    assertQuery("SELECT t, s, i, b FROM test_int_bounds WHERE h = 1",
        "Row[127, 32767, 2147483647, 9223372036854775807]");

    runInvalidStmt(String.format(insertStmt, "t", "128"));
    runInvalidStmt(String.format(insertStmt, "s", "32768"));
    runInvalidStmt(String.format(insertStmt, "i", "2147483648"));
    runInvalidStmt(String.format(insertStmt, "b", "9223372036854775808"));

    // Test lower bounds.
    session.execute(String.format(insertStmt, "t", "-128"));
    session.execute(String.format(insertStmt, "s", "-32768"));
    session.execute(String.format(insertStmt, "i", "-2147483648"));
    session.execute(String.format(insertStmt, "b", "-9223372036854775808"));
    assertQuery("SELECT t, s, i, b FROM test_int_bounds WHERE h = 1",
        "Row[-128, -32768, -2147483648, -9223372036854775808]");

    runInvalidStmt(String.format(insertStmt, "t", "-129"));
    runInvalidStmt(String.format(insertStmt, "s", "-32769"));
    runInvalidStmt(String.format(insertStmt, "i", "-2147483649"));
    runInvalidStmt(String.format(insertStmt, "b", "-9223372036854775809"));
  }

  @Test
  public void testCasts() throws Exception {
    // Create test table.
    session.execute("CREATE TABLE test_local (c1 int PRIMARY KEY, c2 float, c3 double, c4 " +
        "smallint, c5 bigint, c6 text);");
    session.execute("INSERT INTO test_local (c1, c2, c3, c4, c5, c6) values (1, 2.5, 3.3, 4, 5, " +
        "'100')");
    selectAndVerify("SELECT CAST(c1 as integer) FROM test_local", 1);
    selectAndVerify("SELECT CAST(c1 as int) FROM test_local", 1);
    selectAndVerify("SELECT CAST(c1 as smallint) FROM test_local", (short)1);
    selectAndVerify("SELECT CAST(c1 as bigint) FROM test_local", 1L);
    selectAndVerify("SELECT CAST(c1 as float) FROM test_local", 1.0f);
    selectAndVerify("SELECT CAST(c1 as double) FROM test_local", 1.0d);
    selectAndVerify("SELECT CAST(c1 as text) FROM test_local", "1");

    selectAndVerify("SELECT CAST(c2 as integer) FROM test_local", 2);
    selectAndVerify("SELECT CAST(c2 as smallint) FROM test_local", (short)2);
    selectAndVerify("SELECT CAST(c2 as bigint) FROM test_local", 2L);
    selectAndVerify("SELECT CAST(c2 as double) FROM test_local", 2.5d);
    selectAndVerify("SELECT CAST(c2 as text) FROM test_local", "2.500000");

    selectAndVerify("SELECT CAST(c3 as float) FROM test_local", 3.3f);
    selectAndVerify("SELECT CAST(c3 as integer) FROM test_local", 3);
    selectAndVerify("SELECT CAST(c3 as bigint) FROM test_local", 3L);
    selectAndVerify("SELECT CAST(c3 as smallint) FROM test_local", (short)3);
    selectAndVerify("SELECT CAST(c3 as text) FROM test_local", "3.300000");

    selectAndVerify("SELECT CAST(c4 as float) FROM test_local", 4f);
    selectAndVerify("SELECT CAST(c4 as integer) FROM test_local", 4);
    selectAndVerify("SELECT CAST(c4 as bigint) FROM test_local", 4L);
    selectAndVerify("SELECT CAST(c4 as smallint) FROM test_local", (short)4);
    selectAndVerify("SELECT CAST(c4 as double) FROM test_local", 4d);
    selectAndVerify("SELECT CAST(c4 as text) FROM test_local", "4");

    selectAndVerify("SELECT CAST(c5 as float) FROM test_local", 5f);
    selectAndVerify("SELECT CAST(c5 as integer) FROM test_local", 5);
    selectAndVerify("SELECT CAST(c5 as bigint) FROM test_local", 5L);
    selectAndVerify("SELECT CAST(c5 as smallint) FROM test_local", (short)5);
    selectAndVerify("SELECT CAST(c5 as double) FROM test_local", 5d);
    selectAndVerify("SELECT CAST(c5 as text) FROM test_local", "5");

    selectAndVerify("SELECT CAST(c6 as float) FROM test_local", 100f);
    selectAndVerify("SELECT CAST(c6 as integer) FROM test_local", 100);
    selectAndVerify("SELECT CAST(c6 as bigint) FROM test_local", 100L);
    selectAndVerify("SELECT CAST(c6 as smallint) FROM test_local", (short)100);
    selectAndVerify("SELECT CAST(c6 as double) FROM test_local", 100d);
    selectAndVerify("SELECT CAST(c6 as text) FROM test_local", "100");

    // Try edge cases.
    session.execute("INSERT INTO test_local (c1, c2, c3, c4, c5, c6) values (2147483647, 2.5, " +
        "3.3, 32767, 9223372036854775807, '2147483647')");
    selectAndVerify("SELECT CAST(c1 as int) FROM test_local WHERE c1 = 2147483647", 2147483647);
    selectAndVerify("SELECT CAST(c1 as bigint) FROM test_local WHERE c1 = 2147483647", 2147483647L);
    selectAndVerify("SELECT CAST(c1 as smallint) FROM test_local WHERE c1 = 2147483647",
        (short)2147483647);
    selectAndVerify("SELECT CAST(c5 as int) FROM test_local WHERE c1 = 2147483647",
        (int)9223372036854775807L);
    selectAndVerify("SELECT CAST(c5 as smallint) FROM test_local WHERE c1 = 2147483647",
        (short)9223372036854775807L);
    selectAndVerify("SELECT CAST(c6 as smallint) FROM test_local WHERE c1 = 2147483647",
        (short)2147483647);
    selectAndVerify("SELECT CAST(c6 as int) FROM test_local WHERE c1 = 2147483647",
        2147483647);
    selectAndVerify("SELECT CAST(c6 as bigint) FROM test_local WHERE c1 = 2147483647",
        2147483647L);
    selectAndVerify("SELECT CAST(c6 as text) FROM test_local WHERE c1 = 2147483647",
        "2147483647");
    selectAndVerify("SELECT CAST(c5 as text) FROM test_local WHERE c1 = 2147483647",
        "9223372036854775807");
  }
}
