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
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.gson.JsonObject;
import java.io.ByteArrayOutputStream;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.TimeUnit;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

// When a logical client that is not attached to any server connection closes
// the connection, it sends a Terminate ('X') message. The connection manager
// used to treat that read as regular client activity: it attached a server
// connection to the client (running the RESET and deploy phase on it) only to
// drop the client right after. Closing many idle clients at once therefore
// caused a burst of active server connections.
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestNoAttachOnClientTerminate extends BaseYsqlConnMgr {
  private static final int NUM_CLIENTS = 20;
  private static final long DRAIN_TIMEOUT_MS = 30000;
  private static final long LOG_WAIT_SECS = 5;
  // Message type byte plus the int32 length.
  private static final int HEADER_LEN = 5;
  private static final String CLIENT_ATTACH_REGEX = "client \\S+ attached to \\S+";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings", "log_debug");
  }

  private int getLogicalConns() throws Exception {
    JsonObject pool = getPool("yugabyte", "yugabyte");
    if (pool == null) {
      return 0;
    }
    return pool.get("active_logical_connections").getAsInt()
        + pool.get("queued_logical_connections").getAsInt()
        + pool.get("waiting_logical_connections").getAsInt();
  }

  @Test
  public void testNoAttachOnTerminateFromDetachedClient() throws Exception {
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);

    List<Connection> conns = new ArrayList<>();
    for (int i = 0; i < NUM_CLIENTS; i++) {
      Connection conn = getConnectionBuilder()
          .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
          .connect();
      conns.add(conn);
      // Run a query so that the client gets attached to and then detached
      // from a server connection, as an idle client in a real workload is.
      try (Statement stmt = conn.createStatement();
           ResultSet rs = stmt.executeQuery("SELECT 1")) {
        assertTrue(rs.next());
        assertEquals(1, rs.getInt(1));
      }
    }

    TestUtils.waitFor(() -> getLogicalConns() == NUM_CLIENTS, DRAIN_TIMEOUT_MS);

    // Ignore the attaches done for the queries above.
    tailer.skipToEnd();

    // Connection.close() sends Terminate ('X') to the connection manager.
    for (Connection conn : conns) {
      conn.close();
    }

    TestUtils.waitFor(() -> getLogicalConns() == 0, DRAIN_TIMEOUT_MS);

    // Odyssey writes log lines asynchronously through its logger task, so an
    // attach done just before a client was freed may not have reached the
    // tailer yet. Wait a bounded time for it instead of checking once.
    String attach = tailer.waitForLogRegex(
        CLIENT_ATTACH_REGEX, LOG_WAIT_SECS, TimeUnit.SECONDS);
    assertNull("Server attached to a client that sent Terminate: " + attach, attach);
  }

  // The connection manager only relays complete messages, so a client can be
  // detached while its next message is only partly received. When the rest of
  // that message arrives, its first byte is message body, not a message type,
  // and must not be taken for a Terminate even if it is 'X'.
  @Test
  public void testPartialMessageStartingWithTerminateByte() throws Exception {
    byte[] q2 = PgWireProtocol.buildQuery("SELECT 'XYZ'");
    // Split Q2 inside its body, right before the 'X'.
    int split = HEADER_LEN;
    while (q2[split] != 'X') {
      split++;
    }

    try (WireConn conn = rawConnBuilder().connect()) {
      // Q1 and the start of Q2 in one write, so that the connection manager
      // has the partial Q2 buffered when Q1's ReadyForQuery detaches it.
      ByteArrayOutputStream first = new ByteArrayOutputStream();
      first.write(PgWireProtocol.buildQuery("SELECT 1"));
      first.write(q2, 0, split);
      conn.sendRaw(first.toByteArray());
      assertEquals(Collections.singletonList("1"), readRowsUntilReady(conn));

      conn.sendRaw(Arrays.copyOfRange(q2, split, q2.length));
      assertEquals(Collections.singletonList("XYZ"), readRowsUntilReady(conn));
    }
  }

  // Returns the first column of each DataRow up to the next ReadyForQuery.
  private static List<String> readRowsUntilReady(WireConn conn) throws Exception {
    List<String> rows = new ArrayList<>();
    for (;;) {
      PgWireProtocol.PgMessage msg = conn.readMessage();
      switch (msg.type) {
        case PgWireProtocol.BE_DATA_ROW:
          rows.add(PgWireProtocol.parseDataRow(msg.body).get(0));
          break;
        case PgWireProtocol.BE_ERROR_RESPONSE:
          fail("Unexpected ErrorResponse: " + PgWireProtocol.parseErrorResponse(msg.body));
          break;
        case PgWireProtocol.BE_READY_FOR_QUERY:
          return rows;
        default:
          break;
      }
    }
  }
}
