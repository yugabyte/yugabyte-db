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

import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.SimpleStatement;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestPagingSelect extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPagingSelect.class);

  /**
   * Override test timeout. testContinuousQuery() inserts 20100 rows and selects them back and
   * need more time to execute in CentOS debug and asan builds.
   */
  public int getTestMethodTimeoutSec() {
    // No need to adjust for TSAN vs. non-TSAN here, it will be done automatically.
    return 300;
  }

  @Test
  public void testContinuousQuery() throws Exception {
    LOG.info("TEST CQL CONTINUOUS SELECT QUERY - Start");

    // Setup test table.
    setupTable("test_select", 0);

    // Insert multiple rows with the same partition key.
    // Default page size is 5000, so make it around 4 pages.
    int num_rows_per_page = cluster.getConfiguration().getQueryOptions().getFetchSize();
    int num_rows = num_rows_per_page * 4 + 100;
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
    ResultSet rs = session.execute(multi_stmt);

    int row_count = 0;
    Iterator<Row> iter = rs.iterator();
    // Verifying that there are only number of rows fetched equal to the max page size.
    assertEquals(num_rows_per_page, rs.getAvailableWithoutFetching());
    assertFalse(rs.isFullyFetched());
    // Iterator will perform additional fetching to retrieve the entire set.
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
      assertEquals(h1_shared, row.getInt(0));
      assertEquals(h2_shared, row.getString(1));
      assertEquals(row_count + 100, row.getInt(2));
      assertEquals(String.format("r%d", row_count + 100), row.getString(3));
      assertEquals(row_count + 1000, row.getInt(4));
      assertEquals(String.format("v%d", row_count + 1000), row.getString(5));
      row_count++;
      assert(row_count <= num_rows);
    }
    assertEquals(num_rows, row_count);
    LOG.info("TEST CQL CONTINUOUS SELECT QUERY - end");
  }

  private void assertIncompleteRangeColumns(String query, String column, int total) {
    // Set page size to 1 row and verify that all the range rows are returned ok.
    int v = 0;
    SimpleStatement stmt = new SimpleStatement(query);
    stmt.setFetchSize(1);
    for (Row row : session.execute(stmt)) {
      assertEquals(v, row.getInt(column));
      v++;
    }
    assertEquals(v, total);
  }

  @Test
  public void testIncompleteRangeColumns() throws Exception {

    // Create test table with seed data.
    final int TOTAL_ROWS = 10;
    session.execute("CREATE TABLE test_paging (h int, r1 int, r2 int, PRIMARY KEY ((h), r1, r2));");
    PreparedStatement pstmt = session.prepare("INSERT INTO test_paging (h, r1, r2) " +
                                              "VALUES (0, ?, 0);");
    for (int r1 = 0; r1 < TOTAL_ROWS; r1++) {
      session.execute(pstmt.bind(Integer.valueOf(r1)));
    }

    // Verify that page results are returned fine when no/incomplete range columns are specified.
    assertIncompleteRangeColumns("SELECT r1 FROM test_paging;", "r1", TOTAL_ROWS);
    assertIncompleteRangeColumns("SELECT r1 FROM test_paging WHERE h = 0;", "r1", TOTAL_ROWS);
    assertIncompleteRangeColumns("SELECT r1 FROM test_paging " +
                                 "WHERE h = 0 AND r1 >= 0 AND r1 <= 10;", "r1", TOTAL_ROWS);
    assertIncompleteRangeColumns("SELECT r1 FROM test_paging WHERE h = 0 AND r1 >= 0;",
                                 "r1", TOTAL_ROWS);
    assertIncompleteRangeColumns("SELECT r1 FROM test_paging WHERE h = 0 AND r1 <= 10;",
                                 "r1", TOTAL_ROWS);
  }

  @Test
  public void testMultiPartitionSelect() throws Exception {
    session.execute("CREATE TABLE test_in_paging (h1 int, h2 int, r int, v int, " +
                        "PRIMARY KEY ((h1, h2), r));");
    PreparedStatement pstmt = session.prepare("INSERT INTO test_in_paging (h1, h2, r, v) " +
                                                  "VALUES (?, ?, ?, 0);");
    BatchStatement batch = new BatchStatement();
    for (Integer h1 = 0; h1 < 30; h1++) {
      for (Integer h2 = 0; h2 < 30; h2++) {
        for (Integer r = 0; r < 5; r++) {
          batch.add(pstmt.bind(h1, h2, r));
        }
      }
    }
    session.execute(batch);

    SimpleStatement stmt = new SimpleStatement("SELECT * FROM test_in_paging " +
                                                   "WHERE h1 IN (4,3) AND h2 = 1 " +
                                                   "AND r >= 2 and r <= 8");
    stmt.setFetchSize(4);
    Set<String> expectedRows = new HashSet<>();
    String rowTemplate = "Row[%d, 1, %d, 0]";
    for (int h1 = 3; h1 <= 4; h1++) {
      for (int r = 2; r < 5; r++) {
        expectedRows.add(String.format(rowTemplate, h1, r));
      }
    }
    assertQuery(stmt, expectedRows);

  }

  @Test
  public void testReverseScan() throws Exception {
    session.execute("CREATE TABLE test_reverse_scan_paging (h int, r int, " +
                            "PRIMARY KEY (h, r))");
    PreparedStatement pstmt = session.prepare("INSERT INTO test_reverse_scan_paging " +
                                                      "(h, r) VALUES (1, ?)");

    List<String> expectedRows = new ArrayList<>();
    String rowTemplate = "Row[1, %d]";

    BatchStatement batch = new BatchStatement();
    for (Integer r = 1; r < 15; r++) {
      batch.add(pstmt.bind(r));
      expectedRows.add(String.format(rowTemplate, r));
    }
    session.execute(batch);
    int numRows = expectedRows.size();

    // Double-check rows.
    SimpleStatement stmt = new SimpleStatement("SELECT * FROM test_reverse_scan_paging " +
                                                       "WHERE h = 1");
    assertQueryRowsOrdered(stmt, expectedRows);

    // Reverse expected rows for reverse scans.
    Collections.reverse(expectedRows);

    // Test full reverse scan (different fetch sizes).
    stmt = new SimpleStatement("SELECT * FROM test_reverse_scan_paging WHERE h = 1" +
                                                       "ORDER BY r DESC");
    stmt.setFetchSize(1);
    assertQueryRowsOrdered(stmt, expectedRows);
    stmt.setFetchSize(3);
    assertQueryRowsOrdered(stmt, expectedRows);

    // Test reverse scan with exclusive bounds: (3, 10).
    // Note: sublist [4, 10) means indexes [numRows - 9, numRows - 3) in reversed list.
    stmt = new SimpleStatement("SELECT * FROM test_reverse_scan_paging WHERE h = 1" +
                                       "AND r > 3 AND r < 10 ORDER BY r DESC");
    stmt.setFetchSize(1);
    assertQueryRowsOrdered(stmt, expectedRows.subList(numRows - 9, numRows - 3));
    stmt.setFetchSize(3);
    assertQueryRowsOrdered(stmt, expectedRows.subList(numRows - 9, numRows - 3));

    // Test reverse scan with inclusive bounds: [5, 12].
    // Note: sublist [5, 13) means indexes [numRows - 12, numRows - 4) in reversed list.
    stmt = new SimpleStatement("SELECT * FROM test_reverse_scan_paging WHERE h = 1" +
                                       "AND r >= 5 AND r <= 12 ORDER BY r DESC");
    stmt.setFetchSize(1);
    assertQueryRowsOrdered(stmt, expectedRows.subList(numRows - 12, numRows - 4));
    stmt.setFetchSize(3);
    assertQueryRowsOrdered(stmt, expectedRows.subList(numRows - 12, numRows - 4));

    // Test limit.
    // Test reverse scan upper (start) bound and limit: 10,9,8,7,6.
    // Note: sublist [6, 11) means indexes [numRows - 10, numRows - 5) in reversed list.
    stmt = new SimpleStatement("SELECT * FROM test_reverse_scan_paging WHERE h = 1" +
                                       "AND r < 11 ORDER BY r DESC LIMIT 5");
    stmt.setFetchSize(1);
    assertQueryRowsOrdered(stmt, expectedRows.subList(numRows - 10, numRows - 5));
    stmt.setFetchSize(3);
    assertQueryRowsOrdered(stmt, expectedRows.subList(numRows - 10, numRows - 5));
  }

  @Test
  public void testReverseScanMultiRangeCol() throws Exception {
    session.execute("CREATE TABLE test_reverse_scan_multicol (h int, r1 int, r2 int, r3 int, " +
                            "PRIMARY KEY (h, r1, r2, r3))");
    PreparedStatement pstmt = session.prepare("INSERT INTO test_reverse_scan_multicol " +
                                                      "(h, r1, r2, r3) VALUES (1, ?, ?, ?)");


    BatchStatement batch = new BatchStatement();
    for (Integer r1 = 1; r1 <= 5; r1++) {
      for (Integer r2 = 1; r2 <= 5; r2++) {
        for (Integer r3 = 1; r3 <= 5; r3++) {
          batch.add(pstmt.bind(r1, r2, r3));
        }
      }
    }
    session.execute(batch);

    // Test reverse scan with prefix bounds: r1(1, 4), r2[2,3].
    SimpleStatement stmt =
            new SimpleStatement("SELECT * FROM test_reverse_scan_multicol WHERE h = 1" +
                                       "AND r1 >= 2 AND r1 <= 4 AND r2 > 1 and r2 < 4" +
                                       "ORDER BY r1 DESC, r2 DESC, r3 DESC");
    stmt.setFetchSize(1);
    Iterator<Row> rows1 = session.execute(stmt).iterator();
    stmt.setFetchSize(3);
    Iterator<Row> rows2 = session.execute(stmt).iterator();

    String rowTemplate = "Row[1, %d, %d, %d]";
    for (Integer r1 = 4; r1 >= 2; r1--) {
      for (Integer r2 = 3; r2 > 1; r2--) {
        for (Integer r3 = 5; r3 >= 1; r3--) {
          String expected = String.format(rowTemplate, r1, r2, r3);
          assertEquals(rows1.next().toString(), expected);
          assertEquals(rows2.next().toString(), expected);
        }
      }
    }
    assertFalse(rows1.hasNext());
    assertFalse(rows2.hasNext());

    // Test reverse scan with non-prefix bounds: r1(1, 4), r3[2, 3].
    stmt = new SimpleStatement("SELECT * FROM test_reverse_scan_multicol WHERE h = 1" +
                                       "AND r1 > 1 AND r1 < 4 AND r3 >= 2 and r3 <= 3" +
                                       "ORDER BY r1 DESC, r2 DESC, r3 DESC");
    stmt.setFetchSize(1);
    rows1 = session.execute(stmt).iterator();
    stmt.setFetchSize(3);
    rows2 = session.execute(stmt).iterator();

    for (Integer r1 = 3; r1 > 1; r1--) {
      for (Integer r2 = 5; r2 >= 1; r2--) {
        for (Integer r3 = 3; r3 >= 2; r3--) {
          String expected = String.format(rowTemplate, r1, r2, r3);
          assertEquals(rows1.next().toString(), expected);
          assertEquals(rows2.next().toString(), expected);
        }
      }
    }
    assertFalse(rows1.hasNext());
    assertFalse(rows2.hasNext());
  }

  @Test
  public void testSelectWithIndex() throws Exception {
    session.execute("CREATE TABLE test_table (h INT PRIMARY KEY, a int, b varchar) WITH " +
        "tablets = 100 AND default_time_to_live = 0 AND transactions = {'enabled': 'true'};");
    session.execute("CREATE INDEX test_index_ab ON test_table (a) include (b);");

    // Allow the index back-fill process to complete as it can alter the schema due to which we can
    // get schema-mismatch errors. See https://github.com/yugabyte/yugabyte-db/issues/15806.
    waitForReadPermsOnAllIndexes("test_table");

    // Insert dummy data.
    for (int i = 0; i < 15; i++) {
      session.execute(
          String.format("INSERT INTO test_table (h, a, b) VALUES (%d, 10, 'varstr%d')", i, i));
    }

    // Index only scan.
    {
      HashSet<String> expectedRows = new HashSet<>(Arrays.asList(
          "Row[10, varstr0]", "Row[10, varstr1]", "Row[10, varstr2]", "Row[10, varstr3]",
          "Row[10, varstr4]", "Row[10, varstr5]", "Row[10, varstr6]", "Row[10, varstr7]",
          "Row[10, varstr8]", "Row[10, varstr9]", "Row[10, varstr10]", "Row[10, varstr11]",
          "Row[10, varstr12]", "Row[10, varstr13]", "Row[10, varstr14]"));
      String query = "SELECT a, b FROM test_table WHERE a = 10";

      // Prepared statement.
      PreparedStatement prepared = session.prepare(query);
      assertQuery(prepared.bind().setFetchSize(1), expectedRows);

      // Direct select.
      assertQuery(new SimpleStatement(query).setFetchSize(1), expectedRows);
    }

    HashSet<String> expectedRows = new HashSet<>(Arrays.asList(
        "Row[0, 10, varstr0]", "Row[1, 10, varstr1]", "Row[2, 10, varstr2]",
        "Row[3, 10, varstr3]", "Row[4, 10, varstr4]", "Row[5, 10, varstr5]",
        "Row[6, 10, varstr6]", "Row[7, 10, varstr7]", "Row[8, 10, varstr8]",
        "Row[9, 10, varstr9]", "Row[10, 10, varstr10]", "Row[11, 10, varstr11]",
        "Row[12, 10, varstr12]", "Row[13, 10, varstr13]", "Row[14, 10, varstr14]"));

    // Index + Base table.
    {
      String query = "SELECT * FROM test_table WHERE a = 10";

      // Prepared statement.
      PreparedStatement prepared = session.prepare(query);
      assertQuery(prepared.bind().setFetchSize(1),expectedRows);

      // Direct select.
      assertQuery(new SimpleStatement(query).setFetchSize(1), expectedRows);
    }

    // Base table only.
    {
      String query = "SELECT * FROM test_table";

      // Prepared statement.
      PreparedStatement prepared = session.prepare(query);
      assertQuery(prepared.bind().setFetchSize(1), expectedRows);

      // Direct select.
      assertQuery(new SimpleStatement(query).setFetchSize(1), expectedRows);
    }
  }
}
