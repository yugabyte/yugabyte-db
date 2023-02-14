package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.atomic.AtomicBoolean;

import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertFalse;

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

  @Test
  public void testModifiedTableWrite() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE TABLE t(k INT PRIMARY KEY, v1 INT DEFAULT 10, v2 INT DEFAULT 20)");
      int count = 50;
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
      threads[0] = new Thread(() -> {
        try (Statement lstmt = connections[0].createStatement()) {
          while (!stopped.get() && !errorsDetected.get()) {
            barrier.await();
            lstmt.execute("ALTER TABLE t DROP COLUMN IF EXISTS v");
            lstmt.execute("ALTER TABLE t ADD COLUMN v INT DEFAULT 100");
          }
        } catch (SQLException e) {
          LOG.error("Unexpected exception", e);
          errorsDetected.set(true);
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
                 !stopped.get() && !errorsDetected.get() && item_idx < 1000000;
                 item_idx += 2) {
              barrier.await();
              try {
                // TODO(dmitry): In spite of the fact system catalog is being read in consistent
                // manner PgSession::table_cache_ may have outdated YBTable object. As a result
                // an error like 'Invalid argument: Invalid column number 8' might be raised by
                // the next statement. Github issue #8096 is created for the problem.
                lstmt.execute(String.format("INSERT INTO t(k) VALUES(%d), (%d)",
                                            idx * 10000000L + item_idx,
                                            idx * 10000000L + item_idx + 1));
              } catch (Exception e) {
                final String msg = e.getMessage();
                if (!(msg.contains("Catalog Version Mismatch") ||
                      msg.contains("Restart read required") ||
                      msg.contains("schema version mismatch"))) {
                  LOG.error("Unexpected exception", e);
                  errorsDetected.set(true);
                  return;
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
      final long startTimeMs = System.currentTimeMillis();
      while (System.currentTimeMillis() - startTimeMs < 10000 && !errorsDetected.get()) {
        Thread.sleep(1000);
      }
      stopped.set(true);
      for (Thread t : threads) {
        t.join();
      }
      assertFalse(errorsDetected.get());
      Row row = getSingleRow(stmt, "SELECT COUNT(*) FROM t");
      assertGreaterThan(row.getLong(0), 0L);
    }
  }
}
