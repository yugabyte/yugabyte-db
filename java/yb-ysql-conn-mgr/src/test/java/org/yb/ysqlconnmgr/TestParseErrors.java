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

import static org.yb.AssertionWrappers.assertEquals;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.Statement;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/*
 * Tests for correct Odyssey behaviour when handling parse errors.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestParseErrors extends BaseYsqlConnMgr {
  private static final Logger LOG = LoggerFactory.getLogger(TestParseErrors.class);

  private static final int SOCKET_TIMEOUT_MS = 10000;
  private static final int SLEEP_BEFORE_FINAL_SYNC_MS = 5000;

  private static final String PID_QUERY = "SELECT pg_backend_pid()";
  private static final String PID_QUERY_V2 = "SELECT pg_backend_pid(), 2";

  private static final String SYNTAX_ERROR_SQL = "random_synatax_error;";

  private WireConn connect() throws Exception {
    return rawConnBuilder().socketTimeoutMs(SOCKET_TIMEOUT_MS).connect();
  }

  // Verifies that the connection manager correctly handles parse errors within
  // pipelined requests across multiple backends in round-robin mode,
  // and keeps its prepared-statement metadata consistent afterward.
  //
  // Setup: Three backends (round-robin routing). Named prepared statements S1,
  // S2, S3 are each initially created on a different backend.
  //
  // The test then sends pipelines that interleave valid bind/execute of named
  // statements with intentional syntax errors to Backend 1 and Backend 2. When
  // a named statement (e.g. S2) is used on a backend where it wasn't originally
  // parsed, the connection manager transparently re-parses it via a
  // redeploy. The expected response sequence -- successes for
  // valid statements, errors for bad parses -- is verified exactly.
  //
  // Finally, all three prepared statements are executed on all three backends to
  // confirm that the connection manager's metadata was updated correctly and no
  // staleness was introduced by the mid-pipeline errors.
  @Test
  public void testRoundRobinModePipelineWithError() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), ROUND_ROBIN_FLAGS);

    try (WireConn c = connect()) {
      // Create S1, S2, and S3 on Backend1, Backend2, and Backend3 respectively.
      for (int i = 1; i <= 3; ++i) {
        c.createPipeline()
         .parse("S" + i, "SELECT " + i).bind("S" + i).execute().rowCount(1)
         .sync()
         .run();
      }

      c.createPipeline()
       .bind("S1").execute().rowCount(1)
       .sync()
       .parse(SYNTAX_ERROR_SQL).expectError("syntax error")
       .bind().execute()
       // S2 has already been parsed by the client, so Backend 1 gets a
       // redeploy of it.
       .bind("S2").execute()
       .sync()
       // S3 has already been parsed by the client, so Backend 1 gets a
       // redeploy of it.
       .bind("S3").execute().rowCount(1)
       .parse(SYNTAX_ERROR_SQL).expectError("syntax error")
       .bind().execute()
       .sync()
       .run();

      c.createPipeline()
       // S1 has already been parsed by the client, so Backend 2 gets a
       // redeploy of it.
       .bind("S1").execute().rowCount(1)
       .sync()
       .parse(SYNTAX_ERROR_SQL).expectError("syntax error")
       .bind().execute()
       // Re-Parse of a name the client already registered overwrites it
       // rather than raising "prepared statement already exists". This one is
       // skipped by the preceding error, so its undo restores the old mapping
       // and S2 stays bindable.
       .parse("S2", "SELECT 2").bind("S2").execute()
       .sync()
       // S3 has already been parsed by the client, so Backend 2 gets a
       // redeploy of it.
       .bind("S3").execute().rowCount(1)
       .parse(SYNTAX_ERROR_SQL).expectError("syntax error")
       .bind().execute()
       .sync()
       .run();

      // The meta data of conn mgr should be updated correctly on each server
      // Test by executing all prep statements on each server.
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 3; j++) {
          LOG.info("Executing Prepare Stmt S" + i + " On Backend " + j);
          c.createPipeline()
           .bind("S" + i).execute().rowCount(1)
           .sync()
           .run();
        }
      }
    }
  }

  @Test
  public void testClientHashmapHandling() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>(NO_WARMUP_FLAGS);
    tserverFlags.put("ysql_conn_mgr_enable_multi_route_pool", "true");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("S1", "SELECT 1").bind("S1").execute().row("1")
       .sync()
       .run();

      try (Connection conn = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
              .connect()) {
        try (PreparedStatement stmt = conn.prepareStatement("BEGIN")) {
          stmt.execute();
        }

        // PostgreSQL reports the Parse error, ignores every subsequent extended
        // query message (including Bind(S1) and Execute), and resumes at Sync.
        c.createPipeline()
         .parse(SYNTAX_ERROR_SQL).expectError("syntax error")
         .bind().execute()
         .bind("S1").execute()
         .sync()
         .run();

        c.createPipeline()
         .bind("S1").execute().row("1")
         .sync()
         .run();
      }
    }
  }

  // Tests that the connection manager does not retain stale prepared-statement
  // metadata when a parse error occurs mid-pipeline.
  //
  // Setup: No warmup pools. A table with a single column is created in Phase 1.
  //
  // Phase 2 sends a pipeline containing four named statements in one batch:
  //   S1 (INSERT, valid) -> S2 (SELECT, valid) -> S_Wrong (INSERT with too many
  //   columns, fails) -> S3 (SELECT COUNT, skipped because of preceding error).
  // The test verifies that S_Wrong produces an error and that S3 was never
  // registered. It then re-parses and successfully executes S3, confirming the
  // connection manager cleaned up after the error. Finally, the table is ALTERed
  // to add a second column, and S_Wrong is re-parsed and executed successfully,
  // proving no stale metadata blocks a valid retry.
  @Test
  public void testNoStalenessWhenErrorComes() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), NO_WARMUP_FLAGS);

    String tableName = "test_no_staleness_when_error_comes";
    createTable(tableName);

    Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("S1", "INSERT INTO " + tableName + " VALUES (42)").bind("S1").execute()
       .parse("S2", "SELECT * from " + tableName).bind("S2").execute()
       .parse("S_Wrong", "INSERT INTO " + tableName + " VALUES (42, 43)")
           .expectError("INSERT has more expressions than target columns")
       .bind("S_Wrong").execute()
       .parse("S3", "SELECT COUNT(*) FROM " + tableName).bind("S3").execute()
       .sync()
       .run();

      // P(S3) was skipped after S_Wrong's error, so its undo unregistered S3.
      c.createPipeline()
       .bind("S3")
           .expectConnMgrError("26000", "prepared statement \"S3\" does not exist")
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("S3", "SELECT COUNT(*) FROM " + tableName).bind("S3").execute().rowCount(1)
       .sync()
       .run();

      c.createPipeline()
       .parse("ALTER TABLE " + tableName + " ADD COLUMN num_id int DEFAULT 42")
       .bind().execute().rowCount(0)
       .sync()
       .run();

      // Verify S_Wrong works now.
      c.createPipeline()
       .parse("S_Wrong", "INSERT INTO " + tableName + " VALUES (42, 43)")
       .bind("S_Wrong").execute().rowCount(0)
       .sync()
       .run();
    }
  }

  // Tests that conn mgr correctly handles Query, FunctionCall, and bare Sync
  // messages when they appear before extended query protocol messages in a
  // pipeline.
  //
  // These message types each implicitly act as sync points that generate
  // ReadyForQuery. Query enqueues a parse-queue record that YbQueryAck
  // consumes, FunctionCall enqueues nothing and only Sync enqueues a sync
  // marker, so that subsequent Parse+Bind+Execute+Sync messages are
  // matched to the correct pipeline boundary.
  //
  // The test sends three pipelines:
  //   1. Query('SELECT 1') -> Parse('SELECT 2') -> Bind -> Execute -> Sync
  //   2. FunctionCall(invalid_oid) -> Parse('SELECT 3') -> Bind -> Execute -> Sync
  //   3. Sync -> Parse('SELECT 4') -> Bind -> Execute -> Sync
  //
  // Each verifies the exact expected response sequence, confirming that the
  // extended query cycle after each sync-point message works correctly.
  @Test
  public void testQueryFunctionCallAndSyncInPipeline() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), NO_WARMUP_FLAGS);

    try (WireConn c = connect()) {
      c.createPipeline()
       .query("SELECT 1").row("1")
       .parse("S2", "SELECT 2").bind("S2").execute().row("2")
       .sync()
       .run();

      c.createPipeline()
       .bind("S2").execute().row("2")
       .sync()
       .run();

      c.createPipeline()
       .functionCall(99999).expectError("99999")
       .parse("S3", "SELECT 3").bind("S3").execute().row("3")
       .sync()
       .run();

      c.createPipeline()
       .bind("S3").execute().row("3")
       .sync()
       .run();

      c.createPipeline()
       .sync()
       .parse("S4", "SELECT 4").bind("S4").execute().row("4")
       .sync()
       .run();

      c.createPipeline()
       .bind("S4").execute().row("4")
       .sync()
       .run();
    }
  }

  // Tests that the connection manager correctly updates it's state when
  // error occurs in pipeline. It specifically re-uses the same prepared statement
  // name after sync packet which get ignored due to an error, and verifies that the
  // connection manager correctly updates it's state and able to execute the statement.
  @Test
  public void testSyncAfterError() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), NO_WARMUP_FLAGS);

    String tableName = "test_no_staleness_when_error_comes";
    createTable(tableName);

    Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("S1", "INSERT INTO " + tableName + " VALUES (42)").bind("S1").execute()
       .sync()
       .parse("S_Wrong", "INSERT INTO " + tableName + " VALUES (42, 43)")
           .expectError("INSERT has more expressions than target columns")
       .bind("S_Wrong").execute()
       .parse("S2", "SELECT * from " + tableName).bind("S2").execute()
       .sync()
       .parse("S2", "SELECT * from " + tableName).bind("S2").execute()
       .sync()
       .run();
    }
  }

  // Reproduces a bug where a pipeling failure causes eviction of wrong entry from
  // server hashmap. This happened because we were consulting client hashmap to figure
  // out query text, but that can be overwritten
  //
  // Shape of the bug (S3 -> Q1 is already in the client hashmap):
  //   ... error ... P(S3, Q1) B E    <- skipped by the backend, but conn mgr had
  //                                     already recorded hash(S3, Q1) as present
  //                                     on the server
  //                 P(S3, Q2) B E    <- also skipped; client hashmap now S3 -> Q2
  //                 Sync             <- the drain used to evict using the client
  //                                     hashmap, so only hash(S3, Q2) went away
  //                                     and hash(S3, Q1) stayed in server hashmap
  // A later plain Bind of S3 -> Q1 landing on that same backend then finds the
  // leaked entry, is forwarded without a re-parse, and the backend rejects it.
  //
  // Round-robin allotment gives a deterministic three-backend rotation (one hop
  // per transaction), which is what lets the test come back to the backend that
  // saw the failed pipeline. Every hop asserts pg_backend_pid() so a routing
  // change cannot make the test pass vacuously.
  @Test
  public void testStaleServerStateAfterRebindInFailedPipeline() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), ROUND_ROBIN_FLAGS);

    try (WireConn c = connect()) {
      // Let the pool finish warming up to min_pool_size before relying on the
      // round-robin rotation.
      Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

      // Learn the rotation: one transaction per backend, three distinct pids.
      int[] pids = new int[3];
      for (int i = 0; i < pids.length; i++) {
        pids[i] = c.createPipeline()
            .parse("PROBE" + i, PID_QUERY).bind("PROBE" + i).execute().label("pid")
            .sync()
            .run()
            .intValue("pid");
      }
      LOG.info("Round-robin rotation: " + Arrays.toString(pids));
      assertEquals("Expected three distinct backends in round-robin mode",
          3, new HashSet<>(Arrays.asList(pids[0], pids[1], pids[2])).size());

      // Backend pids[0]: a pipeline that fails and then re-binds S3 to a second
      // query. Everything after the syntax error is skipped by the backend.
      assertEquals("Failing pipeline did not run on the first backend", pids[0],
          c.createPipeline()
           .parse("PROBE_ERR", PID_QUERY).bind("PROBE_ERR").execute().label("pid")
           .parse("S_BAD", "this is not valid sql").expectError("syntax error")
           .bind("S_BAD").execute()
           .parse("S3", PID_QUERY).bind("S3").execute()
           .parse("S3", PID_QUERY_V2).bind("S3").execute()
           .sync()
           .run()
           .intValue("pid"));

      // Backend pids[1]: re-parse S3 back to Q1, so the client hashmap once more
      // maps S3 -> Q1 -- the mapping whose server-side entry leaked on pids[0].
      assertEquals("Re-parse of S3 did not run on the second backend", pids[1],
          c.createPipeline()
           .parse("S3", PID_QUERY).bind("S3").execute().label("pid")
           .sync()
           .run()
           .intValue("pid"));

      // Backend pids[2]: plain Bind on a backend that never saw S3 -- the normal
      // redeploy path, and the hop that brings the rotation back to pids[0].
      assertEquals("Plain bind did not run on the third backend", pids[2],
          c.createPipeline()
           .bind("S3").execute().label("pid")
           .sync()
           .run()
           .intValue("pid"));

      // Back on pids[0]: the plain Bind must still be redeployed. If conn mgr
      // kept hash(S3, Q1) from the skipped Parse it forwards the Bind as-is and
      // the backend answers with its own 26000.
      assertEquals("Final bind did not run on the backend that saw the failed pipeline",
          pids[0],
          c.createPipeline()
           .bind("S3").execute().label("pid")
           .sync()
           .run()
           .intValue("pid"));
    }
  }

  private void createTable(String tableName) throws Exception {
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS " + tableName);
      stmt.execute("CREATE TABLE " + tableName + " (id int)");
    }
  }
}
