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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.ThreadUtil;
import org.yb.YBTestRunner;

/**
 * This test mimics the scenario in #12767 -
 *
 * Before this commit's fix, the following scenario leads to a situation where 2 single statement
 * read committed transactions block each other -
 *
 *    CREATE TABLE test (id int primary key, v timestamp with time zone NOT NULL);
 *    CREATE INDEX idx ON test USING btree (v);
 *
 * 1. 2 read committed isolation transactions are started in separate sessions with begin;
 * 2. Both sessions get the request from client to execute a INSERT ON CONFLICT DO UPDATE query on a
 *    row in the main table.
 *
 *    INSERT INTO test AS x (id, v) VALUES (1, statement_timestamp())
 *    ON CONFLICT ON CONSTRAINT test_pkey
 *    DO UPDATE SET v = statement_timestamp() WHERE x.id = excluded.id
 *
 * 3. Backends of both sessions first read the main table to check if the key already exists. Since
 *    it does, both backends then try to perform the ON CONFLICT DO UPDATE by issuing three rpcs
 *    simultaneously to tserver processes -
 *
 *      i) a PGSQL_UPDATE to the main table to update the v time
 *      ii) a PGSQL_DELETE to the secondary index to remove the entry with existing v value
 *      iii) a PGSQL_UPSERT to the secondary index to insert a new index entry with new v value
 *
 * 4. Rpc [a] of session 1 reaches the main table first and performs the write.
 *    Rpc [b] (and/or [c]) of session 2 reaches the index table first and performs the write there.
 *
 * 5. So, session 1 has successfully written a provisional entry to the main table and session 2 has
 *    successfully written provisional entries to the index table.
 *
 * 6. Now, the other rpcs in both session will fail due to a conflict i.e., rpc [b] and [c] of
 *    session 1 and rpc [a] of session 2 will fail.
 *
 * 7. Both sessions, after facing the conflict, will retry the statement by rolling back to the
 *    internal savepoint registered before the statement's execution (as is done by our read
 *    committed implementation). Rolling back will effectively mark the provisional data written by
 *    earlier rpcs in the statement as invalid so that the backends can retry their statement and no
 *    other transactions should be able to see those invalid provisional entries.
 *
 * 8. However, even after rolling back savepoints, other transactions consider the rolled back
 *    provisional entries for conflict resolution and hence still face conflicts. This is because
 *    the list of aborted sub txns isn't available to the txn participants during conflict
 *    detection.
 *
 * 9. This leads to both sessions retrying and facing false conflicts till statement timeout is hit.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgConflictWithAbortedSubTxns extends BasePgSQLTest {
  private static final Logger LOG =
    LoggerFactory.getLogger(TestPgConflictWithAbortedSubTxns.class);

  private static final int NUM_THREADS = 9;
  private static final int TEST_DURATION_SECS = 120;
  private static final int STATEMENT_TIMEOUT_MS = 30 * 1000;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("yb_enable_read_committed_isolation", "true");
    flags.put("ysql_pg_conf_csv", "statement_timeout=" + STATEMENT_TIMEOUT_MS);
    // This test is designed to validate a scenario encountered in RC without wait queues.
    flags.put("enable_wait_queues", "false");
    flags.put("enable_deadlock_detection", "false");
    return flags;
  }

  @Test
  public void ensureNoConflictsWithAbortedSubTxns() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute(
        "CREATE TABLE test (id int primary key, v timestamp with time zone NOT NULL)");
      statement.execute("CREATE INDEX idx ON test USING btree (v)");
    }

    ExecutorService es = Executors.newFixedThreadPool(NUM_THREADS);
    List<Future<?>> futures = new ArrayList<>();
    List<Runnable> runnables = new ArrayList<>();

    for (int i = 0; i < NUM_THREADS; i++) {
      runnables.add(() -> {
        try (Connection conn =
               getConnectionBuilder().withIsolationLevel(IsolationLevel.READ_COMMITTED)
               .withAutoCommit(AutoCommit.ENABLED).connect();
             Statement stmt = conn.createStatement();) {

          long end_time = System.currentTimeMillis() + TEST_DURATION_SECS*1000;

          while (System.currentTimeMillis() < end_time) {
            stmt.execute("BEGIN");
            stmt.execute(
              "INSERT INTO test AS x (id, v) VALUES (1, statement_timestamp()) " +
              "ON CONFLICT ON CONSTRAINT test_pkey " +
              "DO UPDATE SET v = statement_timestamp() WHERE x.id = excluded.id"
            );
            stmt.execute("COMMIT");
          }
        } catch (Exception ex) {
          fail("Failed due to exception: " + ex.getMessage());
        }
      });
    }

    for (Runnable r : runnables) {
      futures.add(es.submit(r));
    }

    try {
      LOG.info("Waiting for all threads");
      for (Future<?> future : futures) {
        future.get(TEST_DURATION_SECS + 30, TimeUnit.SECONDS);
      }
    } catch (TimeoutException ex) {
      LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
      fail("Waiting for threads timed out, this is unexpected!");
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("DROP TABLE test");
    }
  }
}
