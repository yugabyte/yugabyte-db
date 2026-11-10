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

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunner.class)
public class TestRandomModeSeed extends BaseYsqlConnMgr {
  private static final int NUM_QUERIES = 30;
  private static final String TEST_USER = "seed_user";

  // Backend picks for one client as first-seen indexes, so runs with different pids compare.
  // The client runs as its own role so that it is the only client on its route: other clients
  // on a shared route (cluster start probes, role setup) would shift its route ordinal.
  private List<Integer> pickSequence(long seed) throws Exception {
    Map<String, String> flags = new HashMap<>();
    flags.put("TEST_ysql_conn_mgr_random_seed", Long.toUnsignedString(seed));
    restartClusterWithAdditionalFlags(Collections.emptyMap(), flags);

    try (Connection conn = getConnectionBuilder()
             .withTServer(TSERVER_IDX)
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE ROLE " + TEST_USER + " WITH LOGIN");
    }

    Map<Integer, Integer> pidToIndex = new HashMap<>();
    List<Integer> picks = new ArrayList<>();
    try (Connection conn = getConnectionBuilder()
             .withTServer(TSERVER_IDX)
             .withUser(TEST_USER)
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect();
         Statement stmt = conn.createStatement()) {
      for (int i = 0; i < NUM_QUERIES; i++) {
        try (ResultSet rs = stmt.executeQuery("SELECT pg_backend_pid()")) {
          rs.next();
          int pid = rs.getInt(1);
          picks.add(pidToIndex.computeIfAbsent(pid, p -> pidToIndex.size()));
        }
      }
    }
    LOG.info("Seed " + Long.toUnsignedString(seed) + " picks: " + picks);
    return picks;
  }

  @Test
  public void testSameSeedSamePicks() throws Exception {
    List<Integer> first = pickSequence(12345);
    List<Integer> second = pickSequence(12345);
    List<Integer> other = pickSequence(67890);
    assertEquals(first, second);
    assertNotEquals(first, other);
  }
}
