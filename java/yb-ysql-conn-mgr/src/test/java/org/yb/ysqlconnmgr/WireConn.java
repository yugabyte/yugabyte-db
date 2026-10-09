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
import java.util.LinkedHashMap;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBCluster;
import org.yb.pgsql.ConnectionEndpoint;
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

    Builder(MiniYBCluster cluster) {
      this.cluster = cluster;
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
        PgWireProtocol.readUntilReady(conn.in);
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
        WireConn conn = new WireConn(socket);
        conn.sendRaw(PgWireProtocol.buildStartupMessage(USER, DATABASE, startupParams));
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
  }
}
