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

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.util.Collections;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import org.yb.ysqlconnmgr.PgWireProtocol.PgMessage;

// Tests the per-client prepared-statement state the connection manager keeps for
// named statements: which names a client may Bind or Describe, and how Close,
// re-Parse and the undo of messages the backend skipped after a mid-pipeline
// error change that.
//
// A name the client never successfully parsed gets an in-stream 26000 that
// leaves the connection usable. expectConnMgrError additionally asserts the
// connection manager raised it, so no test can pass on the backend's own 26000.
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestClientPrepStmtState extends BaseYsqlConnMgr {
  private static final int SOCKET_TIMEOUT_MS = 10000;
  private static final int POOL_WARMUP_MS = 5000;
  private static final int ABANDON_PIPELINE_MS = 2000;

  private static final String BAD_SQL = "THIS IS NOT VALID SQL $$$$";
  private static final String SYNTAX_ERROR = "syntax error";
  private static final String PID_QUERY = "SELECT pg_backend_pid()";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings", "log_query,log_debug");
    disableWarmupRandomMode(builder);
  }

  private WireConn connect() throws Exception {
    return rawConnBuilder().socketTimeoutMs(SOCKET_TIMEOUT_MS).connect();
  }

  private static String notPrepared(String stmtName) {
    return "prepared statement \"" + stmtName + "\" does not exist";
  }

  // Before client-state tracking this was an OD_ESERVER_WRITE that dropped the
  // connection.
  @Test
  public void testBindUnknownStmtKeepsConnection() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .bind("nope").expectConnMgrError("26000", notPrepared("nope"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();
    }
  }

  @Test
  public void testDescribeUnknownStmt() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .describeStmt("nope").expectConnMgrError("26000", notPrepared("nope"))
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 1").describeStmt("s1")
       .sync()
       .run();
    }
  }

  // Close removes the name from the client map, so a later Bind of it is an
  // error rather than a redeploy of what the backend still holds.
  @Test
  public void testCloseThenBind() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1")
       .sync()
       .run();

      c.createPipeline()
       .closeStmt("s1")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").expectConnMgrError("26000", notPrepared("s1"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();

      // Close of a name the client never registered is a no-op that still
      // answers CloseComplete, as in vanilla PG.
      c.createPipeline()
       .closeStmt("never_parsed")
       .sync()
       .run();
    }
  }

  // A Close the backend skipped must not take the name away from the client.
  @Test
  public void testCloseSkippedByPipelineError() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1")
       .sync()
       .run();

      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .closeStmt("s1")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("1")
       .sync()
       .run();
    }
  }

  // Re-Parse of a registered name overwrites it instead of raising 42P05, and a
  // later redeploy onto a backend that never saw the name uses the new query.
  @Test
  public void testReparseOverwrites() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), ROUND_ROBIN_FLAGS);
    markClusterNeedsRecreation();

    try (WireConn c = connect()) {
      Thread.sleep(POOL_WARMUP_MS);

      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 2").bind("s1").execute().row("2")
       .sync()
       .run();

      // Round-robin moves each transaction to the next backend, so every one of
      // these bare Binds has to be redeployed from the client map.
      for (int i = 0; i < 3; i++) {
        c.createPipeline()
         .bind("s1").execute().row("2")
         .sync()
         .run();
      }
    }
  }

  // A re-Parse the backend skipped must leave the previous query in place, and
  // several skipped re-Parses of one name must unwind in reverse order.
  @Test
  public void testReparseSkippedRestoresOldQuery() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s1", "SELECT 2")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s1", "SELECT 2")
       .parse("s1", "SELECT 3")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("1")
       .sync()
       .run();
    }
  }

  // The injected error puts the backend into skip-till-Sync, so the Parse behind
  // it never takes effect and its undo unregisters the name again.
  @Test
  public void testThrowErrorSkipsRestOfPipeline() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .bind("nope").expectConnMgrError("26000", notPrepared("nope"))
       .execute()
       .parse("s1", "SELECT 1")
       .bind("s1").execute()
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").expectConnMgrError("26000", notPrepared("s1"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();
    }
  }

  // A portal Close is answered by the backend, so CloseComplete consumes its
  // queue record; a skipped one is dropped by the drain. Either way the queue
  // stays aligned, which the following pipeline would notice.
  @Test
  public void testPortalClose() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1").bindPortal("p1", "s1").closePortal("p1")
       .sync()
       .run();

      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .closePortal("p1")
       .sync()
       .run();

      c.createPipeline()
       .parse("s2", "SELECT 2").bind("s2").execute().row("2")
       .sync()
       .run();
    }
  }

  // One pipeline covering every queue entry kind: each ack must consume the
  // record its own message pushed, or the queue shifts.
  @Test
  public void testMixedPipelineQueueIntegrity() throws Exception {
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);
    tailer.skipToEnd();

    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("m1", "SELECT 1").bind("m1").execute().row("1")
       .parse("SELECT 2").bind().execute().row("2")
       .describeStmt("m1")
       .bindPortal("mp", "m1").closePortal("mp")
       .closeStmt("m1")
       .sync()
       .query("SELECT 3").row("3")
       .parse("m2", "SELECT 4").bind("m2").execute().row("4")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .bind("m2").execute()
       .closeStmt("m2")
       .sync()
       .run();

      c.createPipeline()
       .parse("m3", "SELECT 5").bind("m3").execute().row("5")
       .sync()
       .run();

      c.createPipeline()
       .bind("m3").execute().row("5")
       .sync()
       .run();
    }

    tailer.assertNoMatch(
        "unexpected parse queue entry kind|with empty queue|failed to dequeue");
  }

  // Client A's Parse(s2) is skipped by an earlier error. Client B then parses
  // and binds s2 on every backend in the round-robin rotation, including the
  // one that skipped A's Parse. Finally A binds s2 and gets 26000.
  @Test
  public void testSkippedParseNotVisibleToOtherClient() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), ROUND_ROBIN_FLAGS);
    markClusterNeedsRecreation();

    try (WireConn a = connect(); WireConn b = connect()) {
      Thread.sleep(POOL_WARMUP_MS);

      int failedPid = a.createPipeline()
          .parse("probe", PID_QUERY).bind("probe").execute().label("pid")
          .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
          .parse("s2", PID_QUERY).bind("s2").execute()
          .sync()
          .run()
          .intValue("pid");

      boolean sawFailedBackend = false;
      for (int i = 0; i < 3; i++) {
        Pipeline p = b.createPipeline();
        if (i == 0) {
          p.parse("s2", PID_QUERY);
        }
        int pid = p.bind("s2").execute().label("pid")
            .sync()
            .run()
            .intValue("pid");
        sawFailedBackend |= pid == failedPid;
      }
      assertTrue("Round-robin never routed the second client to backend " + failedPid,
          sawFailedBackend);

      a.createPipeline()
       .bind("s2").expectConnMgrError("26000", notPrepared("s2"))
       .execute()
       .sync()
       .run();
    }
  }

  // One pipeline with four Sync segments. The third segment has a syntax error
  // before Parse(p3). Afterwards p1, p2 and p4 are bindable and p3 is not.
  @Test
  public void testErrorBetweenSyncsInOnePipeline() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("p1", "SELECT 1").bind("p1").execute().row("1")
       .sync()
       .parse("p2", "SELECT 2").bind("p2").execute().row("2")
       .sync()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("p3", "SELECT 3").bind("p3").execute()
       .sync()
       .parse("p4", "SELECT 4").bind("p4").execute().row("4")
       .sync()
       .run();

      c.createPipeline()
       .bind("p1").execute().row("1")
       .bind("p2").execute().row("2")
       .bind("p4").execute().row("4")
       .bind("p3").expectConnMgrError("26000", notPrepared("p3"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("p3", "SELECT 3").bind("p3").execute().row("3")
       .sync()
       .run();
    }
  }

  // Parse(s2) is skipped by an error. In the same pipeline, after the Sync,
  // Bind(s2) gets 26000, then s3 and a fresh s2 are parsed and run. The next
  // pipeline binds all three.
  @Test
  public void testBindSkippedNameWithinOnePipeline() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .bind("bad").execute()
       .parse("s2", "SELECT 2").bind("s2").execute()
       .sync()
       .bind("s2").expectConnMgrError("26000", notPrepared("s2"))
       .execute()
       .sync()
       .parse("s3", "SELECT 3").bind("s3").execute().row("3")
       .sync()
       .parse("s2", "SELECT 2").bind("s2").execute().row("2")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("1")
       .bind("s2").execute().row("2")
       .bind("s3").execute().row("3")
       .sync()
       .run();
    }
  }

  // Two Parses of s1 are both skipped by an error before them. Bind(s1) then
  // gets 26000 and a fresh Parse(s1) works.
  @Test
  public void testDuplicateSkippedParsesOfUnregisteredName() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s1", "SELECT 1").bind("s1").execute()
       .parse("s1", "SELECT 2").bind("s1").execute()
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").expectConnMgrError("26000", notPrepared("s1"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 3").bind("s1").execute().row("3")
       .sync()
       .run();
    }
  }

  // Bind(s1) fails with a bad parameter, and Execute of s3 fails with division
  // by zero. Both statements stay bindable afterwards. The Parses skipped
  // behind each error (s2, s4) do not.
  @Test
  public void testBindAndExecuteErrorsKeepParse() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT $1::int")
       .bind("s1", "abc").expectError("22P02", "invalid input syntax for type integer")
       .execute()
       .parse("s2", "SELECT 2").bind("s2").execute()
       .sync()
       .run();

      c.createPipeline()
       .bind("s1", "5").execute().row("5")
       .bind("s2").expectConnMgrError("26000", notPrepared("s2"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("s3", "SELECT 1 / ($1::int * (random() < 2)::int)")
       .bind("s3", "0").execute().expectError("22012", "division by zero")
       .parse("s4", "SELECT 4").bind("s4").execute()
       .sync()
       .run();

      c.createPipeline()
       .bind("s3", "1").execute().row("1")
       .bind("s4").expectConnMgrError("26000", notPrepared("s4"))
       .execute()
       .sync()
       .run();
    }
  }

  // Close(s1) followed by Bind(s1) in the same pipeline. The Bind gets 26000.
  @Test
  public void testCloseThenBindSamePipeline() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .closeStmt("s1")
       .bind("s1").expectConnMgrError("26000", notPrepared("s1"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();
    }
  }

  // Close(s1) lands between the Bind and the Execute of a portal. PG defers the
  // deallocation while a portal still references the statement, so Execute(pb)
  // still returns its row; only the Bind after the Sync gets 26000.
  @Test
  public void testCloseStmtWithOpenPortal() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT $1::int")
       .bindPortal("pa", "s1", "1").executePortal("pa").row("1")
       .sync()
       .bindPortal("pb", "s1", "2")
       .closeStmt("s1")
       .executePortal("pb").row("2")
       .sync()
       .bindPortal("pc", "s1", "3").expectConnMgrError("26000", notPrepared("s1"))
       .executePortal("pc")
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT $1::int").bind("s1", "4").execute().row("4")
       .sync()
       .run();
    }
  }

  // Close(s1) and re-Parse(s1) in one pipeline, followed by an error. Three
  // variants: one re-Parse then the error; two re-Parses, the error, then a
  // third re-Parse; a plain Close and re-Parse. Each time, the next Bind(s1)
  // runs the last re-Parse before the error.
  @Test
  public void testCloseReparseThenError() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .closeStmt("s1")
       .parse("s1", "SELECT 2")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("2")
       .sync()
       .run();

      c.createPipeline()
       .closeStmt("s1")
       .parse("s1", "SELECT 3")
       .parse("s1", "SELECT 4")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s1", "SELECT 5")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("4")
       .sync()
       .run();

      c.createPipeline()
       .closeStmt("s1")
       .parse("s1", "SELECT 6")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("6")
       .sync()
       .run();
    }
  }

  // Three pipelines mixing Close(s1) and re-Parse(s1) with an error.
  // 1. Parse(s1), Close(s1), error, Parse(s1): s1 ends up not bindable.
  // 2. Parse(s1), error, Parse(s2), Parse(s1): s1 still runs the first query,
  //    s2 is not bindable.
  // 3. error, Parse(s2), Close(s1), Parse(s1): same result as 2.
  @Test
  public void testCloseSkippedWithReparse() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("s1", "SELECT 1")
       .closeStmt("s1")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s1", "SELECT 2")
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").expectConnMgrError("26000", notPrepared("s1"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s2", "SELECT 2").bind("s2").execute()
       .parse("s1", "SELECT 3").bind("s1").execute()
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("1")
       .bind("s2").expectConnMgrError("26000", notPrepared("s2"))
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s2", "SELECT 2").bind("s2").execute()
       .closeStmt("s1")
       .parse("s1", "SELECT 3").bind("s1").execute()
       .sync()
       .run();

      c.createPipeline()
       .bind("s1").execute().row("1")
       .bind("s2").expectConnMgrError("26000", notPrepared("s2"))
       .execute()
       .sync()
       .run();
    }
  }

  // Client A sends a pipeline with an error and no Sync, then drops the
  // socket. With a one-backend pool, client B must get a different backend
  // pid and be able to parse the name A's pipeline left pending.
  @Test
  public void testAbruptCloseMidPipelineClosesBackend() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), SINGLE_BACKEND_FLAGS);
    markClusterNeedsRecreation();

    int abandonedPid;
    try (WireConn a = connect()) {
      abandonedPid = a.createPipeline()
          .query(PID_QUERY).label("pid")
          .run()
          .intValue("pid");

      a.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("s1", "SELECT 1").bind("s1").execute()
       .pause(ABANDON_PIPELINE_MS)
       .allowUnreadTail()
       .run();
      a.closeAbruptly();
    }

    try (WireConn b = connect()) {
      int pid = b.createPipeline()
          .query(PID_QUERY).label("pid")
          .run()
          .intValue("pid");
      assertTrue("Backend " + abandonedPid + " was handed out again after an unfinished pipeline",
          pid != abandonedPid);

      b.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();
    }
  }

  // YbThrowError may only arrive from the connection manager, and only with a
  // 5-character sqlstate.
  @Test
  public void testThrowErrorPacketRejected() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), SINGLE_BACKEND_FLAGS);
    markClusterNeedsRecreation();

    try (WireConn c = rawConnBuilder()
            .endpoint(ConnectionEndpoint.POSTGRES)
            .socketTimeoutMs(SOCKET_TIMEOUT_MS)
            .connect()) {
      c.sendRaw(PgWireProtocol.buildRaw('x', throwErrorBody("26000", "injected")));
      assertFatal(c.readMessage(), "08P01", "invalid frontend message type 120");
    }

    int pid;
    try (WireConn c = connect()) {
      pid = c.createPipeline()
          .query(PID_QUERY).label("pid")
          .run()
          .intValue("pid");
    }

    for (String sqlState : new String[] { "26000", "2600" }) {
      try (WireConn c = connect()) {
        c.sendRaw(PgWireProtocol.buildRaw('x', throwErrorBody(sqlState, "injected")));
        try {
          PgMessage msg = c.readMessage();
          fail("Expected the connection manager to drop the connection, got " + msg);
        } catch (EOFException expected) {
          // Connection manager closed the connection without forwarding 'x'.
        }
      }
    }

    try (WireConn c = connect()) {
      // Had 'x' reached the backend it would have died on the ereport(FATAL), and
      // the one-backend pool would have had to start a new one.
      assertEquals("The pooled backend was lost to the rejected YbThrowError packet",
          pid, c.createPipeline()
                .query(PID_QUERY).label("pid")
                .run()
                .intValue("pid"));

      c.createPipeline()
       .parse("s1", "SELECT 1").bind("s1").execute().row("1")
       .sync()
       .run();
    }
  }

  private static void assertFatal(PgMessage msg, String sqlState, String substring) {
    assertEquals("Expected an ErrorResponse, got " + msg,
        PgWireProtocol.BE_ERROR_RESPONSE, msg.type);
    ErrorResponse error = ErrorResponse.parse(msg.body);
    assertEquals("FATAL", error.severity());
    assertEquals(sqlState, error.sqlstate());
    assertTrue("Unexpected error: " + error, error.message().contains(substring));
  }

  private static byte[] throwErrorBody(String sqlState, String message) throws IOException {
    ByteArrayOutputStream buf = new ByteArrayOutputStream();
    DataOutputStream out = new DataOutputStream(buf);
    PgWireProtocol.writeString(out, sqlState);
    PgWireProtocol.writeString(out, message);
    out.flush();
    return buf.toByteArray();
  }
}
