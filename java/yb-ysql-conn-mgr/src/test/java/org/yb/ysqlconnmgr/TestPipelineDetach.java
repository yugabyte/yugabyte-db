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
import static org.yb.AssertionWrappers.fail;
import static org.yb.ysqlconnmgr.PgWireProtocol.*;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBClusterBuilder;

/**
 * Tests for correct Odyssey behaviour when handling pipelined extended-query
 * sequences that contain intermediate Sync messages or omit a trailing Sync.
 *
 * Standard JDBC cannot produce these patterns (it always places a single Sync
 * at the end of a batch), so these tests speak the PostgreSQL v3 wire protocol
 * directly over a raw TCP socket using the helpers in {@link PgWireProtocol}.
 */
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
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
    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);
    LOG.info("Connecting raw socket to Odyssey at " + addr);

    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      // 1. Startup handshake
      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Startup complete, connection is ready");

      // 2. Build the pipeline:
      //    Parse("SELECT 1") + Bind + Execute + Sync + Bind + Execute
      //
      //    S1 is in the *middle* -- the second Bind+Execute follow it without
      //    their own Sync yet.
      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("SELECT 1"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      pipeline.write(buildSync());    // S1
      pipeline.write(buildBind());    // B2 (reuses unnamed statement from Parse)
      pipeline.write(buildExecute()); // E2

      // 3. Send the entire pipeline in one write
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: Parse + B1 + E1 + S1 + B2 + E2");

      // 4. Sleep to give Odyssey time to process the first Sync's RFQ
      //    and (if the bug is present) detach from the backend.
      LOG.info("Sleeping " + SLEEP_BEFORE_FINAL_SYNC_MS +
          "ms to allow premature detach if bug is present...");
      Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

      // 5. Send the final Sync (S2)
      out.write(buildSync());
      out.flush();
      LOG.info("Sent final Sync (S2)");

      // 6. Read ALL responses and assert the expected sequence
      //    Expected (correct behaviour):
      //      '1' ParseComplete
      //      '2' BindComplete   (B1)
      //      'D' DataRow        (E1)
      //      'C' CommandComplete (E1)
      //      'Z' ReadyForQuery  (S1)
      //      '2' BindComplete   (B2)
      //      'D' DataRow        (E2)
      //      'C' CommandComplete (E2)
      //      'Z' ReadyForQuery  (S2)
      char[] expectedTypes = {
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };

      List<PgMessage> responses = new ArrayList<>();
      try {
        for (int i = 0; i < expectedTypes.length; i++) {
          PgMessage msg = readMessage(in);
          LOG.info("Response[" + i + "]: " + msg);

          if (msg.type == BE_ERROR_RESPONSE) {
            String errText = new String(msg.body, StandardCharsets.UTF_8);
            fail("Received ErrorResponse at position " + i + ": " + errText);
          }

          responses.add(msg);
        }
      } catch (java.net.SocketTimeoutException e) {
        fail("Timed out after receiving only " + responses.size() +
            " of " + expectedTypes.length + " expected messages. " +
            "This indicates the bug: Odyssey detached prematurely on the " +
            "intermediate ReadyForQuery and lost subsequent responses.");
      }

      // 7. Assert message types in order
      assertEquals("Wrong number of responses", expectedTypes.length, responses.size());
      for (int i = 0; i < expectedTypes.length; i++) {
        assertEquals(
            "Message type mismatch at position " + i +
                " (expected '" + expectedTypes[i] + "', got '" + responses.get(i).type + "')",
            expectedTypes[i], responses.get(i).type);
      }

      LOG.info("All " + expectedTypes.length + " messages received in correct order");

      // 8. Clean up
      out.write(buildTerminate());
      out.flush();
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
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_max_conns_per_db", "1");
    tserverFlags.put("ysql_conn_mgr_enable_multi_route_pool", "false");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);
    String tableName = "test_no_commit_without_sync";

    // Phase 1: set up a clean table
    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);

      // DROP TABLE IF EXISTS
      out.write(buildParse("DROP TABLE IF EXISTS " + tableName));
      out.write(buildBind());
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      for (int i = 0; i < 4; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Drop response[" + i + "]: " + msg);
      }

      // CREATE TABLE
      out.write(buildParse("CREATE TABLE " + tableName + " (id int)"));
      out.write(buildBind());
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      for (int i = 0; i < 4; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Create response[" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error during table creation: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
      }

      out.write(buildTerminate());
      out.flush();
    }

    Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

    // Phase 2: send pipeline with missing final Sync, then close
    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Pipeline connection ready");

      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("SELECT 1"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      pipeline.write(buildParse("INSERT INTO " + tableName + " VALUES (42)"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      // Deliberately omitting the final Sync

      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P(SELECT 1)+B+E+Sync + P(INSERT)+B+E (no final Sync)");

      // Read responses for the first command through S1:
      //   ParseComplete, BindComplete, DataRow, CommandComplete, ReadyForQuery
      for (int i = 0; i < 5; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("S1 response[" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error in first command: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
      }

      // The responses for the second command (Parse+Bind+Execute) may not be
      // flushed by the backend without a Sync, so just wait for it to be processed.
      Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

      LOG.info("INSERT likely executed but no Sync sent; closing socket to trigger rollback");
      // try-with-resources closes the socket - no Terminate, no final Sync
    }

    // Phase 3: verify the INSERT was NOT committed
    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);

      out.write(buildParse("SELECT COUNT(*) FROM " + tableName + " WHERE id = 42"));
      out.write(buildBind());
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();

      // ParseComplete, BindComplete, DataRow, CommandComplete, ReadyForQuery
      int count = -1;
      for (int i = 0; i < 5; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Verify response[" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error during verification: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        if (msg.type == BE_DATA_ROW) {
          ByteBuffer bb = ByteBuffer.wrap(msg.body);
          short numCols = bb.getShort();
          assertEquals("Expected 1 column", 1, numCols);
          int colLen = bb.getInt();
          byte[] colData = new byte[colLen];
          bb.get(colData);
          count = Integer.parseInt(new String(colData, StandardCharsets.UTF_8));
        }
      }

      assertEquals(
          "INSERT should not have been committed without final Sync",
          0, count);
      LOG.info("Verified: INSERT was correctly NOT committed");

      // Clean up
      out.write(buildParse("DROP TABLE IF EXISTS " + tableName));
      out.write(buildBind());
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

      for (int i = 0; i < 4; i++) {
        readMessage(in);
      }

      out.write(buildTerminate());
      out.flush();
    }
  }
}
