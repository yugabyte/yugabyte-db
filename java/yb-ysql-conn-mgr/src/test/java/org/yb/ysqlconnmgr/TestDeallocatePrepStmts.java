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

import java.sql.*;
import java.util.Properties;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
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
}
