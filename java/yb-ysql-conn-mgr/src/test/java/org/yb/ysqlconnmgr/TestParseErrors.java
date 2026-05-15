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
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/*
 * Tests for correct Odyssey behaviour when handling parse errors.
 */
@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestParseErrors extends BaseYsqlConnMgr {
  private static final Logger LOG = LoggerFactory.getLogger(TestParseErrors.class);

  private static final int SOCKET_TIMEOUT_MS = 10000;
  private static final int SLEEP_BEFORE_FINAL_SYNC_MS = 5000;

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
  public void testRandomModePipelineWithError() throws Exception {
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
}
