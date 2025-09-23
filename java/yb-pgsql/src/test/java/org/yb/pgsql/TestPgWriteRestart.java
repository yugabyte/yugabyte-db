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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.ThreadUtil;
import org.yb.YBTestRunner;

import java.sql.*;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.TimeUnit;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import com.yugabyte.util.PSQLException;
import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgWriteRestart extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgWriteRestart.class);
  private static final int SLEEP_DURATION = 500;

  private class ParallelRun implements Runnable {
    public ParallelRun(Statement s, List<String> queries) {
      this.s = s;
      this.queries = queries;
    }

    public final void run() {
      try {
        for (String query: queries) {
          s.execute(query);
        }
      } catch (SQLException ex) {
        // It is OK to swallow errors here since we ensure that the operations
        // complete using the asserts.
        LOG.error("Error encountered", ex);
      }
    }

    private Statement s;
    private List<String> queries;
  }

  private class ParallelRunWithImplicitPrepared implements Runnable {
    public ParallelRunWithImplicitPrepared(Connection c, String query, int key) {
      this.c = c;
      this.query = query;
      this.key = key;
    }

    public final void run() {
      try {
        PreparedStatement p = c.prepareStatement(query);
        if (key != -1) {
          p.setInt(1, key);
        }
        p.execute();
      } catch (SQLException ex) {
        // It is OK to swallow errors here since we ensure that the operations
        // complete using the asserts.
        LOG.error("Error encountered", ex);
      }
    }

    private Connection c;
    private String query;
    private int key;
  }

  // This function creates a thread that executes all the queries in
  // 'thread2Statements' once the first thread acquires an explicit lock on the
  // row. Ensure that once the first thread commits, the second thread finishes
  // its execution.
  private void runParallelExecutes(List<String> thread2Statements, String queryMode) throws Exception {
    try (Connection c1 = getConnectionBuilder().withPreferQueryMode(queryMode).connect();
         Connection c2 = getConnectionBuilder().withPreferQueryMode(queryMode).connect();
         Statement s1 = c1.createStatement();
         Statement s2 = c2.createStatement()) {
      s1.execute("begin");
      s1.execute("select * from writerestarts where a=1 for update");

      Thread t = new Thread(new ParallelRun(s2, thread2Statements));
      t.start();

      // Sleep for 5 seconds to simulate a longer running transaction.
      // The other request is retried for a max of 20 times with binary
      // exponential backoff that could total up to 17.5 seconds (at the
      // default values) across the 20 requests.
      Thread.sleep(SLEEP_DURATION);
      s1.execute("commit");
      t.join();
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }
  }

  private int getValue(String tableName) throws Exception {
    try (Statement statement = connection.createStatement()) {
      ResultSet rs =
        statement.executeQuery(String.format("select * from %s where a=1", tableName));
      while (rs.next()) {
        return rs.getInt("b");
      }
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }
    return -1;
  }

  private void performParallelWrites(
      boolean usePreparedStatements, String queryMode) throws Exception {
    // Use a normal transaction with a begin and commit for the second
    // connection. Ensure that the second transaction completes its execution
    // by reading the value after completion and asserting its correctness.
    int initialValue = getValue("writerestarts");
    if (usePreparedStatements) {
      runParallelExecutes(Arrays.asList(
        "prepare update_stmt(int) as update writerestarts set b=b+1 where a=$1",
        "execute update_stmt(1)"), queryMode);
    } else {
      runParallelExecutes(Arrays.asList(
        "update writerestarts set b=b+1 where a=1"), queryMode);
    }

    int finalValue = getValue("writerestarts");

    LOG.info("Initial and Final values: " + initialValue + " , " + finalValue);
    assertEquals(finalValue, initialValue + 1);

    // Use fast writes to see if we can still execute the update. Ensure that
    // the write completed by reading the value and asserting its correctness.
    initialValue = finalValue;
    if (usePreparedStatements) {
      runParallelExecutes(Arrays.asList(
        "prepare update_stmt(int) as update writerestarts set b=b+1 where a=$1",
        "begin",
        "execute update_stmt(1)",
        "commit"), queryMode);
    } else {
      runParallelExecutes(Arrays.asList(
        "begin",
        "update writerestarts set b=b+1 where a=1",
        "commit"), queryMode);
    }
    finalValue = getValue("writerestarts");

    LOG.info("Initial and Final values: " + initialValue + " , " + finalValue);
    assertEquals(finalValue, initialValue + 1);
  }

  private void performImplicitPreparedParallelWrites() throws Exception {
    int initialValue = getValue("writerestarts");

    try (Connection c1 = getConnectionBuilder().withPreferQueryMode("extended").connect();
         Connection c2 = getConnectionBuilder().withPreferQueryMode("extended").connect();
         Statement s1 = c1.createStatement()) {
      s1.execute("begin");
      s1.execute("select * from writerestarts where a=1 for update");

      Thread t = new Thread(new ParallelRunWithImplicitPrepared(c2,
        "update writerestarts set b=b+2 where a=1", -1));
      t.start();

      Thread.sleep(SLEEP_DURATION);
      s1.execute("commit");
      t.join();

      // Perform the same experiment as before but using bind arguments.
      s1.execute("begin");
      s1.execute("select * from writerestarts where a=1 for update");
      t = new Thread(new ParallelRunWithImplicitPrepared(c2,
        "update writerestarts set b=b+2 where a=?", 1));
      t.start();
      Thread.sleep(SLEEP_DURATION);
      s1.execute("commit");
      t.join();
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }

    int finalValue = getValue("writerestarts");

    LOG.info("Initial and Final values: " + initialValue + " , " + finalValue);
    assertEquals(finalValue, initialValue + 4);
  }

  private void createTrigger() throws Exception {
    // Create a trigger that increments the number of update operations
    // encountered.
    try (Statement statement = connection.createStatement()) {
      statement.execute("create table operations(a int primary key, b int)");
      statement.execute("insert into operations values(1, 0)");

      statement.execute("CREATE OR REPLACE FUNCTION audit() " +
        "RETURNS TRIGGER AS $$ " +
        "BEGIN " +
        "update operations set b=b+1 where a=1; " +
        "RETURN NEW; " +
        "END; " +
        "$$ LANGUAGE plpgsql;");
      statement.execute("CREATE TRIGGER audit_trigger " +
        "after update on writerestarts " +
        "FOR EACH ROW EXECUTE PROCEDURE audit()");
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }
  }

  @Test
  public void testWriteRestart() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("create table writerestarts(a int primary key, b int)");
      statement.execute("insert into writerestarts values(1, 1)");
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }

    // Test without triggers.
    performParallelWrites(false /* usePreparedStatements */, "extended" /* query_mode */);
    performParallelWrites(false /* usePreparedStatements */, "simple" /* query_mode */);

    createTrigger();

    // Now that the trigger is created perform the same set of operations and
    // ensure that they work. Also make sure that the number of operations
    // increases properly.
    int initialValue = getValue("operations");
    performParallelWrites(false /* usePreparedStatements */, "extended" /* query_mode */);
    int finalValue = getValue("operations");
    assertEquals(finalValue, initialValue + 2);

    // Test the same operations using prepared statements.
    initialValue = finalValue;
    performParallelWrites(true /* usePreparedStatements */, "extended" /* query_mode */);
    finalValue = getValue("operations");
    assertEquals(finalValue, initialValue + 2);

    initialValue = finalValue;
    performParallelWrites(false /* usePreparedStatements */, "simple" /* query_mode */);
    finalValue = getValue("operations");
    assertEquals(finalValue, initialValue + 2);


    initialValue = finalValue;
    performParallelWrites(true /* usePreparedStatements */, "simple" /* query_mode */);
    finalValue = getValue("operations");
    assertEquals(finalValue, initialValue + 2);

    initialValue = finalValue;
    performImplicitPreparedParallelWrites();
    finalValue = getValue("operations");
    assertEquals(finalValue, initialValue + 2);

    try (Statement statement = connection.createStatement()) {
      statement.execute("drop table writerestarts");
      statement.execute("drop table operations");
    } catch (PSQLException ex) {
      LOG.error("Unexpected exception:", ex);
      throw ex;
    }
  }

  @Test
  public void testSelectConflictRestart() throws Exception {
    int test_duration_secs = 10;

    try (Statement statement = connection.createStatement()) {
      statement.execute("create table test (k SERIAL primary key, v int)");
      statement.execute("insert into test (v) SELECT generate_series(0,100)");
    } catch (Exception ex) {
      fail("Unexpected exception: " + ex.getMessage());
    }

    ExecutorService es = Executors.newFixedThreadPool(2);
    List<Future<?>> futures = new ArrayList<>();

    final class SelectRunnable implements Runnable {

      public SelectRunnable() {}

      public void run() {
        int executionsAttempted = 0;
        int executionsAborted = 0;
        int executionsAbortedAfterSelectDone = 0;
        try (Connection conn = getConnectionBuilder().connect();
             Statement stmt = conn.createStatement()) {
          for (/* No setup */;; ++executionsAttempted) {
            if (Thread.interrupted()) break; // Skips all post-loop checks
            stmt.execute("start transaction isolation level serializable");
            try {
              stmt.execute("select * from test where v > 0 for update");
              try {
                stmt.execute("commit");
              } catch (Exception ex) {
                if (ex.getMessage().toLowerCase().contains("abort")) {
                  ++executionsAbortedAfterSelectDone;
                } else {
                  fail("failed with ex: " + ex.getMessage());
                }
                try {
                  stmt.execute("rollback");
                } catch (Exception ex2) {
                  fail("rollback shouldn't fail: " + ex2.getMessage());
                }
              }
            } catch (Exception ex) {
              // We expect no kConflict/kReadRestart errors in the first SELECT statement of a txn,
              if (ex.getMessage().toLowerCase().contains("abort")) {
                ++executionsAborted;
              } else {
                fail("failed with ex: " + ex.getMessage());
              }
              try {
                stmt.execute("rollback");
              } catch (Exception ex2) {
                fail("rollback shouldn't fail: " + ex2.getMessage());
              }
            }
          }
        } catch (Exception ex) {
          fail("failed with ex: " + ex.getMessage());
        }
        LOG.info("executionsAttempted=" + executionsAttempted +
                 " executionsAborted=" + executionsAborted +
                 ", executionsAbortedAfterSelectDone=" + executionsAbortedAfterSelectDone);
      }
    }

    futures.add(es.submit(new SelectRunnable()));
    futures.add(es.submit(new SelectRunnable()));

    Thread.sleep(test_duration_secs * 1000); // Run test for 10 seconds
    LOG.info("Shutting down executor service");
    es.shutdownNow(); // This should interrupt all submitted threads
    if (es.awaitTermination(10, TimeUnit.SECONDS)) {
      LOG.info("Executor shutdown complete");
    } else {
      LOG.info("Executor shutdown failed (timed out)");
    }
    try {
      LOG.info("Waiting for SELECT threads");
      for (Future<?> future : futures) {
        future.get(1, TimeUnit.SECONDS);
      }
    } catch (TimeoutException ex) {
      LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
      fail("Waiting for SELECT threads timed out, this is unexpected!");
    }
  }
}
