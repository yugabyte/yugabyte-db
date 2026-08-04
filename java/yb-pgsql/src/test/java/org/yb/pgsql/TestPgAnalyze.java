// Copyright (c) YugabyteDB, Inc.
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

      assertNoHistogram(stmt, "t_small", "h");
      assertNoHistogram(stmt, "t_small", "r1");
      assertNoHistogram(stmt, "t_small", "r2");
      assertAccurateHistogram(stmt, "t_small", "v1");
      assertNoHistogram(stmt, "t_small", "v2");
    }
  }

  private void assertNoHistogram(Statement stmt, String tablename, String attname)
      throws Exception {
    // Check that the histogram bounds are not set for the given attribute.
    // These cases can happen where all the distinct values are fit inside the
    // MCV (most common values) structures.
    String query_string = "SELECT histogram_bounds FROM pg_stats WHERE tablename = '%s'" +
    " AND attname = '%s'";
    ResultSet rs = stmt.executeQuery(String.format(query_string, tablename,
        attname));
    assertTrue(rs.next());
    assertNull(rs.getString(1));
  }

  private void assertAccurateHistogram(Statement stmt, String tablename, String attname)
      throws Exception {
    // Checks the histogram's accuracy by comparing each bucket width with the
    // "ideal" bucket width of (number of distinct histogram elements) / (number of buckets).
    // Take the perfect bucket width as P. We compute the maximum relative
    // error of a histogram as max(b_j - P) / P over all bucket widths b_j.
    // PG models its histogram error from this error metric based on the paper
    // "Random sampling for histogram construction: how much is enough?"
    // by Surajit Chaudhuri, Rajeev Motwani and Vivek Narasayya.
    // Unfortunately that error metric assumes that the analyzed column only
    // consists of distinct values. In order to account for that, we take
    // maximum multiplicity of the histogram bounds into account and loosen
    // the error bounds accordingly.
    // If the analyzed column is distinct the relative error according to PG should be below 0.5.

    String multiplicity_format =
      "select count(*) from (select unnest(histogram_bounds::text::int[]) as m " +
      "from pg_stats where tablename='%s' and attname='%s') p join " +
      "%s on %s = p.m group by p.m order by 1 desc limit 1;";
    String multiplicity_query =
      String.format(multiplicity_format, tablename, attname, tablename, attname);

    ResultSet rs = stmt.executeQuery(multiplicity_query);

    // Fail if histograms don't exist.
    assertTrue(rs.next());
    double max_multiplicity = rs.getDouble(1);

    String error_calc_format =
      "/*+Set(random_page_cost 1e42)*/WITH bucket_info(w, c) AS (" +
        "select width_bucket(%s, p.bounds_array) as w, count(%s) from %s, " +
            "(select histogram_bounds::text::int[] as bounds_array, " +
              "most_common_vals::text::int[] as m " +
              "from pg_stats where tablename='%s' and attname='%s') p " +
            "where %s is not null and %s not in (select * from unnest(m)) and " +
            "width_bucket(%s, p.bounds_array) between 1 and 100 group by w)" +
        " SELECT q.*, p.perfect_size from (select avg(c) as perfect_size from bucket_info) p " +
        "left join lateral (select max(abs(c - perfect_size))/perfect_size from bucket_info) q " +
        "on true;";
    String error_calc_query =
      String.format(error_calc_format, attname, attname, tablename, tablename, attname, attname,
                    attname, attname);
    rs = stmt.executeQuery(error_calc_query);

    assertTrue(rs.next());
    double perfect_size = rs.getDouble(2);

    assertLessThan(rs.getDouble(1), 0.5 + (max_multiplicity - 1) / perfect_size);
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

  @Test
  public void testSimpleOrderedUniqueColumn() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE sample(r int, primary key(r asc))");
      stmt.execute("INSERT INTO sample SELECT generate_series(1,100000)");
      stmt.execute("ANALYZE sample");

      assertAccurateHistogram(stmt, "sample", "r");
    }
  }
}
