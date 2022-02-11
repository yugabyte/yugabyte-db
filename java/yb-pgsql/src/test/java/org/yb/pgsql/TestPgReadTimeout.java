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
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgReadTimeout extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgReadTimeout.class);
  private static final long kPgYbSessionTimeoutMs = 2000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Limit Read RPC timeout to reduce test running time
    flagMap.put("pg_yb_session_timeout_ms", Long.toString(kPgYbSessionTimeoutMs));
    return flagMap;
  }

  // SELECT count(*) in multinode cluster runs in parallel, that would require
  // more work to reach desired query timing, so make one-node cluster
  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumMasters() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  /**
   * Test if long aggregate query can run without triggering Read RPC timeout.
   * Aggregate queries may scan entire partition without returning any results.
   * If there are too many rows, scan may take too long, and connection may
   * timeout. To prevent that DocDB returns partial result when deadline is
   * approaching, and client sends another request to resume the scan.
   *
   * This test loads chuncks of data into a table, until "SELECT count(*)"
   * against this table takes at least twice as long as the timeout to make sure
   * that partial result is returned in time and scan is properly resumed.
   *
   * @throws Exception
   */
  @Test
  public void testReadTimeout() throws Exception {
    Statement statement = connection.createStatement();
    // readtimeouttest(h bigint, r float, vi int, vs text)
    createSimpleTable(statement, "readtimeouttest");
    String loadTemplate = "INSERT INTO readtimeouttest(h, r, vi, vs) " +
                          "SELECT s, s::float * 1.5, s %% 5, 'value ' || s::text " +
                          "FROM generate_series(%d, %d) s";
    String query = "SELECT count(*) FROM readtimeouttest";
    // prepare iteration
    int chunk_size = 10000;
    int rows_loaded = 0;
    while (true) {
      // generate chunk_size rows
      int from_val = rows_loaded + 1;
      int to_val = rows_loaded + chunk_size;
      statement.execute(String.format(loadTemplate, from_val, to_val));
      rows_loaded = to_val;
      LOG.info("Loaded " + rows_loaded + " rows");
      // count the rows
      final long startTimeMillis = System.currentTimeMillis();
      ResultSet rs = statement.executeQuery(query);
      long durationMillis = System.currentTimeMillis() - startTimeMillis;
      LOG.info("SELECT count(*) FROM readtimeouttest; took " + durationMillis + "ms");
      // if se;ect statement took long enough check result and exit,
      // otherwise generate more rows and try again
      if (durationMillis > 2 * kPgYbSessionTimeoutMs) {
        assertTrue(rs.next());
        assertEquals(rows_loaded, rs.getInt(1));
        break;
      }
    }
    LOG.info("Done with the test");
  }
}
