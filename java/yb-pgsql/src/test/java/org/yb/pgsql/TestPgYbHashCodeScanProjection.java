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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.io.OutputStream;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestPgYbHashCodeScanProjection extends BasePgSQLTest {
  private static final String kTableName = "scan_test_table";
  private static final int kNumTableColumns = 10;
  private static final int kTableRows = 2000;
  // enforce single batch to avoid the influences from pagination
  private static final int kPrefetchLimit = kTableRows + 1;
  private static final int kYbHashCodeMax = 65535;
  private static final int kNoHashCode = -1;
  private static final double kErrorTolerance = 0.05;

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_prefetch_limit", Integer.toString(kPrefetchLimit));
    return flagMap;
  }

  @Before
  public void setUp() throws Exception {
    logInterceptor.attach();
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format(
          "CREATE TABLE %s ("
          + "k int, a text, b text, c int, d double precision, "
          + "v int, w int, x int, y int, z date, "
          + "PRIMARY KEY(k HASH))", kTableName));

      // Verify that kNumTableColumns and the table definition are in sync
      assertEquals(kNumTableColumns, getNumTableColumns(stmt, kTableName));

      stmt.execute(String.format(
          "INSERT INTO %s SELECT "
          + "i, lpad((i+2000)::text, 500, '0'), lpad((i%%512)::text, 1000, '#'), i, i::float8/17, "
          + "i, i, i, i, now() FROM generate_series(1, %d) i",
          kTableName, kTableRows));

      // Run a query once to put rocksDB in some predictable state and reduce the chances
      // of intermittent failures.
      collectFullIndexScanInfo(stmt, "*");
    }
  }

  @After
  public void tearDown() throws Exception {
    logInterceptor.detach();
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DROP TABLE " + kTableName);
    }
  }

  // rocksdb_db_iter_bytes_read is accumulated key and the value sizes whenever
  // a valid entry was found at each Seek/Next/Prev call.
  private int getRocksDbIterBytesRead(String tableName) throws Exception {
    return getTableCounterMetric(tableName, "rocksdb_db_iter_bytes_read");
  }

  private ScanInfo executeQueryAndCollectScanInfo(Statement stmt, String query) throws Exception {
    stmt.execute("SET yb_debug_log_docdb_requests = true");
    final int iterBytesReadInitial = getRocksDbIterBytesRead(kTableName);
    String logs;
    logInterceptor.start();
    try (ResultSet rs = stmt.executeQuery(query)) {
      while (rs.next()) {
      }
      Thread.sleep(5); // Reduce flakiness by giving a moment for the logs to flush
      rs.close();
    } finally {
      logs = logInterceptor.stop();
    }
    stmt.execute("SET yb_debug_log_docdb_requests = false");
    return infoBuilder.build(getRocksDbIterBytesRead(kTableName) - iterBytesReadInitial, logs);
  }

  private ScanInfo collectSeqScanInfo(Statement stmt, String selectList) throws Exception {
    final String query = String.format("SELECT %s FROM %s", selectList, kTableName);
    assertFalse(isIndexScan(stmt, query, kTableName + "_pkey"));
    return executeQueryAndCollectScanInfo(stmt, query);
  }

  // Note: the range predicate is specified only for choosing the index scan access path.
  // Specifying a partial range on the hash key column won't reduce the actual scan range.
  private ScanInfo collectFullIndexScanInfo(Statement stmt, String selectList) throws Exception {
    final String query = String.format(
        "/*+ Set(enable_seqscan off) */SELECT %s FROM %s WHERE k < %d",
        selectList, kTableName, (kTableRows + 1));
    assertFalse(isIndexScan(stmt, query, kTableName + "_pkey"));
    return executeQueryAndCollectScanInfo(stmt, query);
  }

  private ScanInfo collectHashCodeScanInfo(Statement stmt, String selectList, int maxHashCode)
      throws Exception {
    final String query = String.format(
      "/*+ Set(enable_seqscan off) */ SELECT %s FROM %s WHERE yb_hash_code(k) <= %d",
      selectList, kTableName, maxHashCode);
    assertTrue(isIndexScan(stmt, query, kTableName + "_pkey"));
    assertFalse(doesNeedPgFiltering(stmt, query));
    return executeQueryAndCollectScanInfo(stmt, query);
  }

  private static void verifyPartialScanIterBytesRead(
      String message, int fullScanBytes, double scanFraction, int actualBytes,
      double errorTolerance) throws Exception {
    assertEquals(message, fullScanBytes * scanFraction, actualBytes,
                 fullScanBytes * scanFraction * errorTolerance);
  }

  // Execute a single table query with yb_hash_code predicate that returns specified SELECT-list
  // items and verify that:
  //   1. The column_refs and max_hash_code fields in the DocDB request is as expected.
  //   2. The rocksdb_db_iter_bytes_read stats delta is inline with the scaled values from
  //      the executions of similar queries using Seq Scan and (full range) Index Scan.
  // @param stmt The statement used to execute the queries
  // @param selectList The SELECT-list items used in the queries
  // @param expectedNumColumns Expected number of items in the column_refs DocDB request field
  // @param maxHashCode The yb_hash_code predicate upper bound value to use in the query.
  // @param expectedMaxHashCodeInRequest The upper bound for hash code in request sent to t-server
  private void testOneCase(
      Statement stmt, String selectList, int expectedNumColumns,
      int maxHashCode, int expectedMaxHashCodeInRequest)
      throws Exception {
    final ScanInfo seqScan = collectSeqScanInfo(stmt, selectList);
    final ScanInfo indexScan = collectFullIndexScanInfo(stmt, selectList);
    final ScanInfo hashScan = collectHashCodeScanInfo(stmt, selectList, maxHashCode);

    final double scanFraction =
        (double)(Math.min(maxHashCode, kYbHashCodeMax) + 1) / (kYbHashCodeMax + 1);
    final double errorTolerance = (maxHashCode >= kYbHashCodeMax? 0.0: kErrorTolerance);

    final String message = String.format(
        "selectList=[%s] maxHashCode=%d seqScan: %s indexScan: %s hashScan: %s",
        selectList, maxHashCode, seqScan, indexScan, hashScan);

    assertEquals(message, expectedMaxHashCodeInRequest, hashScan.maxHashCode);
    assertEquals(message, expectedNumColumns, hashScan.projection.size());

    verifyPartialScanIterBytesRead(
      message, seqScan.iterBytesRead, scanFraction, hashScan.iterBytesRead, errorTolerance);

    verifyPartialScanIterBytesRead(
      message, indexScan.iterBytesRead, scanFraction, hashScan.iterBytesRead, errorTolerance);
  }

  private void testOneCase(
      Statement stmt, String selectList, int expectedNumColumns, int maxHashCode)
      throws Exception {
    testOneCase(stmt, selectList, expectedNumColumns, maxHashCode, maxHashCode);
  }

  @Test
  public void testScans() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      // Note: In case of using yb_hash_code function all its argument columns are fetched.
      //       They are required for row recheck.

      // full range
      testOneCase(stmt, "z", 2, kYbHashCodeMax, kNoHashCode);

      // more than full range
      testOneCase(stmt, "z", 2, kYbHashCodeMax + 1, kNoHashCode);

      // almost full range
      final int maxHashCode = kYbHashCodeMax - 1;
      testOneCase(stmt, "z", 2, maxHashCode);
      testOneCase(stmt, "z, d", 3, maxHashCode);
      testOneCase(stmt, "*", kNumTableColumns, maxHashCode);
      testOneCase(stmt, "k", 1, maxHashCode);  // the hash key
      testOneCase(stmt, "0", 1, maxHashCode);  // no table column
      testOneCase(stmt, "yb_hash_code(k)", 1, maxHashCode);

      // partial range
      testOneCase(stmt, "z", 2, 32767); // 1/2
      testOneCase(stmt, "z", 2, 16383); // 1/4
      testOneCase(stmt, "z", 2, 8191);  // 1/8
      testOneCase(stmt, "z", 2, 4095);  // 1/16
    }
  }

  private static class OutputStreamCombiner extends OutputStream {

    public OutputStreamCombiner(OutputStream str[]) {
      streams = str;
    }

    @Override
    public void write(int b) throws IOException {
      for (OutputStream s : streams) {
        s.write(b);
      }
    }

    @Override
    public void flush() throws IOException {
      for (OutputStream s : streams) {
        s.flush();
      }
    }

    @Override
    public void close() throws IOException {
      for (OutputStream s : streams) {
        s.close();
      }
    }

    private final OutputStream[] streams;
  }

  private static class LogInterceptor extends OutputStream {
    public void attach() {
      original = System.out;
      System.setOut(new PrintStream(new OutputStreamCombiner(new OutputStream[]{original, this})));
    }

    public void detach() {
      System.setOut(original);
    }

    public void start() {
      intercepting = true;
    }

    public String stop() {
      intercepting = false;
      String result = intercepted.toString();
      intercepted.reset();
      return result;
    }

    @Override
    public void write(int b) throws IOException {
      if (intercepting) {
        intercepted.write(b);
      }
    }

    private PrintStream original;
    private boolean intercepting = false;
    private final ByteArrayOutputStream intercepted = new ByteArrayOutputStream();
  };

  private static class ScanInfo {
    final List<String> projection; // requested columns
    final int maxHashCode;         // requested hash code upper bound. -1 if none.
    final int iterBytesRead;       // ITER_BYTES_READ RocksDB stats

    ScanInfo(List<String> p, int hC, int bR) {
      projection = p;
      maxHashCode = hC;
      iterBytesRead = bR;
    }

    @Override
    public String toString() {
      return String.format(
        "{ iterBytesRead: %d colIds: %s maxHashCode: %d }",
        iterBytesRead, projection, maxHashCode);
    }
  };

  private static class ScanInfoBuilder {
    public ScanInfo build(int bytesRead, String logs) {
      Matcher colRefMatcher = colRef.matcher(logs);
      assertTrue("column_refs list not found in:\n" + logs, colRefMatcher.find());
      List<String> colRefIds = new ArrayList<>();
      Matcher colIdMatcher = colId.matcher(colRefMatcher.group(1));
      for (int start = 0; colIdMatcher.find(start); start = colIdMatcher.end()) {
        colRefIds.add(colIdMatcher.group(2));
      }
      Matcher hashMatcher = hashCode.matcher(logs);
      return new ScanInfo(
          colRefIds,
          hashMatcher.find() ? Integer.parseInt(hashMatcher.group(1))
                             : kNoHashCode,
          bytesRead);
    }

    private final Pattern colRef = Pattern.compile("(column_refs \\{ (ids: \\d+ )*\\})");
    private final Pattern colId = Pattern.compile("(ids: (\\d+))");
    private final Pattern hashCode = Pattern.compile("max_hash_code: (\\d+)");
  };

  private final LogInterceptor logInterceptor = new LogInterceptor();
  private final ScanInfoBuilder infoBuilder = new ScanInfoBuilder();
}
