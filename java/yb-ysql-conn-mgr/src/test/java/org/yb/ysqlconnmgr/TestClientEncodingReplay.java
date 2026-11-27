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
import static org.yb.AssertionWrappers.assertNotEquals;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.Pair;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestClientEncodingReplay extends BaseYsqlConnMgr {
  private static final Charset LATIN1 = StandardCharsets.ISO_8859_1;
  private static final String CAFE = "caf\u00e9";
  private static final String EURO = "\u20ac";
  private static final String POUND = "\u00a3";
  private static final AtomicInteger dbCounter = new AtomicInteger();

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
  }

  private static int backendPid(WireConn conn) throws Exception {
    return conn.createPipeline().query("SELECT pg_backend_pid()").label("pid").run()
        .intValue("pid");
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
    holder.createPipeline().query("BEGIN").run();
    assertEquals(pid, backendPid(holder));
    assertNotEquals(pid, backendPid(client));
    return holder;
  }

  // A non-ASCII value set after switching to LATIN1 mid-session replays onto a backend that
  // starts on UTF8 and reads back unchanged in LATIN1.
  @Test
  public void testLatin1ValueReplayedOnUtf8Backend() throws Exception {
    String db = createDatabase();
    try (WireConn client = connect(db, null)) {
      client.createPipeline(LATIN1)
          .query("SET client_encoding = LATIN1")
          .query("SET myapp.x = '" + CAFE + "'")
          .run();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        client.createPipeline(LATIN1)
            .query("SHOW myapp.x").row(CAFE)
            .query("SHOW client_encoding").row("LATIN1")
            .run();
      }
    }
  }

  // LATIN1 comes from the startup packet, so the new backend starts on LATIN1. A value set
  // after switching to UTF8 must not be reinterpreted as LATIN1 on replay.
  @Test
  public void testUtf8ValueReplayedForLatin1StartupClient() throws Exception {
    String db = createDatabase();
    try (WireConn client = connect(db, "LATIN1")) {
      client.createPipeline()
          .query("SET client_encoding = UTF8")
          .query("SET myapp.x = '" + CAFE + "'")
          .run();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        client.createPipeline()
            .query("SHOW myapp.x").row(CAFE)
            .query("SHOW client_encoding").row("UTF8")
            .run();
      }
    }
  }

  // A value set under UTF8 before the client switched to LATIN1 replays and reads back
  // converted to LATIN1.
  @Test
  public void testUtf8ValueReplayedAfterSwitchToLatin1() throws Exception {
    String db = createDatabase();
    try (WireConn client = connect(db, null)) {
      client.createPipeline()
          .query("SET myapp.x = '" + CAFE + "'")
          .query("SET client_encoding = LATIN1")
          .run();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        client.createPipeline(LATIN1)
            .query("SHOW myapp.x").row(CAFE)
            .query("SHOW client_encoding").row("LATIN1")
            .run();
      }
    }
  }

  // session_authorization is GUC_REPORT: the client gets exactly one ParameterStatus for it, in
  // its own encoding rather than the server-encoded value ConnMgr is sent.
  @Test
  public void testSessionAuthorizationReportedInClientEncoding() throws Exception {
    String db = createDatabase();
    String role = "r\u00f4le";
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
             .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE ROLE \"" + role + "\" LOGIN");
    }
    try (WireConn client = connect(db, "LATIN1")) {
      PipelineResult result = client.createPipeline(LATIN1)
          .query("SET SESSION AUTHORIZATION \"" + role + "\"")
          .run();
      List<String> reported = new ArrayList<>();
      for (Pair<String, String> status : result.parameterStatuses()) {
        if (status.getFirst().equals("session_authorization")) {
          reported.add(status.getSecond());
        }
      }
      assertEquals(Arrays.asList(role), reported);
    }
  }

  // A value with no LATIN1 equivalent still replays. Only reading it fails, with 22P05 as in
  // PG, and the session stays usable.
  @Test
  public void testUntranslatableValueReplayedAfterSwitchToLatin1() throws Exception {
    String db = createDatabase();
    try (WireConn client = connect(db, null)) {
      client.createPipeline()
          .query("SET myapp.x = '" + EURO + "'")
          .query("SET client_encoding = LATIN1")
          .run();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        client.createPipeline(LATIN1)
            .query("SHOW myapp.x").expectBackendError("22P05", null)
            .query("SHOW client_encoding").row("LATIN1")
            .run();
      }
    }
  }

  // A startup-packet value is taken as server-encoded bytes whatever client_encoding the same
  // packet asks for. A LATIN1 client with a non-ASCII one reads it back in LATIN1, before and
  // after a backend switch.
  @Test
  public void testNonAsciiStartupValueForLatin1Client() throws Exception {
    String db = createDatabase();
    try (WireConn client = rawConnBuilder().database(db)
             .startupParam("client_encoding", "LATIN1")
             .startupParam("myapp.x", POUND)
             .connect()) {
      client.createPipeline(LATIN1)
          .query("SELECT 1").row("1")
          .query("SHOW myapp.x").row(POUND)
          .run();
      try (WireConn holder = pinCurrentBackend(db, client)) {
        client.createPipeline(LATIN1)
            .query("SHOW myapp.x").row(POUND)
            .query("SHOW client_encoding").row("LATIN1")
            .run();
      }
    }
  }
}
