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
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;
import static org.yb.ysqlconnmgr.PgWireProtocol.*;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;

import org.apache.commons.lang3.StringUtils;
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

  private static PgMessage[] expectMessages(DataInputStream in, String what, char... expected)
      throws Exception {
    PgMessage[] msgs = new PgMessage[expected.length];
    for (int i = 0; i < expected.length; i++) {
      PgMessage msg = readMessage(in);
      LOG.info(what + " [" + i + "]: " + msg +
          (msg.type == BE_ERROR_RESPONSE
              ? " " + new String(msg.body, StandardCharsets.UTF_8) : ""));
      assertEquals(what + ": message type mismatch at position " + i,
          expected[i], msg.type);
      msgs[i] = msg;
    }
    assertEquals(what + ": unexpected trailing bytes after ReadyForQuery",
        0, in.available());
    return msgs;
  }

  private static int readPid(PgMessage dataRow) {
    assertEquals("Expected DataRow", BE_DATA_ROW, dataRow.type);
    ByteBuffer bb = ByteBuffer.wrap(dataRow.body);
    bb.getShort();
    int len = bb.getInt();
    byte[] col = new byte[len];
    bb.get(col);
    return Integer.parseInt(new String(col, StandardCharsets.UTF_8));
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
  // parsed, the connection manager transparently re-parses it via
  // PARSE_NO_PARSE_COMPLETE. The expected response sequence -- successes for
  // valid statements, errors for bad parses -- is verified exactly.
  //
  // Finally, all three prepared statements are executed on all three backends to
  // confirm that the connection manager's metadata was updated correctly and no
  // staleness was introduced by the mid-pipeline errors.
  @Test
  public void testRoundRobinModePipelineWithError() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "round_robin");
    tserverFlags.put("ysql_conn_mgr_enable_multi_route_pool", "true");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

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

     // Create S1, S2, and S3 on Backend1, Backend2, and Backend3 respectively.
      for (int i = 1;i <= 3; ++i) {
        ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
        pipeline.write(buildParse("S" + i, "SELECT " + i, new int[0]));
        pipeline.write(buildBind("S" + i, new String[0]));
        pipeline.write(buildExecute());
        pipeline.write(buildSync());
        out.write(pipeline.toByteArray());
        out.flush();
        LOG.info("Sent pipeline: P(S" + i + ")+B(S" + i + ")+E(S" + i + ")+Sync");
        for (int j = 0; j < 5; j++) {
          PgMessage msg = readMessage(in);
          LOG.info("response for S" + i + " [" + j + "]: " + msg);
          if (msg.type == BE_ERROR_RESPONSE) {
            fail("Error in S" + i + ": " +
                new String(msg.body, StandardCharsets.UTF_8));
          }
        }
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // Build Pipeline on Backend 1.
      // B(S1) + E + Sync + P(Error) + B + E + B(S2) + E + Sync +
      // B(S3) + E + P(Error) + B + E + Sync
      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildBind("S1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      pipeline.write(buildParse("random_synatax_error;"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      // S2 has already been parsed by the client.
      // On Backend 1, PARSE_NO_PARSE_COMPLETE will be sent by enqueuing S2.
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      // S3 has already been parsed by the client.
      // On Backend 1, PARSE_NO_PARSE_COMPLETE will be sent by enqueuing S3.
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("random_synatax_error;"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: B(S1)+E+Sync + P(Error)+B+E + "
          + "B(S2)+E+Sync + B(S3)+E+P(Error)+B+E+Sync");


      char[] expectedTypes = {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
          BE_ERROR_RESPONSE,
          BE_READY_FOR_QUERY,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_ERROR_RESPONSE,
          BE_READY_FOR_QUERY,
      };


      for (int j = 0; j < expectedTypes.length; j++) {
        PgMessage msg = readMessage(in);
        LOG.info("response on Backend 1 [" + j + "]: " + msg);
        assertEquals("Message type mismatch at position " + j +
            " (expected '" + expectedTypes[j] + "', got '" + msg.type + "')",
            expectedTypes[j], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // The pipeline below re-Parses S2. That is a no-op re-Parse only on a
      // backend that already has S2; elsewhere conn mgr sends a real Parse,
      // which the preceding error drops, unregistering S2 for the client
      // (TODO[#30543]). Deploy S2 everywhere first: round-robin sends each
      // Bind to the next backend.
      char[] bindExpectedTypes = {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 1; i <= 3; ++i) {
        out.write(buildBind("S2", new String[0]));
        out.write(buildExecute());
        out.write(buildSync());
        out.flush();
        LOG.info("Sent pipeline: B(S2)+E+Sync to deploy S2 on backend " + i);
        for (int j = 0; j < bindExpectedTypes.length; j++) {
          PgMessage msg = readMessage(in);
          LOG.info("response for S2 deploy on backend " + i + " [" + j + "]: " + msg);
          assertEquals("Message type mismatch at position " + j +
              " (expected '" + bindExpectedTypes[j] + "', got '" + msg.type + "')",
              bindExpectedTypes[j], msg.type);
        }
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // Build Pipeline on Backend 2.
      // B(S1) + E + Sync + P(Error) + B + E + P(S2) + B(S2) + E + Sync +
      // B(S3) + E + P(Error) + B + E + Sync
      pipeline.reset();
      // S1 has already been parsed by the client.
      // On Backend 2, PARSE_NO_PARSE_COMPLETE will be sent by enqueuing S1.
      pipeline.write(buildBind("S1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      pipeline.write(buildParse("random_synatax_error;"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      // Although no driver would re-parse already PARSED statement S2 and
      // pg is also expected to throw error, "prep stmt already exists".
      // But conn mgr today overrides this behavior and re-parses the statement
      // and sends NO_PARSE_PARSE_COMPLETE to backend which shouldn't enqueue.
      // In this case, SELECT 2 won't be executed on backend since error will
      // come before processing it. We want to test NO_PARSE_PARSE_COMPLETE
      // behaviour here.
      // TODO[#30543]: Requirement improvement on client hashmap handling.
      pipeline.write(buildParse("S2", "SELECT 2", new int[0]));
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      // S3 has already been parsed by the client.
      // On Backend 2, PARSE_NO_PARSE_COMPLETE will be sent by enqueuing S3.
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("random_synatax_error;"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: B(S1)+E+Sync + P(Error)+B+E + "
          + "P(S2)+B(S2)+E+Sync + B(S3)+E+P(Error)+B+E+Sync");


      for (int j = 0; j < expectedTypes.length; j++) {
        PgMessage msg = readMessage(in);
        LOG.info("response on Backend 1 [" + j + "]: " + msg);
        assertEquals("Message type mismatch at position " + j +
            " (expected '" + expectedTypes[j] + "', got '" + msg.type + "')",
            expectedTypes[j], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      expectedTypes = new char[] {
        BE_BIND_COMPLETE,
        BE_DATA_ROW,
        BE_COMMAND_COMPLETE,
        BE_READY_FOR_QUERY,
      };

      // The meta data of conn mgr should be updated correctly on each server
      // Test by executing all prep statements on each server.
      for (int i = 1; i <= 3; i++) {
        for (int j = 1; j <= 3; j++) {
          // Execute Prepare Stmt Si On Backend j.
          LOG.info("Executing Prepare Stmt S" + i + " On Backend " + j);
          out.write(buildBind("S" + i, new String[0]));
          out.write(buildExecute());
          out.write(buildSync());
          out.flush();
          LOG.info("Sent pipeline: B(S" + i + ")+E+Sync On Backend " + j);
          for (int k = 0; k < expectedTypes.length; k++) {
            PgMessage msg = readMessage(in);
            LOG.info("response on Backend " + i + " [" + k + "]: " + msg);
            assertEquals("Message type mismatch at position " + k +
                " (expected '" + expectedTypes[k] + "', got '" +
                msg.type + "')",
                expectedTypes[k], msg.type);
          }
        }
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // Clean up
      out.write(buildTerminate());
      out.flush();
    }
  }

  @Test
  public void testClientHashmapHandling() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_enable_multi_route_pool", "true");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

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

      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("S1", "SELECT 1", new int[0]));
      pipeline.write(buildBind("S1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P(S1)+B(S1)+E(S1)+Sync");
      char[] expectedTypes = {
        BE_PARSE_COMPLETE,
        BE_BIND_COMPLETE,
        BE_DATA_ROW,
        BE_COMMAND_COMPLETE,
        BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expectedTypes.length; ++i) {
        PgMessage response = readMessage(in);
        LOG.info("response for initial S1 [" + i + "]: " + response);
        assertEquals("Unexpected initial S1 response at position " + i,
            expectedTypes[i], response.type);
      }

      pipeline.reset();
      try (Connection conn = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
              .connect()) {
        try (PreparedStatement stmt = conn.prepareStatement("BEGIN")) {
          stmt.execute();
        }

        pipeline.write(buildParse("random_synatax_error;"));
        pipeline.write(buildBind());
        pipeline.write(buildExecute());
        pipeline.write(buildBind("S1", new String[0]));
        pipeline.write(buildExecute());
        pipeline.write(buildSync());
        out.write(pipeline.toByteArray());
        out.flush();
        LOG.info("Sent pipeline: P(Error)+B+E+B(S1)+E+Sync");

        // PostgreSQL reports the Parse error, ignores every subsequent extended
        // query message (including Bind(S1) and Execute), and resumes at Sync.
        expectedTypes = new char[] {
          BE_ERROR_RESPONSE,
          BE_READY_FOR_QUERY,
        };
        for (int i = 0; i < expectedTypes.length; ++i) {
          PgMessage response = readMessage(in);
          LOG.info("response for error pipeline [" + i + "]: " + response);
          assertEquals("Unexpected error-pipeline response at position " + i,
              expectedTypes[i], response.type);
        }

        pipeline.reset();
        pipeline.write(buildBind("S1", new String[0]));
        pipeline.write(buildExecute());
        pipeline.write(buildSync());
        out.write(pipeline.toByteArray());
        out.flush();
        LOG.info("Sent pipeline: B(S1)+E+Sync");
        expectedTypes = new char[] {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
        };
        for (int i = 0; i < expectedTypes.length; ++i) {
          PgMessage response = readMessage(in);
          LOG.info("response for final S1 [" + i + "]: " + response);
          assertEquals("Unexpected final S1 response at position " + i,
              expectedTypes[i], response.type);
        }
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
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);
    String tableName = "test_no_staleness_when_error_comes";

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
      while(true) {
        PgMessage msg = readMessage(in);
        LOG.info("Drop response[" + msg.type + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error during table drop: " +
              new String(msg.body, StandardCharsets.UTF_8));
        } else if (msg.type == BE_READY_FOR_QUERY) {
          break;
        }
      }

      // CREATE TABLE
      out.write(buildParse("CREATE TABLE " + tableName + " (id int)"));
      out.write(buildBind());
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      while(true) {
        PgMessage msg = readMessage(in);
        LOG.info("Create response[" + msg.type + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error during table creation: " +
              new String(msg.body, StandardCharsets.UTF_8));
        } else if (msg.type == BE_READY_FOR_QUERY) {
          break;
        }
      }

      out.write(buildTerminate());
      out.flush();
    }

    Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

    // Phase 2: send pipeline with an error, then verify recovery
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

      // P(S1) + B(S1) + E + P(S2) + B(S2) + E +
      // P(S_Wrong) + B(S_Wrong) + E + P(S3) + B(S3) + E + Sync

      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("S1", "INSERT INTO " +
            tableName + " VALUES (42)", new int[0]));
      pipeline.write(buildBind("S1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S2", "SELECT * from " + tableName, new int[0]));
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S_Wrong",
          "INSERT INTO " + tableName + " VALUES (42, 43)", new int[0]));
      pipeline.write(buildBind("S_Wrong", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S3",
          "SELECT COUNT(*) FROM " + tableName, new int[0]));
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());


      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P(S1)+B(S1)+E + P(S2)+B(S2)+E + "
          + "P(S_Wrong)+B(S_Wrong)+E + P(S3)+B(S3)+E + Sync");

      for (;;) {
        PgMessage msg = readMessage(in);
        LOG.info("response[" + msg.type + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          LOG.info("Expected error in S_Wrong: " +
              new String(msg.body, StandardCharsets.UTF_8));
          continue;
        }
        if (msg.type == BE_READY_FOR_QUERY)
          break;
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // Verify sending BIND S3 throws an error since client has not prepared it.
      // TODO[#30543]: Requirement improvement on client hashmap handling. This disconnects
      // the client and the sever connections. Whereas PG is just expected to throw error
      // "prepare statement does not exist".
      // pipeline.write(buildBind("S3", new String[0]));
      // pipeline.write(buildExecute());
      // pipeline.write(buildSync());
      // out.write(pipeline.toByteArray());
      // out.flush();
      // LOG.info("Sent pipeline: B(S3)+E(S3)+Sync");

      pipeline.reset();
      // P(S3) + B(S3) + E(S3) + Sync
      pipeline.write(buildParse("S3", "SELECT COUNT(*) FROM " + tableName, new int[0]));
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());

      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P(S3)+B(S3)+E(S3)+Sync");

      for (int i = 0; i < 5; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("response[" + msg.type + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error in third command: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // Add a column to the table.
      out.write(buildParse("ALTER TABLE " + tableName + " ADD COLUMN num_id int DEFAULT 42"));
      out.write(buildBind());
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      for (int i = 0; i < 4; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("ALTER response[" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error during ALTER TABLE: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // Verify S_Wrong works now.
      pipeline.reset();
      // P(S_Wrong) + B(S_Wrong) + E() + Sync
      pipeline.write(buildParse("S_Wrong",
          "INSERT INTO " + tableName + " VALUES (42, 43)", new int[0]));
      pipeline.write(buildBind("S_Wrong", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());

      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P(S_Wrong)+B(S_Wrong)+E+Sync");

      for (int i = 0; i < 4; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("response[" + msg.type + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error in third command: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // Clean up
      out.write(buildTerminate());
      out.flush();
    }
  }

  // Tests that conn mgr correctly handles Query, FunctionCall, and bare Sync
  // messages when they appear before extended query protocol messages in a
  // pipeline.
  //
  // These message types each implicitly act as sync points that generate
  // ReadyForQuery. Conn mgr must properly enqueue sync markers in its parse
  // queue for each, so that subsequent Parse+Bind+Execute+Sync messages are
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
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);

    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Startup complete, connection is ready");

      // ---- Pipeline 1: Query -> Parse -> Bind -> Execute -> Sync ----
      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildQuery("SELECT 1"));
      pipeline.write(buildParse("S2", "SELECT 2", new int[0]));
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: Q('SELECT 1') + P('SELECT 2') + B + E + Sync");

      char[] expectedTypes = {
          BE_ROW_DESCRIPTION,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };

      for (int i = 0; i < expectedTypes.length; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Query pipeline [" + i + "]: " + msg);
        assertEquals("Query pipeline mismatch at " + i,
            expectedTypes[i], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      pipeline.reset();
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P('SELECT 2') + B + E + Sync");

      expectedTypes = new char[] {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };

      for (int i = 0; i < expectedTypes.length; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Query pipeline [" + i + "]: " + msg);
        assertEquals("Query pipeline mismatch at " + i,
            expectedTypes[i], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // ---- Pipeline 2: FunctionCall(invalid) -> Parse -> Bind -> Execute -> Sync ----
      pipeline.reset();
      pipeline.write(buildFunctionCall(99999));
      pipeline.write(buildParse("S3", "SELECT 3", new int[0]));
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: F(99999) + P('SELECT 3') + B + E + Sync");

      expectedTypes = new char[] {
          BE_ERROR_RESPONSE,
          BE_READY_FOR_QUERY,
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };

      for (int i = 0; i < expectedTypes.length; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("FunctionCall pipeline [" + i + "]: " + msg);
        assertEquals("FunctionCall pipeline mismatch at " + i,
            expectedTypes[i], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      pipeline.reset();
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P('SELECT 3') + B + E + Sync");

      expectedTypes = new char[] {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };

      for (int i = 0; i < expectedTypes.length; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("FunctionCall pipeline [" + i + "]: " + msg);
        assertEquals("FunctionCall pipeline mismatch at " + i,
            expectedTypes[i], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      // ---- Pipeline 3: Sync -> Parse -> Bind -> Execute -> Sync ----
      pipeline.reset();
      pipeline.write(buildSync());
      pipeline.write(buildParse("S4", "SELECT 4", new int[0]));
      pipeline.write(buildBind("S4", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: Sync + P('SELECT 4') + B + E + Sync");

      expectedTypes = new char[] {
          BE_READY_FOR_QUERY,
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };

      for (int i = 0; i < expectedTypes.length; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Sync pipeline [" + i + "]: " + msg);
        assertEquals("Sync pipeline mismatch at " + i,
            expectedTypes[i], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      pipeline.reset();
      pipeline.write(buildBind("S4", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P('SELECT 4') + B + E + Sync");

      expectedTypes = new char[] {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };

      for (int i = 0; i < expectedTypes.length; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Sync pipeline [" + i + "]: " + msg);
        assertEquals("Sync pipeline mismatch at " + i,
            expectedTypes[i], msg.type);
      }
      // After ReadyForQuery, the input stream must be fully drained.
      assertEquals("Unexpected trailing bytes after ReadyForQuery",
          0, in.available());

      out.write(buildTerminate());
      out.flush();
    }
  }

  // Tests that the connection manager correctly updates it's state when
  // error occurs in pipeline. It specifically re-uses the same prepared statement
  // name after sync packet which get ignored due to an error, and verifies that the
  // connection manager correctly updates it's state and able to execute the statement.
  @Test
  public void testSyncAfterError() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);
    String tableName = "test_no_staleness_when_error_comes";

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

    // Phase 2: send pipeline with an error, then verify recovery
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
      pipeline.write(buildParse("S1", "INSERT INTO " +
            tableName + " VALUES (42)", new int[0]));
      pipeline.write(buildBind("S1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      pipeline.write(buildParse("S_Wrong",
          "INSERT INTO " + tableName + " VALUES (42, 43)", new int[0]));
      pipeline.write(buildBind("S_Wrong", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S2", "SELECT * from " + tableName, new int[0]));
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      pipeline.write(buildParse("S2", "SELECT * from " + tableName, new int[0]));
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());


      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P(S1)+B(S1)+E + P(S_Wrong)+B(S_Wrong)+E "
          + "P(S2)+B(S2)+E + SYNC + P(S2)+B(S2)+E + Sync");

      int count_rfq = 0;
      for (;;) {
        PgMessage msg = readMessage(in);
        LOG.info("response[" + msg.type + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          if (StringUtils.contains(new String(msg.body, StandardCharsets.UTF_8),
          "INSERT has more expressions than target columns" )) {
            LOG.info("Expected error in S_Wrong: " +
                new String(msg.body, StandardCharsets.UTF_8));
            continue;
          }
          fail("Unexpected error: " + new String(msg.body, StandardCharsets.UTF_8));
        }
        if (msg.type == BE_READY_FOR_QUERY)
          count_rfq++;
        if (count_rfq == 3)
          break;
      }

      // Clean up
      out.write(buildTerminate());
      out.flush();
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
  //                 Sync             <- the drain evicts using the client hashmap,
  //                                     so only hash(S3, Q2) goes away and
  //                                     hash(S3, Q1) stays in server hashmap
  // A later plain Bind of S3 -> Q1 landing on that same backend then finds the
  // leaked entry, is forwarded without a re-parse, and the backend rejects it.
  //
  // Round-robin allotment gives a deterministic three-backend rotation (one hop
  // per transaction), which is what lets the test come back to the backend that
  // saw the failed pipeline. Every hop asserts pg_backend_pid() so a routing
  // change cannot make the test pass vacuously.
  @Test
  public void testStaleServerStateAfterRebindInFailedPipeline() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "round_robin");
    tserverFlags.put("ysql_conn_mgr_enable_multi_route_pool", "true");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);
    LOG.info("Connecting raw socket to Odyssey at " + addr);

    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Startup complete, connection is ready");

      // Let the pool finish warming up to min_pool_size before relying on the
      // round-robin rotation.
      Thread.sleep(SLEEP_BEFORE_FINAL_SYNC_MS);

      // Learn the rotation: one transaction per backend, three distinct pids.
      int[] pids = new int[3];
      for (int i = 0; i < pids.length; i++) {
        out.write(buildParse("PROBE" + i, PID_QUERY, new int[0]));
        out.write(buildBind("PROBE" + i, new String[0]));
        out.write(buildExecute());
        out.write(buildSync());
        out.flush();
        PgMessage[] msgs = expectMessages(in, "probe " + i,
            BE_PARSE_COMPLETE, BE_BIND_COMPLETE, BE_DATA_ROW,
            BE_COMMAND_COMPLETE, BE_READY_FOR_QUERY);
        pids[i] = readPid(msgs[2]);
      }
      LOG.info("Round-robin rotation: " + Arrays.toString(pids));
      assertEquals("Expected three distinct backends in round-robin mode",
          3, new HashSet<>(Arrays.asList(pids[0], pids[1], pids[2])).size());

      // Backend pids[0]: a pipeline that fails and then re-binds S3 to a second
      // query. Everything after the syntax error is skipped by the backend.
      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("PROBE_ERR", PID_QUERY, new int[0]));
      pipeline.write(buildBind("PROBE_ERR", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S_BAD", "this is not valid sql", new int[0]));
      pipeline.write(buildBind("S_BAD", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S3", PID_QUERY, new int[0]));
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S3", PID_QUERY_V2, new int[0]));
      pipeline.write(buildBind("S3", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline: P(PROBE_ERR)+B+E + P(S_BAD)+B+E + "
          + "P(S3,Q1)+B+E + P(S3,Q2)+B+E + Sync");

      PgMessage[] msgs = expectMessages(in, "failing pipeline",
          BE_PARSE_COMPLETE, BE_BIND_COMPLETE, BE_DATA_ROW, BE_COMMAND_COMPLETE,
          BE_ERROR_RESPONSE, BE_READY_FOR_QUERY);
      assertEquals("Failing pipeline did not run on the first backend",
          pids[0], readPid(msgs[2]));
      String errText = new String(msgs[4].body, StandardCharsets.UTF_8);
      assertTrue("Expected a syntax error from S_BAD, got: " + errText,
          StringUtils.contains(errText, "syntax error"));

      // Backend pids[1]: re-parse S3 back to Q1, so the client hashmap once more
      // maps S3 -> Q1 -- the mapping whose server-side entry leaked on pids[0].
      out.write(buildParse("S3", PID_QUERY, new int[0]));
      out.write(buildBind("S3", new String[0]));
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      LOG.info("Sent pipeline: P(S3,Q1)+B(S3)+E+Sync");
      msgs = expectMessages(in, "re-parse of S3",
          BE_PARSE_COMPLETE, BE_BIND_COMPLETE, BE_DATA_ROW, BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY);
      assertEquals("Re-parse of S3 did not run on the second backend",
          pids[1], readPid(msgs[2]));

      // Backend pids[2]: plain Bind on a backend that never saw S3 -- the normal
      // redeploy path, and the hop that brings the rotation back to pids[0].
      out.write(buildBind("S3", new String[0]));
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      LOG.info("Sent pipeline: B(S3)+E+Sync on a backend without S3");
      msgs = expectMessages(in, "plain bind after redeploy",
          BE_BIND_COMPLETE, BE_DATA_ROW, BE_COMMAND_COMPLETE, BE_READY_FOR_QUERY);
      assertEquals("Plain bind did not run on the third backend",
          pids[2], readPid(msgs[1]));

      // Back on pids[0]: the plain Bind must still be redeployed. If conn mgr
      // kept hash(S3, Q1) from the skipped Parse it forwards the Bind as-is and
      // the backend answers with 26000.
      out.write(buildBind("S3", new String[0]));
      out.write(buildExecute());
      out.write(buildSync());
      out.flush();
      LOG.info("Sent pipeline: B(S3)+E+Sync on the backend that saw the failed pipeline");

      PgMessage first = readMessage(in);
      LOG.info("plain bind on poisoned backend [0]: " + first);
      if (first.type == BE_ERROR_RESPONSE) {
        fail("Stale server-side prepared statement left behind by the failed "
            + "pipeline: " + new String(first.body, StandardCharsets.UTF_8));
      }
      assertEquals("Expected BindComplete", BE_BIND_COMPLETE, first.type);
      msgs = expectMessages(in, "plain bind on poisoned backend",
          BE_DATA_ROW, BE_COMMAND_COMPLETE, BE_READY_FOR_QUERY);
      assertEquals("Final bind did not run on the backend that saw the failed pipeline",
          pids[0], readPid(msgs[0]));

      out.write(buildTerminate());
      out.flush();
    }
  }
}
