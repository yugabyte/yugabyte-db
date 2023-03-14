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

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestPgIndex extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgIndex.class);

  @Test
  public void testConcurrentInsert() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE t(value INT NOT NULL UNIQUE, worker_idx INT NOT NULL)");
      final int recordCount = 200;
      final int workerCount = 3;
      AtomicBoolean fWorkerFailed = new AtomicBoolean(false);
      CyclicBarrier barrier = new CyclicBarrier(workerCount);
      Worker[] workers = new Worker[workerCount];
      for (int i = 0; i < workers.length; ++i) {
        workers[i] = new Worker(getConnectionBuilder(), i, recordCount, barrier, fWorkerFailed);
      }
      LOG.info("Starting workers");
      Arrays.stream(workers).forEach(Worker::start);
      LOG.info("Waiting for results");
      int insertedCount = Arrays.stream(workers).mapToInt(w -> w.result()).sum();

      assertFalse(fWorkerFailed.get());
      assertEquals(recordCount, insertedCount);

      statement.execute("SELECT worker_idx, COUNT(value) FROM t GROUP BY worker_idx");
      ResultSet groups = statement.getResultSet();
      while (groups.next()) {
        LOG.info("Worker {} inserted {} items", groups.getInt(1), groups.getInt(2));
        assertEquals(groups.getInt(2), workers[groups.getInt(1)].result());
      }

      assertOneRow(statement, "SELECT COUNT(*) FROM t", (long) recordCount);
    }
  }

  private static class Worker implements Runnable {
    private volatile int insertedCount = 0;
    final private ConnectionBuilder connBldr;
    final private int idx;
    final private int count;
    final private Thread thread = new Thread(this);
    CyclicBarrier barrier;
    AtomicBoolean fWorkerFailed;

    Worker(ConnectionBuilder connBldr, int idx, int count, CyclicBarrier barrier,
        AtomicBoolean fWorkerFailed) {
      this.connBldr = connBldr;
      this.idx = idx;
      this.count = count;
      this.barrier = barrier;
      this.fWorkerFailed = fWorkerFailed;
    }

    void start() {
      thread.start();
    }

    int result() {
      try {
        thread.join();
      } catch (InterruptedException e) {
        LOG.error("Exception", e);
      }
      LOG.info("Worker {} finished with {} items", idx, insertedCount);
      return insertedCount;
    }

    @Override
    public void run() {
      LOG.info("Worker {} started", idx);
      int insertedCount = 0;
      try (Connection conn = connBldr.connect();
          Statement statement = conn.createStatement()) {
        for (int i = 0; i < count && !fWorkerFailed.get(); ++i) {
          try {
            barrier.await();
            statement.execute(String.format("INSERT INTO t values(%d, %d)", i, idx));
            ++insertedCount;
          } catch (SQLException e) {
            LOG.info("Insert by worker#{} expectedly failed due to {}", idx, e.getMessage());
          } catch (BrokenBarrierException e) {
            LOG.info("Early exit as some other worker failed. Exception", e);
            return;
          }
        }
      } catch (Exception e) {
        LOG.error("Exception", e);
        fWorkerFailed.set(true);
        try {
          // Wait with 0 timeout. If we are the last to enter the barrier, then we wake up all the
          // other threads. They will check for fWorkerFailed and exit. If we are not the last, then
          // we break the barrier, causing all future calls to await to throw a
          // BrokenBarrierException.
          barrier.await(0, TimeUnit.SECONDS);
        } catch (Exception inner_e) {
          // Exception is expected here
        }
      }

      this.insertedCount = insertedCount;
    }
  }

  @Test
  public void testCreateTableWithDuplicateIndexes() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table(id int," +
          "CONSTRAINT tt_id_pkey PRIMARY KEY(id)," +
          "CONSTRAINT tt_id_unq UNIQUE(id))");

      statement.execute("INSERT INTO test_table(id) VALUES (1), (2), (3)");

      // Primary key was added.
      runInvalidQuery(
          statement,
          "INSERT INTO test_table VALUES (1)",
          "duplicate key value violates unique constraint \"tt_id_pkey\""
      );

      // Unique constraint was not added.
      assertQuery(statement,
          "SELECT indexname FROM pg_indexes WHERE tablename='test_table'",
          new Row("tt_id_pkey"));
    }
  }
}
