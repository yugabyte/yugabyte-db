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

import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;

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

@RunWith(value=YBTestRunner.class)
public class TestPagingSelect extends BaseCQLTest {

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
    // Iterator will perform additional fetching to retreive the entire set.
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
}
