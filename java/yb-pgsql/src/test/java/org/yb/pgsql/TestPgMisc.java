// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.util.*;

import org.junit.Test;
import org.junit.runner.RunWith;
import com.yugabyte.util.PSQLException;
import com.yugabyte.util.PSQLWarning;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.minicluster.YsqlSnapshotVersion;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.Statement;
import java.sql.SQLWarning;

@RunWith(value=YBTestRunner.class)
public class TestPgMisc extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestPgMisc.class);

  @Override
  protected void resetSettings() {
    super.resetSettings();
    // Starts CQL proxy for the cross Postgres/CQL testNamespaceSeparation test case.
    startCqlProxy = true;
  }

  @Test
  public void testTemplateConnectionWithOldInitdb() throws Exception {
    recreateWithYsqlVersion(YsqlSnapshotVersion.EARLIEST);

    ConnectionBuilder templateCb =
        getConnectionBuilder().withTServer(0).withDatabase("template1");

    // Testing that first connection (that creates a relcache init file)
    // and second one (that reads it) both work.
    for (int i = 0; i < 2; ++i) {
      try (Connection conn = templateCb.connect();
           Statement stmt = conn.createStatement()) {
        // Any query would do.
        assertGreaterThan(getSingleRow(stmt, "SELECT COUNT(*) FROM pg_class").getLong(0), 0L);
      }
    }
  }

  @Test
  public void testTableCreationInTemplate() throws Exception {
    try {
      executeQueryInTemplate("CREATE TABLE test (a int);");
      fail("Table created in template");
    } catch(PSQLException e) {
    }
  }

  @Test
  public void testTableCreationAsInTemplate() throws Exception {
    try {
      executeQueryInTemplate("CREATE TABLE test AS SELECT 1;");
      fail("Table created in template");
    } catch(PSQLException e) {
    }
  }

  @Test
  public void testSequenceCreationInTemplate() throws Exception {
    try {
      executeQueryInTemplate("CREATE SEQUENCE test START 101;");
      fail("Sequence created in template");
    } catch(PSQLException e) {
    }
  }

  private void executeVacuumTest(Statement statement, String sql) throws Exception {
    String EXPECTED_NOTICE_STRING =
        "VACUUM is a no-op statement since YugabyteDB performs garbage collection " +
        "of dead tuples automatically";
    statement.execute(sql);
    SQLWarning notice = statement.getWarnings();
    assertNotNull("Vacuum executed without notice", notice);
    assertTrue(String.format("Unexpected Notice Message. Got: '%s', expected to contain: '%s'",
                            notice.getMessage(),
                            EXPECTED_NOTICE_STRING),
              notice.getMessage().equals(EXPECTED_NOTICE_STRING));
    assertNull("Unexpected additional warning", notice.getNextWarning());
  }

  @Test
  public void testVacuum() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TEMP TABLE test_table(a int);");
      executeVacuumTest(statement, "VACUUM;");
      executeVacuumTest(statement, "VACUUM test_table;");
      executeVacuumTest(statement, "VACUUM FULL");
      executeVacuumTest(statement, "VACUUM VERBOSE test_table;");
      executeVacuumTest(statement, "VACUUM ANALYZE test_table;");
    } catch (PSQLException e) {
      fail("Vacuum executed with exception");
    }
  }

  @Test
  public void testTemporaryTableAnalyze() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TEMP TABLE test_table(a int);");
      statement.execute("ANALYZE test_table;");
      if (statement.getWarnings() != null) {
        throw statement.getWarnings();
      }
    } catch(PSQLException e) {
      fail("Analyze executed with exception");
    } catch(PSQLWarning w) {
      fail("Analyze executed with warning");
    }
  }

  /*
   * Test for CHECKPOINT no-op functionality
   */
  @Test
  public void testCheckpoint() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CHECKPOINT");
      if (statement.getWarnings() != null) {
        throw statement.getWarnings();
      }
      fail("Checkpoint executed without warnings");
    } catch(PSQLWarning w) {
    }
  }

  @Test
  public void testTemporaryTableTransactionInExecute() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TEMP TABLE test_table(a int, b int, c int, PRIMARY KEY (a))");

      // Can insert using JDBC prepared statement.
      statement.execute("INSERT INTO test_table(a, b, c) VALUES (1, 2, 3)");

      // Can insert explicitly prepared statement.
      statement.execute("PREPARE ins AS INSERT INTO test_table(a, b, c) VALUES (2, 3, 4)");
      statement.execute("EXECUTE ins");
    }
  }

  @Test
  public void testTemporaryTableTransactionInProcedure() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TEMP TABLE test_table(k int PRIMARY KEY)");
      stmt.execute("CREATE PROCEDURE test_temp_table(k int) AS $$ " +
        "BEGIN" +
        "  INSERT INTO test_table VALUES(k);" +
        "END; $$ LANGUAGE 'plpgsql';");
      stmt.execute("CALL test_temp_table(1)");
      stmt.execute("CALL test_temp_table(2)");
      // Can insert explicitly prepared statement.
      assertOneRow(stmt, "SELECT COUNT(*) FROM test_table", 2);
    }
  }

  private void executeQueryInTemplate(String query) throws Exception {
    try (Connection connection = getConnectionBuilder().withTServer(0).withDatabase("template1")
        .connect();
        Statement statement = connection.createStatement()) {
      statement.execute(query);
    }
  }

  /*
   * Test to make sure that pg_hint_plan works consistently with interleaved extended queries.
   * See issue #12741.
   */
  @Test
  public void testPgHintPlanExtendedQuery() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.executeUpdate("CREATE TABLE test_table(r1 int, r2 int," +
                              "PRIMARY KEY(r1 asc, r2 asc))");
      statement.executeUpdate("CREATE INDEX bad_index on test_table(r1 asc)");

      String HINT_QUERY = "/*+IndexScan(test_table bad_index)*/ " +
                          "SELECT count(*) FROM test_table " +
                          "WHERE r1 <= 20 AND r2 <= ?";
      PreparedStatement withhint = connection.prepareStatement(HINT_QUERY);
      // Make sure we're always preparing statements at the server
      com.yugabyte.jdbc.PgStatement pgstmt = (com.yugabyte.jdbc.PgStatement) withhint;
      pgstmt.setPrepareThreshold(1);

      // Make sure we set pg_hint_plan debug statements to WARNING so we can detect them
      // here
      statement.executeUpdate("SET pg_hint_plan.enable_hint TO ON;");
      statement.executeUpdate("SET pg_hint_plan.debug_print TO ON;");
      statement.executeUpdate("SET pg_hint_plan.message_level TO WARNING");

      String EXPECTED_AVAILABLE_IDX_STRING = "available indexes for IndexScan(test_table): " +
                                             "bad_index";
      String EXPECTED_HINT_DEBUG_STRING = "pg_hint_plan:\n" +
                                          "used hint:\n" +
                                          "IndexScan(test_table bad_index)\n" +
                                          "not used hint:\n" +
                                          "duplication hint:\n" +
                                          "error hint:\n";

      withhint.setInt(1, 28);
      withhint.executeQuery();

      // The following checks are to make sure the hints were processed
      SQLWarning warning = withhint.getWarnings();
      assertTrue("Expected a SQL warning for hints", warning != null);
      assertTrue(String.format("Unexpected Warning Message. Got: '%s', expected to contain : '%s",
                               warning.getMessage(),
                               EXPECTED_AVAILABLE_IDX_STRING),
                 warning.getMessage().equals(EXPECTED_AVAILABLE_IDX_STRING));
      warning = warning.getNextWarning();
      assertTrue("Expected a SQL warning for hints", warning != null);
      assertTrue(String.format("Unexpected Warning Message. Got: '%s', expected to contain : '%s",
                               warning.getMessage(),
                               EXPECTED_HINT_DEBUG_STRING),
                 warning.getMessage().equals(EXPECTED_HINT_DEBUG_STRING));
      withhint.clearWarnings();

      // Execute a query with no hint. This should not affect hint processing for
      // subsequent queries.
      statement.executeQuery("SELECT 1;");
      withhint.clearWarnings();

      // This query should still process hints despite the preceding hintless query.
      withhint.setInt(1, 30);
      withhint.executeQuery();

      warning = withhint.getWarnings();
      assertTrue("Expected a SQL warning for hints", warning != null);
      assertTrue(String.format("Unexpected Warning Message. Got: '%s', expected to contain : '%s",
                               warning.getMessage(),
                               EXPECTED_AVAILABLE_IDX_STRING),
                 warning.getMessage().equals(EXPECTED_AVAILABLE_IDX_STRING));
      warning = warning.getNextWarning();
      assertTrue("Expected a SQL warning for hints", warning != null);
      assertTrue(String.format("Unexpected Warning Message. Got: '%s', expected to contain : '%s",
                               warning.getMessage(),
                               EXPECTED_HINT_DEBUG_STRING),
                 warning.getMessage().equals(EXPECTED_HINT_DEBUG_STRING));

      statement.executeUpdate("DROP TABLE test_table;");
    }
  }

  /*
   * Test to make sure that batched nested loops joins work with extended queries over
   * multiple invocations.
   * See issue #14278.
   */
  @Test
  public void testBatchedNestLoopExtendedQuery() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.executeUpdate("CREATE TABLE test_table1(r1 int, r2 int," +
                              "PRIMARY KEY(r1 asc))");
      statement.executeUpdate("CREATE TABLE test_table2(r1 int, r2 int," +
                              "PRIMARY KEY(r1 asc))");

      String QUERY = "/*+Set(enable_hashjoin off) Set(enable_mergejoin off) " +
                     "Set(yb_bnl_batch_size 4) Set(enable_seqscan off) " +
                     "Set(enable_material off)*/ " +
                     "SELECT * FROM test_table1, test_table2 " +
                     "WHERE test_table1.r1 = test_table2.r1";
      PreparedStatement stmt = connection.prepareStatement(QUERY);
      // Make sure we're always preparing statements at the server
      com.yugabyte.jdbc.PgStatement pgstmt = (com.yugabyte.jdbc.PgStatement) stmt;
      pgstmt.setPrepareThreshold(1);

      stmt.executeQuery();
      stmt.executeQuery();
      stmt.executeQuery();

      statement.executeUpdate("DROP TABLE test_table1;");
      statement.executeUpdate("DROP TABLE test_table2;");
    }
  }
}
