package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;
import org.yb.util.BuildTypeUtil;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.Map;

import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

@RunWith(value=YBTestRunner.class)
public class TestPgDdlConcurrency extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgDdlConcurrency.class);

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  protected int getInitialNumMasters() {
    return 1;
  }

  protected int getInitialNumTServers() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    // TODO(#28745): Revisit this. Runs into a deadlock issue with table locks/txn-ddl enabled.
    flags.put("enable_object_locking_for_table_locks", "false");
    flags.put("ysql_yb_ddl_transaction_block_enabled", "false");
    return flags;
  }

  private boolean timeoutReached = false;
  private long currentTimeMs = 0;
  private boolean hasTimedout(final long startTimeMs) {
    final long timeoutMs = 1500000;
    if (timeoutReached) {
      return true;
    }
    currentTimeMs = System.currentTimeMillis();
    if (currentTimeMs - startTimeMs < timeoutMs) {
      return false;
    }
    timeoutReached = true;
    return true;
  }

  @Test
  public void testModifiedTableWrite() throws Exception {
    final long startTimeMs = System.currentTimeMillis();
    String enable_invalidation_messages = miniCluster.getClient().getFlag(
          miniCluster.getTabletServers().keySet().iterator().next(),
          "ysql_yb_enable_invalidation_messages");
    LOG.info("enable_invalidation_messages: " + enable_invalidation_messages);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t(k INT PRIMARY KEY, v1 INT DEFAULT 10, v2 INT DEFAULT 20)");
      final int count = 10;

      final AtomicBoolean errorsDetected = new AtomicBoolean(false);
      final AtomicBoolean stopped = new AtomicBoolean(false);
      final CyclicBarrier barrier = new CyclicBarrier(count);
      final Thread[] threads = new Thread[count];
      final Connection[] connections = new Connection[count];
      for (int i = 0; i < count; ++i) {
        ConnectionBuilder b = getConnectionBuilder();
        b.withTServer(count % miniCluster.getNumTServers());
        connections[i] = b.connect();
      }
      final AtomicInteger expectedExceptionsCount = new AtomicInteger(0);
      threads[0] = new Thread(() -> {
        try (Statement lstmt = connections[0].createStatement()) {
          while (!stopped.get() && !errorsDetected.get() && expectedExceptionsCount.get() == 0 &&
                 !hasTimedout(startTimeMs)) {
            barrier.await();
            for (int i = 0; i < 20; ++i) {
              lstmt.execute("ALTER TABLE t DROP COLUMN IF EXISTS v");
              lstmt.execute("ALTER TABLE t ADD COLUMN v INT DEFAULT 100");
            }
          }
        } catch (SQLException e) {
          final String msg = e.getMessage();
          if (msg.matches(".*tables can have at most \\d+ columns.*") &&
              enable_invalidation_messages.equals("true")) {
            // With incremental catalog cache refresh, it is not easy to hit one of the
            // normal expected errors. Because dropped columns also count towards the
            // PG's 1600-column limit, we may hit this error first before test timeout.
            // However, tsan or asan build types do not run as fast to hit 1600-column
            // error before they run out of test time.
            assertTrue(!BuildTypeUtil.isSanitizerBuild());
            expectedExceptionsCount.incrementAndGet();
          } else {
            LOG.error("Unexpected exception", e);
            errorsDetected.set(true);
          }
          return;
        } catch (InterruptedException | BrokenBarrierException throwables) {
          LOG.info("Infrastructure exception, can be ignored", throwables);
        } finally {
          barrier.reset();
        }
      });
      for (int i = 1; i < count; ++i) {
        final int idx = i;
        threads[i] = new Thread(() -> {
          try (Statement lstmt = connections[idx].createStatement()) {
            for (int item_idx = 0;
                 !stopped.get() && !errorsDetected.get() && expectedExceptionsCount.get() == 0 &&
                 !hasTimedout(startTimeMs); item_idx += 2) {
              barrier.await();
              try {
                lstmt.execute(String.format("INSERT INTO t(k) VALUES(%d), (%d)",
                                            idx * 10000000L + item_idx,
                                            idx * 10000000L + item_idx + 1));
              } catch (SQLException e) {
                final String msg = e.getMessage();
                if (e.getSQLState().equals(SERIALIZATION_FAILURE_PSQL_STATE) ||
                    msg.contains("Catalog Version Mismatch") ||
                    msg.contains("Restart read required") ||
                    msg.contains("schema version mismatch") ||
                    msg.contains("marked for deletion in table")) {
                    expectedExceptionsCount.incrementAndGet();
                } else if (msg.contains("Invalid column number") ||
                           msg.matches(".*Column with id \\d+ marked for deletion in table.*")) {
                  // TODO(dmitry): In spite of the fact system catalog is being read in consistent
                  // manner PgSession::table_cache_ may have outdated YBTable object. As a result
                  // an error like 'Invalid argument: Invalid column number 8' or
                  // 'Column with id 3 marked for deletion in table' might be raised by the next
                  // statement. Github issue #8096 is created for the problem.
                  LOG.warn("Inconsistent table cache error detected", e);
                  expectedExceptionsCount.incrementAndGet();
                } else {
                  LOG.error("Unexpected exception", e);
                  errorsDetected.set(true);
                  return;
                }
                if (e.getSQLState().equals(SERIALIZATION_FAILURE_PSQL_STATE)) {
                  LOG.info("found SERIALIZATION_FAILURE_PSQL_STATE");
                } else {
                  LOG.info("Expected exception", e);
                }
              }
            }
          } catch (InterruptedException | BrokenBarrierException | SQLException throwables) {
            LOG.info("Infrastructure exception, can be ignored", throwables);
          } finally {
            barrier.reset();
          }
        });
      }
      Arrays.stream(threads).forEach(t -> t.start());
      while (!errorsDetected.get() && expectedExceptionsCount.get() == 0 &&
             !hasTimedout(startTimeMs)) {
        Thread.sleep(1000);
      }
      stopped.set(true);
      for (Thread t : threads) {
        t.join();
      }
      LOG.info("startTimeMs: " + startTimeMs + ", currentTimeMs: " + currentTimeMs +
               ", timeoutReached: " + Boolean.toString(timeoutReached));
      assertFalse(errorsDetected.get());
      Row row = getSingleRow(stmt, "SELECT COUNT(*) FROM t");
      assertGreaterThan(row.getLong(0), 0L);
      if (enable_invalidation_messages.equals("false")) {
        assertGreaterThan(expectedExceptionsCount.get(), 0);
      } else if (expectedExceptionsCount.get() == 0) {
        // With incremental catalog cache refresh, we may not hit any of the expected errors
        // before timeout is reached.
        assertTrue(timeoutReached);
      }
    }
  }
}
