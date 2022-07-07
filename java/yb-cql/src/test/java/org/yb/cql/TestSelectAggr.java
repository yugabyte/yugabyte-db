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
import org.yb.minicluster.RocksDBMetrics;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.InvalidQueryException;
import com.datastax.driver.core.exceptions.QueryValidationException;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestSelectAggr extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestSelectAggr.class);

  int h_min = 1;
  int r_min = 2;
  long v1_min = 1000;
  int  v2_min = 100;
  float v5_min = 77.77f;
  double v6_min = 999.99;

  long v1_max = v1_min;
  int v2_max = v2_min;
  float v5_max = v5_min;
  double v6_max = v6_min;
  int h_max = h_min;
  int r_max = 10000;

  long v1_total = v1_min;
  int v2_total = v2_min;
  float v5_total = v5_min;
  double v6_total = v6_min;

  long total_inserted_row = 27;

  public void setupAggrTest() throws Exception {
    String create_stmt =
        "CREATE TABLE test_aggr_expr(h int, r int," +
        "  v1 bigint, v2 int, v5 float, v6 double, " +
        "  primary key(h, r));";
    session.execute(create_stmt);

    // Insert a rows whose hash value is '1'.
    int row_idx = 0;
    String insert_stmt = String.format(
        "INSERT INTO test_aggr_expr(h, r, v1, v2, v5, v6)" +
        "  VALUES(%d, %d, %d, %d, %f, %f);",
        h_min, r_max, v1_min, v2_min, v5_min, v6_min);
    session.execute(insert_stmt);

    // Insert the rest of rows, one of which has hash value of '1'.
    for (row_idx++; row_idx < total_inserted_row - 1; row_idx++) {
      insert_stmt = String.format(
          "INSERT INTO test_aggr_expr(h, r, v1, v2, v5, v6)" +
          "  VALUES(%d, %d, %d, %d, %f, %f);",
          h_min + row_idx - 1, r_min + row_idx - 1,
          row_idx + v1_min, row_idx + v2_min, row_idx + v5_min, row_idx + v6_min);
      session.execute(insert_stmt);

      v1_total += (row_idx + v1_min);
      v2_total += (row_idx + 100);
      v5_total += (row_idx + 77.77);
      v6_total += (row_idx + 999.99);
    }
    v1_max = row_idx - 1 + v1_min;
    v2_max = row_idx - 1 + v2_min;
    v5_max = row_idx - 1 + v5_min;
    v6_max = row_idx - 1 + v6_min;

    // Insert NULL values to test that COUNT() does not count row of NULL values.
    insert_stmt = String.format(
        "INSERT INTO test_aggr_expr(h, r, v1, v2, v5, v6)" +
        "  VALUES(%d, %d, NULL, NULL, NULL, NULL);",
        h_min + row_idx - 1, r_min + row_idx - 1);
    session.execute(insert_stmt);
    h_max = h_min + row_idx - 1;
  }

  @Test
  public void testBasic() throws Exception {
    LOG.info("TEST BASIC USAGE FOR AGGREGATE FUNCTIONS");
    setupAggrTest();

    //----------------------------------------------------------------------------------------------
    {
      // Test Basic - Not existing data.
      ResultSet rs = session.execute("SELECT count(*), max(h), min(r), sum(v6), avg(v1) " +
              "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 0);
      assertTrue(row.isNull(1));
      assertTrue(row.isNull(2));
      assertEquals(row.getDouble(3), 0, 0.1);
      assertEquals(row.getLong(4), 0, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test Basic - Query with completely-specified primary key.
      ResultSet rs = session.execute(String.format("SELECT count(*), max(h), min(r), sum(v6), " +
                      "avg(v1) " +
                      "  FROM test_aggr_expr WHERE h = %d AND r = %d;",
              h_min, r_max));
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 1);
      assertEquals(row.getInt(1), 1);
      assertEquals(row.getInt(2), r_max);
      assertEquals(row.getDouble(3), v6_min, 0.1);
      assertEquals(row.getLong(4), v1_min, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test Basic - Query with completely-specified hash key.
      ResultSet rs = session.execute("SELECT count(*), max(h), min(r), sum(v6), avg(v1) " +
              "  FROM test_aggr_expr WHERE h = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 2);
      assertEquals(row.getInt(1), h_min);
      assertEquals(row.getInt(2), r_min);
      assertEquals(row.getDouble(3), 2 * v6_min + 1, 0.1);
      assertEquals(row.getLong(4), (2 * v1_min + 1) / 2);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test Basic - All rows.
      ResultSet rs = session.execute("SELECT count(*), max(h), min(r), sum(v6), " +
                      "avg(v1), count(v1) FROM test_aggr_expr;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), total_inserted_row);
      assertEquals(row.getInt(1), h_max);
      assertEquals(row.getInt(2), 2);
      assertEquals(row.getDouble(3), v6_total, 0.1);
      assertEquals(row.getLong(4), v1_total / (total_inserted_row - 1));
      assertEquals(row.getLong(5), total_inserted_row - 1);
      assertEquals(row.getLong("count"), total_inserted_row);
      assertEquals(row.getInt("system.max(h)"), h_max);
      assertEquals(row.getInt("system.min(r)"), 2);
      assertEquals(row.getDouble("system.sum(v6)"), v6_total, 0.1);
      assertEquals(row.getLong("system.avg(v1)"), v1_total / (total_inserted_row - 1));
      assertEquals(row.getLong("system.count(v1)"), total_inserted_row - 1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test Basic - Query with completely-specified primary key, and specify column by name
      ResultSet rs = session.execute(String.format("SELECT h, r, v6, v1 " +
                      "  FROM test_aggr_expr WHERE h = %d AND r = %d;",
              h_min, r_max));
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getInt("h"), h_min);
      assertEquals(row.getInt("r"), r_max);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
  }

  @Test
  public void testCount() throws Exception {
    LOG.info("TEST AGGREGATE FUNCTION COUNT()");
    setupAggrTest();

    //----------------------------------------------------------------------------------------------
    {
      // Test COUNT() - Not existing data.
      ResultSet rs = session.execute("SELECT count(*), count(h), count(r), count(v1) " +
                                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 0);
      assertEquals(row.getLong(1), 0);
      assertEquals(row.getLong(2), 0);
      assertEquals(row.getLong(3), 0);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }

    {
      // Test COUNT() - Query with completely-specified primary key.
      ResultSet rs =
        session.execute(String.format("SELECT count(*), count(h), count(r), count(v1) " +
                                      "  FROM test_aggr_expr WHERE h = %d AND r = %d;",
                                      h_min, r_max));
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 1);
      assertEquals(row.getLong(1), 1);
      assertEquals(row.getLong(2), 1);
      assertEquals(row.getLong(3), 1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }

    {
      // Test COUNT() - Query with completely-specified hash key.
      ResultSet rs = session.execute("SELECT count(*), count(h), count(r), count(v1) " +
                                     "  FROM test_aggr_expr WHERE h = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 2);
      assertEquals(row.getLong(1), 2);
      assertEquals(row.getLong(2), 2);
      assertEquals(row.getLong(3), 2);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }

    {
      // Test COUNT() - All rows.
      ResultSet rs = session.execute("SELECT count(*), count(h), count(r), count(v1) " +
                                     "  FROM test_aggr_expr;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), total_inserted_row);
      assertEquals(row.getLong(1), total_inserted_row);
      assertEquals(row.getLong(2), total_inserted_row);
      // Not counting the NULL row of "v1" column.
      assertEquals(row.getLong(3), total_inserted_row - 1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
  }

  @Test
  public void testSum() throws Exception {
    LOG.info("TEST AGGREGATE FUNCTION SUM()");
    setupAggrTest();

    //----------------------------------------------------------------------------------------------
    {
      // Test SUM() - Not existing data.
      ResultSet rs = session.execute("SELECT sum(v1), sum(v2), sum(v5), sum(v6)" +
                                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 0);
      assertEquals(row.getInt(1), 0);
      assertEquals(row.getFloat(2), 0, 0.1);
      assertEquals(row.getDouble(3), 0, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test SUM() - Query with completely-specified primary key.
      ResultSet rs = session.execute(String.format("SELECT sum(v1), sum(v2), sum(v5), sum(v6)" +
                                                   "  FROM test_aggr_expr WHERE h = %d AND r = %d;",
                                                   h_min, r_max));
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_min);
      assertEquals(row.getInt(1), v2_min);
      assertEquals(row.getFloat(2), v5_min, 0.1);
      assertEquals(row.getDouble(3), v6_min, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test SUM() - Query with completely-specified hash key.
      ResultSet rs = session.execute("SELECT sum(v1), sum(v2), sum(v5), sum(v6)" +
                                     "  FROM test_aggr_expr WHERE h = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 2*v1_min + 1);
      assertEquals(row.getInt(1), 2*v2_min + 1);
      assertEquals(row.getFloat(2), 2*v5_min + 1, 0.1);
      assertEquals(row.getDouble(3), 2*v6_min + 1, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test SUM() - All rows.
      ResultSet rs = session.execute("SELECT sum(v1), sum(v2), sum(v5), sum(v6)" +
                                     "  FROM test_aggr_expr;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_total);
      assertEquals(row.getInt(1), v2_total);
      assertEquals(row.getFloat(2), v5_total, 0.1);
      assertEquals(row.getDouble(3), v6_total, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
  }

  @Test
  public void testAvg() throws Exception {
    LOG.info("TEST AGGREGATE FUNCTION AVG()");
    setupAggrTest();

    //----------------------------------------------------------------------------------------------
    {
      // Test AVG() - Not existing data.
      ResultSet rs = session.execute("SELECT avg(v1), avg(v2), avg(v5), avg(v6)" +
              "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), 0);
      assertEquals(row.getInt(1), 0);
      assertEquals(row.getFloat(2), 0, 0.1);
      assertEquals(row.getDouble(3), 0, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test AVG() - Query with completely-specified primary key.
      ResultSet rs = session.execute(String.format("SELECT avg(v1), avg(v2), avg(v5), avg(v6)" +
                      "  FROM test_aggr_expr WHERE h = %d AND r = %d;",
              h_min, r_max));
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_min);
      assertEquals(row.getInt(1), v2_min);
      assertEquals(row.getFloat(2), v5_min, 0.1);
      assertEquals(row.getDouble(3), v6_min, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test AVG() - Query with completely-specified hash key.
      ResultSet rs = session.execute("SELECT avg(v1) MyAvg, avg(v2), avg(v5), avg(v6)" +
              "  FROM test_aggr_expr WHERE h = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), (2*v1_min + 1) / 2);
      assertEquals(row.getInt(1), (2*v2_min + 1) / 2);
      assertEquals(row.getFloat(2), (2*v5_min + 1) / 2, 0.1);
      assertEquals(row.getDouble(3), (2*v6_min + 1) / 2, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test AVG() - All rows.
      ResultSet rs = session.execute("SELECT avg(v1), avg(v2), avg(v5), avg(v6)" +
              "  FROM test_aggr_expr;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_total / (total_inserted_row - 1));
      assertEquals(row.getInt(1), v2_total / (total_inserted_row - 1));
      assertEquals(row.getFloat(2), v5_total / (total_inserted_row - 1), 0.1);
      assertEquals(row.getDouble(3), v6_total / (total_inserted_row - 1), 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
  }

  @Test
  public void testMax() throws Exception {
    LOG.info("TEST AGGREGATE FUNCTION MAX()");
    setupAggrTest();

    //----------------------------------------------------------------------------------------------
    {
      // Test MAX() - Not existing data.
      ResultSet rs = session.execute("SELECT max(v1), max(v2), max(v5), max(v6)" +
                                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertTrue(row.isNull(0));
      assertTrue(row.isNull(1));
      assertTrue(row.isNull(2));
      assertTrue(row.isNull(3));
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test MAX() - Query with completely-specified primary key.
      ResultSet rs = session.execute(String.format("SELECT max(v1), max(v2), max(v5), max(v6)" +
                                                   "  FROM test_aggr_expr WHERE h = %d AND r = %d;",
                                                   h_min, r_max));
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_min);
      assertEquals(row.getInt(1), v2_min);
      assertEquals(row.getFloat(2), v5_min, 0.1);
      assertEquals(row.getDouble(3), v6_min, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test MAX() - Query with completely-specified hash key.
      ResultSet rs = session.execute("SELECT max(v1), max(v2), max(v5), max(v6)" +
                                     "  FROM test_aggr_expr WHERE h = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_min + 1);
      assertEquals(row.getInt(1), v2_min + 1);
      assertEquals(row.getFloat(2), v5_min + 1, 0.1);
      assertEquals(row.getDouble(3), v6_min + 1, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test MAX() - All rows.
      ResultSet rs = session.execute("SELECT max(v1), max(v2), max(v5), max(v6)" +
                                     "  FROM test_aggr_expr;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_max);
      assertEquals(row.getInt(1), v2_max);
      assertEquals(row.getFloat(2), v5_max, 0.1);
      assertEquals(row.getDouble(3), v6_max, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
  }

  @Test
  public void testMin() throws Exception {
    LOG.info("TEST AGGREGATE FUNCTION MIN()");
    setupAggrTest();

    //----------------------------------------------------------------------------------------------
    {
      // Test MIN() - Not existing data.
      ResultSet rs = session.execute("SELECT min(v1), min(v2), min(v5), min(v6)" +
                                     "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertTrue(row.isNull(0));
      assertTrue(row.isNull(1));
      assertTrue(row.isNull(2));
      assertTrue(row.isNull(3));
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test MIN() - Query with completely-specified primary key.
      ResultSet rs = session.execute(String.format("SELECT min(v1), min(v2), min(v5), min(v6)" +
                                                   "  FROM test_aggr_expr WHERE h = %d AND r = %d;",
                                                   h_min, r_max));
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_min);
      assertEquals(row.getInt(1), v2_min);
      assertEquals(row.getFloat(2), v5_min, 0.1);
      assertEquals(row.getDouble(3), v6_min, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test MIN() - Query with completely-specified hash key.
      ResultSet rs = session.execute("SELECT min(v1), min(v2), min(v5), min(v6)" +
                                     "  FROM test_aggr_expr WHERE h = 1;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_min);
      assertEquals(row.getInt(1), v2_min);
      assertEquals(row.getFloat(2), v5_min, 0.1);
      assertEquals(row.getDouble(3), v6_min, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
    {
      // Test MIN() - All rows.
      ResultSet rs = session.execute("SELECT min(v1), min(v2), min(v5), min(v6)" +
                                     "  FROM test_aggr_expr;");
      Iterator<Row> iter = rs.iterator();
      Row row = iter.next();
      assertEquals(row.getLong(0), v1_min);
      assertEquals(row.getInt(1), v2_min);
      assertEquals(row.getFloat(2), v5_min, 0.1);
      assertEquals(row.getDouble(3), v6_min, 0.1);
      // Expect only one row.
      assertFalse(iter.hasNext());
    }
  }

  private void execErroneousAggrCalls(String stmt) {
    try {
      ResultSet rs = session.execute(stmt);
      LOG.info("Execute statement: " + stmt);
      fail("Erroneous aggregate function call did not fail when executing " + stmt);
    } catch (InvalidQueryException e) {
      LOG.info("Caught expected exception: " + e.getMessage());
    } catch (QueryValidationException e) {
      LOG.info("Caught expected exception: " + e.getMessage());
    } catch (Exception e)  {
      fail("Caught unexpected exception: " + e.getMessage());
    }
  }

  @Test
  public void testError() throws Exception {
    LOG.info("TEST ERRONEOUS CASES FOR AGGREGATE FUNCTIONS");
    setupAggrTest();

    // Cannot pass an expression such as constant to AGGR.
    execErroneousAggrCalls("SELECT COUNT(2 + 2) " +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    execErroneousAggrCalls("SELECT MAX(h + 2) " +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    execErroneousAggrCalls("SELECT MIN(h + r) " +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    execErroneousAggrCalls("SELECT SUM(v1 - v2) " +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");

    // Cannot pass '*' to AGGR except COUNT.
    execErroneousAggrCalls("SELECT SUM(*) " +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    execErroneousAggrCalls("SELECT MAX(*) " +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    execErroneousAggrCalls("SELECT MIN(*) " +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");

    // AGGR calls cannot be nested in other expressions.
    execErroneousAggrCalls("SELECT COUNT(*) + MAX(v1)" +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");
    execErroneousAggrCalls("SELECT SUM(MIN(v1))" +
                           "  FROM test_aggr_expr WHERE h = 1 AND r = 1;");

    // AGGR calls cannot be used outside of SELECT.
    execErroneousAggrCalls("INSERT INTO test_aggr_expr(h, r, v1)" +
                           "  VALUES(COUNT(*), MAX(v2), MIN(v3));");

    execErroneousAggrCalls("DELETE FROM test_aggr_expr WHERE h = COUNT(*) AND r = 4;");

    execErroneousAggrCalls("UPDATE test_aggr_expr SET r = 4 WHERE h = MIN(v3);");
  }
}
