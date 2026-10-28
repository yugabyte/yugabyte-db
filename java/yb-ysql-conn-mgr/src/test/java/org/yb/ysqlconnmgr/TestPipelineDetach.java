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
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import org.yb.minicluster.MiniYBClusterBuilder;

/**
 * Tests for correct Odyssey behaviour when handling pipelined extended-query
 * sequences that contain intermediate Sync messages or omit a trailing Sync.
 *
 * Standard JDBC cannot produce these patterns (it always places a single Sync
 * at the end of a batch), so these tests speak the PostgreSQL v3 wire protocol
 * directly over a raw TCP socket using the helpers in {@link PgWireProtocol}.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestPipelineDetach extends BaseYsqlConnMgr {
  private static final Logger LOG = LoggerFactory.getLogger(TestPipelineDetach.class);

  private static final int SOCKET_TIMEOUT_MS = 10000;
  private static final int SLEEP_BEFORE_FINAL_SYNC_MS = 5000;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> flags = new HashMap<String, String>() {
      {
        // Round robin mode is required since the client connecting to the same backend
        // (which is possible in random mode) will make the test pass
        put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "round_robin");
      }
    };
    builder.addCommonTServerFlags(flags);
  }

  private WireConn connect() throws Exception {
    return rawConnBuilder().socketTimeoutMs(SOCKET_TIMEOUT_MS).connect();
  }

  // Reproduces a bug where Odyssey prematurely detaches from a backend when an
  // intermediate Sync in a pipelined sequence triggers a ReadyForQuery whose
  // sync_reply matches sync_request, even though further Bind/Execute packets
  // have already been forwarded to the same backend.
  //
  // Pipeline: Parse + Bind + Execute + Sync(S1) + Bind + Execute ... sleep ...
  // + Sync(S2).  The sleep between the two Syncs gives Odyssey time to
  // (incorrectly) detach on S1's ReadyForQuery before S2 arrives.
  @Test
  public void testDontDetachWithPendingPipelinePackets() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .bind().execute().row("1")
       .pause(SLEEP_BEFORE_FINAL_SYNC_MS)
       .sync()
       .run();
    }
  }

  // Verifies that an INSERT executed via extended query protocol without a
  // final Sync is NOT committed when the socket is closed.
  //
  // Pipeline sent:
  //   Parse("SELECT 1") + Bind + Execute + Sync
  //   + Parse("INSERT ...") + Bind + Execute   (NO Sync)
  //   then close the socket.
  //
  // The first Sync commits the SELECT's implicit transaction. The INSERT
  // runs inside a new implicit transaction that is never committed (no Sync)
  // and must be rolled back when the connection drops.
  //
  // This is executed in three phases:
  //    Phase 1: Set up a new table
  //    Phase 2: Send pipeline with missing final Sync, then close the connection
  //    Phase 3: Verify the INSERT was NOT committed through a new connection
  @Test
  public void testNoCommitWithoutFinalSync() throws Exception {
    // Limit to a single backend and disable multi-route pooling (without which
    // the limit is not enforced) so that the Phase 3 connection waits for the
    // Phase 2 backend to become free or get killed. Otherwise the connection
    // manager may keep waiting to synchronize the stale backend (which will
    // never complete) and spawn a new one that trivially sees zero rows while
    // the original backend is still alive.
    restartClusterWithAdditionalFlags(Collections.emptyMap(), SINGLE_BACKEND_FLAGS);

    String tableName = "test_no_commit_without_sync";

    // Phase 1: set up a clean table
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP TABLE IF EXISTS " + tableName);
      stmt.execute("CREATE TABLE " + tableName + " (id int)");
    }

    Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

    // Phase 2: send pipeline with missing final Sync, then close
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .parse("INSERT INTO " + tableName + " VALUES (42)").bind().execute()
       // The responses for the second command (Parse+Bind+Execute) may not be
       // flushed by the backend without a Sync, so just wait for it to be processed.
       .pause(SLEEP_BEFORE_FINAL_SYNC_MS)
       .allowUnreadTail()
       .run();

      LOG.info("INSERT likely executed but no Sync sent; closing socket to trigger rollback");
      c.closeAbruptly();
    }

    // Phase 3: verify the INSERT was NOT committed
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT COUNT(*) FROM " + tableName + " WHERE id = 42")
       .bind().execute().row("0")
       .sync()
       .run();

      c.createPipeline()
       .parse("DROP TABLE IF EXISTS " + tableName).bind().execute().rowCount(0)
       .sync()
       .run();
    }
  }
}
