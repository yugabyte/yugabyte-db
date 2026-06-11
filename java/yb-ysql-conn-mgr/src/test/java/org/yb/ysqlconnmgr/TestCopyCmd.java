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
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBClusterBuilder;
import java.sql.ResultSet;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestCopyCmd extends BaseYsqlConnMgr {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestCopyCmd.class);

  /** Timeout for operations that must not deadlock. */
  private static final int TEST_TIMEOUT_MS = 10_000;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    Map<String, String> additionalTserverFlags = new HashMap<String, String>() {
      {
        put("ysql_conn_mgr_wait_for_rfq_on_sync", "true");
        put("ysql_conn_mgr_log_settings", "log_debug,log_query");
      }
    };
    builder.addCommonTServerFlags(additionalTserverFlags);
  }

  private void createCopyTable() throws Exception {
    try (Connection conn = getConnectionBuilder().connect();
         Statement st = conn.createStatement()) {
      st.execute("DROP TABLE IF EXISTS copytest");
      st.execute("CREATE TABLE copytest (a text, b int, c numeric(5,2))");
    }
  }

  /**
   * Reads backend messages until (and including) the first ReadyForQuery.
   * Everything before it is silently discarded.
   */
  private static void drainUntilRfq(DataInputStream in) throws IOException {
    for (;;) {
      if (readMessage(in).type == BE_READY_FOR_QUERY)
        return;
    }
  }

  // It's been tested using raw packets since JDBC explicitly uses
  // simple query protocol for COPY operations and it's easy to validate
  // the response received from the backend.

  /**
   * Verifies that executing COPY via the extended-query protocol
   * does not hangup client operations when
   * ysql_conn_mgr_wait_for_rfq_on_sync is enabled.
   *
   * Packet sequence:
   *   Client P("COPY copytest FROM STDIN") + B + E + S
   *   Server ParseComplete + BindComplete + CopyInResponse
   *   Client CopyFail("test-cancel") + S
   *   Server ErrorResponse + ReadyForQuery
   */
  @Test
  public void testCopyFromViaExtendedQuery() throws Exception {
    createCopyTable();

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);

    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(TEST_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in  = new DataInputStream(socket.getInputStream());

      // Startup handshake.
      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Startup complete");

      // Send P + B + E + S for COPY in one flush.
      // The Sync triggers OD_WAIT_SYNC, pausing the client relay.
      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("COPY copytest FROM STDIN"));
      pipeline.write(buildBind());
      // Backend has entered COPY mode after EXECUTE.
      pipeline.write(buildExecute());
      // It's important to send SYNC to receive any packet from backend.
      // Postgres wouldn't send RFQ for this sync packet as it goes into
      // COPY mode.
      pipeline.write(buildSync());
      // Only after SYNC, COPYINRESPONSE will be sent by backend.
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent P+B+E+S for COPY copytest FROM STDIN");

      // Read ParseComplete.
      PgMessage msg = readMessageSkipNotice(in);
      LOG.info("Received ParseComplete from backend: {}", msg.typeToString());
      assertEquals("Expected ParseComplete from backend",
                    BE_PARSE_COMPLETE, msg.type);

      // Read BindComplete.
      msg = readMessageSkipNotice(in);
      LOG.info("Received BindComplete from backend: {}", msg.typeToString());
      assertEquals("Expected BindComplete from backend",
                    BE_BIND_COMPLETE, msg.type);
      // Read CopyInResponse.
      // Server to client forwarding still works even when relay is paused, so
      // this message DOES arrive. The deadlock manifests only when we try to
      // send CopyFail back and wait for the resulting ErrorResponse.
      msg = readMessageSkipNotice(in);
      LOG.info("Received CopyInResponse from backend: {}", msg.typeToString());
      assertEquals(
          "Expected CopyInResponse",
          BE_COPY_IN_RESPONSE, msg.type);

      out.write(buildCopyFail("test-cancel"));
      out.write(buildSync());
      out.flush();

      // Read ErrorResponse (backend rejected COPY via CopyFail).
      msg = readMessageSkipNotice(in);
      LOG.info("Received ErrorResponse from backend: {}", msg.typeToString());
      assertEquals("Expected ErrorResponse after CopyFail",
                    BE_ERROR_RESPONSE, msg.type);

      // Read ReadyForQuery.
      msg = readMessageSkipNotice(in);
      LOG.info("Received ReadyForQuery from backend: {}", msg.typeToString());
      assertEquals("Expected ReadyForQuery after ErrorResponse",
                    BE_READY_FOR_QUERY, msg.type);

      out.write(buildTerminate());
      out.flush();
    }
  }

  /**
   * Verifies COPY TO STDOUT via extended query protocol works successfully
   * with connection manager by sending extra packets before and after
   * COPY TO STDOUT command.
   *
   * Client Packet sequence:
   *   Client P("SELECT * FROM copytest") + B + E
   *   Client P("COPY copytest TO STDOUT") + B + E
   *   Client P("SELECT 2") + B + E + SYNC.
   */
  @Test
  public void testCopyToViaExtendedQuery() throws Exception {
    createCopyTable();

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);

    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(TEST_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in  = new DataInputStream(socket.getInputStream());

      // Startup handshake.
      out.write(buildStartupMessage("yugabyte", "yugabyte"));
      out.flush();
      readUntilReady(in);
      LOG.info("Startup complete");

      // Insert rows so COPY TO returns actual data.
      out.write(buildQuery(
          "INSERT INTO copytest VALUES ('foo', 1, 1.23), ('bar', 2, 4.56)"));
      out.flush();
      for (int i = 0; i < 2; i++) {
        PgMessage msg = readMessage(in);
        LOG.info("Insert response[" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Error during insert: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
      }
      LOG.info("Inserted 2 test rows");

      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("S1", "SELECT * FROM copytest", new int[0]));
      pipeline.write(buildBind("S1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("COPY copytest TO STDOUT"));
      pipeline.write(buildBind());
      pipeline.write(buildExecute());
      pipeline.write(buildParse("S2", "SELECT 2", new int[0]));
      pipeline.write(buildBind("S2", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent packets to test COPY TO operation");

      char expectedTypes[] = {
        BE_PARSE_COMPLETE,
        BE_BIND_COMPLETE,
        BE_DATA_ROW,
        BE_DATA_ROW,
        BE_COMMAND_COMPLETE,
        BE_PARSE_COMPLETE,
        BE_BIND_COMPLETE,
        BE_COPY_OUT_RESPONSE,
        BE_COPY_DATA,
        BE_COPY_DATA,
        BE_COPY_DONE,
        BE_COMMAND_COMPLETE,
        BE_PARSE_COMPLETE,
        BE_BIND_COMPLETE,
        BE_DATA_ROW,
        BE_COMMAND_COMPLETE,
        BE_READY_FOR_QUERY,
      };

      for(int i = 0; i < expectedTypes.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("Received: {}", msg.typeToString());
        assertEquals("Expected " + expectedTypes[i], expectedTypes[i], msg.type);
      }

      out.write(buildTerminate());
      out.flush();
    }
  }
}
