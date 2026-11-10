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

  // Restarts the cluster with the seed and runs the queries as a new role, so this client is
  // alone on its route. Returns each query's backend as the order that backend was first seen.
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

  // Runs with seed 12345 twice and seed 67890 once. Both 12345 runs pick backends in the same
  // order. The 67890 run picks a different order.
  @Test
  public void testSameSeedSamePicks() throws Exception {
    List<Integer> first = pickSequence(12345);
    List<Integer> second = pickSequence(12345);
    List<Integer> other = pickSequence(67890);
    assertEquals(first, second);
    assertNotEquals(first, other);
  }
}
