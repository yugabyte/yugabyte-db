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

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.net.Socket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.ysqlconnmgr.PgWireProtocol.PgMessage;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestMessageSizeLimits extends BaseYsqlConnMgr {
  // Well below ysql_conn_mgr_auth_msg_timeout (15s), so a connection manager that waits for the
  // declared body instead of rejecting the header fails the test with a socket timeout.
  private static final int SOCKET_TIMEOUT_MS = 5000;
  private static final String PASSWORD = "yugabyte";

  private interface MessageReader {
    PgMessage read() throws Exception;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
  }

  @Override
  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder.withUser("yugabyte").withPassword(PASSWORD);
  }

  @Test
  public void testOversizedStartupPacket() throws Exception {
    try (Connection bystander = connectBystander();
         Socket socket = openSocket()) {
      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      out.write(ByteBuffer.allocate(8).putInt(0x27ffffff).putInt(196608).array());
      out.flush();
      DataInputStream in = new DataInputStream(socket.getInputStream());
      expectDropped(() -> PgWireProtocol.readMessage(in));
      assertAlive(bystander);
    }
  }

  @Test
  public void testOversizedMessageAfterAuth() throws Exception {
    try (Connection bystander = connectBystander()) {
      for (int len : new int[] {0x7fffffff, 0x80000000}) {
        try (WireConn c = rawConnBuilder().socketTimeoutMs(SOCKET_TIMEOUT_MS).connect()) {
          c.sendRaw(header('P', len, 1));
          expectDropped(c::readMessage);
        }
        assertAlive(bystander);
      }
    }
  }

  @Test
  public void testOversizedMessageBeforeAuth() throws Exception {
    forEachAuthMode(header('Q', 0x7fffffff, 1));
  }

  // Sends the probe in place of the client's password/SASL response, in both auth modes; the
  // mode is not runtime settable.
  private void forEachAuthMode(byte[] probe) throws Exception {
    markClusterNeedsRecreation();
    for (boolean useAuthBackend : new boolean[] {false, true}) {
      LOG.info("Running with ysql_conn_mgr_use_auth_backend={}", useAuthBackend);
      Map<String, String> flags = new HashMap<>();
      flags.put("ysql_enable_auth", "true");
      flags.put("ysql_conn_mgr_use_auth_backend", Boolean.toString(useAuthBackend));
      restartClusterWithAdditionalFlags(Collections.emptyMap(), flags);

      try (Connection bystander = connectBystander();
           Socket socket = openSocket()) {
        DataOutputStream out = new DataOutputStream(socket.getOutputStream());
        DataInputStream in = new DataInputStream(socket.getInputStream());
        out.write(PgWireProtocol.buildStartupMessage("yugabyte", "yugabyte", null));
        out.flush();
        PgMessage authRequest = PgWireProtocol.readMessage(in);
        assertEquals("Expected an authentication request, got " + authRequest,
            PgWireProtocol.BE_AUTHENTICATION, authRequest.type);
        out.write(probe);
        out.flush();
        expectDropped(() -> PgWireProtocol.readMessage(in));
        assertAlive(bystander);
      }
    }
  }

  private Connection connectBystander() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withPassword(PASSWORD)
        .connect();
  }

  private Socket openSocket() throws Exception {
    Socket socket = new Socket();
    socket.setSoTimeout(SOCKET_TIMEOUT_MS);
    socket.connect(miniCluster.getYsqlConnMgrContactPoints().get(0));
    return socket;
  }

  private static byte[] header(char type, int len, int bodyBytes) {
    ByteBuffer buf = ByteBuffer.allocate(5 + bodyBytes);
    buf.put((byte) type);
    buf.putInt(len);
    return buf.array();
  }

  private static void expectDropped(MessageReader reader) throws Exception {
    try {
      for (;;) {
        PgMessage msg = reader.read();
        LOG.info("Read {} before the connection was dropped", msg);
      }
    } catch (EOFException | SocketException expected) {
    } catch (SocketTimeoutException e) {
      fail("The connection manager kept the connection open instead of rejecting the header");
    }
  }

  // A crash of the connection manager drops every client connection on the node.
  private static void assertAlive(Connection bystander) throws Exception {
    try (Statement stmt = bystander.createStatement();
         ResultSet rs = stmt.executeQuery("SELECT 1")) {
      rs.next();
      assertEquals(1, rs.getInt(1));
    }
  }
}
