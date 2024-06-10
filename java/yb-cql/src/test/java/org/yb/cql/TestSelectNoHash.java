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

import java.util.Iterator;

import org.junit.Test;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestSelectNoHash extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestSelectNoHash.class);

  @Test
  public void testSelectNoHashQuery() throws Exception {
    LOG.info("TEST CQL SIMPLE NO HASH QUERY - Start");
    int num_rows = 100;

    // Setup test table.
    setupTable("test_select", num_rows);

    // Select data from the test table.
    String select_stmt = "SELECT * FROM test_select;";
    ResultSet rs = session.execute(select_stmt);

    int row_count = 0;
    int total = 0;
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
      total += row.getInt(0);
      row_count++;
      assertTrue(row_count <= num_rows);
    }
    // Check if sum of all values are correct.
    assertEquals((row_count - 1) * row_count / 2, total);
    assertEquals(num_rows, row_count);

    LOG.info("Starting test for queries with only h1 defined.");
    // Testing partial hash key test for h1.
    select_stmt = "SELECT * FROM test_select WHERE h1 = 7;";
    rs = session.execute(select_stmt);
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

      assertEquals(row.getInt(0), 7);
      assertEquals(row.getString(1), "h7");
      assertEquals(row.getInt(2), 107);
      assertEquals(row.getString(3), "r107");
      assertEquals(row.getInt(4), 1007);
      assertEquals(row.getString(5), "v1007");
      row_count++;
    }
    assertEquals(1, row_count);

    LOG.info("Starting test for queries with only h2 defined.");
    // Testing partial hash key test for h2.
    select_stmt = "SELECT * FROM test_select WHERE h2 = 'h7';";
    rs = session.execute(select_stmt);
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
    num_rows = 20;
    int num_filter_rows = 10;
    int h1_shared = 1111111;
    String h2_shared = "h2_shared_key";
    for (int idx = 0; idx < num_rows; idx++) {
      // INSERT: Valid statement with column list.
      String insert_stmt = String.format(
        "INSERT INTO test_select(h1, h2, r1, r2, v1, v2) VALUES(%d, '%s', %d, 'r%d', %d, 'v%d');",
        h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
      session.execute(insert_stmt);
    }

    // Verify multi-row select with partial hash and partial range key.
    String multi_stmt = String.format("SELECT h1, h2, r1, r2, v1, v2 FROM test_select" +
                                      "  WHERE h1 = %d AND r1 > %d;",
                                      h1_shared, num_filter_rows + 100);
    rs = session.execute(multi_stmt);

    row_count = num_filter_rows + 1;
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

    LOG.info("TEST CQL SIMPLE NO HASH QUERY - End");
  }
}
