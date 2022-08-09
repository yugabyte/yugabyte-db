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
import java.io.PrintStream;
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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgYbHashCodeScanProjection extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgYbHashCodeScanProjection.class);
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
    try (Statement stmt = connection.createStatement()) {
      LOG.info("Creating table " + kTableName);
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
    try (Statement stmt = connection.createStatement()) {
      LOG.info("Dropping table " + kTableName);
      stmt.execute("DROP TABLE " + kTableName);
    }
  }

  // Extract the column reference ids from a DocDB request debug string pattern:
  // "column_refs { ids: <id0> ids: <id1> ... }"
  // "column_refs {}" can be empty e.g. when COUNT(*) gets pushed down
  private static List<String> extractColumnRefIdsFromDocDbRequest(String docDbReqStr) {
    Matcher colRefMatcher = Pattern.compile("(column_refs \\{ (ids: \\d+ )*\\})")
                            .matcher(docDbReqStr);
    boolean found = colRefMatcher.find();
    assertTrue("column_refs list not found in:\n" + docDbReqStr, found);
    String colRefStr = colRefMatcher.group(1);

    List<String> colRefIds = new ArrayList<String>();
    Matcher colIdMatcher = Pattern.compile("(ids: (\\d+))").matcher(colRefStr);
    for (int start = 0; colIdMatcher.find(start); start = colIdMatcher.end()) {
      colRefIds.add(colIdMatcher.group(2));
    }
    return colRefIds;
  }

  private static int extractMaxHashCodeFromDocDbRequest(String docDbReqStr) {
    Matcher matcher = Pattern.compile("max_hash_code: (\\d+)")
                            .matcher(docDbReqStr);
    if (matcher.find()) {
      return Integer.parseInt(matcher.group(1));
    }
    return kNoHashCode;
  }

  // rocksdb_db_iter_bytes_read is accumulated key and the value sizes whenever
  // a valid entry was found at each Seek/Next/Prev call.
  private int getRocksDbIterBytesRead(String tableName) throws Exception {
    return getTableCounterMetric(tableName, "rocksdb_db_iter_bytes_read");
  }

  private static class ScanInfo {
    List<String> projection;  // requested columns
    int maxHashCode;          // requested hash code upper bound. -1 if none.
    int iterBytesRead;        // ITER_BYTES_READ RocksDB stats

    public String toString() {
      return String.format("{ iterBytesRead: %d colIds: %s maxHashCode: %d }",
                           iterBytesRead, projection.toString(), maxHashCode);
    }
  };

  private ScanInfo executeQueryAndCollectScanInfo(Statement stmt, String query) throws Exception {
    final ByteArrayOutputStream docDbReq = new ByteArrayOutputStream();
    final PrintStream origOut = System.out;
    ScanInfo info = new ScanInfo();

    origOut.flush();
    System.setOut(new PrintStream(docDbReq, true/*autoFlush*/));
    try {
      stmt.execute("SET yb_debug_log_docdb_requests = true");
      int iterBytesRead0 = getRocksDbIterBytesRead(kTableName);
      try (ResultSet rs = stmt.executeQuery(query)) {
        while (rs.next()) {
        }
        rs.close();
      }
      info.iterBytesRead = getRocksDbIterBytesRead(kTableName) - iterBytesRead0;
      stmt.execute("SET yb_debug_log_docdb_requests = false");
      String docDbReqStr = docDbReq.toString();
      info.projection = extractColumnRefIdsFromDocDbRequest(docDbReqStr);
      info.maxHashCode = extractMaxHashCodeFromDocDbRequest(docDbReqStr);
    } finally {
      // send the captured output to the original stdout stream, too.
      origOut.print(docDbReq.toString());
      System.setOut(origOut);
    }
    return info;
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
    final String query
        = String.format("/*+ Set(enable_seqscan off) */"
                        + "SELECT %s FROM %s WHERE yb_hash_code(k) <= %d",
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
  private void testOneCase(Statement stmt, String selectList, int expectedNumColumns,
                           int maxHashCode) throws Exception {
    final ScanInfo seqScan = collectSeqScanInfo(stmt, selectList);
    final ScanInfo indexScan = collectFullIndexScanInfo(stmt, selectList);
    final ScanInfo hashScan = collectHashCodeScanInfo(stmt, selectList, maxHashCode);

    final double scanFraction = (double)(maxHashCode + 1) / (kYbHashCodeMax + 1);
    final double errorTolerance = (maxHashCode >= kYbHashCodeMax? 0.0: kErrorTolerance);

    final String message = String.format(
        "selectList=[%s] maxHashCode=%d seqScan: %s indexScan: %s hashScan: %s",
        selectList, maxHashCode, seqScan.toString(), indexScan.toString(), hashScan.toString());

    assertEquals(message, maxHashCode, hashScan.maxHashCode);
    assertEquals(message, expectedNumColumns, hashScan.projection.size());

    verifyPartialScanIterBytesRead(message, seqScan.iterBytesRead, scanFraction,
                                   hashScan.iterBytesRead, errorTolerance);

    verifyPartialScanIterBytesRead(message, indexScan.iterBytesRead, scanFraction,
                                   hashScan.iterBytesRead, errorTolerance);
  }

  @Test
  public void testScans() throws Exception {
    String results = "";
    try (Statement stmt = connection.createStatement()) {
      // full range
      testOneCase(stmt, "z", 1, kYbHashCodeMax);
      testOneCase(stmt, "z, d", 2, kYbHashCodeMax);
      testOneCase(stmt, "*", kNumTableColumns, kYbHashCodeMax);
      testOneCase(stmt, "k", 1, kYbHashCodeMax);  // the hash key
      testOneCase(stmt, "0", 0, kYbHashCodeMax);  // no table column
      testOneCase(stmt, "yb_hash_code(k)", 1, kYbHashCodeMax);

      // partial range
      testOneCase(stmt, "z", 1, 32767);  // 1/2
      testOneCase(stmt, "z", 1, 16383);  // 1/4
      testOneCase(stmt, "z", 1, 8191);   // 1/8
      testOneCase(stmt, "z", 1, 4095);   // 1/16
    }
  }
}
