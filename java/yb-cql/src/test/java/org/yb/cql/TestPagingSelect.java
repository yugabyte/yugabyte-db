// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Iterator;
import java.util.Date;
import java.util.Calendar;
import java.util.GregorianCalendar;
import java.util.TimeZone;

import org.junit.Test;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public class TestPagingSelect extends TestBase {
  @Test
  public void testContinuousQuery() throws Exception {
    LOG.info("TEST CQL CONTINUOUS SELECT QUERY - Start");

    // Setup test table.
    SetupTable("test_select", 0);

    // Insert multiple rows with the same partition key.
    // Default page size is 5000, so make it around 4 pages.
    int num_rows_per_page = cluster.getConfiguration().getQueryOptions().getFetchSize();;
    int num_rows = num_rows_per_page * 4;
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
    }
    assertEquals(num_rows, row_count);
    LOG.info("TEST CQL CONTINUOUS SELECT QUERY - end");
  }
}
