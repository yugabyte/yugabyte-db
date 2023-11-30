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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgAnalyze extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgAnalyze.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("ysql_beta_features", "true");
    flags.put("vmodule", "pgsql_operation=2");
    return flags;
  }

  public void ensureUniformRandomSampling() throws Exception {
    // Test ensures that ANALYZE collects a uniformly random sample from a table by
    // asserting that the that the statistics collected are not skewed.

    String ERROR_MESSAGE = "Since ANALYZE's sampling is probabilistic, there is a chance, although"
      + " very low, that this check might fail.";

    try (Statement stmt = connection.createStatement()) {
      LOG.info("Creating table t_small");
      stmt.execute(
          "CREATE TABLE t_small(h int, r1 int, r2 int, v1 int, v2 int, v3 text, " +
          "PRIMARY KEY (h, r1, r2)) SPLIT INTO 10 TABLETS;");
      for (int i = 0; i < 10; i++) {
        stmt.execute(
          "INSERT INTO t_small (h, r1, r2, v1, v2, v3) " +
          "    SELECT a, " +
          "           (b - 50) * 1000, " +
          "           (c + 20)," +
          "           a + b + c," +
          "           CASE " +
          "             WHEN ((a + b + c) % 7 = 0) THEN null" +
          "     WHEN ((a + b + c) % 7 = 1) THEN 77" +
          "             WHEN ((a + b + c) % 7 = 2) THEN 87" +
          "             WHEN ((a + b + c) % 7 = 3) THEN 97" +
          "             WHEN ((a + b + c) % 7 = 4) THEN 107" +
          "             WHEN ((a + b + c) % 7 = 5) THEN 117" +
          "             ELSE (a + b + c) % 70" +
          "           END," +
          "           repeat('xyz', c)" +
          "    FROM generate_series(1, 100) as b, " +
          "         generate_series(1, 5) as c," +
          String.format("         generate_series(%d, %d) as a;", i * 10 + 1, (i + 1) * 10));
      }
      stmt.execute("ANALYZE t_small");
      ResultSet rs = stmt.executeQuery(
          "SELECT attname, null_frac, avg_width, n_distinct FROM pg_stats " +
          "where tablename IN ('t_small') ORDER BY attname ASC;");
      assertTrue(rs.next());
      Row actual = Row.fromResultSet(rs);
      LOG.info("actual= " + actual);
      assertTrue(actual.elementEquals(0, "h"));
      assertEquals(actual.getFloat(1), new Float(0));
      assertEquals(actual.getInt(2), new Integer(4));
      assertEquals(ERROR_MESSAGE, actual.getFloat(3), new Float(100));

      assertTrue(rs.next());
      actual = Row.fromResultSet(rs);
      LOG.info("actual= " + actual);
      assertTrue(actual.elementEquals(0, "r1"));
      assertEquals(actual.getFloat(1), new Float(0));
      assertEquals(actual.getInt(2), new Integer(4));
      assertEquals(ERROR_MESSAGE, actual.getFloat(3), new Float(100));

      assertTrue(rs.next());
      actual = Row.fromResultSet(rs);
      LOG.info("actual= " + actual);
      assertTrue(actual.elementEquals(0, "r2"));
      assertEquals(actual.getFloat(1), new Float(0));
      assertEquals(actual.getInt(2), new Integer(4));
      assertEquals(ERROR_MESSAGE, actual.getFloat(3), new Float(5));

      assertTrue(rs.next());
      actual = Row.fromResultSet(rs);
      LOG.info("actual= " + actual);
      assertTrue(actual.elementEquals(0, "v1"));
      assertEquals(actual.getFloat(1), new Float(0));
      assertEquals(actual.getInt(2), new Integer(4));
      float num_distinct_v1 = 203;
      assertTrue(ERROR_MESSAGE, actual.getFloat(3) >= num_distinct_v1 * 0.98);

      assertTrue(rs.next());
      actual = Row.fromResultSet(rs);
      LOG.info("actual= " + actual);
      assertTrue(actual.elementEquals(0, "v2"));
      float null_fract_v2 = (float) 0.142;
      assertTrue(
        ERROR_MESSAGE,
        0.96 * null_fract_v2 <= actual.getFloat(1) && actual.getFloat(1) <= 1.04 * null_fract_v2);
      assertEquals(actual.getInt(2), new Integer(4));
      assertEquals(ERROR_MESSAGE, actual.getFloat(3), new Float(15));

      assertTrue(rs.next());
      actual = Row.fromResultSet(rs);
      LOG.info("actual= " + actual);
      assertTrue(actual.elementEquals(0, "v3"));
      assertEquals(actual.getFloat(1), new Float(0));
      // The row sizes range uniformly from 3 to 15.
      assertTrue(ERROR_MESSAGE, 8 <= actual.getInt(2) && actual.getInt(2) <= 10);
      assertEquals(ERROR_MESSAGE, actual.getFloat(3), new Float(5));
    }
  }

  @Test
  public void testUniformRandomSampling() throws Exception {
    ensureUniformRandomSampling();
  }

  @Test
  public void testUniformRandomSamplingWithPaging() throws Exception {
    // The random sampling on Tserver done as part of ANALYZE sends back a paging state to YSQL
    // in case the scan deadline is exceeded. YSQL then re-issues sampling on the same tablet where
    // the scan left off.
    //
    // This test is to exercise that code path.
    Map<String, String> extra_tserver_flags = new HashMap<String, String>();
    // yb_client_admin_operation_timeout_sec also applies to the PgPerformRequestPB used for
    // sampling.
    extra_tserver_flags.put("yb_client_admin_operation_timeout_sec", "100");
    // Since it isn't possible to specify a scan deadline of a few milliseconds,
    // ysql_scan_deadline_margin_ms is set to 99990 ms. This brings the effective deadline to 10 ms.
    extra_tserver_flags.put("ysql_scan_deadline_margin_ms", "99990");
    restartClusterWithFlags(Collections.emptyMap(), extra_tserver_flags);
    ensureUniformRandomSampling();
  }
}
