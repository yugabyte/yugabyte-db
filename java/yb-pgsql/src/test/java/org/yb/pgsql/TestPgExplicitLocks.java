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

import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.Pair;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;

import com.yugabyte.util.PSQLException;
import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgExplicitLocks extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSelect.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("yb_enable_read_committed_isolation", "true");
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    flagMap.put("enable_wait_queues", "false");
    return flagMap;
  }

  @Test
  public void testExplicitLocks() throws Exception {
    setupSimpleTable("explicitlocks");
    Connection c1 = getConnectionBuilder().connect();
    Connection c2 = getConnectionBuilder().connect();

    Statement s1 = c1.createStatement();
    Statement s2 = c2.createStatement();

    try {
      String query = "begin transaction isolation level serializable";
      s1.execute(query);
      query = "select * from explicitlocks where h=0 and r=0 for update";
      s1.execute(query);
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }

    boolean conflict_occurred = false;
    try {
      String query = "update explicitlocks set vi=5 where h=0 and r=0";
      s2.execute(query);
    } catch (PSQLException ex) {
      if (ex.getMessage().contains("could not serialize access due to concurrent update")) {
        assertTrue(ex.getMessage().contains("conflicts with higher priority transaction"));
        LOG.info("Conflict ERROR");
        conflict_occurred = true;
      }
    }
    assertEquals(conflict_occurred, true);
    LOG.info("Done with the test");
  }

  private class ParallelQueryRunner {
    private Statement stmts[];

    ParallelQueryRunner(Statement s1, Statement s2) throws SQLException {
      stmts = new Statement[]{s1, s2};
      runOnBoth("SET retry_max_backoff = 0");
      stmts[0].execute("SET yb_transaction_priority_lower_bound = 0.5");
      stmts[1].execute("SET yb_transaction_priority_upper_bound = 0.1");
    }

    Pair<Integer, Integer> runWithConflict(String query1, String query2) throws SQLException {
      return run(query1, query2, true);
    }

    Pair<Integer, Integer> runWithoutConflict(String query1, String query2) throws SQLException {
      return run(query1, query2, false);
    }

    private Pair<Integer, Integer> run(
      String query1, String query2, boolean conflict_expected) throws SQLException {
      runOnBoth("BEGIN");
      stmts[0].execute(query1);
      int first = stmts[0].getUpdateCount();
      int second = 0;
      if (conflict_expected) {
        runInvalidQuery(stmts[1], query2, true,
          "could not serialize access due to concurrent update",
          "conflicts with higher priority transaction");
        stmts[0].execute("COMMIT");
        stmts[1].execute("ROLLBACK");
      } else {
        stmts[1].execute(query2);
        second = stmts[1].getUpdateCount();
        runOnBoth("COMMIT");
      }
      return new Pair<>(first, second);
    }

    private void runOnBoth(String query) throws SQLException {
      for (Statement s : stmts) {
        s.execute(query);
      }
    }
  }

  private static class QueryBuilder {
    private String table;

    public QueryBuilder(String table) {
      this.table = table;
    }

    String insert(String values) {
      return String.format("INSERT INTO %s VALUES %s", table, values);
    }

    String delete(String where) {
      return String.format("DELETE FROM %s WHERE %s", table, where);
    }

    String selectKeyShare(String where) {
      return String.format("SELECT * FROM %s WHERE %s FOR KEY SHARE OF %1$s", table, where);
    }
  };

  private void runRangeKeyLocksTest(ParallelQueryRunner runner,
                                    IsolationLevel pgIsolationLevel) throws SQLException {
    QueryBuilder builder = new QueryBuilder("table_with_hash");
    // NOTE: for all examples below, the SELECT KEY SHARE statement will lock the whole predicate
    // for SERIALIZABLE isolation level and only matching rows in other isolation levels. The
    // predicate is locked by locking the longest prefix of pk that is specified in the where
    // clause.
    // E.g:
    //   SELECT ... WHERE h=1 : lock full partition (h=1)
    //   SELECT ... WHERE h=1 and r2=100 : lock only (h=1) chunk since value for r1 is missing
    //   SELECT ... WHERE h=1 and r1=100 : lock full range (h=1, r1:100)
    //   SELECT ... WHERE h=1 and r1=100, r2=100 : lock full pk (h=1, r1:100, r2: 100)
    //   SELECT ... WHERE r1=100, r2=100 : lock whole table since value of h is missing

    // SELECT with (h: 1)
    // 1. DELETE on row that exists and matches SELECT where clause
    //      Conflicts will occur in both isolation levels since the row that DELETE tries to remove
    //      is already locked either way (lock predicate or only matching rows).
    runner.runWithConflict(
      builder.selectKeyShare("h = 1"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 100"));
    // 2. DELETE on row that matches the same predicate but doesn't exist.
    runner.run(
      builder.selectKeyShare("h = 1"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 102"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);

    runner.runWithConflict(
      builder.selectKeyShare("h = 1 AND r1 = 10 AND r2 = 100"),
      builder.delete("h = 1"));

    // SELECT with (h: 1, r1: 10)
    // 1. DELETE on rows that exists and matches SELECT where clause
    runner.runWithConflict(
      builder.selectKeyShare("h = 1 AND r1 = 10"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 100"));
    // 2. DELETE on row that matches the same predicate but doesn't exist.
    runner.run(
      builder.selectKeyShare("h = 1 AND r1 = 10"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 102"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);

    // SELECT with (h:1, r1:10, r2:100) and (h:1, r1:11, r2:100)
    // 1. DELETE on row that exists and matches SELECT where clause
    runner.runWithConflict(
      builder.selectKeyShare("h = 1 AND r1 IN (10, 11) AND r2 = 100"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 100"));
    // 2. DELETE on row that matches the same predicate but doesn't exist.
    runner.run(
      builder.selectKeyShare("h = 1 AND r1 IN (10, 11) AND r2 = 100"),
      builder.delete("h = 1 AND r1 = 11 AND r2 = 100"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);

    // SELECT with (h:1, r1: 10, r2: 100) and (h:1, r1: 10, r2: 101)
    // 1. DELETE on row that exists and matches SELECT where clause
    runner.run(
      builder.selectKeyShare("h = 1 AND r1 = 10 AND r2 IN (100, 101)"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 100"),
      true /* conflict_expected */);
    // 2. DELETE on row that matches the same predicate but doesn't exist.
    runner.run(
      builder.selectKeyShare("h = 1 AND r1 = 10 AND r2 IN (100, 101)"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 101"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);

    // Lock full partition (h: 1) if SERIALIZABLE.
    runner.run(
      builder.selectKeyShare("h = 1 AND r2 = 102"),
      builder.delete("h = 1 AND r1 = 11 AND r2 = 101"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);

    // Lock full tablet if SERIALIZABLE.
    runner.run(
      builder.selectKeyShare("r1 = 11"),
      builder.delete("h = 2 AND r1 = 21 AND r2 = 201"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);
    runner.run(
      builder.selectKeyShare("r2 = 102"),
      builder.delete("h = 2 AND r1 = 21 AND r2 = 201"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);

    runner.runWithConflict(
      builder.selectKeyShare("h = 1 AND r1 = 10 AND r2 = 100"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 100"));

    runner.runWithoutConflict(
      builder.selectKeyShare("h = 1"),
      builder.delete("h = 2 AND r1 = 20 AND r2 = 200"));
    runner.runWithoutConflict(
      builder.selectKeyShare("h = 1 AND r1 = 10"),
      builder.delete("h = 1 AND r1 = 11 AND r2 = 101"));
    runner.runWithoutConflict(
      builder.selectKeyShare("h = 1 AND r1 in (10, 11) AND r2 = 102"),
      builder.delete("h = 2 AND r1 = 21 AND r2 = 201"));
    runner.runWithoutConflict(
      builder.selectKeyShare("h = 1 AND r1 = 11 AND r2 in (100, 101)"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 100"));
    runner.runWithoutConflict(
      builder.selectKeyShare("h = 1 AND r2 = 102"),
      builder.delete("h = 2 AND r1 = 22 AND r2 = 202"));
    runner.runWithoutConflict(
      builder.selectKeyShare("h = 1 AND r1 = 10 AND r2 = 100"),
      builder.delete("h = 1 AND r1 = 10 AND r2 = 1000"));

    builder = new QueryBuilder("table_without_hash");
    // Lock full tablet if SERIALIZABLE.
    runner.run(
      builder.selectKeyShare("r2 = 11"),
      builder.delete("r1 = 2 AND r2 = 22"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);

    runner.run(
      builder.selectKeyShare("r1 = 1 OR r2 = 11"),
      builder.delete("r1 = 2 AND r2 = 22"),
      pgIsolationLevel == IsolationLevel.SERIALIZABLE /* conflict_expected */);
    runner.run(
      builder.selectKeyShare("r1 = 1"),
      builder.delete("r1 = 1 AND r2 = 10"),
      true /* conflict_expected */);
    runner.runWithConflict(
      builder.selectKeyShare("r2 = 10 AND r1 IN (1)"),
      builder.delete("r1 = 1 AND r2 = 10"));

    runner.runWithoutConflict(
      builder.selectKeyShare("r1 = 1"),
      builder.delete("r1 = 2 AND r2 = 20"));
    runner.runWithoutConflict(
      builder.selectKeyShare("r2 = 10 AND r1 IN (1)"),
      builder.delete("r1 = 1 AND r2 = 11"));
  }

  private void runFKLocksTest(ParallelQueryRunner runner) throws SQLException {
    QueryBuilder childBuilder = new QueryBuilder("child_with_hash");
    QueryBuilder parentBuilder = new QueryBuilder("parent_with_hash");
    runner.runWithConflict(
      childBuilder.insert("(1, 1, 10, 100)"),
      parentBuilder.delete("h = 1"));
    runner.runWithConflict(
      childBuilder.insert("(2, 1, 10, 100)"),
      parentBuilder.delete("h = 1 AND r1 = 10"));
    runner.runWithConflict(
      childBuilder.insert("(3, 1, 10, 100)"),
      parentBuilder.delete("h = 1 AND r1 = 10 AND r2 = 100"));

    runner.runWithoutConflict(
      childBuilder.insert("(4, 1, 10, 100)"),
      parentBuilder.delete("h = 2"));
    runner.runWithoutConflict(
      childBuilder.insert("(5, 1, 10, 100)"),
      parentBuilder.delete("h = 1 AND r1 = 11"));
    runner.runWithoutConflict(
      childBuilder.insert("(6, 1, 10, 100)"),
      parentBuilder.delete("h = 1 AND r1 = 10 AND r2 = 1000"));
    Pair<Integer, Integer> result = runner.runWithoutConflict(
      parentBuilder.delete("h = 3 AND r1 = 1 AND r2 = 10"),
      parentBuilder.delete("h = 3 AND r1 = 1 AND r2 = 20"));
    LOG.info("RESULT " + result.getFirst() + ", " + result.getSecond());
    assertEquals(new Pair<>(1, 1), result);

    childBuilder = new QueryBuilder("child_without_hash");
    parentBuilder = new QueryBuilder("parent_without_hash");
    runner.runWithoutConflict(
      childBuilder.insert("(3, 1, 10)"),
      parentBuilder.delete("r1 = 2"));
    runner.runWithoutConflict(
      childBuilder.insert("(4, 1, 10)"),
      parentBuilder.delete("r1 = 1 AND r2 = 11"));

    runner.runWithConflict(
      childBuilder.insert("(1, 1, 10)"),
      parentBuilder.delete("r1 = 1"));
    runner.runWithConflict(
      childBuilder.insert("(2, 1, 10)"),
      parentBuilder.delete("r1 = 1 AND r2 = 10"));
    result = runner.runWithoutConflict(
      parentBuilder.delete("r1 = 3 AND r2 = 10"),
      parentBuilder.delete("r1 = 3 AND r2 = 20"));
    assertEquals(new Pair<>(1, 1), result);
  }

  private void testRangeKeyLocks(IsolationLevel pgIsolationLevel) throws Exception {
    ConnectionBuilder builder = getConnectionBuilder().withIsolationLevel(pgIsolationLevel);
    try (Connection conn = builder.connect();
         Statement stmt = conn.createStatement();
         Connection extraConn = builder.connect();
         Statement extraStmt = extraConn.createStatement()) {
      stmt.execute(
        "CREATE TABLE table_with_hash(h INT, r1 INT, r2 INT, PRIMARY KEY(h, r1 ASC, r2 DESC))");
      stmt.execute(
        "CREATE TABLE table_without_hash(r1 INT, r2 INT, PRIMARY KEY(r1 ASC, r2 DESC))");
      stmt.execute("INSERT INTO table_with_hash VALUES " +
        "(1, 10, 100), (1, 10, 1000), (1, 10, 1001), (1, 11, 101), (1, 12, 102), " +
        "(2, 20, 200), (2, 21, 201), (2, 22, 202)");
      stmt.execute("INSERT INTO table_without_hash VALUES " +
        "(1, 10), (1, 11), (1, 12), " +
        "(2, 10), (2, 11), (2, 20), (2, 21), (2, 22)");
      runRangeKeyLocksTest(new ParallelQueryRunner(stmt, extraStmt), pgIsolationLevel);
    }
  }

  private void testFKLocks(IsolationLevel pgIsolationLevel) throws Exception {
    ConnectionBuilder builder = getConnectionBuilder().withIsolationLevel(pgIsolationLevel);
    try (Connection conn = builder.connect();
         Statement stmt = conn.createStatement();
         Connection extraConn = builder.connect();
         Statement extraStmt = extraConn.createStatement()) {
      stmt.execute(
        "CREATE TABLE parent_with_hash(h INT, r1 INT, r2 INT, PRIMARY KEY(h, r1 ASC, r2 DESC))");
      stmt.execute(
        "CREATE TABLE parent_without_hash(r1 INT, r2 INT, PRIMARY KEY(r1 ASC, r2 DESC))");

      stmt.execute("INSERT INTO parent_with_hash VALUES " +
        "(1, 10, 100), (1, 10, 1000), (1, 11, 101), (1, 12, 102), " +
        "(2, 20, 200), (3, 1, 10), (3, 1, 20)");
      stmt.execute("INSERT INTO parent_without_hash VALUES " +
        "(1, 10), (1, 11), (1, 12), (2, 20), (3, 10), (3, 20)");

      stmt.execute("CREATE TABLE child_with_hash(" +
        "k INT PRIMARY KEY, pH INT, pR1 INT, pR2 INT, " +
        "FOREIGN KEY(pH, PR1, pR2) REFERENCES parent_with_hash(h, r1, r2))");
      stmt.execute("CREATE TABLE child_without_hash(" +
        "k INT PRIMARY KEY, pR1 INT, pR2 INT, " +
        "FOREIGN KEY(pR1, pR2) REFERENCES parent_without_hash(r1, r2))");

      // Indices are necessary.
      // As after deletion from parent items referenced item is searched in child.
      // In case there are no index full scan is used which creates intent for a whole tablet.
      stmt.execute("CREATE INDEX ON child_with_hash(pH, pR1, pR2)");
      stmt.execute("CREATE INDEX ON child_without_hash(pR1, pR2)");

      runFKLocksTest(new ParallelQueryRunner(stmt, extraStmt));
    }
  }

  private void testLocksIsolationLevel(IsolationLevel pgIsolationLevel) throws Exception {
    testRangeKeyLocks(pgIsolationLevel);
    testFKLocks(pgIsolationLevel);
  }

  @Test
  public void testLocksSerializableIsolation() throws Exception {
    testLocksIsolationLevel(IsolationLevel.SERIALIZABLE);
  }

  @Test
  public void testLocksSnapshotIsolation() throws Exception {
    testLocksIsolationLevel(IsolationLevel.REPEATABLE_READ);
  }

  @Test
  public void testNoWait() throws Exception {
    ConnectionBuilder builder = getConnectionBuilder();
    try (Connection conn1 = builder.connect();
         Connection conn2 = builder.connect();
         Statement stmt1 = conn1.createStatement();
         Statement stmt2 = conn2.createStatement();
         Connection extraConn = builder.connect();
         Statement extraStmt = extraConn.createStatement()) {
      extraStmt.execute("CREATE TABLE test (k INT PRIMARY KEY, v INT)");
      extraStmt.execute("INSERT INTO test VALUES (1, 1)");

      // The below SELECT is done so that catalog reads are done before the NOWAIT statement. This
      // helps us accurately measure the number of read rpcs performed during the NOWAIT query for a
      // later assertion.
      //
      // The sleep is added to ensure that the cache refresh is complete before we measure
      // the number of read rpcs.
      stmt2.execute("SELECT * FROM test");
      Thread.sleep(2000);

      // Case 1: for REPEATABLE READ (not fully supported yet as explained below).

      // This test uses 2 txns which can be assigned random priorities. Txn1 does just a SELECT FOR
      // UPDATE. Txn2 later does the same but with the NOWAIT clause. There are 2 possible outcomes
      // based on whether txn2 is assigned higher or lower priority than txn1:
      //   1. Txn2 has higher priority: txn1 is aborted.
      //   2. Txn2 has lower priority: txn2 is aborted.
      //
      // TODO(Piyush): The semantics of NOWAIT require that txn2 is aborted always. The statement in
      // with NOWAIT should not kill other txns. So, the semantics of case 1 above need to be fixed.
      //
      // Since only case (2) works as of now, we need to ensure that txn2 has lower priority.
      stmt1.execute("SET yb_transaction_priority_lower_bound = 0.5");
      stmt2.execute("SET yb_transaction_priority_upper_bound = 0.4");

      stmt1.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      stmt2.execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ");
      stmt1.execute("SELECT * FROM test WHERE k=1 FOR UPDATE");

      Long read_count_before = getTServerMetric(
        "handler_latency_yb_tserver_TabletServerService_Read").count;
      LOG.info("read_count_before=" + read_count_before);
      try {
        stmt2.execute("SELECT * FROM test WHERE k=1 FOR UPDATE NOWAIT");
        assertTrue("Should not reach here since the statement is supposed to fail", false);
      } catch (SQLException e) {
        // If txn2 had a lower priority than txn1, instead of attempting retries for
        // ysql_max_write_restart_attempts, it would fail immediately due to the NOWAIT clause
        // with the appropriate message.
        assertTrue(StringUtils.containsIgnoreCase(e.getMessage(),
          "ERROR: could not obtain lock on row in relation \"test\""));

        // Assert that we failed immediately without retrying at all. This is done by ensuring that
        // we make only 2 read rpc call to tservers - one for reading the tuple and one for locking
        // the row.
        Long read_count_after = getTServerMetric(
          "handler_latency_yb_tserver_TabletServerService_Read").count;
        LOG.info("read_count_after=" + read_count_after);
        assertTrue((read_count_after - read_count_before) == 2);
        stmt1.execute("COMMIT");
        stmt2.execute("ROLLBACK");
      }

      // Case 2: for READ COMMITTED isolation.
      // All txns use the same priority in this isolation level.
      stmt1.execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED");
      stmt2.execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED");
      stmt1.execute("SELECT * FROM test WHERE k=1 FOR UPDATE");

      read_count_before = getTServerMetric(
        "handler_latency_yb_tserver_TabletServerService_Read").count;
      LOG.info("read_count_before=" + read_count_before);
      runInvalidQuery(stmt2, "SELECT * FROM test WHERE k=1 FOR UPDATE NOWAIT",
        "ERROR: could not obtain lock on row in relation \"test\"");

      // Assert that we failed immediately without retrying at all.
      Long read_count_after = getTServerMetric(
          "handler_latency_yb_tserver_TabletServerService_Read").count;
        LOG.info("read_count_after=" + read_count_after);
      assertTrue((read_count_after - read_count_before) == 2);
      stmt1.execute("COMMIT");
      stmt2.execute("ROLLBACK");
    }
  }
}
