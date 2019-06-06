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

import org.apache.commons.lang3.RandomUtils;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.postgresql.core.TransactionState;
import org.postgresql.util.PSQLException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.SanitizerUtil;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgTransactions extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgTransactions.class);

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
  }

  @Test
  public void testBasicTransaction() throws Exception {
    createSimpleTable("test", "v");
    final IsolationLevel isolation = IsolationLevel.REPEATABLE_READ;
    Connection connection1 = createConnection(isolation, AutoCommit.DISABLED);
    Statement statement = connection1.createStatement();

    // For the second connection we still enable auto-commit, so that every new SELECT will see
    // a new snapshot of the database.
    Connection connection2 = createConnection(isolation, AutoCommit.ENABLED);
    Statement statement2 = connection2.createStatement();

    for (int i = 0; i < 100; ++i) {
      try {
        int h1 = i * 10;
        int h2 = i * 10 + 1;
        LOG.debug("Inserting the first row within a transaction but not committing yet");
        statement.execute("INSERT INTO test(h, r, v) VALUES (" + h1 + ", 2, 3)");

        LOG.debug("Trying to read the first row from another connection");
        assertFalse(statement2.executeQuery("SELECT h, r, v FROM test WHERE h = " + h1).next());

        LOG.debug("Inserting the second row within a transaction but not committing yet");
        statement.execute("INSERT INTO test(h, r, v) VALUES (" + h2 + ", 5, 6)");

        LOG.debug("Trying to read the second row from another connection");
        assertFalse(statement2.executeQuery("SELECT h, r, v FROM test WHERE h = " + h2).next());

        LOG.debug("Committing the transaction");
        connection1.commit();

        LOG.debug("Checking first row from the other connection");
        ResultSet rs = statement2.executeQuery("SELECT h, r, v FROM test WHERE h = " + h1);
        assertTrue(rs.next());
        assertEquals(h1, rs.getInt("h"));
        assertEquals(2, rs.getInt("r"));
        assertEquals(3, rs.getInt("v"));

        LOG.debug("Checking second row from the other connection");
        rs = statement2.executeQuery("SELECT h, r, v FROM test WHERE h = " + h2);
        assertTrue(rs.next());
        assertEquals(h2, rs.getInt("h"));
        assertEquals(5, rs.getInt("r"));
        assertEquals(6, rs.getInt("v"));
      } catch (PSQLException ex) {
        LOG.error("Caught a PSQLException at iteration i=" + i, ex);
        throw ex;
      }
    }
  }

  /**
   * This test runs conflicting transactions trying to insert the same row and verifies that
   * exactly one of them gets committed.
   */
  @Test
  public void testTransactionConflicts() throws Exception {
    createSimpleTable("test", "v");
    final IsolationLevel isolation = IsolationLevel.REPEATABLE_READ;

    Connection connection1 = createConnection(isolation, AutoCommit.DISABLED);
    Statement statement1 = connection1.createStatement();

    Connection connection2 = createConnection(isolation, AutoCommit.DISABLED);
    Statement statement2 = connection2.createStatement();

    int numFirstWinners = 0;
    int numSecondWinners = 0;
    final int totalIterations = SanitizerUtil.nonTsanVsTsan(300, 100);
    for (int i = 1; i <= totalIterations; ++i) {
      LOG.info("Starting iteration: i=" + i);
      if (RandomUtils.nextBoolean()) {
        // Shuffle the two connections between iterations.
        Connection tmpConnection = connection1;
        connection1 = connection2;
        connection2 = tmpConnection;

        Statement tmpStatement = statement1;
        statement1 = statement2;
        statement2 = tmpStatement;
      }

      executeWithTimeout(statement1,
          String.format("INSERT INTO test(h, r, v) VALUES (%d, %d, %d)", i, i, 100 * i));
      boolean executed2 = false;
      try {
        executeWithTimeout(statement2,
            String.format("INSERT INTO test(h, r, v) VALUES (%d, %d, %d)", i, i, 200 * i));
        executed2 = true;
      } catch (PSQLException ex) {
        // TODO: validate the exception message.
        // Not reporting a stack trace here on purpose, because this will happen a lot in a test.
        LOG.info("Error while inserting on the second connection:" + ex.getMessage());
      }
      TransactionState txnState1BeforeCommit = getPgTxnState(connection1);
      TransactionState txnState2BeforeCommit = getPgTxnState(connection2);

      boolean committed1 = commitAndCatchException(connection1, "first connection");
      TransactionState txnState1AfterCommit = getPgTxnState(connection1);

      boolean committed2 = commitAndCatchException(connection2, "second connection");
      TransactionState txnState2AfterCommit = getPgTxnState(connection2);

      LOG.info("i=" + i +
          " executed2=" + executed2 +
          " committed1=" + committed1 +
          " committed2=" + committed2 +
          " txnState1BeforeCommit=" + txnState1BeforeCommit +
          " txnState2BeforeCommit=" + txnState2BeforeCommit +
          " txnState1AfterCommit=" + txnState1AfterCommit +
          " txnState2AfterCommit=" + txnState2AfterCommit +
          " numFirstWinners=" + numFirstWinners +
          " numSecondWinners=" + numSecondWinners);

      // Whether or not a transaction commits successfully, its state is changed to IDLE after the
      // commit attempt.
      assertEquals(TransactionState.IDLE, txnState1AfterCommit);
      assertEquals(TransactionState.IDLE, txnState2AfterCommit);

      if (!committed1 && !committed2) {
        // TODO: if this happens, look at why two transactions could fail at the same time.
        throw new AssertionError("Did not expect both transactions to fail!");
      }

      if (executed2) {
        assertFalse(committed1);
        assertTrue(committed2);
        assertEquals(TransactionState.OPEN, txnState1BeforeCommit);
        assertEquals(TransactionState.OPEN, txnState2BeforeCommit);
        numSecondWinners++;
      } else {
        assertTrue(committed1);
        // It looks like in case we get an error on an operation on the second connection, the
        // commit on that connection succeeds. This makes sense in a way because the client already
        // knows that the second transaction failed from the original operation failure. BTW the
        // second transaction is already in a FAILED state before we successfully "commit" it:
        //
        // executed2=false
        // committed1=true
        // committed2=true
        // txnState1BeforeCommit=OPEN
        // txnState2BeforeCommit=FAILED
        // txnState1AfterCommit=IDLE
        // txnState2AfterCommit=IDLE
        //
        // TODO: verify if this is consistent with vanilla PostgreSQL behavior.
        // assertFalse(committed2);
        assertEquals(TransactionState.OPEN, txnState1BeforeCommit);
        assertEquals(TransactionState.FAILED, txnState2BeforeCommit);

        numFirstWinners++;
      }
    }
    LOG.info(String.format(
        "First txn won in %d cases, second won in %d cases", numFirstWinners, numSecondWinners));
    double skew = Math.abs(numFirstWinners - numSecondWinners) * 1.0 / totalIterations;
    LOG.info("Skew between the number of wins by two connections: " + skew);
    final double SKEW_THRESHOLD = 0.3;
    assertTrue("Expecting the skew to be below the threshold " + SKEW_THRESHOLD + ", got " + skew,
        skew < SKEW_THRESHOLD);
  }

  @Test
  public void testFailedTransactions() throws Exception {
    createSimpleTable("test", "v");
    final IsolationLevel isolation = IsolationLevel.REPEATABLE_READ;
    try (
         Connection connection1 = createConnection(isolation, AutoCommit.ENABLED);
         Statement statement1 = connection1.createStatement();
         Connection connection2 = createConnection(isolation, AutoCommit.DISABLED);
         Statement statement2 = connection2.createStatement()) {
      Set<Row> expectedRows = new HashSet<>();
      String scanQuery = "SELECT * FROM test";

      // Check second-op failure with auto-commit (first op should get committed).
      statement1.execute("INSERT INTO test(h, r, v) VALUES (1, 1, 1)");
      expectedRows.add(new Row(1, 1, 1));
      runInvalidQuery(statement1,
                      "INSERT INTO test(h, r, v) VALUES (1, 1, 2)",
                      "Duplicate key");
      assertRowSet(scanQuery, expectedRows);

      // Check second op failure with no-auto-commit (both ops should get aborted).
      statement2.execute("INSERT INTO test(h, r, v) VALUES (1, 2, 1)");
      runInvalidQuery(statement2,
                      "INSERT INTO test(h, r, v) VALUES (1, 1, 2)",
                      "Duplicate key");
      connection2.commit(); // Overkill, transaction should already be aborted, this will be noop.
      assertRowSet(scanQuery, expectedRows);

      // Check failure for one value set -- primary key (1,1) already exists.
      runInvalidQuery(statement1,
                      "INSERT INTO test(h, r, v) VALUES (1, 2, 1), (1, 1, 2)",
                      "Duplicate key");
      // Entire query should get aborted.
      assertRowSet(scanQuery, expectedRows);

      // Check failure for query with WITH clause side-effect -- primary key (1,1) already exists.
      String query = "WITH ret AS (INSERT INTO test(h, r, v) VALUES (2, 2, 2) RETURNING h, r, v) " +
          "INSERT INTO test(h,r,v) SELECT h - 1, r - 1, v FROM ret";
      runInvalidQuery(statement1, query, "Duplicate key");
      // Entire query should get aborted (including the WITH clause INSERT).
      assertRowSet(scanQuery, expectedRows);

      // Check failure within function with side-effect -- primary key (1,1) already exists.
      // TODO enable this test once functions are enabled in YSQL.
      if (false) {
        statement1.execute("CREATE FUNCTION bar(in int) RETURNS int AS $$ " +
                              "INSERT INTO test(h,r,v) VALUES($1,$1,$1) RETURNING h - 1;$$" +
                              "LANGUAGE SQL;");
        runInvalidQuery(statement1,
                        "INSERT INTO test(h, r, v) VALUES (bar(2), 1, 1)",
                        "Duplicate key");
        // Entire query should get aborted (including the function's side-effect).
        assertRowSet(scanQuery, expectedRows);
      }

    }

  }
}
