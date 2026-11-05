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

import static org.yb.AssertionWrappers.assertArrayEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;

import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import org.yb.ysqlconnmgr.PgWireProtocol.PgMessage;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestClientEncodingReplay extends BaseYsqlConnMgr {
  private static final Charset LATIN1 = StandardCharsets.ISO_8859_1;
  private static final Charset UTF8 = StandardCharsets.UTF_8;
  private static final String CAFE = "café";
  private static final String EURO = "€";
  private static final String UTF8_SWITCH_REGEX = "deploy: SET client_encoding='UTF8';";
  private static final AtomicInteger dbCounter = new AtomicInteger();

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings", "log_query,log_debug");
  }

  private static final class Reply {
    final List<byte[]> values = new ArrayList<>();
    final List<PgMessage> parameterStatuses = new ArrayList<>();
    String sqlState;
  }

  private static byte[] queryBytes(String sql, Charset charset) {
    byte[] text = sql.getBytes(charset);
    return Arrays.copyOf(text, text.length + 1);
  }

  private static Reply query(WireConn conn, String sql, Charset charset) throws Exception {
    conn.sendRaw(PgWireProtocol.buildRaw('Q', queryBytes(sql, charset)));
    Reply reply = new Reply();
    for (;;) {
      PgMessage msg = conn.readMessage();
      switch (msg.type) {
        case PgWireProtocol.BE_DATA_ROW:
          reply.values.add(firstColumn(msg.body));
          break;
        case PgWireProtocol.BE_PARAMETER_STATUS:
          reply.parameterStatuses.add(msg);
          break;
        case PgWireProtocol.BE_ERROR_RESPONSE:
          reply.sqlState = PgWireProtocol.parseErrorResponse(msg.body).get('C');
          break;
        case PgWireProtocol.BE_READY_FOR_QUERY:
          return reply;
        default:
          break;
      }
    }
  }

  private static Reply queryOk(WireConn conn, String sql, Charset charset) throws Exception {
    Reply reply = query(conn, sql, charset);
    assertNull("'" + sql + "' failed with " + reply.sqlState, reply.sqlState);
    return reply;
  }

  private static byte[] firstColumn(byte[] body) {
    ByteBuffer bb = ByteBuffer.wrap(body);
    bb.getShort();
    int len = bb.getInt();
    if (len < 0) {
      return null;
    }
    byte[] data = new byte[len];
    bb.get(data);
    return data;
  }

  private static int backendPid(WireConn conn) throws Exception {
    Reply reply = queryOk(conn, "SELECT pg_backend_pid()", UTF8);
    return Integer.parseInt(new String(reply.values.get(0), UTF8));
  }

  private String createDatabase() throws Exception {
    String db = "enc_db_" + dbCounter.incrementAndGet();
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
             .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE DATABASE " + db);
    }
    return db;
  }

  private WireConn connect(String db, String clientEncoding) throws Exception {
    WireConn.Builder builder = rawConnBuilder().database(db);
    if (clientEncoding != null) {
      builder.startupParam("client_encoding", clientEncoding);
    }
    return builder.connect();
  }

  // Pins the client's current backend in a transaction held by another client, so the client's
  // next query runs on a fresh backend that replays its session settings.
  private WireConn pinCurrentBackend(String db, WireConn client) throws Exception {
    int pid = backendPid(client);
    WireConn holder = connect(db, null);
    queryOk(holder, "BEGIN", UTF8);
    assertEquals(pid, backendPid(holder));
    assertNotEquals(pid, backendPid(client));
    return holder;
  }

  private static void assertValue(Reply reply, String expected, Charset charset) {
    assertEquals(1, reply.values.size());
    assertArrayEquals(expected.getBytes(charset), reply.values.get(0));
  }

  @Test
  public void testLatin1ValueReplayedOnUtf8Backend() throws Exception {
    String db = createDatabase();
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);
    try (WireConn client = connect(db, null)) {
      queryOk(client, "SET client_encoding = LATIN1", UTF8);
      queryOk(client, "SET myapp.x = '" + CAFE + "'", LATIN1);
      tailer.skipToEnd();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        assertNotNull(tailer.waitForLogRegex(UTF8_SWITCH_REGEX, 10, TimeUnit.SECONDS));
        assertValue(queryOk(client, "SHOW myapp.x", LATIN1), CAFE, LATIN1);
        assertValue(queryOk(client, "SHOW client_encoding", LATIN1), "LATIN1", LATIN1);
      }
    }
  }

  @Test
  public void testUtf8ValueReplayedForLatin1StartupClient() throws Exception {
    String db = createDatabase();
    try (WireConn client = connect(db, "LATIN1")) {
      queryOk(client, "SET client_encoding = UTF8", UTF8);
      queryOk(client, "SET myapp.x = '" + CAFE + "'", UTF8);
      try (WireConn holder = pinCurrentBackend(db, client)) {
        assertValue(queryOk(client, "SHOW myapp.x", UTF8), CAFE, UTF8);
        assertValue(queryOk(client, "SHOW client_encoding", UTF8), "UTF8", UTF8);
      }
    }
  }

  @Test
  public void testUtf8ValueReplayedAfterSwitchToLatin1() throws Exception {
    String db = createDatabase();
    try (WireConn client = connect(db, null)) {
      queryOk(client, "SET myapp.x = '" + CAFE + "'", UTF8);
      queryOk(client, "SET client_encoding = LATIN1", UTF8);
      try (WireConn holder = pinCurrentBackend(db, client)) {
        assertValue(queryOk(client, "SHOW myapp.x", LATIN1), CAFE, LATIN1);
        assertValue(queryOk(client, "SHOW client_encoding", LATIN1), "LATIN1", LATIN1);
      }
    }
  }

  @Test
  public void testSessionAuthorizationReportedInClientEncoding() throws Exception {
    String db = createDatabase();
    String role = "rôle";
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
             .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE ROLE \"" + role + "\" LOGIN");
    }
    try (WireConn client = connect(db, "LATIN1")) {
      Reply reply = queryOk(client, "SET SESSION AUTHORIZATION \"" + role + "\"", LATIN1);
      List<byte[]> reported = new ArrayList<>();
      for (PgMessage msg : reply.parameterStatuses) {
        byte[] name = "session_authorization\0".getBytes(UTF8);
        if (msg.body.length > name.length
            && Arrays.equals(Arrays.copyOf(msg.body, name.length), name)) {
          reported.add(Arrays.copyOfRange(msg.body, name.length, msg.body.length - 1));
        }
      }
      assertEquals(1, reported.size());
      assertArrayEquals(role.getBytes(LATIN1), reported.get(0));
    }
  }

  @Test
  public void testAsciiValuesUseSingleDeployQuery() throws Exception {
    String db = createDatabase();
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);
    try (WireConn client = connect(db, null)) {
      queryOk(client, "SET client_encoding = LATIN1", UTF8);
      queryOk(client, "SET myapp.x = 'cafe'", LATIN1);
      tailer.skipToEnd();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        assertNotNull(tailer.waitForLogRegex("deploy: SET ", 10, TimeUnit.SECONDS));
        assertValue(queryOk(client, "SHOW myapp.x", LATIN1), "cafe", LATIN1);
        assertValue(queryOk(client, "SHOW client_encoding", LATIN1), "LATIN1", LATIN1);
      }
      tailer.assertNoMatch(UTF8_SWITCH_REGEX);
    }
  }

  @Test
  public void testDeployLeavesNoClientEncodingWhenClientNeverSetIt() throws Exception {
    String db = createDatabase();
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);
    try (WireConn client = connect(db, null)) {
      queryOk(client, "SET myapp.x = '" + CAFE + "'", UTF8);
      tailer.skipToEnd();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        assertNotNull(tailer.waitForLogRegex(UTF8_SWITCH_REGEX, 10, TimeUnit.SECONDS));
        tailer.skipToEnd();
        assertValue(queryOk(client, "SHOW myapp.x", UTF8), CAFE, UTF8);
        assertValue(queryOk(client, "SHOW client_encoding", UTF8), "UTF8", UTF8);
        assertNull(tailer.waitForLogRegex(
            "deploy: Reset to transaction backend default", 2, TimeUnit.SECONDS));
      }
    }
  }

  @Test
  public void testUntranslatableValueReplayedAfterSwitchToLatin1() throws Exception {
    String db = createDatabase();
    try (WireConn client = connect(db, null)) {
      queryOk(client, "SET myapp.x = '" + EURO + "'", UTF8);
      queryOk(client, "SET client_encoding = LATIN1", UTF8);
      try (WireConn holder = pinCurrentBackend(db, client)) {
        assertEquals("22P05", query(client, "SHOW myapp.x", LATIN1).sqlState);
        assertValue(queryOk(client, "SHOW client_encoding", LATIN1), "LATIN1", LATIN1);
      }
    }
  }
}
