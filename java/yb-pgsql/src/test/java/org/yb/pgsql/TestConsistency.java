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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.stream.Collectors;

import org.apache.commons.lang3.RandomUtils;
import org.apache.commons.lang3.time.StopWatch;
import org.junit.Test;
import org.junit.runner.RunWith;
import com.yugabyte.util.PSQLException;
import com.yugabyte.util.PSQLState;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.Timeouts;
import org.yb.YBTestRunner;

/**
 * Simple consistency tests inspired by Jepsen. Only tests {@code SERIALIZABLE} isolation for now.
 */
@RunWith(value = YBTestRunner.class)
public class TestConsistency extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestConsistency.class);

  /** How long (in seconds) do we want workloads to run? */
  private final int TIME_LIMIT_S = 200;

  @Override
  public int getTestMethodTimeoutSec() {
    // Global time limit doesn't depend on a build type, so we only adjust a margin time.
    return (int) ((TIME_LIMIT_S + Timeouts.adjustTimeoutSecForBuildType(50)) * 1.5);
  }

  /** Use a (non-unique) index instead of a PK for the long-fork. */
  @Test
  public void longFork_serializable_index() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.executeUpdate("CREATE TABLE long_fork(key int PRIMARY KEY, key2 int, val int)");
      stmt.executeUpdate("CREATE INDEX long_fork_idx ON long_fork (key2) INCLUDE (val)");
    }
    longForkWorkload(
        IsolationLevel.SERIALIZABLE,
        (keysCsv) -> String.format("SELECT key2, val FROM long_fork WHERE key2 IN (%s)", keysCsv),
        (k) -> String.format("INSERT INTO long_fork(key, key2, val) VALUES (%d, %d, %d)", k, k, k));
  }

  /**
   * Execute simplified Jepsen's long-fork workload. Tables should already be created.
   *
   * @param readQuery
   *          given a comma-separated list of keys, return an SQL selecting (key, val)
   * @param writeQuery
   *          given a key, return an SQL inserting a new row with that value in every column
   */
  public void longForkWorkload(
      IsolationLevel isolation,
      Function<String, String> readQuery,
      Function<Integer, String> writeQuery) throws Exception {

    // Predefined paramters.
    // Probably not much value in increasing complexity by making them configurable.

    final int concurrencyPerNode = 4; // How many worker threads per node do we want?
    final int groupsNum = 3; // How many logical worker groups do we want?
    final double readProbablitiy = 0.5; // What percent of ops do we want to be reads?
    final int hindsight = 10; // How many recent writes (from each group) do we want to read?
    // For each read, how many next reads we want to analyze?
    // (Anomaly would probably be in the nearest 1-2 reads, but better safe than sorry)
    final int neightborsToLookAt = 500;

    AtomicInteger lastKeyUsed = new AtomicInteger();
    Map<Integer, List<Integer>> keysUsedPerGroup = Collections.synchronizedMap(new HashMap<>());
    for (int i = 0; i < groupsNum; ++i) {
      keysUsedPerGroup.put(i, Collections.synchronizedList(new ArrayList<>()));
    }

    List<Map<Integer, Integer>> reads = Collections.synchronizedList(new ArrayList<>());
    Workload wl = new Workload(isolation);
    for (int c = 0; c < concurrencyPerNode; ++c) {
      for (int tsIdx = 0; tsIdx < miniCluster.getNumTServers(); ++tsIdx) {
        final int group = RandomUtils.nextInt(0, groupsNum);
        wl.addThread(tsIdx, (conn) -> {
          boolean shouldRead = Math.random() < readProbablitiy;

          try (Statement stmt = conn.createStatement()) {
            if (shouldRead) {
              List<Integer> keysToRead = new ArrayList<>();
              for (List<Integer> groupKeys : keysUsedPerGroup.values()) {
                int lowerIdx = Math.max(0, groupKeys.size() - hindsight);
                synchronized (groupKeys) {
                  keysToRead.addAll(groupKeys.subList(lowerIdx, groupKeys.size()));
                }
              }
              if (keysToRead.isEmpty()) {
                return; // Nothing to read yet.
              }
              String query = readQuery.apply(
                  keysToRead.stream()
                      .map((v) -> v.toString())
                      .collect(Collectors.joining(", ")));
              List<Row> rows = getRowList(stmt.executeQuery(query));
              Map<Integer, Integer> read = new TreeMap<>();
              for (Integer k : keysToRead) {
                read.put(k, null);
              }
              for (Row row : rows) {
                read.put(row.getInt(0), row.getInt(1));
              }
              reads.add(read);
            } else {
              int k = lastKeyUsed.incrementAndGet();
              keysUsedPerGroup.get(group).add(k);
              stmt.executeUpdate(writeQuery.apply(k));
            }
          } catch (PSQLException ex) {
            // Ignore transactional errors, re-throw the rest.
            String sqlState = ex.getSQLState();
            // Using Yoda conditions to avoid NPE in the theoretical case of sqlState being null.
            if (!PSQLState.IN_FAILED_SQL_TRANSACTION.getState().equals(sqlState)
                && !SNAPSHOT_TOO_OLD_PSQL_STATE.equals(sqlState)
                && !SERIALIZATION_FAILURE_PSQL_STATE.equals(sqlState)) {
              LOG.info("=== sqlstate = " + sqlState);
              throw ex;
            }
          }
        });
      }
    }
    wl.executeWorkload();
    LOG.info(reads.size() + " reads performed");
    assertFalse("No reads has been performed, test bug?", reads.isEmpty());

    // Reads may be out of order, but each read (except first) must observe
    // a superset of what another read saw.
    // E.g. suppose three reads saw this:
    //   a. [1, 2, 3]
    //   b. [-, 2, -]
    //   c. [1, 2, -]
    // This is fine because there is an order [b, c, a] that would make them serial.
    // What we DON'T want to see is this:
    //   a. [1, -]
    //   b. [-, 2]
    // That's the type of anomaly this workload looks for.

    // Cross-compare every read with its neighbors
    // (cutting corners to avoid  O(n^2) comparison of every pair).
    LOG.info("Checking invariants");
    StopWatch sw = StopWatch.createStarted();
    for (int i = 0; i < reads.size(); ++i) {
      Map<Integer, Integer> read1 = reads.get(i);
      int maxCheckIdx = Math.min(reads.size() - 1, i + neightborsToLookAt);
      for (int j = i + 1; j <= maxCheckIdx; ++j) {
        Map<Integer, Integer> read2 = reads.get(j);

        boolean read2ComesAfter = false;
        // We don't care about actual values - we know them in advance anyway.
        for (int key : read2.keySet()) {
          if (read1.containsKey(key) && read1.get(key) == null && read2.get(key) != null) {
            read2ComesAfter = true;
            break;
          }
        }
        if (read2ComesAfter) {
          // read2 has to see everything read1 saw.
          for (int key : read1.keySet()) {
            if (read2.containsKey(key) && read2.get(key) == null && read1.get(key) != null) {
              LOG.info("Invariants chcked in " + sw.getTime() + " ms, analysis invalid!");
              fail("Anomaly found! Read values:\n"
                  + "#" + i + " " + read1 + "\n"
                  + "#" + j + " " + read2);
            }
          }
        }
      }
    }
    LOG.info("Invariants chcked in " + sw.getTime() + " ms, everything looks good");
  }

  //
  // Helpers
  //

  /**
   * Manages a Jepsen-like workload consisting of many concurrent worker threads querying different
   * nodes.
   */
  private class Workload {
    public final ThreadGroup tg = new ThreadGroup("workloads");

    // ThreadGroup can only be queried for *ACTIVE* threads,
    // so we have keep track of *ALL* threads manually.
    private final List<WorkloadThread> threads = new ArrayList<>();

    private final IsolationLevel isolation;

    public Workload(IsolationLevel isolation) {
      this.isolation = isolation;
    }

    /**
     * Add a worker thread executing a given operation in a dedicated connection on a given tserver.
     * Thread isn't started yet.
     */
    public void addThread(int tserverIdx, Operation op) {
      WorkloadThread wt = new WorkloadThread(tserverIdx, op);
      threads.add(wt);
    }

    /**
     * Start worker threads and allow the workload to execute for
     * {@link TestConsistency#TIME_LIMIT_S} seconds, then interrupt it. If any worker fails,
     * re-throw that failure.
     */
    private void executeWorkload() throws Exception {
      for (WorkloadThread wt : threads) {
        wt.start();
      }

      // Wait for N seconds in 1 sec intervals, detecting early failures in the process.
      for (int sec = 0; sec < TIME_LIMIT_S; ++sec) {
        Thread.sleep(1000);
        for (WorkloadThread t : threads) {
          if (t.getFailure() != null) {
            tg.interrupt();
            throw t.getFailure();
          }
        }
      }

      tg.interrupt();
      for (WorkloadThread t : threads) {
        t.join(10000); // 10s should be more than enough to finish any remaining ops.
      }
      for (WorkloadThread t : threads) {
        if (t.getFailure() != null) throw t.getFailure();
      }
      assertTrue("Some worker threads are still alive!", tg.activeCount() == 0);
    }

    /** Worker thread for workload. Operations should preferrably be quick and simple. */
    private class WorkloadThread extends Thread {
      private final int tserverIdx;
      private Exception failure;
      private Operation op;

      public WorkloadThread(int tserverIdx, Operation op) {
        super(tg, "WorkloadThread for ts#" + tserverIdx
            + " (rnd=" + RandomUtils.nextInt(0, 1000) + ")");
        this.tserverIdx = tserverIdx;
        this.op = op;
      }

      @Override
      public void run() {
        try (Connection conn = getConnectionBuilder()
            .withTServer(tserverIdx)
            .withIsolationLevel(isolation)
            .connect()) {
          while (!Thread.interrupted()) {
            op.execute(conn);
          }
        } catch (InterruptedException ex) {
          // Don't care, that's normal - just terminate.
        } catch (Exception ex) {
          failure = ex;
        }
      }

      public Exception getFailure() {
        return failure;
      }
    }
  }

  /**
   * Execute a single workload operation. Uncaught exception would mean the workload has crashed.
   */
  @FunctionalInterface
  private interface Operation {

    void execute(Connection conn) throws Exception;
  }
}
