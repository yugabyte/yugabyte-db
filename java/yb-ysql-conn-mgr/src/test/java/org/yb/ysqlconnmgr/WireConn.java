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

import static org.yb.AssertionWrappers.fail;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBCluster;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.ysqlconnmgr.PgWireProtocol.NegotiateProtocolVersion;
import org.yb.ysqlconnmgr.PgWireProtocol.PgMessage;

public class WireConn implements AutoCloseable {
  private static final Logger LOG = LoggerFactory.getLogger(WireConn.class);

  private static final int DEFAULT_SOCKET_TIMEOUT_MS = 30000;
  private static final String USER = "yugabyte";
  private static final String DATABASE = "yugabyte";

  private final Socket socket;
  private final DataOutputStream out;
  private final DataInputStream in;
  private boolean tailUnread;
  private List<PgMessage> startupMessages = new ArrayList<>();

  private WireConn(Socket socket) throws IOException {
    this.socket = socket;
    this.out = new DataOutputStream(socket.getOutputStream());
    this.in = new DataInputStream(socket.getInputStream());
  }

  public static Builder builder(MiniYBCluster cluster) {
    return new Builder(cluster);
  }

  public Pipeline createPipeline() {
    if (tailUnread) {
      throw new IllegalStateException(
          "The previous pipeline declared allowUnreadTail(), so unread responses are still queued"
          + " on this connection and would be misread as the new pipeline's");
    }
    return new Pipeline(this);
  }

  public void sendRaw(byte[] bytes) throws IOException {
    out.write(bytes);
    out.flush();
  }

  public PgMessage readMessage() throws IOException {
    return PgWireProtocol.readMessage(in);
  }

  // The messages the server sent before the first ReadyForQuery, in order.
  public List<PgMessage> startupMessages() {
    return startupMessages;
  }

  // The NegotiateProtocolVersion the server sent during startup, or null if it sent none.
  public NegotiateProtocolVersion negotiateProtocolVersion() throws IOException {
    for (PgMessage msg : startupMessages) {
      if (msg.type == PgWireProtocol.BE_NEGOTIATE_PROTOCOL_VERSION) {
        return NegotiateProtocolVersion.parse(msg.body);
      }
    }
    return null;
  }

  // Values the server reported for `name` in ParameterStatus during startup, in order.
  public List<String> startupParameterValues(String name) {
    List<String> values = new ArrayList<>();
    for (PgMessage msg : startupMessages) {
      if (msg.type != PgWireProtocol.BE_PARAMETER_STATUS) {
        continue;
      }
      String body = new String(msg.body, StandardCharsets.UTF_8);
      int sep = body.indexOf('\0');
      if (body.substring(0, sep).equals(name))
        values.add(body.substring(sep + 1, body.length() - 1));
    }
    return values;
  }

  public int available() throws IOException {
    return in.available();
  }

  public void closeAbruptly() {
    try {
      socket.close();
    } catch (IOException e) {
      LOG.info("Ignoring error while dropping the connection", e);
    }
  }

  @Override
  public void close() {
    try {
      if (!socket.isClosed()) {
        out.write(PgWireProtocol.buildTerminate());
        out.flush();
      }
    } catch (IOException e) {
      LOG.info("Ignoring error while sending Terminate", e);
    }
    closeAbruptly();
  }

  void markTailUnread() {
    tailUnread = true;
  }

  public static class Builder {
    private final MiniYBCluster cluster;
    private final Map<String, String> startupParams = new LinkedHashMap<>();
    private int socketTimeoutMs = DEFAULT_SOCKET_TIMEOUT_MS;
    private ConnectionEndpoint endpoint = ConnectionEndpoint.YSQL_CONN_MGR;
    private int protocolVersion = PgWireProtocol.protocolVersion(3, 0);
    private boolean gssEncRequestFirst = false;
    private boolean tls = false;

    Builder(MiniYBCluster cluster) {
      this.cluster = cluster;
    }

    // The protocol version the StartupMessage asks for; 3.0 by default.
    public Builder protocolVersion(int protocolVersion) {
      this.protocolVersion = protocolVersion;
      return this;
    }

    // Sends a GSSENCRequest first and expects the server to decline it.
    public Builder gssEncRequestFirst() {
      this.gssEncRequestFirst = true;
      return this;
    }

    // Sends an SSLRequest and runs the TLS handshake before startup. The server certificate is
    // not verified.
    public Builder tls() {
      this.tls = true;
      return this;
    }

    // Bypasses the connection manager when set to POSTGRES.
    public Builder endpoint(ConnectionEndpoint endpoint) {
      this.endpoint = endpoint;
      return this;
    }

    public Builder startupParam(String name, String value) {
      startupParams.put(name, value);
      return this;
    }

    public Builder socketTimeoutMs(int socketTimeoutMs) {
      this.socketTimeoutMs = socketTimeoutMs;
      return this;
    }

    public WireConn connect() throws Exception {
      WireConn conn = openAndSendStartup();
      try {
        conn.startupMessages = PgWireProtocol.readUntilReady(conn.in);
      } catch (Exception e) {
        conn.closeAbruptly();
        throw e;
      }
      LOG.info("Raw wire connection is ready");
      return conn;
    }

    public ErrorResponse connectExpectingError() throws Exception {
      WireConn conn = openAndSendStartup();
      try {
        PgMessage msg = conn.readMessage();
        if (msg.type != PgWireProtocol.BE_ERROR_RESPONSE) {
          fail("Expected startup to be rejected with an ErrorResponse, got " + msg);
        }
        return ErrorResponse.parse(msg.body);
      } finally {
        conn.closeAbruptly();
      }
    }

    private WireConn openAndSendStartup() throws Exception {
      InetSocketAddress target = endpoint == ConnectionEndpoint.POSTGRES
          ? cluster.getPostgresContactPoints().get(0)
          : cluster.getYsqlConnMgrContactPoints().get(0);
      LOG.info("Connecting raw socket to {} as {}/{}", target, USER, DATABASE);
      Socket socket = new Socket();
      try {
        socket.setTcpNoDelay(true);
        socket.setSoTimeout(socketTimeoutMs);
        socket.connect(target);
        if (gssEncRequestFirst) {
          sendRequestCode(socket, PgWireProtocol.GSS_ENC_REQUEST_CODE, 'N');
        }
        if (tls) {
          sendRequestCode(socket, PgWireProtocol.SSL_REQUEST_CODE, 'S');
          socket = startTls(socket, target);
        }
        WireConn conn = new WireConn(socket);
        conn.sendRaw(PgWireProtocol.buildStartupMessage(
            protocolVersion, USER, DATABASE, startupParams));
        return conn;
      } catch (Exception e) {
        try {
          socket.close();
        } catch (IOException suppressed) {
          e.addSuppressed(suppressed);
        }
        throw e;
      }
    }

    // Sends an SSLRequest or GSSENCRequest and checks the server's single-byte answer.
    private static void sendRequestCode(Socket socket, int code, char expectedAnswer)
        throws IOException {
      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      out.writeInt(8);
      out.writeInt(code);
      out.flush();
      int answer = socket.getInputStream().read();
      if (answer != expectedAnswer) {
        throw new IOException("Expected '" + expectedAnswer + "' in answer to request code "
            + code + ", got " + answer);
      }
    }

    private Socket startTls(Socket plain, InetSocketAddress target) throws Exception {
      TrustManager[] trustAll = { new X509TrustManager() {
        @Override public void checkClientTrusted(X509Certificate[] chain, String authType) {}
        @Override public void checkServerTrusted(X509Certificate[] chain, String authType) {}
        @Override public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
      } };
      SSLContext ctx = SSLContext.getInstance("TLS");
      ctx.init(null, trustAll, null);
      SSLSocket tlsSocket = (SSLSocket) ctx.getSocketFactory().createSocket(
          plain, target.getHostString(), target.getPort(), true /* autoClose */);
      tlsSocket.setSoTimeout(socketTimeoutMs);
      tlsSocket.startHandshake();
      return tlsSocket;
    }
  }
}
