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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.BitSet;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertGreaterThan;

/**
 * This test suite is to test features such as ACID transaction properties, requiring multiple
 * concurrent transactions. It contains helper classes making it easier to run parallel clients
 * doing predefined tasks: modify the database, read modified data and make sure the data remain
 * consistent at all times.
 *
 * By the nature of concurrent processes it is not guaranteed the tests reproduce the problems
 * 100% of times, but hopefully they are useful to catch regressions.
 *
 * Note: the helper classes are implemented as inner classes initially, but if (or when) we have
 * more tests of that kind, and need more test suites, we can (and should) refactor them into a
 * separate package.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgConcurrency extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgConcurrency.class);

  /**
   * Regression test, it fails if an INSERT ... SELECT statement has a pseudo-constant condition
   * that is not evaluated in planner. In such a case Postgres planner creates a Result plan node
   * on top of the SELECT plan and assigns the pseudo-constant condition to it. At execution time
   * the condition is evaluated, and, if false, the SELECT plan is not executed.
   *
   * However, such a construction may make the INSERT statement mistaken for a single row insert.
   * Single row inserts are committed and visible immediately, so concurrent read have a chance to
   * see part of the rows inserted by a statement.
   *
   * The write thread of the test runs sequence of insert statements each of them generates and
   * inserts 500 rows. Parallel thread repeatedly runs SELECT count(*) on that table and checks if
   * the result is a multiple of 500. If it is not, the transaction atomicity is violated.
   *
   * @throws Exception
   */
  @Test
  public void testPseudoSingleRowInsert() throws Exception {
    try (Statement statement = connection.createStatement()) {
      // Number of rows inserted by each statement
      final int rowCount = 500;
      // Number of statements
      final int repetitions = 100;
      // Prepare test table
      statement.execute("CREATE TABLE t1(k int primary key, v0 int)");
      // Write thread
      Worker<Integer> worker = new PseudoSingleRowInsertWorker(getConnectionBuilder(),
                                                               rowCount,
                                                               repetitions);
      // Read thread
      Worker<Pair> checker = new CountAllChecker(getConnectionBuilder(), rowCount);
      LOG.info("Starting workers");
      worker.start();
      checker.start();
      LOG.info("Waiting for results");
      // The writer completed the task and ran specified number of statements
      assertEquals(repetitions, worker.result().intValue());
      // Stop the reader
      checker.stop();
      // The reader has seen no anomalies
      assertEquals(0, checker.result().getFailed());
      // The reader was able to read the data at least once
      assertGreaterThan(checker.result().getTotal(), 0);
    }
  }

  /**
   * Helper class representing a database client executing queries and returning results.
   * Type parameter defines the type of the result. The class is abstract, subclasses are
   * supposed to override the run() method of the Runnable interface. The run() method is executed
   * in a background thread. Whether it runs some fixed workload or runs until some external event,
   * it is recommended to periodically invoke the isStopped() method and exit if it returns true.
   * If implemented that way the worker can be stopped using the stop() method.
   * Before exiting the run method should assign the result field.
   * Object lifecycle is as follows:
   *  - Constructor takes a ConnectionBuilder parameter, a thread is created upon construction;
   *  - The start() method is called, which kicks off the thread executing the run method;
   *  - Optionally the stop() method is called;
   *  - The result method is called to return the result assigned in run(). The method waits until
   *    the thread is completed.
   */
  private static abstract class Worker<T> implements Runnable {
    /* The internal thread should stop when convenient */
    private AtomicBoolean stopped = new AtomicBoolean(false);
    /* Worker execution result, assign it in run() */
    protected volatile T result;
    /* The connection builder */
    final private ConnectionBuilder connBldr;
    /* The thread */
    final private Thread thread = new Thread(this);

    /* The constructor */
    protected Worker(ConnectionBuilder connBldr) {
      this.connBldr = connBldr;
    }

    /* Start the internal thread */
    public void start() {
      thread.start();
    }

    /* Tell the internal thread to stop */
    public void stop() {
      stopped.set(true);
    }

    /**
     * Use isStopped() in the run() method to check if the stop() method was
     * called. The run() method should exit whether it return true.
     * Use result() to wait until internal thread is actually stopped.
     * @return true if stop() method was called
     */
    protected boolean isStopped() {
      return stopped.get();
    }

    /* Object name. Used in the log messages. */
    protected String getId() {
      return getClass().getName();
    }

    /* Wait until the thread is completed, then return the result */
    T result() {
      try {
        thread.join();
      } catch (InterruptedException e) {
        LOG.error("Exception", e);
      }
      LOG.info("{} has finished, result: {}", getId(), result);
      return result;
    }

    /* Create new connection fron the connection builder */
    protected Connection connect() throws Exception{
      return connBldr.connect();
    }
  }

  /**
   * Container to hold two immutable integers: number of failed checks and total number of checks.
   * The toString() method returns descriptive string, so instance can be simply output to the log.
   */
  private static class Pair {
    final private int failed;
    final private int total;

    Pair(int failed, int total) {
      this.failed = failed;
      this.total = total;
    }

    public int getFailed() {
      return failed;
    }

    public int getTotal() {
      return total;
    }

    @Override
    public String toString() {
      return String.format("%d out of %d checks failed");
    }
  }

  /**
   * Test worker that runs specified number of insert statements with a pseudo-constant conditions.
   * Each statement inserts specified number of rows. A multi-row insert requires distributed
   * transaction, however the statement plan may be confused for a single row, and run without
   * transaction, in which case other transaction may be able to perform dirty reads.
   * Worker result is the actual number of statements executed.
   */
  private static class PseudoSingleRowInsertWorker extends Worker<Integer> {
    final private int rowCount;
    final private int count;

    /**
     * The constructor
     *
     * @param connBldr - the connection builder
     * @param rowCount - number of rows to insert per statement
     * @param count - number of statements to run
     */
    PseudoSingleRowInsertWorker(ConnectionBuilder connBldr, int rowCount, int count) {
      super(connBldr);
      this.rowCount = rowCount;
      this.count = count;
    }

    /**
     * Run the test
     */
    @Override
    public void run() {
      // Init the counter
      int writeCount = 0;
      // Make a connection and a statement
      try (Connection conn = connect();
          Statement statement = conn.createStatement()) {
        // Repeat specified number of times
        for (int i = 0; i < count && !isStopped(); ++i) {
          try {
            // Insert statement with a pseudo-constant (always true) condition
            String stmt = "INSERT INTO t1 SELECT i, 0 FROM generate_series(%d, %d) i " +
                          "WHERE 0.14198202::MONEY >= 0.14222479::MONEY";
            // The range of values
            int from = i * rowCount + 1;
            int to = (i + 1) * rowCount;
            LOG.info("Insert keys from {} to {}", from, to);
            // Execute the statement
            int affected = statement.executeUpdate(String.format(stmt, from, to));
            // Verify number of rows affected
            if (affected == rowCount)
              ++writeCount;
          } catch (SQLException e) {
            LOG.info("Insert failed due to {}", e.getMessage());
          }
        }
      } catch (Exception e) {
        LOG.error("Connection failed: {}", e.getMessage());
      }
      // Publish the result
      this.result = writeCount;
    }
  }

  /**
   * Paired with PseudoSingleRowInsertWorker, checker to catch dirty reads.
   * The internal query attempts to count the target table's rows.
   * If each transaction inserts rowCount rows into the table the result is expected to be a
   * multiple of rowCount, unless query reads dirty rows.
   */
  private static class CountAllChecker extends Worker<Pair> {
    final private int rowCount;

    /**
     * The constructor
     *
     * @param connBldr - the connection builder
     * @param rowCount - the number of rows per statement, expected row count is a multiple of it
     */
    CountAllChecker(ConnectionBuilder connBldr, int rowCount) {
      super(connBldr);
      this.rowCount = rowCount;
    }

    /**
     * Run the tests
     */
    @Override
    public void run() {
      // Init the counters
      int readCount = 0;
      int failCount = 0;

      // Make a connection and a statement
      try (Connection conn = connect();
          Statement statement = conn.createStatement()) {
        // Run until stopped
        while (!isStopped()) {
          try {
            // Exec the query
            ResultSet rs = statement.executeQuery("SELECT count(*) FROM t1");
            ++readCount;
            // Can't really happen
            if (!rs.next()) {
              LOG.error("Query returns no rows");
              ++failCount;
            }
            // Check if the total count is a multiple of rowCount
            else {
              int count = rs.getInt(1);
              if (count % rowCount != 0) {
                LOG.error("Dirty read: count is {}, which is not a multiple of {}",
                          count, rowCount);
                ++failCount;
              }
            }
          } catch (SQLException e) {
            LOG.error("Read failed due to {}", e.getMessage());
            ++failCount;
          }
        }
      } catch (Exception e) {
        LOG.error("Connection failed: {}", e.getMessage());
      }
      // Publish the result
      this.result = new Pair(failCount, readCount);
    }
  }

  /**
   * Test transactional consistency of concurrent multi row updates.
   * Initially test table has certain amount of key/value rows, where all values are 0.
   * Then test runs a worker executing a sequence of update statements. Each statement writes an
   * integer constant to the value field, and has a pseudo-constant condition, which is always true,
   * therefore all rows are updated and for any other transaction all values are equal, unless
   * atomicity is violated.
   * To check for violations, a parallel worker executes a SELECT sum() query, and if all values are
   * equal, the result is a multiple of row count.
   *
   * @throws Exception
   */
  @Test
  public void testPseudoSingleRowUpdate() throws Exception {
    try (Statement statement = connection.createStatement()) {
      // Number of rows
      final int rowCount = 500;
      // Number of update statements to execute
      final int repetitions = 100;
      // Prepare target table
      statement.execute("CREATE TABLE t2(k int primary key, v0 int)");
      statement.execute("INSERT INTO t2 SELECT i, 0 FROM generate_series(1, " + rowCount + ") i");
      // Prepare workers
      Worker<Integer> worker = new PseudoSingleRowUpdateWorker(getConnectionBuilder(),
                                                               rowCount,
                                                               repetitions);
      Worker<Pair> checker = new SumAllChecker(getConnectionBuilder(), rowCount);
      LOG.info("Starting workers");
      worker.start();
      checker.start();
      LOG.info("Waiting for results");
      // The writer completed the task and ran specified number of statements
      assertEquals(repetitions, worker.result().intValue());
      // Stop the reader
      checker.stop();
      // The reader has seen no anomalies
      assertEquals(0, checker.result().getFailed());
      // The reader was able to read the data at least once
      assertGreaterThan(checker.result().getTotal(), 0);
    }
  }

  /**
   * Test worker that runs specified number of update statements with a pseudo-constant conditions.
   * Each statement updates all rows in the table. A multi-row update requires distributed
   * transaction, however the statement plan may be confused for a single row, and run without
   * transaction, in which case other transaction may be able to perform dirty reads.
   * Worker result is the actual number of statements executed.
   */
  private static class PseudoSingleRowUpdateWorker extends Worker<Integer> {
    final private int rowCount;
    final private int count;

    /**
     * The constructor
     *
     * @param connBldr - the connection builder
     * @param rowCount - expected number of rows affected per statement
     * @param count - number of statements to run
     */
    PseudoSingleRowUpdateWorker(ConnectionBuilder connBldr, int rowCount, int count) {
      super(connBldr);
      this.rowCount = rowCount;
      this.count = count;
    }

    /**
     * Run the test
     */
    @Override
    public void run() {
      // Init the counter
      int writeCount = 0;
      // Make a connection and a statement
      try (Connection conn = connect();
          Statement statement = conn.createStatement()) {
        // Repeat specified number of times
        for (int i = 1; i <= count && !isStopped(); ++i) {
          try {
            // Update statement with a pseudo-constant (always true) condition
            String stmt = "UPDATE t2 SET v0 = %d WHERE 0.14198202::MONEY >= 0.14222479::MONEY";
            LOG.info("Update all values to be {}", i);
            // Execute the statement
            int affected = statement.executeUpdate(String.format(stmt, i));
            LOG.info("Rows affected: {}", affected);
            // Sanity check, there should be exactly rowCount of updated rows
            if (affected == rowCount)
              ++writeCount;
          } catch (SQLException e) {
            LOG.info("Update failed due to {}", e.getMessage());
          }
        }
      } catch (Exception e) {
        LOG.error("Connection failed: {}", e.getMessage());
      }
      // Publish the result
      this.result = writeCount;
    }
  }

  /**
   * Paired with PseudoSingleRowUpdateWorker, checker to catch dirty reads.
   * The internal query attempts to sum the values in the target table's rows.
   * If each transaction updates each value to the same constant integer, their sum is expected
   * to be a multiple of rowCount, unless query reads dirty rows.
   */
  private static class SumAllChecker extends Worker<Pair> {
    final private int rowCount;

    /**
     * The constructor
     *
     * @param connBldr - the connection builder
     * @param rowCount - the number of rows in the table, expected sum is a multiple of it
     */
    SumAllChecker(ConnectionBuilder connBldr, int rowCount) {
      super(connBldr);
      this.rowCount = rowCount;
    }

    /**
     * Run the tests
     */
    @Override
    public void run() {
      // Init the counter
      int readCount = 0;
      int failCount = 0;
      // Make a connection and a statement
      try (Connection conn = connect();
          Statement statement = conn.createStatement()) {
        // Run until stopped
        while (!isStopped()) {
          try {
            // Exec the query
            ResultSet rs = statement.executeQuery("SELECT sum(v0) FROM t2");
            ++readCount;
            // Can't really happen
            if (!rs.next()) {
              LOG.error("Rows not found");
              ++failCount;
            }
            // Check if the total sum is a multiple of rowCount
            else {
              int sum = rs.getInt(1);
              if (sum % rowCount != 0) {
                LOG.error("Dirty read: sum is {}, which is not a multiple of {}",
                          sum, rowCount);
                ++failCount;
              }
            }
          } catch (SQLException e) {
            LOG.error("Read failed due to {}", e.getMessage());
            ++failCount;
          }
        }
      } catch (Exception e) {
        LOG.error("Connection failed: {}", e.getMessage());
      }
      // Publish the result
      this.result = new Pair(failCount, readCount);
    }
  }

  /**
   *
   * @throws Exception
   */
  @Test
  public void testSequence() throws Exception {
    try (Statement statement = connection.createStatement()) {
      // Number of concurrent workers
      final int concurrency = 50;
      // Number of nextvals to get by each worker
      final int count = 1234;
      // Prepare target table
      statement.execute("CREATE SEQUENCE seq2");
      // Prepare workers
      Worker<BitSet> workers[] = new SequenceTestWorker[concurrency];
      for (int i = 0; i < concurrency; i++) {
        workers[i] = new SequenceTestWorker(getConnectionBuilder(), count);
      }
      LOG.info("Starting workers");
      for (Worker<BitSet> worker: workers) {
        worker.start();
      }
      LOG.info("Waiting for results");
      BitSet total = new BitSet();
      for (Worker<BitSet> worker: workers) {
        BitSet result = worker.result();
        assertEquals(count, result.cardinality());
        total.or(result);
      }
      assertEquals(concurrency * count, total.cardinality());
    }
  }

  private static class SequenceTestWorker extends Worker<BitSet> {
    final private int count;

    /**
     * The constructor
     *
     * @param connBldr - the connection builder
     * @param rowCount - expected number of rows affected per statement
     * @param count - number of statements to run
     */
    SequenceTestWorker(ConnectionBuilder connBldr, int count) {
      super(connBldr);
      this.count = count;
    }

    /**
     * Run the test
     */
    @Override
    public void run() {
      BitSet result = new BitSet();
      LOG.info("Begin work");
      try (Connection conn = connect();
          Statement statement = conn.createStatement()) {
        // Run until stopped
        for (int i = 0; i < count; i++) {
          try {
            // Exec the query
            ResultSet rs = statement.executeQuery("SELECT nextval('seq2')");
            if (!rs.next()) {
              LOG.error("No nextval");
            } else {
              int nextval = rs.getInt(1);
              result.set(nextval);
            }
          } catch (SQLException e) {
            LOG.error("Read failed due to {}", e.getMessage());
          }
        }
      } catch (Exception e) {
        LOG.error("Connection failed: {}", e.getMessage());
      }
      LOG.info("End work");
      this.result = result;
    }
  }
}
