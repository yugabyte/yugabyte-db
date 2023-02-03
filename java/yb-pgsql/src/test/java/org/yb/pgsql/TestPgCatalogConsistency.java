package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.assertGreaterThan;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.Queue;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

@RunWith(value=YBTestRunner.class)
public class TestPgCatalogConsistency extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgCatalogConsistency.class);

  /**
   * Test continuously runs the 'ALTER TABLE' query to add/remove column with default value
   * in a dedicated thread. At same time other threads read multiple catalog tables
   * ('pg_attribute' and 'pg_attrdef' both are changed by the 'ALTER TABLE' query) and check
   * that read state is consistent.
   */
  @Test
  public void testAlterTable() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(
        "CREATE TABLE unstable_table(k INT PRIMARY KEY, v1 INT DEFAULT 10, v2 INT DEFAULT 20)");
      // The relation_def_args function gets the number of attributes and
      // the number of attributes with default values by reading 2 system tables in one statement.
      // This behavior emulates postgres's internal cache update mechanism for relation.
      stmt.execute(
          "CREATE OR REPLACE FUNCTION relation_def_args(relid int) RETURNS RECORD AS $$" +
          "    DECLARE ac INT;" +
          "    DECLARE dac INT;" +
          "    DECLARE r RECORD;" +
          "BEGIN" +
          "    ac := 0;" +
          "    dac := 0;" +
          "    FOR r IN SELECT * FROM pg_attribute WHERE attrelid=relid AND attisdropped=false" +
          "    LOOP" +
          "        ac := ac + 1;" +
          "    END LOOP;" +
          "    FOR r IN SELECT * FROM pg_attrdef WHERE adrelid=relid" +
          "    LOOP" +
          "        dac := dac + 1;" +
          "    END LOOP;" +
          "    SELECT ac, dac INTO r;" +
          "    RETURN r;" +
          "END" +
          "$$ LANGUAGE plpgsql;");
      long relid = getSingleRow(stmt, String.format(
          "SELECT oid FROM pg_class WHERE relname='%s'", "unstable_table")).getLong(0);
      String testQuery = String.format(
          "SELECT * FROM relation_def_args(%d) AS r(attr_count INT, def_attr_count INT)", relid);
      Row baseCounts = getSingleRow(stmt, testQuery);
      int noDefaultAttrCount = baseCounts.getInt(0) - baseCounts.getInt(1);
      int count = 50;
      AtomicInteger alterTableSuccessfulCycles = new AtomicInteger(0);
      AtomicInteger catalogCheckSuccessfulCycles = new AtomicInteger(0);
      Queue<String> detectedErrors = new ConcurrentLinkedQueue<>();
      AtomicBoolean stopped = new AtomicBoolean(false);
      CyclicBarrier barrier = new CyclicBarrier(count);
      Thread[] threads = new Thread[count];
      Connection[] connections = new Connection[count];
      for (int i = 0; i < count; ++i) {
        ConnectionBuilder b = getConnectionBuilder();
        b.withTServer(count % miniCluster.getNumTServers());
        connections[i] = b.connect();
      }
      threads[0] = new Thread(() -> {
        int successfulCycles = 0;
        try (Statement lstmt = connections[0].createStatement()) {
          while(!stopped.get() && detectedErrors.isEmpty()) {
            barrier.await();
            lstmt.execute("ALTER TABLE unstable_table DROP COLUMN IF EXISTS v");
            lstmt.execute("ALTER TABLE unstable_table ADD COLUMN v INT DEFAULT 100");
            ++successfulCycles;
          }
        } catch (SQLException e) {
          LOG.error("Unexpected exception", e);
          detectedErrors.add(e.getMessage());
        } catch (InterruptedException | BrokenBarrierException throwables) {
          LOG.info("Infrastructure exception, can be ignored", throwables);
        } finally {
          // Barrier is reset at the end of each thread to prevent other threads from hanging
          // out on next call of 'barrier.await()'. Instead of waiting the BrokenBarrierException
          // exception is raised in this case. This exception is treated as expected.
          barrier.reset();
          alterTableSuccessfulCycles.addAndGet(successfulCycles);
        }
      });
      for (int i = 1; i < count; ++i) {
        int idx = i;
        threads[i] = new Thread(() -> {
          int successfulCycles = 0;
          try (Statement lstmt = connections[idx].createStatement()) {
            for (; !stopped.get() && detectedErrors.isEmpty();) {
              barrier.await();
              Row counts = getSingleRow(lstmt, testQuery);
              int attrCount = counts.getInt(0);
              int defAttrCount = counts.getInt(1);
              // Because ALTER TABLE adds/removes attribute with default value the number
              // of attributes without default value must be equal all the time.
              if ((attrCount - defAttrCount) != noDefaultAttrCount) {
                detectedErrors.add(
                  String.format(
                    "Unexpected number of attributes %d where is default %d",
                    attrCount, defAttrCount));
                return;
              }
              ++successfulCycles;
            }
          } catch (SQLException e) {
            LOG.error("Unexpected exception", e);
            detectedErrors.add(e.getMessage());
          } catch (InterruptedException | BrokenBarrierException throwables) {
            LOG.info("Infrastructure exception, can be ignored", throwables);
          } finally {
            barrier.reset();
            catalogCheckSuccessfulCycles.addAndGet(successfulCycles);
          }
        });
      }
      Arrays.stream(threads).forEach(t -> t.start());
      long startTimeMs = System.currentTimeMillis();
      while (System.currentTimeMillis() - startTimeMs < 10000 && detectedErrors.isEmpty()) {
        Thread.sleep(1000);
      }
      stopped.set(true);
      for (Thread t : threads) {
        t.join();
      }
      for (String error : detectedErrors) {
        LOG.error("Error detected: {}", error);
      }
      assertTrue(detectedErrors.isEmpty());
      assertGreaterThan(alterTableSuccessfulCycles.get(), 0);
      assertGreaterThan(catalogCheckSuccessfulCycles.get(), 0);
    }
  }
}
