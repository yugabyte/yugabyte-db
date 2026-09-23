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

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.util.Collections;
import java.util.Properties;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

// Tests the connection manager's handling of the unnamed prepared statement.
//
// A backend holds one unnamed statement, so the connection manager records
// which client it currently belongs to and keeps that client's copy. When
// ownership has moved on -- another client used the backend, or it detached,
// which drops the unnamed plan during reset -- a Bind or Describe is served by
// redeploying the client's copy, or gets 26000 if it has none.
//
// Ownership resets on every detach, so consecutive pipelines on one connection
// exercise the redeploy path. Tests that need the backend to stay attached
// across a message hold an explicit transaction.
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestUnnamedPrepStmtState extends BaseYsqlConnMgr {
  private static final int SOCKET_TIMEOUT_MS = 10000;

  private static final String BAD_SQL = "THIS IS NOT VALID SQL $$$$";
  private static final String SYNTAX_ERROR = "syntax error";
  private static final String NO_UNNAMED_STMT = "unnamed prepared statement does not exist";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings", "log_query,log_debug");
    disableWarmupRandomMode(builder);
  }

  private WireConn connect() throws Exception {
    return rawConnBuilder().socketTimeoutMs(SOCKET_TIMEOUT_MS).connect();
  }

  @Test
  public void testBindWithoutParse() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .run();
    }
  }

  // A simple Query drops the backend's unnamed statement, so YbQueryAck drops
  // the copy too rather than redeploying what vanilla PG considers gone.
  @Test
  public void testRedeployAcrossDetach() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .query("SELECT 2").row("2")
       .run();

      c.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .bind().execute().row("1")
       .sync()
       .run();
    }
  }

  // Two clients sharing one backend: each Bind must resolve to the query that
  // client parsed, whoever owns the backend copy at the time.
  @Test
  public void testTwoClientsSameBackend() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), SINGLE_BACKEND_FLAGS);
    markClusterNeedsRecreation();

    try (WireConn a = connect(); WireConn b = connect()) {
      a.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .run();

      b.createPipeline()
       .parse("SELECT 2").bind().execute().row("2")
       .sync()
       .run();

      a.createPipeline()
       .bind().execute().row("1")
       .sync()
       .run();

      b.createPipeline()
       .bind().execute().row("2")
       .sync()
       .run();

      // Closing the unnamed statement drops only the closing client's copy.
      a.createPipeline()
       .closeStmt("")
       .sync()
       .run();

      a.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      b.createPipeline()
       .bind().execute().row("2")
       .sync()
       .run();
    }
  }

  // With the backend still attached and still owning this client's unnamed
  // statement, the Bind is forwarded untouched, so the 26000 is the backend's
  // own -- the absent prefix is what says so.
  @Test
  public void testQueryClearsUnnamed() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .query("BEGIN")
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .query("SELECT 2").row("2")
       .bind().expectBackendError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .query("ROLLBACK")
       .run();
    }
  }

  // A failed unnamed Parse drops the backend's unnamed statement before parsing,
  // and YB_UNNAMED_PARSE_FAILED tells the connection manager to drop its copy;
  // without it the next Bind redeploys the statement from before.
  @Test
  public void testFailedUnnamedParseClears() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .parse(BAD_SQL).expectError(SYNTAX_ERROR)
       .sync()
       .run();

      c.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();
    }
  }

  // An unnamed Parse the backend skipped leaves the previous one in place.
  @Test
  public void testSkippedUnnamedParseKeepsPrevious() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("SELECT 2").bind().execute()
       .sync()
       .run();

      c.createPipeline()
       .bind().execute().row("1")
       .sync()
       .run();
    }
  }

  // Describe has the same redeploy path as Bind.
  @Test
  public void testDescribeUnnamedRedeploy() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1")
       .sync()
       .run();

      c.createPipeline()
       .describeStmt("")
       .sync()
       .run();
    }

    try (WireConn fresh = connect()) {
      fresh.createPipeline()
       .describeStmt("").expectConnMgrError("26000", NO_UNNAMED_STMT)
       .sync()
       .run();
    }
  }

  // A skipped Close of the unnamed statement is undone, so the client keeps it.
  @Test
  public void testUnnamedCloseSkippedByError() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .closeStmt("")
       .sync()
       .run();

      c.createPipeline()
       .bind().execute().row("1")
       .sync()
       .run();
    }
  }

  // Unnamed Parse of "SELECT 1" runs, then an unnamed Parse fails, then an
  // unnamed Parse of "SELECT 2" is skipped. After the Sync, Bind() gets 26000
  // in the same pipeline and again in the next one.
  @Test
  public void testFailedUnnamedParseMidPipeline() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .parse(BAD_SQL).expectError(SYNTAX_ERROR)
       .bind().execute()
       .parse("SELECT 2").bind().execute()
       .sync()
       .bind().expectError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("SELECT 3").bind().execute().row("3")
       .sync()
       .run();
    }
  }

  // Unnamed Parse of "SELECT 1" runs, then a named Parse fails, then an
  // unnamed Parse of "SELECT 2" is skipped. After the Sync, Bind() in the same
  // pipeline still returns 1.
  @Test
  public void testSkippedUnnamedParseSamePipeline() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .parse("SELECT 2").bind().execute()
       .sync()
       .bind().execute().row("1")
       .sync()
       .parse("SELECT 3").bind().execute().row("3")
       .sync()
       .run();

      c.createPipeline()
       .bind().execute().row("3")
       .sync()
       .run();
    }
  }

  // Execute of the unnamed statement fails with division by zero, then a Bind
  // fails with a bad parameter. After each error, Bind() with a good parameter
  // runs the same statement, both in the same pipeline and in the next one.
  @Test
  public void testUnnamedExecuteErrorKeepsStmt() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .parse("SELECT 1 / ($1::int * (random() < 2)::int)")
       .bind("", "0").execute().expectError("22012", "division by zero")
       .parse("SELECT 2")
       .sync()
       .bind("", "1").execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .bind("", "1").execute().row("1")
       .sync()
       .run();

      c.createPipeline()
       .parse("SELECT $1::int")
       .bind("", "abc").expectError("22P02", "invalid input syntax for type integer")
       .execute()
       .sync()
       .bind("", "7").execute().row("7")
       .sync()
       .run();
    }
  }

  // Close("") with Bind() right after it: the Bind gets 26000. Close("")
  // skipped by an error: Bind() after the Sync still runs. Close("") followed
  // by an error: the next pipeline's Bind() gets 26000. On a fresh connection,
  // Close("") with nothing parsed is harmless.
  @Test
  public void testUnnamedCloseSamePipeline() throws Exception {
    try (WireConn c = connect()) {
      c.createPipeline()
       .parse("SELECT 1").bind().execute().row("1")
       .closeStmt("")
       .bind().expectError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .parse("SELECT 2").bind().execute().row("2")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .closeStmt("")
       .bind().execute()
       .sync()
       .bind().execute().row("2")
       .sync()
       .run();

      c.createPipeline()
       .bind().execute().row("2")
       .sync()
       .run();

      c.createPipeline()
       .parse("SELECT 3").bind().execute().row("3")
       .closeStmt("")
       .parse("bad", BAD_SQL).expectError(SYNTAX_ERROR)
       .execute()
       .sync()
       .run();

      c.createPipeline()
       .bind().expectConnMgrError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();
    }

    try (WireConn fresh = connect()) {
      fresh.createPipeline()
       .closeStmt("")
       .bind().expectError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .bind().expectError("26000", NO_UNNAMED_STMT)
       .execute()
       .sync()
       .run();

      fresh.createPipeline()
       .parse("SELECT 4").bind().execute().row("4")
       .sync()
       .run();
    }
  }

  // The driver-level view: below prepareThreshold pgjdbc uses the unnamed
  // statement, and two connections on one backend keep taking it from each other.
  @Test
  public void testJdbcUnnamedAcrossTransactions() throws Exception {
    restartClusterWithAdditionalFlags(Collections.emptyMap(), SINGLE_BACKEND_FLAGS);
    markClusterNeedsRecreation();

    Properties props = new Properties();
    props.setProperty("prepareThreshold", "5");

    try (Connection connA = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(props);
        Connection connB = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(props);
        PreparedStatement stmtA = connA.prepareStatement("SELECT 1");
        PreparedStatement stmtB = connB.prepareStatement("SELECT 2")) {
      assertTrue(connA.getAutoCommit());

      for (int i = 0; i < 4; i++) {
        assertSingleValue(stmtA, 1);
        assertSingleValue(stmtB, 2);
      }
    }
  }

  private static void assertSingleValue(PreparedStatement stmt, int expected) throws Exception {
    try (ResultSet rs = stmt.executeQuery()) {
      assertTrue("Expected a row", rs.next());
      assertEquals(expected, rs.getInt(1));
    }
  }
}
