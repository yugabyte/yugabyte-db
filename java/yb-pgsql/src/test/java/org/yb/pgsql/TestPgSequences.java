// Copyright (c) YugabyteDB, Inc.
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

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.*;
import java.util.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgSequences extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSequences.class);

  private static final int TURN_OFF_SEQUENCE_CACHE_FLAG = 0;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_sequence_cache_minval", Integer.toString(TURN_OFF_SEQUENCE_CACHE_FLAG));
    return flagMap;
  }

  protected Connection getConnectionWithNewCache() throws Exception {
    return getConnectionBuilder().connect();
  }

  protected String getYsqlSequenceCacheMethod() {
    String mode = getTServerFlags().get("ysql_sequence_cache_method");
    return mode != null ? mode : "connection";
  }

  @After
  public void deleteSequences() throws Exception {
    if (connection == null) {
      LOG.warn("No connection created, skipping dropping sequences");
      return;
    }
    try (Statement statement = connection.createStatement()) {
      statement.execute("DROP SEQUENCE s1 CASCADE");
      statement.execute("DROP SEQUENCE s2 CASCADE");
    } catch (Exception e) {
      LOG.info("Exception while dropping sequences s1 and s2", e);
    }
  }

  @Test
  public void testSequencesSimple() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1");
      ResultSet rs = statement.executeQuery("SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(1, rs.getInt("nextval"));
    }

    try (Connection connection2 = getConnectionBuilder().connect();
        Statement statement = connection2.createStatement()) {
      ResultSet rs = statement.executeQuery("SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(2, rs.getInt("nextval"));
    }
  }

  @Test
  public void testSequencesWithCacheAndIncrement() throws Exception {

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1 CACHE 50 INCREMENT 3");
      for (int i = 1; i <= 21; i+=3) {
        ResultSet rs = statement.executeQuery("SELECT nextval('s1')");
        assertTrue(rs.next());
        assertEquals(i, rs.getInt("nextval"));
      }
    }

    try (Connection connection2 = getConnectionWithNewCache();
        Statement statement = connection2.createStatement()) {
      ResultSet rs = statement.executeQuery("SELECT nextval('s1')");
      assertTrue(rs.next());
      // The previous session should have allocated 50 values: 1, 4, 7, 10 ... 145, 148. So the next
      // value should be 151.
      assertEquals(151, rs.getInt("nextval"));
    }
  }

  @Test
  public void testCurrval() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1");
      statement.execute("SELECT nextval('s1')");
      ResultSet rs = statement.executeQuery("SELECT currval('s1')");
      assertTrue(rs.next());
      assertEquals(1, rs.getLong("currval"));
    }

    try (Connection connection2 = getConnectionBuilder().connect();
        Statement statement = connection2.createStatement()) {
      statement.execute("SELECT nextval('s1')");
      ResultSet rs = statement.executeQuery("SELECT currval('s1')");
      assertTrue(rs.next());
      assertEquals(2, rs.getLong("currval"));
    }
  }

  @Test
  public void testNextValAsDefaultValueInTable() throws Exception {

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1 CACHE 20");
      statement.execute("CREATE TABLE t(k int NOT NULL DEFAULT nextval('s1'), v int)");
      for (int k = 1; k <= 10; k++) {
        statement.execute("INSERT INTO t(v) VALUES (10)");
        ResultSet rs = statement.executeQuery("SELECT * FROM t WHERE k = " + k);
        assertTrue(rs.next());
      }
    }

    try (Connection connection2 = getConnectionWithNewCache();
        Statement statement = connection2.createStatement()) {
      // Because of our current implementation, the first value is 22 for now instead of 21.
      for (int k = 21; k <= 30; k++) {
        statement.execute("INSERT INTO t(v) VALUES (10)");
        ResultSet rs = statement.executeQuery("SELECT * FROM t WHERE k = " + k);
        assertTrue(rs.next());
      }
    }
  }

  @Test
  public void testAlterSequenceRestart() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1");

      ResultSet rs = statement.executeQuery("SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(1, rs.getLong("nextval"));

      statement.execute("ALTER SEQUENCE s1 RESTART WITH 100");
    }

    try (Connection connection2 = getConnectionBuilder().connect();
        Statement statement = connection2.createStatement()) {
      ResultSet rs = statement.executeQuery("SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(100, rs.getLong("nextval"));
    }
  }

  // ALTER SEQUENCE ... RESTART and setval() must be reflected by the next nextval() on the same
  // connection, regardless of the cache method: any values cached ahead (in the connection or,
  // with ysql_sequence_cache_method=server, on the TServer) must be dropped. A large CACHE forces
  // a block to be cached, which is what makes a stale cache observable.
  @Test
  public void testCacheInvalidationOnRestartAndSetval() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1 CACHE 100");
      assertOneRow(statement, "SELECT nextval('s1')", 1);

      // ALTER SEQUENCE ... RESTART must invalidate the cache so the next value restarts at 1.
      statement.execute("ALTER SEQUENCE s1 RESTART");
      assertOneRow(statement, "SELECT nextval('s1')", 1);

      // RESTART WITH resumes from the specified value.
      statement.execute("ALTER SEQUENCE s1 RESTART WITH 50");
      assertOneRow(statement, "SELECT nextval('s1')", 50);
      assertOneRow(statement, "SELECT nextval('s1')", 51);

      // setval() (is_called defaults to true) must invalidate the cache: next value is set + 1.
      assertOneRow(statement, "SELECT setval('s1', 10)", 10);
      assertOneRow(statement, "SELECT nextval('s1')", 11);

      // setval() with is_called = false: the next value is exactly the value that was set.
      assertOneRow(statement, "SELECT setval('s1', 100, false)", 100);
      assertOneRow(statement, "SELECT nextval('s1')", 100);
      assertOneRow(statement, "SELECT nextval('s1')", 101);
    }
  }

  @Test
  public void testMetadataOnlyAlterCacheEffect() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1 CACHE 100");
      assertOneRow(statement, "SELECT nextval('s1')", 1);
      assertOneRow(statement, "SELECT nextval('s1')", 2);

      statement.execute("ALTER SEQUENCE s1 MAXVALUE 1000000");
      // Connection mode: Postgres drops the backend cache on any ALTER, so the next
      // value comes from a freshly reserved block (first nextval reserved [1..100],
      // hence 101). Server mode: a metadata-only ALTER does not rewrite the durable
      // row, so the TServer cache is left in place and nextval continues in the
      // cached block (3).
      final long nextvalExpected = "server".equals(getYsqlSequenceCacheMethod()) ? 3 : 101;
      assertOneRow(statement, "SELECT nextval('s1')", nextvalExpected);

      statement.execute("DROP SEQUENCE s1");
      statement.execute("CREATE SEQUENCE s1 CACHE 100");
      assertOneRow(statement, "SELECT nextval('s1')", 1);
      assertOneRow(statement, "SELECT nextval('s1')", 2);

      // Raise MINVALUE (default is 1); START must be raised too (catalog check).
      // Connection mode re-fetches and returns 101 (> 50). Server mode keeps the
      // stale block and returns 3 (< 50) until metadata-only ALTER invalidation.
      statement.execute("ALTER SEQUENCE s1 MINVALUE 50 START WITH 50");
      assertOneRow(statement, "SELECT nextval('s1')", nextvalExpected);

      // TRUNCATE ... RESTART IDENTITY rewrites the durable row (ResetSequence), so
      // both cache methods must restart at 1 rather than continue the cached block.
      statement.execute(
          "CREATE TABLE t1 (k bigint GENERATED BY DEFAULT AS IDENTITY (CACHE 100), v int)");
      statement.execute("INSERT INTO t1(v) VALUES (1)");
      statement.execute("INSERT INTO t1(v) VALUES (1)");
      statement.execute("TRUNCATE TABLE t1 RESTART IDENTITY");
      statement.execute("INSERT INTO t1(v) VALUES (1)");
      assertOneRow(statement, "SELECT k FROM t1", 1);
      statement.execute("DROP TABLE t1");
    }
  }

  // Demonstrates that ALTER SEQUENCE ... INCREMENT must drop cached values (Postgres does this
  // for the backend cache). With ysql_sequence_cache_method=server the TServer cache is not
  // invalidated today, so nextval keeps walking the old block with the new step.
  @Test
  public void testIncrementAlterCacheEffect() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE SEQUENCE s1 CACHE 100");
      assertOneRow(statement, "SELECT nextval('s1')", 1);
      assertOneRow(statement, "SELECT nextval('s1')", 2);

      statement.execute("ALTER SEQUENCE s1 INCREMENT BY 10");
      // Connection mode: backend cache is dropped on any ALTER, then re-fetch from
      // DocDB: last_value is 100, INCREMENT 10 => 110. Server mode (TOFIX): the
      // TServer cache is not invalidated, so nextval walks the old block with the
      // new step (3). Flip the server expectation to 110 when metadata-only ALTER
      // invalidation is implemented.
      assertOneRow(statement, "SELECT nextval('s1')",
          "server".equals(getYsqlSequenceCacheMethod()) ? 3 : 110);
    }
  }

  // Retries once if we get error "Catalog Version Mismatch: A DDL occurred while processing this
  // query"
  private void ExecuteWithRetry(Statement statement, String stmt) throws Exception {
    for (int i = 0; i < 2; i++) {
      try {
        statement.execute(stmt);
        return;
      } catch (Exception e) {
        if (e.toString().contains("Catalog Version Mismatch: A DDL occurred while processing")) {
          continue;
        } else {
          throw e;
        }
      }
    }
    throw new RuntimeException("Failed to execute statement: " + stmt);
  }

  // Retries once if we get error "Catalog Version Mismatch: A DDL occurred while processing this
  // query"
  private ResultSet ExecuteQueryWithRetry(Statement statement, String query) throws Exception {
    for (int i = 0; i < 2; i++) {
      try {
        return statement.executeQuery(query);
      } catch (Exception e) {
        if (e.toString().contains("Catalog Version Mismatch: A DDL occurred while processing")) {
          continue;
        } else {
          throw e;
        }
      }
    }
    throw new RuntimeException("Failed to execute query");
  }

  private void WaitUntilTServerGetsNewYSqlCatalogVersion() throws Exception {
    // ysql_catalog_version gets propagated through the heartbeat. Wait at least one heartbeat
    // (500 ms set through flag raft_heartbeat_interval_ms) period to give TS2 enough time to
    // realize that its cache is invalid.
    waitForTServerHeartbeat();
  }

  @Test
  public void testAlterSequence() throws Exception {
    try (Statement statement = connection.createStatement();
        Connection connection2 = getConnectionBuilder().withTServer(1).connect();
        Statement statement2 = connection2.createStatement()) {

      statement.execute("CREATE SEQUENCE s1");
      ResultSet rs = ExecuteQueryWithRetry(statement, "SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(1, rs.getLong("nextval"));

      // -------------------------------------------------------------------------------------------
      // Test INCREMENT option.
      // -------------------------------------------------------------------------------------------
      statement.execute("ALTER SEQUENCE s1 INCREMENT 100");

      rs = ExecuteQueryWithRetry(statement2, "SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(101, rs.getLong("nextval"));

      // -------------------------------------------------------------------------------------------
      // Test CACHE option.
      // -------------------------------------------------------------------------------------------
      statement.execute("ALTER SEQUENCE s1 CACHE 5");
      rs = ExecuteQueryWithRetry(statement, "SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(201, rs.getLong("nextval"));

      // CACHE is 5 elements. The previous request should have cached 201, 301, 401, 501, and 601.
      WaitUntilTServerGetsNewYSqlCatalogVersion();
      rs = ExecuteQueryWithRetry(statement2, "SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(701, rs.getLong("nextval"));

      // -------------------------------------------------------------------------------------------
      // Test RESTART option.
      // -------------------------------------------------------------------------------------------
      ExecuteWithRetry(statement, "ALTER SEQUENCE s1 RESTART CACHE 1");

      // Consume the rest of the numbers in the cache: 801, 901, 1001, 1101.
      for (int i = 0; i < 4; i++) {
        rs = statement2.executeQuery("SELECT nextval('s1')");
        assertTrue(rs.next());
        assertEquals(801 + i * 100, rs.getLong("nextval"));
      }

      // After all the elements in the cache have been used, the next value should be 1 again
      // because the sequence was restarted.
      WaitUntilTServerGetsNewYSqlCatalogVersion();
      rs = ExecuteQueryWithRetry(statement2,"SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(1, rs.getLong("nextval"));

      // -------------------------------------------------------------------------------------------
      // Test RESTART WITH option.
      // -------------------------------------------------------------------------------------------
      statement.execute("ALTER SEQUENCE s1 RESTART WITH 9");

      WaitUntilTServerGetsNewYSqlCatalogVersion();
      rs = ExecuteQueryWithRetry(statement2,"SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(9, rs.getLong("nextval"));

      // -------------------------------------------------------------------------------------------
      // Test START WITH option.
      // -------------------------------------------------------------------------------------------
      ExecuteWithRetry(statement2, "ALTER SEQUENCE s1 START WITH 1000");

      // After RESTART the sequence should start with 1000 that was set by the previous statement.
      WaitUntilTServerGetsNewYSqlCatalogVersion();
      ExecuteWithRetry(statement, "ALTER SEQUENCE s1 RESTART");

      WaitUntilTServerGetsNewYSqlCatalogVersion();
      rs = ExecuteQueryWithRetry(statement2, "SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(1000, rs.getLong("nextval"));

      // -------------------------------------------------------------------------------------------
      // Test CYCLE option.
      // -------------------------------------------------------------------------------------------
      ExecuteWithRetry(statement, "ALTER SEQUENCE s1 RESTART WITH 1 INCREMENT -1 CACHE 1");
      rs = ExecuteQueryWithRetry(statement2,"SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(1, rs.getLong("nextval"));

      WaitUntilTServerGetsNewYSqlCatalogVersion();
      // Verify that getting next value without CYCLE fails.
      try {
        rs = ExecuteQueryWithRetry(statement2,"SELECT nextval('s1')");
        fail("Expected exception but got none");
      } catch (Exception e) {
        assertTrue(e.getMessage().contains("reached minimum value of sequence \"s1\" (1)"));
      }

      // Alter sequence to add CYCLE option.
      statement2.execute("ALTER SEQUENCE s1 CYCLE");

      WaitUntilTServerGetsNewYSqlCatalogVersion();
      rs = ExecuteQueryWithRetry(statement2,"SELECT nextval('s1')");
      assertTrue(rs.next());
      assertEquals(Long.MAX_VALUE, rs.getLong("nextval"));
    }
  }

  /**
   * Create use and drop a sequence, verifying that generated numbers are sequential, hence
   * confirming there is no interference with concurrent activities in the cluster.
   */
  private class TestSequenceIsolation extends Thread {
    private static final String SEQ_NAME = "myseq1";
    private static final int NEXT_COUNT = 997;

    private String dbName;
    private volatile boolean success = false;

    public TestSequenceIsolation(String dbName) {
      this.dbName = dbName;
    }

    @Override
    public void run() {
      try (Connection conn = getConnectionBuilder().withDatabase(dbName).connect();
           Statement stmt = conn.createStatement()) {
        stmt.execute(String.format("CREATE SEQUENCE %s CACHE 100", SEQ_NAME));
        for (int i = 1; i <= NEXT_COUNT; i++) {
          ResultSet rs = stmt.executeQuery(String.format("SELECT nextval('%s')", SEQ_NAME));
          assertTrue(rs.next());
          assertEquals(i, rs.getInt(1));
          assertFalse(rs.next());
        }
        stmt.execute(String.format("DROP SEQUENCE %s", SEQ_NAME));
        success = true;
      } catch (Exception ex) {
        LOG.error(String.format("Failed sequence isolation test for database %s: ", dbName), ex);
      }
    }

    public boolean isSucceeded() throws InterruptedException {
      join();
      return success;
    }

  }

  /**
   * Test that sequences in different databases can be used independently from each other.
   * @throws Exception
   */
  @Test
  public void testMultiDbIsolation() throws Exception {
    final int DB_COUNT = 3;
    try (Statement stmt = connection.createStatement()) {
      for (int i = 1; i <= DB_COUNT; i++) {
        stmt.execute(String.format("CREATE DATABASE mydb%d", i));
      }
    }
    ArrayList<TestSequenceIsolation> workers = new ArrayList<TestSequenceIsolation>(DB_COUNT);
    for (int i = 1; i <= DB_COUNT; i++) {
      TestSequenceIsolation worker = new TestSequenceIsolation(String.format("mydb%d", i));
      worker.start();
      workers.add(worker);
    }
    for (TestSequenceIsolation worker : workers) {
      assertTrue(worker.isSucceeded());
    }
  }
}
