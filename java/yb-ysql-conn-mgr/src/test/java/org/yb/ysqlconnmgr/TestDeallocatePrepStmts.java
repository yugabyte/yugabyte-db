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
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.fail;
import static org.yb.ysqlconnmgr.PgWireProtocol.*;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.sql.*;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.RequiresLinux;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestDeallocatePrepStmts extends BaseYsqlConnMgr {

  private static final int IDLE_TIME = 1;
  private static final String QUERY_ALL_PREP_STMTS =
    "SELECT name, statement FROM pg_prepared_statements";
  private static final String CREATE_TABLE_QUERY =
  "create table if not exists t_deallocate_prep_stmts_test " +
  "(id int, name varchar(20), value varchar(20))";
  private static final String SELECT_QUERY1 =
    "select name FROM t_deallocate_prep_stmts_test";
  private static final String SELECT_QUERY2 =
    "select value FROM t_deallocate_prep_stmts_test";
  private static final String SELECT_QUERY3 =
    "select id FROM t_deallocate_prep_stmts_test";
  private static final String INSERT_QUERY =
    "insert into t_deallocate_prep_stmts_test values (1, 'test', 'test')";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    // Deallocate support has been added only in optimized
    // extended query protocol mode.
    // GH: #30412 Adds the support in unoptimized mode as well.
    builder.addCommonTServerFlag(
      "ysql_conn_mgr_optimized_extended_query_protocol", "true");
    builder.addCommonTServerFlag("ysql_conn_mgr_log_settings",
    "log_query,log_debug");
    builder.addCommonTServerFlag("ysql_conn_mgr_idle_time",
      Integer.toString(IDLE_TIME));
    builder.addCommonTServerFlag("ysql_conn_mgr_jitter_time", "0");
    disableWarmupRandomMode(builder);
  }

  // Set up assumes that while running its callee test, there was at most a
  // single existing backend process which is required to be closed to start
  // the test fresh.
  private void setUpTestTableAndCleanBackends() throws Exception {
    try (Connection conn = getConnectionBuilder()
         .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
         .withUser("yugabyte")
         .withPassword("yugabyte")
         .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute(CREATE_TABLE_QUERY);
      stmt.execute(INSERT_QUERY);
      // To ensure tests starts with fresh backend with no
      // prepared statements.
      // Temp table will make the backend sticky and get terminated once the
      // connection is closed.
      stmt.execute("CREATE TEMP TABLE drop_backend(id int)");
    }
  }

  @Test
  public void testClosePacket() throws Exception {
    // The following test, tests behaviour of CLOSE packet with conn mgr.
    // It tests if:
    // 1. CLOSE packet sent for valid prepared statement then DB doesn't
    // deallocate the prepared statement.
    // 2. CLOSE packet sent for invalid prepared statement then DB
    // deallocates the prepared statement.
    // 3. CLOSE packet is sent on backend where entry does not exist, it should
    // be no-op similar to PG.

    Properties props = new Properties();
    // Make sure to use named prepared statements.
    props.setProperty("prepareThreshold", "1");
    // Disable's driver's prepared statement cache entirely. This is done so
    // that driver always sends CLOSE packet on calling pstmt.close().
    props.setProperty("preparedStatementCacheQueries", "0");
    String queryPgPreparedStatements =
              "SELECT name, statement, prepare_time FROM " +
              "pg_prepared_statements WHERE " +
              "statement = 'select name FROM t_deallocate_prep_stmts_test'";
    setUpTestTableAndCleanBackends();
    try (Connection conn = getConnectionBuilder()
         .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
         .withUser("yugabyte")
         .withPassword("yugabyte")
         .connect(props);)
      {
        Statement stmt = conn.createStatement();
        PreparedStatement pstmt = conn.prepareStatement(SELECT_QUERY1);
        pstmt.execute();
        pstmt.execute();
        ResultSet rs = stmt.executeQuery(queryPgPreparedStatements);
        rs.next();
        assertEquals(1, rs.getRow());
        String prepareTime = rs.getString("prepare_time");
        assertFalse(rs.next());

        // SEND CLOSE PACKET
        pstmt.close();
        rs = stmt.executeQuery(queryPgPreparedStatements);
        rs.next();
        // CLOSE send by client won't deallocated from server as it's still
        // valid on backend.
        assertEquals(1, rs.getRow());
        assertFalse(rs.next());

        pstmt = conn.prepareStatement(SELECT_QUERY1);
        // After CLOSE, JDBC sends a new name to parse it again.
        pstmt.execute();

        String name = null;
        String secondPrepareTime = null;
        rs = stmt.executeQuery(queryPgPreparedStatements);
        // Find the statement with the same prepare time.
        while (rs.next()) {
          if (prepareTime.equals(rs.getString("prepare_time"))) {
            name = rs.getString("name");
          } else {
            assertNull(secondPrepareTime);
            secondPrepareTime = rs.getString("prepare_time");
          }
        }
        assertNotNull("Expected to find the prepared statement", name);


        stmt.execute("ALTER TABLE t_deallocate_prep_stmts_test ALTER " +
                     "COLUMN name TYPE VARCHAR(41)");

        try {
          pstmt.execute();
          LOG.info("JDBC internally retried after getting cache plan error");
          // JDBC must have sent CLOSE packet on getting cache plan error.
          // The invalid prepared statement must have been deallocated & conn mgr would have
          // removed it's entry from server hashmap.
          // The followed PARSE must have succeeded by creating a new cache plan on server.
        }
        catch (Exception e) {
          LOG.error("Got an unexpected error while executing prepared statement: ", e);
          fail("Got an unexpected error while executing prepared statement: " + e.getMessage());
        }

        rs = stmt.executeQuery(queryPgPreparedStatements);
        while (rs.next()) {
          assertNotEquals("Prepared statement " + rs.getString("name") +
                      " should have been deallocated",
                      secondPrepareTime, rs.getString("prepare_time"));
        }

        // This ensures after ALTER TABLE, the prepared statement became invalid and
        // got deallocated.

        // Wait for single backend to get expired out.
        Thread.sleep(4 * IDLE_TIME * 1000);

        // CLOSE packet will be sent to new backend where entry does not exist, it should be
        // no-op similar to PG. And must receive CloseComplete response without any error.
        // Although JDBC user, can't assert getting CloseComplete but it should be no error.
        pstmt.close();

        rs = stmt.executeQuery(queryPgPreparedStatements);
        rs.next();
        assertEquals(0, rs.getRow());

        stmt.close();

      }
  }

  // Regression test: protocol-level Close ('C') message must not crash when
  // the search-path cache has been invalidated since the last transaction.
  //   1. Client prepares + executes a named statement. The Sync after Execute
  //      runs CommitTransaction(), which sets CurrentResourceOwner = NULL and
  //      tears down TopTransactionResourceOwner.
  //   2. Client sends SET search_path which tries to invalidate the cache.
  //   3. Client sends Close 'S' on the prepared statement which would make
  //      request to check the validity of the prepared statement.
  @Test
  public void testClosePacketAfterSearchPathChange() throws Exception {
    Properties props = new Properties();
    // Force named extended-protocol prepared statements so JDBC sends Parse +
    // Bind + Execute as separate messages.
    props.setProperty("prepareThreshold", "1");
    // Disable the driver-side prepared statement cache so pstmt.close()
    // actually emits a Close 'S' message instead of returning the statement
    // to the cache silently.
    props.setProperty("preparedStatementCacheQueries", "0");
    setUpTestTableAndCleanBackends();
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .withUser("yugabyte")
            .withPassword("yugabyte")
            .connect(props)) {
      Statement stmt = conn.createStatement();

      PreparedStatement pstmt = conn.prepareStatement(SELECT_QUERY1);
      pstmt.execute();

      // Dirty the search-path cache. assign_search_path() flips
      // baseSearchPathValid to false; recomputeNamespacePath() will need to
      // walk pg_authid (for $user) the next time it's called.
      stmt.execute("SET search_path TO public");

      // Without the fix, this Close 'S' segfaults inside the catcache lookup
      // because the 'C' message handler never started a transaction and
      // CurrentResourceOwner is NULL.
      pstmt.close();

      // Connection must still be usable -- a backend crash would have
      // dropped it.
      ResultSet rs = stmt.executeQuery("SELECT 1");
      assertEquals(true, rs.next());
      assertEquals(1, rs.getInt(1));
    }
  }

  @Test
  public void testDeallocateQuery() throws Exception {
    // Validates DEALLOCATE statement sent for valid protocol-level
    // prepared statement doesn't deallocate the prepared statement on database
    // via connection manager.
    Properties props = new Properties();
    // Use named prepared statements.
    props.setProperty("prepareThreshold", "1");
    String queryPgPreparedStatements =
              "SELECT name, statement, prepare_time FROM " +
              "pg_prepared_statements WHERE " +
              "statement = 'select name FROM t_deallocate_prep_stmts_test'";
    setUpTestTableAndCleanBackends();
    try (Connection conn = getConnectionBuilder()
         .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
         .withUser("yugabyte")
         .withPassword("yugabyte")
         .connect(props);)
      {
        Statement stmt = conn.createStatement();

        PreparedStatement pstmt = conn.prepareStatement(SELECT_QUERY1);
        pstmt.execute();
        ResultSet rs = stmt.executeQuery(queryPgPreparedStatements);
        rs.next();
        assertEquals(1, rs.getRow());
        String hash = rs.getString("name");
        assertFalse(rs.next());
        // SEND DEALLOCATE STATEMENT
        stmt.execute(String.format("DEALLOCATE \"%s\"", hash));
        rs = stmt.executeQuery(queryPgPreparedStatements);
        rs.next();
        assertEquals(1, rs.getRow());
        assertFalse(rs.next());

        stmt.execute("ALTER TABLE t_deallocate_prep_stmts_test ALTER COLUMN name TYPE VARCHAR(41)");
        pstmt.execute();

      }
  }

  @Test
  public void testDeallocateAllStatements() throws Exception {

    Properties props = new Properties();
    // Make sure to use named prepared statements.
    props.setProperty("prepareThreshold", "1");
    setUpTestTableAndCleanBackends();
    try (Connection conn = getConnectionBuilder()
         .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
         .withUser("yugabyte")
         .withPassword("yugabyte")
         .connect(props);)
      {
        Statement stmt = conn.createStatement();
        PreparedStatement pstmt1 = conn.prepareStatement(SELECT_QUERY1);
        PreparedStatement pstmt2 = conn.prepareStatement(SELECT_QUERY2);
        PreparedStatement pstmt3 = conn.prepareStatement(SELECT_QUERY3);
        pstmt1.execute();
        pstmt2.execute();
        pstmt3.execute();

        stmt.execute("DEALLOCATE ALL");
        ResultSet rs = stmt.executeQuery(QUERY_ALL_PREP_STMTS);
        int matchCount = 0;
        while (rs.next()) {
          String name = rs.getString("name");
          String statement = rs.getString("statement");
          LOG.info("name: {}, statement: {}", name, statement);
          if (statement.equals(SELECT_QUERY1) ||
              statement.equals(SELECT_QUERY2) ||
              statement.equals(SELECT_QUERY3)) matchCount++;
        }
        assertEquals("Expected none of the DML prepared statements to be " +
                      "deallocated", 3, matchCount);

        stmt.execute("PREPARE testPlan AS SELECT 123");
        stmt.execute("EXECUTE testPlan");

        verifySessionParameterValue(stmt,
          "ysql_conn_mgr_sticky_object_count", "1");

        stmt.execute("DEALLOCATE ALL");
        rs = stmt.executeQuery(QUERY_ALL_PREP_STMTS);
        matchCount = 0;
        while (rs.next()) {
          String name = rs.getString("name");
          String statement = rs.getString("statement");
          LOG.info("name: {}, statement: {}", name, statement);
          if (statement.equals(SELECT_QUERY1) ||
              statement.equals(SELECT_QUERY2) ||
              statement.equals(SELECT_QUERY3) ||
              name.equals("testplan")) matchCount++;
        }
        assertEquals("Expected all of the prepared statements to be " +
                      "deallocated since connection is sticky",
                      0, matchCount);

        // Make sure conn mgr server hashmaps are updated.
        // JDBC send CLOSE followed by PARSE with new name, so prev statements
        // has been evicted from server hashmaps can't be verified here.
        pstmt1.execute();
        pstmt2.execute();
        pstmt3.execute();
      }
  }

  @Test
  public void testDeallocateSqlPrepareStatement() throws Exception {

    Properties props = new Properties();
    // Make sure to use named prepared statements.
    props.setProperty("prepareThreshold", "1");
    String queryPgPreparedStatements =
      "SELECT name FROM pg_prepared_statements WHERE name = 'testplan'";
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {
      // PREPARE query
      stmt.execute("PREPARE testPlan AS SELECT 123");
      // EXECUTE query
      ResultSet rs = stmt.executeQuery("EXECUTE testPlan");
      rs.next();
      assertEquals(123, rs.getInt(1));

      verifySessionParameterValue(stmt,
        "ysql_conn_mgr_sticky_object_count", "1");

      rs = stmt.executeQuery(queryPgPreparedStatements);
      rs.next();
      assertEquals("testplan", rs.getString("name"));

      // DEALLOCATE query
      stmt.execute("DEALLOCATE PREPARE testPlan");

      // Verify the prepared statement is deallocated
      rs = stmt.executeQuery(queryPgPreparedStatements);
      rs.next();
      assertEquals(0, rs.getRow());

    }
  }

  private int countMatchingPrepStmts(Statement stmt) throws Exception {
    ResultSet rs = stmt.executeQuery(QUERY_ALL_PREP_STMTS);
    int count = 0;
    while (rs.next()) {
      String statement = rs.getString("statement");
      if (statement.equals(SELECT_QUERY1) ||
          statement.equals(SELECT_QUERY2) ||
          statement.equals(SELECT_QUERY3)) count++;
    }
    return count;
  }

  private void reExecutePrepStmts(PreparedStatement pstmt1,
      PreparedStatement pstmt2, PreparedStatement pstmt3) throws Exception {
    pstmt1.execute();
    pstmt2.execute();
    pstmt3.execute();
  }

  // Verifies that DEALLOCATE drops all invalid protocol-level prepared
  // statements and preserves valid ones across four plan-invalidation
  // triggers. Each round follows the same pattern: confirm DEALLOCATE of an
  // unknown name is a no-op while plans are valid, trigger invalidation,
  // then confirm DEALLOCATE drops the now-invalid plans and re-execution
  // re-creates them.
  //
  // Invalidation triggers tested (in order):
  //   1. CREATE TEMP TABLE: adds pg_temp to the effective search_path.
  //   2. CREATE SCHEMA: catalog sinval marks all plans invalid.
  //   3. SET search_path: direct search_path change detected by
  //                        OverrideSearchPathMatchesCurrent.
  //   4. DROP SCHEMA: removes a schema from the effective search_path,
  //                    detected by OverrideSearchPathMatchesCurrent.
  @Test
  public void testDeallocateDueToPlanInvalidation() throws Exception {

    Properties props = new Properties();
    props.setProperty("prepareThreshold", "1");
    setUpTestTableAndCleanBackends();
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect(props);
        Statement stmt = conn.createStatement()) {

      PreparedStatement pstmt1 = conn.prepareStatement(SELECT_QUERY1);
      PreparedStatement pstmt2 = conn.prepareStatement(SELECT_QUERY2);
      PreparedStatement pstmt3 = conn.prepareStatement(SELECT_QUERY3);
      reExecutePrepStmts(pstmt1, pstmt2, pstmt3);
      assertEquals("Initial prepared statements", 3,
                    countMatchingPrepStmts(stmt));

      // Valid plans: DEALLOCATE of unknown name should be a no-op.
      stmt.execute("DEALLOCATE RandomName");
      assertEquals("Valid plans should survive DEALLOCATE", 3,
                    countMatchingPrepStmts(stmt));

      // Round 1: CREATE TEMP TABLE adds pg_temp to search_path.
      stmt.execute("CREATE TEMP TABLE test_table(id int)");
      stmt.execute("DEALLOCATE RandomName");
      assertEquals("Plans should be dropped after CREATE TEMP TABLE", 0,
                    countMatchingPrepStmts(stmt));

      reExecutePrepStmts(pstmt1, pstmt2, pstmt3);
      assertEquals("Plans should be re-created after re-execution", 3,
                    countMatchingPrepStmts(stmt));
    }

    // Round 2: CREATE SCHEMA triggers catalog sinval.
    try (Connection conn = getConnectionBuilder()
          .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
          .connect(props);
      Statement stmt = conn.createStatement()) {

      PreparedStatement pstmt1 = conn.prepareStatement(SELECT_QUERY1);
      PreparedStatement pstmt2 = conn.prepareStatement(SELECT_QUERY2);
      PreparedStatement pstmt3 = conn.prepareStatement(SELECT_QUERY3);
      reExecutePrepStmts(pstmt1, pstmt2, pstmt3);


      stmt.execute("CREATE SCHEMA test_schema");
      stmt.execute("DEALLOCATE RandomName");
      assertEquals("Plans should be dropped after CREATE SCHEMA", 0,
                    countMatchingPrepStmts(stmt));

      reExecutePrepStmts(pstmt1, pstmt2, pstmt3);
      assertEquals("Plans should be re-created after re-execution", 3,
                    countMatchingPrepStmts(stmt));

      // Round 3: SET search_path changes the effective namespace path.
      stmt.execute("SET search_path TO test_schema, public");
      stmt.execute("DEALLOCATE RandomName");
      assertEquals("Plans should be dropped after SET search_path", 0,
                    countMatchingPrepStmts(stmt));

      reExecutePrepStmts(pstmt1, pstmt2, pstmt3);
      assertEquals("Plans should be re-created after re-execution", 3,
                    countMatchingPrepStmts(stmt));

      // Round 4: DROP SCHEMA removes a schema from the effective path.
      stmt.execute("DROP SCHEMA test_schema CASCADE");
      stmt.execute("DEALLOCATE RandomName");
      assertEquals("Plans should be dropped after DROP SCHEMA", 0,
                    countMatchingPrepStmts(stmt));
    }
  }

  // Verifies deallocation (via CLOSE) and re-prepare of same name prepared
  // statement before SYNC is a successful operation with conneciton manager.
  // Along with it, verifies if error come in between, conn mgr state should
  // be restored correctly.
  //
  // Wire sequence:
  //   Pipeline 1: Parse(s1, "SELECT 1") + Bind(s1) + Execute + Sync
  //   Pipeline 2: Close(s1) + Parse(s1, "SELECT 1") + Bind(s1) + Execute + Sync
  //   Pipeline 3: Bind(s1) + Execute + Sync
  //   Pipeline 4: Close(s1) + Parse(bad SQL) + Parse(s1) + Sync
  //   Pipeline 5: Parse(s1) + Bind(s1) + Execute + Sync
  //
  // Without the close-hashmap fix, the Close in pipeline 2 would evict the
  // server-hashmap entry, causing the subsequent Bind in pipeline 3 to fail
  // with "operator was not prepared by this client".
  @Test
  public void testCloseThenReparseSameStmtUsesDeferredEviction() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);
    LOG.info("Connecting raw socket to Odyssey at " + addr);

    final int SOCKET_TIMEOUT_MS = 10000;

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

      // Pipeline 1: prime s1 on the backend.
      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("s1", "SELECT 1", new int[0]));
      pipeline.write(buildBind("s1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 1: P(s1)+B(s1)+E+Sync");

      char[] expected = {
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 1 [" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 1: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 1 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 1",
          0, in.available());

      // Invalidate s1's cached plan via a search_path change. So
      // pipeline 2 CLOSE could actually deallocate the prep stmt
      // as plan would become invalid.
      out.write(buildQuery("SET search_path TO pg_catalog, public"));
      out.flush();
      LOG.info("Sent: SET search_path TO pg_catalog, public");
      for (;;) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("SET response: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error setting search_path: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        if (msg.type == BE_READY_FOR_QUERY)
          break;
      }
      assertEquals("Unexpected trailing bytes after SET",
          0, in.available());

      // Pipeline 2: Close(s1) + re-Parse(s1) + Bind(s1) + Execute + Sync.
      // Conn mgr would forward force parse on receiving parse packet after
      // close packet.
      // The customCloseComplete would defer the removal of entry from server
      // hashmap until RFQ by adding it in close hashmap. The
      // forceParseComplete would remove the entry from close hashmap to avoid
      // deleting the entry from server hashmap.
      pipeline.reset();
      pipeline.write(buildClosePreparedStatement("s1"));
      pipeline.write(buildParse("s1", "SELECT 1", new int[0]));
      pipeline.write(buildBind("s1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 2: Close(s1)+P(s1)+B(s1)+E+Sync");

      expected = new char[] {
          BE_CLOSE_COMPLETE,
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 2 [" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 2: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 2 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 2",
          0, in.available());

      // Pipeline 3: Bind(s1) + Execute + Sync.
      // s1 must still be live on the backend because the deferred close was
      // cancelled by the NoParseParseComplete in pipeline 2.
      pipeline.reset();
      pipeline.write(buildBind("s1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 3: B(s1)+E+Sync");

      expected = new char[] {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 3 [" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 3 (deferred close was not "
              + "cancelled): " + new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 3 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 3",
          0, in.available());

      // Invalidate s1's cached plan again before pipeline 4.
      out.write(buildQuery("SET search_path TO public, pg_catalog"));
      out.flush();
      LOG.info("Sent: SET search_path TO public, pg_catalog (pre-pipeline 4)");
      for (;;) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("SET response (pre-p4): " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error setting search_path before pipeline 4: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        if (msg.type == BE_READY_FOR_QUERY)
          break;
      }
      assertEquals("Unexpected trailing bytes after SET (pre-pipeline 4)",
          0, in.available());

      // Pipeline 4: Close(s1) + Parse(bad SQL) + Parse(s1) + Sync.
      // Close(s1), backend sends CloseComplete, Odyssey defers eviction
      //                   into yb_close_prep_stmts.
      // Parse(bad SQL): backend returns ErrorResponse; so all subsequent messages
      //                   are discarded by the backend until Sync.
      // Expected wire responses: CloseComplete, ErrorResponse, ReadyForQuery.
      pipeline.reset();
      pipeline.write(buildClosePreparedStatement("s1"));
      pipeline.write(buildParse("bad_stmt", "THIS IS NOT VALID SQL $$$$", new int[0]));
      pipeline.write(buildParse("s1", "SELECT 1", new int[0]));
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 4: Close(s1)+P(bad)+P(s1)+Sync");

      expected = new char[] {
          BE_CLOSE_COMPLETE,
          BE_ERROR_RESPONSE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 4 [" + i + "]: " + msg);
        if (i != 1 && msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 4 at position " + i + ": " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 4 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 4",
          0, in.available());

      // Pipeline 5: Parse(s1) + Bind(s1) + Execute + Sync.
      pipeline.reset();
      pipeline.write(buildParse("s1", "SELECT 1", new int[0]));
      pipeline.write(buildBind("s1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 5: P(s1)+B(s1)+E+Sync");

      expected = new char[] {
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 5 [" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 5 (s1 was not re-parsed after "
              + "drain): " + new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 5 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 5",
          0, in.available());

      out.write(buildTerminate());
      out.flush();
    }
  }

  // Verifies deallocation (via DEALLOCATE ALL) and re-prepare of same name
  // prepared statement before SYNC is a successful operation with connection
  // manager.
  // Wire sequence:
  //   Pipeline 1 : Parse(s1,"SELECT 1") + Bind(s1) + Execute + Sync
  //   SET search_path (invalidates s1's cached plan)
  //   Pipeline 2 : Parse(d,"DEALLOCATE ALL") + Bind(d) + Execute
  //              + Parse(s1,"SELECT 1") + Bind(s1) + Execute + Sync
  //   Pipeline 3 : Bind(s1) + Execute + Sync
  @Test
  public void testDeallocateAllThenReparseSameStmt() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
    tserverFlags.put("ysql_conn_mgr_log_settings", "log_query,log_debug");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

    InetSocketAddress addr = miniCluster.getYsqlConnMgrContactPoints().get(0);
    LOG.info("Connecting raw socket to Odyssey at " + addr);

    final int SOCKET_TIMEOUT_MS = 10000;

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

      // Pipeline 1: prime s1 on the backend.
      ByteArrayOutputStream pipeline = new ByteArrayOutputStream();
      pipeline.write(buildParse("s1", "SELECT 1", new int[0]));
      pipeline.write(buildBind("s1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 1: P(s1)+B(s1)+E+Sync");

      char[] expected = {
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 1 [" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 1: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 1 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 1",
          0, in.available());

      // Invalidate s1's cached plan so that the backend will re-plan when it
      // re-creates the statement after the DEALLOCATE ALL.
      out.write(buildQuery("SET search_path TO pg_catalog, public"));
      out.flush();
      LOG.info("Sent: SET search_path TO pg_catalog, public");
      for (;;) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("SET response: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error setting search_path: " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        if (msg.type == BE_READY_FOR_QUERY)
          break;
      }
      assertEquals("Unexpected trailing bytes after SET",
          0, in.available());

      // Pipeline 2: DEALLOCATE ALL (via extended protocol) followed immediately
      // by a re-Parse and re-Bind+Execute of s1 in the same pipeline.
      pipeline.reset();
      pipeline.write(buildParse("d", "DEALLOCATE ALL", new int[0]));
      pipeline.write(buildBind("d", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildParse("s1", "SELECT 1", new int[0]));
      pipeline.write(buildBind("s1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 2: P(d)+B(d)+E+P(s1)+B(s1)+E+Sync");

      expected = new char[] {
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_COMMAND_COMPLETE,
          BE_PARSE_COMPLETE,
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 2 [" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 2 (deferred eviction after "
              + "DEALLOCATE ALL was not cancelled): " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 2 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 2",
          0, in.available());

      // Pipeline 3: Bind(s1) + Execute + Sync.
      // s1 was re-created in pipeline 2 (deferred eviction cancelled), so a
      // bare Bind must succeed without any prior Parse in this pipeline.
      pipeline.reset();
      pipeline.write(buildBind("s1", new String[0]));
      pipeline.write(buildExecute());
      pipeline.write(buildSync());
      out.write(pipeline.toByteArray());
      out.flush();
      LOG.info("Sent pipeline 3: B(s1)+E+Sync");

      expected = new char[] {
          BE_BIND_COMPLETE,
          BE_DATA_ROW,
          BE_COMMAND_COMPLETE,
          BE_READY_FOR_QUERY,
      };
      for (int i = 0; i < expected.length; i++) {
        PgMessage msg = readMessageSkipNotice(in);
        LOG.info("pipeline 3 [" + i + "]: " + msg);
        if (msg.type == BE_ERROR_RESPONSE) {
          fail("Unexpected error in pipeline 3 (s1 not live after DEALLOCATE ALL "
              + "deferred eviction was cancelled): " +
              new String(msg.body, StandardCharsets.UTF_8));
        }
        assertEquals("pipeline 3 type mismatch at " + i,
            expected[i], msg.type);
      }
      assertEquals("Unexpected trailing bytes after pipeline 3",
          0, in.available());

      out.write(buildTerminate());
      out.flush();
    }
  }
}
