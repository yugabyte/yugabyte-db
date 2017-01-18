// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Iterator;

import org.junit.Test;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static org.junit.Assert.assertEquals;

public class TestSelect extends TestBase {
  @Test
  public void testSimpleQuery() throws Exception {
    LOG.info("TEST CQL SIMPLE QUERY - Start");

    // Setup test table.
    SetupTable("test_select", 10);

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

    LOG.info("TEST CQL SIMPLE QUERY - End");
  }
}
