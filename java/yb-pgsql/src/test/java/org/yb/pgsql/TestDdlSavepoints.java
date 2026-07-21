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

import org.yb.minicluster.MiniYBClusterBuilder;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.SkipOnTSAN;

import java.util.concurrent.ThreadLocalRandom;
import java.sql.Connection;
import java.sql.Statement;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

import static org.yb.AssertionWrappers.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Runs the pg_regress test suite for DDL savepoint support.
 */
@SkipOnTSAN
@RunWith(value=YBTestRunner.class)
public class TestDdlSavepoints extends BasePgRegressTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestDdlSavepoints.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(500, 1000, 1200, 1200, 1200);
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.numMasters(1);
    builder.enablePgTransactions(true);
    builder.addCommonTServerFlag("ysql_log_statement", "all");
    builder.addCommonTServerFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    builder.addCommonTServerFlag("enable_object_locking_for_table_locks", "true");
    builder.addCommonTServerFlag("ysql_yb_enable_ddl_savepoint_support", "true");
    builder.addCommonTServerFlag("ysql_bypass_anonymous_savepoint_ddl_check", "false");
    builder.addCommonTServerFlag(
        "allowed_preview_flags_csv",
        "ysql_yb_enable_ddl_savepoint_support,ysql_yb_enable_new_relation_fastpath_write_in_txn_"
            + "blocks");
    boolean enableSkipIntents = ThreadLocalRandom.current().nextBoolean();
    if (enableSkipIntents) {
      builder.addCommonTServerFlag(
          "ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks", "true");
    }
    builder.addCommonTServerFlag("yb_enable_read_committed_isolation", "true");
    builder.addMasterFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    builder.addMasterFlag("ysql_yb_enable_ddl_savepoint_support", "true");
    builder.addMasterFlag("allowed_preview_flags_csv", "ysql_yb_enable_ddl_savepoint_support");
    builder.addMasterFlag("vmodule", "catalog_manager=3,ysql_ddl_handler=4");

    builder.addCommonTServerFlag("enable_deadlock_detection", "true");
    builder.addCommonTServerFlag("enable_wait_queues", "true");
    builder.addMasterFlag("enable_deadlock_detection", "true");
    builder.addMasterFlag("enable_wait_queues", "true");
  }

  @Test
  public void yb_ddl_savepoint_tests() throws Exception {
    // GH-27235 - There are issues with schema invalidation on other backends
    // for rolled back transactional DDLs, causing the test to fail with
    // Connection Mangaer enabled. Force the test to operate on one physical
    // connection until this issue is resolved.
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);
    runPgRegressTest("yb_ddl_savepoint_schedule");
  }

  @Test
  public void testSplitBrainDdlSavepointDeadlock() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t1_split (k INT PRIMARY KEY)");
      stmt.execute("CREATE TABLE t2_split (k INT PRIMARY KEY)");
      stmt.execute("CREATE TABLE t3_split (k INT PRIMARY KEY)");
    }

    int attempts = 0;
    boolean reproduced = false;

    // Deadlock victim selection can be non-deterministic.
    // We loop until DocDB chooses conn1 as the deadlock victim.
    while (attempts++ < 20 && !reproduced) {
      try (Connection conn1 = getConnectionBuilder().connect();
           Connection conn2 = getConnectionBuilder().connect()) {

        conn1.setAutoCommit(false);
        conn2.setAutoCommit(false);

        Statement stmt1 = conn1.createStatement();
        Statement stmt2 = conn2.createStatement();

        // 1. Conn1 executes the DDL inside a savepoint
        stmt1.execute("SAVEPOINT sp1");
        stmt1.execute("ALTER TABLE t1_split ADD COLUMN v2 INT");
        stmt1.execute("SAVEPOINT sp2");

        // 2. Conn1 grabs a lock on t2_split
        stmt1.execute("INSERT INTO t2_split VALUES (" + attempts + ")");

        // 3. Conn2 grabs a lock on t3_split
        stmt2.execute("INSERT INTO t3_split VALUES (" + attempts + ")");

        // 4. Force the deadlock concurrently
        CountDownLatch startLatch = new CountDownLatch(1);
        AtomicReference<Exception> conn1Error = new AtomicReference<>();

        final int finalAttempts = attempts;
        Thread thread1 = new Thread(() -> {
          try {
            startLatch.await();
            // Conn1 waits on Conn2's lock
            stmt1.execute("INSERT INTO t3_split VALUES (" + finalAttempts + ")");
          } catch (Exception e) {
            conn1Error.set(e);
          }
        });
        thread1.start();

        startLatch.countDown(); // Release thread1 to try to insert

        // Ensure that thread1 has started and is blocking before conn2 inserts.
        // It guarantees conn1 waits on conn2 before conn2 waits on conn1, causing conn1
        // to be the victim
        // since deadlock detection kills the txn that waited longer.
        Thread.sleep(100);

        try {
          // Conn2 waits on Conn1's lock -> DEADLOCK
          stmt2.execute("INSERT INTO t2_split VALUES (" + attempts + ")");
        } catch (Exception e) {
          // If conn2 is the victim, we just ignore and let the loop retry
        }

        thread1.join();

        // Check if Conn1 was the one chosen by DocDB to be aborted
        if (conn1Error.get() != null && conn1Error.get().getMessage().contains("deadlock")) {
          reproduced = true;
          LOG.info("Conn1 was the deadlock victim. Triggering rollback...");

          try {
            // PostgreSQL believes the deadlock was just a statement-level error.
            stmt1.execute("ROLLBACK TO SAVEPOINT sp2");

            // Try to use the column we just added.
            stmt1.execute("INSERT INTO t1_split (k, v2) VALUES (1000, 1000)");

            // If we reach here, neither the bug nor the fix happened.
            fail("Expected a failure (either Invalid column number or connection drop), " +
                 "but INSERT succeeded.");
          } catch (Exception e) {
            // Unlikely to catch SQLException here because statement executes often wrap them,
            // but we'll check just in case. The main catch block outside handles the bulk.
            if (e.getMessage() != null && e.getMessage().contains("Invalid column number")) {
              // FAIL: we reproduced the bug prior to the fix
              fail("Reproduced split-brain bug: " + e.getMessage());
            } else if (e.getMessage() != null &&
                       (e.getMessage().contains("This connection has been closed") ||
                                                  e.getMessage().contains("I/O error") ||
                                                  e.getMessage().contains("FATAL"))) {
              // Post-Fix behavior:
              // The ROLLBACK TO SAVEPOINT command receives the Aborted/Expired status
              // from the backend and drops the connection with FATAL.
              LOG.info("Got expected post-fix failure: " + e.getMessage());
            } else if (e.getMessage() != null && e.getMessage().contains("deadlock detected")) {
              // FAIL: Depending on exactly how the rollback state propagated, DocDB might throw
              // the deadlock error back to the INSERT query directly if the split-brain check
              // is not in place. Either way, this confirms the bug prior to the fix!
              fail("Reproduced split-brain bug (deadlock hit statement instead of " +
                   "connection drop): " + e.getMessage());
            } else {
              // Unexpected exception
              LOG.error("Unexpected exception during test:", e);
              fail("Unexpected failure: " + e.getMessage());
            }
          }
        }
      } catch (java.sql.SQLException e) {
        if (e.getMessage() != null && e.getMessage().contains("Invalid column number")) {
          // FAIL: we reproduced the bug prior to the fix
          fail("Reproduced split-brain bug: " + e.getMessage());
        }
        // Ignore connection closed errors that happen during resource cleanup
        // after the FATAL drop.
      }
    }

    assertTrue("Failed to ever make conn1 the deadlock victim after " + attempts +
               " attempts", reproduced);
  }
}
