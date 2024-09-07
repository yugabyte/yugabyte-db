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

import static org.yb.AssertionWrappers.*;

import java.sql.SQLException;
import java.sql.Statement;

import org.junit.*;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.Connection;

import java.util.Arrays;

import org.json.JSONArray;
import org.json.JSONObject;

@RunWith(value = YBTestRunner.class)
public class TestPgConstraintCache extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgConstraintCache.class);

  /*
   * Verify that the PG C function remove_useless_groupby_columns is working as
   * expected.
   * When we're grouping by multiple columns, and one of them is the primary key,
   * all columns except the primary key should be removed from the group by
   * clause.
   */
  @Test
  public void groupByTest() throws Exception {
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.ROUND_ROBIN);
    try (Statement stmt = connection.createStatement()) {
      // Set up the table and data
      stmt.execute("CREATE TABLE t(a INT PRIMARY KEY, b INT, c INT);");
      stmt.execute("INSERT INTO t(a, b, c) SELECT i, i, i from generate_series(1, 1000) AS i;");

      String query = "EXPLAIN (ANALYZE, DIST, FORMAT JSON) " +
          "SELECT COUNT(*) FROM t GROUP BY a, b, c;";

      // Check the explain output to ensure we're only grouping by a and not a,b
      ResultSet rs = stmt.executeQuery(query);
      while (rs.next()) {
        String jsonOutput = rs.getString(1);
        LOG.info("First run EXPLAIN output: {}", jsonOutput);

        JSONObject planObject = new JSONArray(jsonOutput)
            .getJSONObject(0)
            .getJSONObject("Plan");
        JSONArray groupKeyArray = planObject.getJSONArray("Group Key");

        assertEquals(1, groupKeyArray.length());
        assertEquals("a", groupKeyArray.getString(0));
      }

      // Run the query a few times to warm up the caches of all backends when
      // Connection Manager is in round-robin warmup mode
      if (isConnMgrWarmupRoundRobinMode()) {
        for (int i = 0; i < CONN_MGR_WARMUP_BACKEND_COUNT; i++) {
          stmt.execute(query);
        }
      }

      // Run the query again and check that we're not doing catalog read requests
      rs = stmt.executeQuery(query);
      while (rs.next()) {
        String jsonOutput = rs.getString(1);
        LOG.info("Second run EXPLAIN output: {}", jsonOutput);

        JSONObject jsonObject = new JSONArray(jsonOutput).getJSONObject(0);
        int catalogReadRequests = jsonObject.getInt("Catalog Read Requests");

        assertEquals("The second run of the query should not be doing catalog read requests.",
            0, catalogReadRequests);
      }

      // Drop the primary key and check that we're grouping by a, b, c
      stmt.execute("ALTER TABLE t DROP CONSTRAINT t_pkey;");

      rs = stmt.executeQuery(query);
      while (rs.next()) {
        String jsonOutput = rs.getString(1);
        LOG.info("Third run EXPLAIN output after dropping primary key: {}", jsonOutput);

        JSONObject planObject = new JSONArray(jsonOutput)
            .getJSONObject(0)
            .getJSONObject("Plan");
        JSONArray groupKeyArray = planObject.getJSONArray("Group Key");

        assertEquals(3, groupKeyArray.length());
        assertTrue(groupKeyArray.toList().containsAll(Arrays.asList("a", "b", "c")));
      }
    } catch (SQLException e) {
      e.printStackTrace();
    }
  }
}
