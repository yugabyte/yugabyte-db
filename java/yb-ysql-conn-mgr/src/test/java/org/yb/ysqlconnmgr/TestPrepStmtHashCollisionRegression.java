// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import java.sql.Connection;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import org.yb.minicluster.MiniYBClusterBuilder;

/**
 * Regression test for a 32-bit MurmurHash collision in Odyssey Connection
 * Manager's prepared-statement handling.
 *
 * Two TPC-C queries with statement names S_165461 and S_167793 produce the
 * same 32-bit MurmurHash (0xd95f1311). When the CM maps client statement
 * names to server-side names using this hash, the second Parse overwrites
 * the first on the backend. Re-executing the first statement then fails
 * because the parameter count no longer matches.
 *
 * This test fails on a 32-bit hash build and passes once the hash is
 * widened to 64 bits.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestPrepStmtHashCollisionRegression extends BaseYsqlConnMgr {

  private static final int SOCKET_TIMEOUT_MS = 30000;

  private static final int OID_INT4 = 23;

  private static final String STMT_UPDATE = "S_165461";
  private static final String UPDATE_QUERY =
      "UPDATE DISTRICT SET D_NEXT_O_ID = D_NEXT_O_ID + 1" +
      "  WHERE D_W_ID = $1    AND D_ID = $2" +
      "   RETURNING D_NEXT_O_ID, D_TAX";

  private static final String STMT_INSERT = "S_167793";
  private static final String INSERT_QUERY =
      "INSERT INTO NEW_ORDER (NO_O_ID, NO_D_ID, NO_W_ID)" +
      "  VALUES ( $1, $2, $3)";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> flags = new HashMap<String, String>() {
      {
        put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
      }
    };
    builder.addCommonTServerFlags(flags);
  }

  @Test
  public void testHashCollisionRegression() throws Exception {
    // Phase 1: Set up tables.
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS NEW_ORDER");
      stmt.execute("DROP TABLE IF EXISTS DISTRICT");
      stmt.execute("CREATE TABLE DISTRICT (D_W_ID INT, D_ID INT, D_NEXT_O_ID INT, D_TAX NUMERIC)");
      stmt.execute("CREATE TABLE NEW_ORDER (NO_O_ID INT, NO_D_ID INT, NO_W_ID INT)");
      stmt.execute("INSERT INTO DISTRICT VALUES (1, 1, 100, 0.05)");
    }

    // Phase 2: Execute the collision test using named statements.
    try (WireConn c = rawConnBuilder().socketTimeoutMs(SOCKET_TIMEOUT_MS).connect()) {
      // Step A: Parse + Bind + Execute the UPDATE (S_165461), 2 INT4 params
      c.createPipeline()
       .parse(STMT_UPDATE, UPDATE_QUERY, OID_INT4, OID_INT4)
       .bind(STMT_UPDATE, "1", "1").execute().row("101", "0.05")
       .sync()
       .run();

      // Step B: Parse + Bind + Execute the INSERT (S_167793), 3 INT4 params.
      // On 32-bit hash, the two server_keys hash to the same value (0xd95f1311)
      // so the CM may either overwrite the UPDATE plan or fail outright.
      c.createPipeline()
       .parse(STMT_INSERT, INSERT_QUERY, OID_INT4, OID_INT4, OID_INT4)
       .bind(STMT_INSERT, "101", "1", "1").execute().rowCount(0).tag("INSERT 0 1")
       .sync()
       .run();

      // Step C: Re-execute the UPDATE using Bind on S_165461 (no new Parse).
      // On 32-bit hash: the server has the INSERT plan under the hashed name,
      // so Bind with 2 params fails (parameter count mismatch).
      // On 64-bit hash: each statement has its own server-side name, UPDATE
      // plan is intact, returns d_next_o_id=102, d_tax=0.05.
      c.createPipeline()
       .bind(STMT_UPDATE, "1", "1").execute().row("102", "0.05")
       .sync()
       .run();
    }
  }
}
