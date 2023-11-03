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
import java.util.Map;
import java.util.regex.Pattern;

import com.yugabyte.util.PSQLException;
import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgReadTimeout extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgReadTimeout.class);
  private static final long kPgYbSessionTimeoutMs = 2000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Limit Read RPC timeout to reduce test running time
    flagMap.put("pg_yb_session_timeout_ms", Long.toString(kPgYbSessionTimeoutMs));
    // Verbose logging to help investigating if test fails
    flagMap.put("vmodule", "pgsql_operation=1");
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
   * Test if a long query can run without triggering Read RPC timeout.
   *
   * Regular scan reaches the per response row limit pretty soon, well before
   * the session timeout, but scans with aggregates return single row after
   * scan completion, so they may timeout on large tables. To prevent that
   * DocDB returns partial result when deadline is approaching, and client
   * sends another request to resume the scan.
   *
   * This test loads chunks of data into a table, until "SELECT count(*)"
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
      ResultSet rs;
      // generate chunk_size rows
      int from_val = rows_loaded + 1;
      int to_val = rows_loaded + chunk_size;
      statement.execute(String.format(loadTemplate, from_val, to_val));
      rows_loaded = to_val;
      LOG.info("Loaded " + rows_loaded + " rows");
      // count the rows
      final long startTimeMillis = System.currentTimeMillis();
      try {
        rs = statement.executeQuery(query);
      } catch (PSQLException ex) {
        if (Pattern.matches(".*RPC .* timed out after.*", ex.getMessage())) {
          throw new Exception("Please check GitHub issue #11477", ex);
        }
        throw ex;
      }
      long durationMillis = System.currentTimeMillis() - startTimeMillis;
      LOG.info("SELECT count(*) FROM readtimeouttest; took " + durationMillis + "ms");
      // if select statement took long enough check result and exit,
      // otherwise generate more rows and try again
      if (durationMillis > 2 * kPgYbSessionTimeoutMs) {
        assertTrue(rs.next());
        assertEquals(rows_loaded, rs.getInt(1));
        break;
      }
      // Adjust number of rows to load to achieve desired query duration next time.
      // Adding extra 20%, for random factors that may affect the duration.
      // If next time duration don't make it, we'll need another try.
      double coefficient = 2.4 * kPgYbSessionTimeoutMs / durationMillis - 1;
      // Do at least 10000 rows per load to avoid situation when we repeatedly
      // load few rows, but duration is a bit shy of double timeout.
      chunk_size = Math.max((int) (coefficient * rows_loaded), 10000);
    }
    LOG.info("Done with the test");
  }
}
