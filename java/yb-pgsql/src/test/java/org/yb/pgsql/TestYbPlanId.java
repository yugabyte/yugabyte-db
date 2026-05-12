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

import java.sql.Statement;
import java.util.ArrayList;

import org.apache.commons.lang3.tuple.Triple;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;
import static org.yb.AssertionWrappers.*;
import static org.yb.pgsql.ExplainAnalyzeUtils.getExplainPlanId;

/**
 * Runs tests for plan id computation.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestYbPlanId extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestYbPlanId.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  /**
   * Test for plan id calculation.
   *
   * @throws Exception
   */
  @Test
  public void testYbPlanId1() throws Exception {
    ArrayList<Triple<String, String, Boolean>> arr = getQueries();

    Statement stmt = connection.createStatement();

    stmt.execute("create table t1(a1 int, b1 int, c1 int, d1 int, ch1 varchar, " +
                    "primary key(a1 asc))");
    stmt.execute("create index t1_b1_ch1_idx on t1(b1, ch1)");
    stmt.execute("create table t2(a2 int, b2 int, c2 int, d2 int, ch2 varchar, " +
                    "primary key(a2 asc))");
    stmt.execute("create table t3(a3 int, b3 int, c3 int, d3 int, ch3 varchar, " +
                    "primary key(a3 asc))");
    stmt.execute("CREATE OR REPLACE FUNCTION func1(OUT a integer, OUT b integer) " +
          "RETURNS SETOF record LANGUAGE sql IMMUTABLE PARALLEL SAFE STRICT " +
          "AS 'SELECT 1, 1'");

    long planId1;
    long planId2;
    for (int i = 0; i < arr.size(); ++i) {
      Triple<String, String, Boolean> testInstance = arr.get(i);

      LOG.info(testInstance.getLeft());
      LOG.info(testInstance.getMiddle());

      planId1 = getExplainPlanId(stmt, testInstance.getLeft());
      planId2 = getExplainPlanId(stmt, testInstance.getMiddle());

      assertTrue((planId1 == planId2) == testInstance.getRight());
    }

    planId1 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE ch1 = 'abc'");
    planId2 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE b1=5");
    long planId3 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT a1 FROM t1 WHERE b1=5");

    stmt.execute("ALTER TABLE t1 ALTER COLUMN ch1 TYPE text");

    long planId4 = getExplainPlanId(stmt,
                      "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE ch1 = 'abc'");
    long planId5 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE b1=5");
    long planId6 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT a1 FROM t1 WHERE b1=5");

    /*
     * Query references ch1 and its type has changed so plan id will change.
     */
    assertTrue(planId1 != planId4);

    /*
     * Query references ch1 (in SELECT) and its type has changed so the plan id will change.
     */
    assertTrue(planId2 != planId5);

    /*
     * Query does not reference ch1 (in SELECT) so plan id will not change.
     */
    assertTrue(planId3 == planId6);

    /*
     * Change the type back.
     */
    stmt.execute("ALTER TABLE t1 ALTER COLUMN ch1 TYPE varchar");

    planId4 = getExplainPlanId(stmt,
                      "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE ch1 = 'abc'");
    planId5 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE b1=5");
    planId6 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT a1 FROM t1 WHERE b1=5");

    /*
     * Should get the same plan ids.
     */
    assertTrue(planId1 == planId4);
    assertTrue(planId2 == planId5);
    assertTrue(planId3 == planId6);

    /*
     * Drop and create the table again.
     */
    stmt.execute("DROP TABLE t1");
    stmt.execute("CREATE TABLE t1(a1 int, b1 int, c1 int, d1 int, ch1 varchar, " +
                  "primary key(a1 asc))");
    stmt.execute("CREATE INDEX t1_b1_ch1_idx ON t1(b1, ch1)");

    planId4 = getExplainPlanId(stmt,
                      "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE ch1 = 'abc'");
    planId5 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT * FROM t1 WHERE b1=5");
    planId6 = getExplainPlanId(stmt, "/*+ IndexScan(t1) */ SELECT a1 FROM t1 WHERE b1=5");

    /*
     * Should get the same plan ids.
     */
    assertTrue(planId1 == planId4);
    assertTrue(planId2 == planId5);
    assertTrue(planId3 == planId6);
  }

  public ArrayList<Triple<String, String, Boolean>> getQueries() {
    ArrayList<Triple<String, String, Boolean>> arr
        = new ArrayList<>();
    arr.add(Triple.of("SELECT a1 FROM t1 WHERE a1<5",
          "SELECT a1 FROM t1 WHERE a1<6", true));
    arr.add(Triple.of("SELECT a1 FROM t1 WHERE a1<5",
          "SELECT b1 FROM t1 WHERE a1<6", false));
    arr.add(Triple.of("SELECT a1 FROM t1 WHERE a1 IN (1, 2, 3)",
          "SELECT a1 FROM t1 WHERE a1 IN (1, 2, 4)", true));
    arr.add(Triple.of("SELECT a1 FROM t1 WHERE a1 IN (1, 2, 3)",
          "SELECT a1 FROM t1 WHERE a1 IN (4, 5, 6, 7)", true));
    arr.add(Triple.of("SELECT SUM(a1 + 1) FROM t1",
          "SELECT SUM(a1 + b1) FROM t1", false));
    arr.add(Triple.of("SELECT SUM(a1 + 1) FROM t1",
          "SELECT SUM(a1 + 2) FROM t1", true));
    arr.add(Triple.of("SELECT SUM(a1 + 1) FROM t1 JOIN t2 ON a1=a2 WHERE a2<5",
          "SELECT SUM(a1 + 2) FROM t2 JOIN t1 ON a1=a2 WHERE a2<5", true));
    arr.add(Triple.of("SELECT SUM(a1 + 1) FROM t1 JOIN t2 ON a1=a2 WHERE a2<5",
          "SELECT SUM(a1 + 2) FROM t2 JOIN t1 ON a1=a2 WHERE a2<5", true));
    arr.add(Triple.of("SELECT SUM(a1 + 1) FROM t1 JOIN t2 ON a1=a2 WHERE a2<5",
          "SELECT SUM(a1 + 2) FROM t2 JOIN t1 ON a2=a1 WHERE a2<5", true));
    arr.add(Triple.of("SELECT SUM(a1 + 1) FROM t1 JOIN t2 ON a1=a2 WHERE a2<5",
          "SELECT SUM(a1 + 2) FROM t2, t1 WHERE a2=a1 AND a2<5", true));
    arr.add(Triple.of("SELECT 1 FROM pg_type t, " +
            "(SELECT func1() as x) AS ss WHERE t.oid = (ss.x).a",
          "SELECT 1 FROM (SELECT func1() as y) AS ss, " +
            "pg_type t WHERE (ss.y).a = t.oid", true));

    /*
     * Plan ids are sensitive to the order of expressions in the final target list.
     */
    arr.add(Triple.of("SELECT * FROM t1 x, t1 y, t1 z WHERE x.a1=y.a1 " +
                        "AND y.b1=z.b1 AND y.c1=1 AND z.c1=2",
                      "SELECT * FROM t1 z, t1 x, t1 y WHERE x.a1=y.a1 " +
                        "AND y.b1=z.b1 AND y.c1=1 AND z.c1=2", false));

    arr.add(Triple.of("SELECT z.*, x.*, y.* FROM t1 x, t1 y, t1 z WHERE x.a1=y.a1 " +
                        "AND y.b1=z.b1 AND y.c1=1 AND z.c1=2",
                      "SELECT z.*, x.*, y.* FROM t1 z, t1 x, t1 y WHERE x.a1=y.a1 " +
                        "AND y.b1=z.b1 AND y.c1=1 AND z.c1=2", true));
    return arr;
  }
}
