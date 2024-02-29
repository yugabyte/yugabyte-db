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

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.util.PSQLException;

import java.sql.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;

import org.junit.*;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;
import org.yb.client.LocatedTablet;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.MasterDdlOuterClass;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public class ConcurrentTablespaceTest extends BaseTablespaceTest {
  private static final Logger LOG = LoggerFactory.getLogger(ConcurrentTablespaceTest.class);

  private static final int numStmtsPerThread = 12;
  private static final int numDmlThreads = 1;
  private static final int numDdlThreads = 1;

  private final AtomicBoolean errorsDetected = new AtomicBoolean(false);
  private final Tablespace[] tablespaces = generateTestTablespaces();
  private Connection[] connections;

  private static void resetBgThreads(int previousBGWait) throws Exception {
    YBClient client = miniCluster.getClient();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(
          client.setFlag(hp, "catalog_manager_bg_task_wait_ms", Integer.toString(previousBGWait)));
      assertTrue(
          client.setFlag(hp, "TEST_skip_placement_validation_createtable_api", "false", true));
    }
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(1500, 1700, 2000, 2000, 2000);
  }

  @After
  public void teardown() throws Exception {
    resetBgThreads(MiniYBCluster.CATALOG_MANAGER_BG_TASK_WAIT_MS);
  }

  private void configureBgThreads() throws Exception {
    String newDelay = "10000";
    YBClient client = miniCluster.getClient();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      // Increase the interval between subsequent runs of bg thread so that
      // it assigns replicas for tablets of both the tables concurrently.
      assertTrue(client.setFlag(hp, "catalog_manager_bg_task_wait_ms", newDelay));

      // Disable client-side placement validation for create table to speed up the test.
      assertTrue(
          client.setFlag(hp, "TEST_skip_placement_validation_createtable_api", "true", true));
    }
    LOG.info("Increased the delay between successive runs of bg threads to %s.", newDelay);
  }

  private Connection[] setupConnections() throws Exception {
    final int totalThreads = numDmlThreads + numDdlThreads;
    Connection[] connections = new Connection[totalThreads];
    for (int i = 0; i < totalThreads; ++i) {
      ConnectionBuilder b = getConnectionBuilder();
      b.withTServer(i % miniCluster.getNumTServers());
      connections[i] = b.connect();
    }
    return connections;
  }

  /**
   * Generates several tablespaces with varying placement and replication.
   *
   * @return Array of generated tablespaces.
   */
  private Tablespace[] generateTestTablespaces() {
    // Single-node tablespaces
    Tablespace ts1 = new Tablespace("testTsZone1", Collections.singletonList(1));
    Tablespace ts2 = new Tablespace("testTsZone2", Collections.singletonList(2));
    Tablespace ts3 = new Tablespace("testTsZone3", Collections.singletonList(3));

    // Double-node tablespaces
    Tablespace ts12 = new Tablespace("testTsZone12", Arrays.asList(1, 2));
    Tablespace ts13 = new Tablespace("testTsZone13", Arrays.asList(1, 3));
    Tablespace ts23 = new Tablespace("testTsZone23", Arrays.asList(2, 3));

    // Triple-node tablespace
    Tablespace ts123 = new Tablespace("testTsZone123", Arrays.asList(1, 2, 3));

    return new Tablespace[] {ts1, ts2, ts3, ts12, ts23, ts13, ts123};
  }

  private List<Thread> setupConcurrentDdlDmlThreads(String ddlTemplate) {
    final int totalThreads = numDmlThreads + numDdlThreads;
    final CyclicBarrier barrier = new CyclicBarrier(totalThreads);
    final List<Thread> threads = new ArrayList<>();

    // Add the DDL thread.
    for (int i = 0; i < numDdlThreads; ++i) {
      threads.add(
          new DDLRunner(
              connections[i],
              ddlTemplate,
              errorsDetected,
              barrier,
              numStmtsPerThread,
              tablespaces));
    }

    // Add the DML threads.
    for (int i = numDdlThreads; i < totalThreads; ++i) {
      threads.add(new DMLRunner(connections[i], errorsDetected, barrier, numStmtsPerThread, i));
    }
    return threads;
  }

  private void runThreads(List<Thread> threads) throws InterruptedException {
    threads.forEach(Thread::start);
    for (Thread t : threads) {
      t.join();
    }
  }

  /**
   * The DDL thread cycles through the tablespaces in the tablespaces array round-robin. In total,
   * each thread does numStmtsPerThread ALTER TABLESPACE commands. This function returns the
   * expected final tablespace after all the threads are done.
   */
  private Tablespace getExpectedFinalTablespace() {
    return tablespaces[(numStmtsPerThread - 1) % tablespaces.length];
  }

  /**
   * Performs the setup for concurrent tests.
   *
   * <p>Creates the objects used for the test, configures the background threads, and creates the
   * necessary connections.
   *
   * @throws Exception
   */
  @Before
  public void setup() throws Exception {
    markClusterNeedsRecreation();

    try (Statement stmt = connection.createStatement()) {
      stmt.execute(
          "CREATE TABLE concurrent_test_tbl (k INT PRIMARY KEY, v1 INT DEFAULT 10, v2 INT DEFAULT"
              + " 20)");
      stmt.execute("CREATE INDEX concurrent_test_idx ON concurrent_test_tbl(v1)");
      stmt.execute(
          "CREATE MATERIALIZED VIEW concurrent_test_mv AS SELECT * FROM concurrent_test_tbl");
    }

    configureBgThreads();

    // Create each tablespace.
    for (Tablespace ts : tablespaces) {
      ts.create(connection);
    }
  }

  @Test
  public void testAlterTableSetTablespace() throws Exception {
    connections = setupConnections();
    List<Thread> threads =
        setupConcurrentDdlDmlThreads("ALTER TABLE concurrent_test_tbl SET TABLESPACE %s");

    runThreads(threads);

    assertFalse(errorsDetected.get());

    verifyTablePlacement("concurrent_test_tbl", getExpectedFinalTablespace());
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DELETE FROM concurrent_test_tbl");
    }
  }

  @Test
  public void testAlterIndexSetTablespace() throws Exception {
    connections = setupConnections();
    List<Thread> threads =
        setupConcurrentDdlDmlThreads("ALTER INDEX concurrent_test_idx SET TABLESPACE %s");

    runThreads(threads);

    assertFalse(errorsDetected.get());

    verifyTablePlacement("concurrent_test_idx", getExpectedFinalTablespace());
    try (Statement stmt = connection.createStatement()) {
      stmt.execute("DELETE FROM concurrent_test_tbl");
    }
  }

  @Test
  public void testTableCreationFailure() throws Exception {
    YBClient client = miniCluster.getClient();
    connections = setupConnections();
    final int totalThreads = numDmlThreads + numDdlThreads;
    final CyclicBarrier barrier = new CyclicBarrier(totalThreads);
    final List<Thread> threads = new ArrayList<>();
    AtomicBoolean invalidPlacementError = new AtomicBoolean(false);

    // Use one of the already-created tablespaces as the valid tablespace.
    Tablespace valid_ts = tablespaces[4];

    // Add the valid DDL thread.
    threads.add(
        new DDLRunner(
            connections[0],
            "CREATE TABLE validplacementtable (a int) TABLESPACE %s",
            errorsDetected,
            barrier,
            1,
            new Tablespace[] {valid_ts}));

    // Add the invalid DDL thread.
    final Tablespace invalid_ts = new Tablespace("invalid_ts", Arrays.asList(1, 4));
    invalid_ts.create(connection);
    threads.add(
        new DDLRunner(
            connections[1],
            "CREATE TABLE invalid_placementtable (a int) TABLESPACE %s",
            invalidPlacementError,
            barrier,
            1,
            new Tablespace[] {invalid_ts}));

    runThreads(threads);

    // Verify that the transaction DDL garbage collector removes this table.
    assertTrue(client.waitForTableRemoval(30000, "invalidplacementtable"));

    assertFalse(errorsDetected.get());
    assertTrue(invalidPlacementError.get());
    verifyTablePlacement("validplacementtable", valid_ts);
  }

  /**
   * Base class for running DML and DDL statements concurrently.
   */
  public abstract class SQLRunner extends Thread {
    protected final Connection conn;
    protected final AtomicBoolean errorsDetected;
    protected final CyclicBarrier barrier;
    protected final int numStmtsPerThread;
    protected int idx; // Only used by DMLRunner

    public SQLRunner(
        Connection conn,
        AtomicBoolean errorsDetected,
        CyclicBarrier barrier,
        int numStmtsPerThread,
        int idx) {
      this.conn = conn;
      this.errorsDetected = errorsDetected;
      this.barrier = barrier;
      this.numStmtsPerThread = numStmtsPerThread;
      this.idx = idx; // This field is not used in DDLRunner
    }

    @Override
    public void run() {
      int item_idx = 0;
      while (item_idx < numStmtsPerThread && !errorsDetected.get()) {
        try (Statement lstmt = conn.createStatement()) {
          barrier.await();
          executeStatement(lstmt, item_idx);
          item_idx++;
        } catch (PSQLException e) {
          handlePSQLException(e);
        } catch (SQLException | InterruptedException | BrokenBarrierException e) {
          logAndSetError(e);
        }
      }
    }

    protected abstract void executeStatement(Statement lstmt, int item_idx) throws SQLException;

    /**
     * Handles PSQLExceptions by checking if the error is expected or unexpected. If the error is
     * expected, the function logs the error and returns. If the error is unexpected, we set
     * errorsDetected to true.
     */
    private void handlePSQLException(PSQLException e) {
      List<String> expectedErrors =
          Arrays.asList(
              "expired or aborted by a conflict",
              "Transaction aborted: kAborted",
              "schema version mismatch for table");
      if (expectedErrors.stream().anyMatch(error -> e.getMessage().contains(error))) {
        LOG.info("SQL thread: encountered expected error %s, retrying", e);
      } else {
        logAndSetError(e);
      }
    }

    protected void logAndSetError(Exception e) {
      LOG.info("SQL thread: Unexpected error: ", e);
      errorsDetected.set(true);
      barrier.reset();
    }
  }

  /** Helper class to run DML statements concurrently. */
  public class DMLRunner extends SQLRunner {
    private final String insert_sql =
        "INSERT INTO concurrent_test_tbl(k, v1, v2) " + "VALUES(%d, %d, %d)";
    private final String update_sql = "UPDATE concurrent_test_tbl SET v1 = v1 + 1 WHERE v1 = %d";

    public DMLRunner(
        Connection conn,
        AtomicBoolean errorsDetected,
        CyclicBarrier barrier,
        int numStmtsPerThread,
        int idx) {
      super(conn, errorsDetected, barrier, numStmtsPerThread, idx);
    }

    @Override
    protected void executeStatement(Statement lstmt, int item_idx) throws SQLException {
      executeInsertStatement(lstmt, item_idx);
      executeUpdateStatement(lstmt, item_idx);
    }

    private void executeInsertStatement(Statement lstmt, int item_idx) throws SQLException {
      lstmt.execute(
          String.format(
              insert_sql,
              idx * 10000000L + item_idx,
              idx * 10000000L + item_idx + 1,
              idx * 10000000L + item_idx + 2));
    }

    private void executeUpdateStatement(Statement lstmt, int item_idx) throws SQLException {
      lstmt.execute(String.format(update_sql, item_idx));
    }
  }

  /** Helper class to run DDL statements concurrently. */
  public class DDLRunner extends SQLRunner {
    private final String sql;
    private final Tablespace[] tablespaces;

    public DDLRunner(
        Connection conn,
        String sql,
        AtomicBoolean errorsDetected,
        CyclicBarrier barrier,
        int numStmtsPerThread,
        Tablespace[] tablespaces) {
      super(conn, errorsDetected, barrier, numStmtsPerThread, 0); // idx is not used here
      this.sql = sql;
      this.tablespaces = tablespaces;
    }

    @Override
    protected void executeStatement(Statement lstmt, int item_idx) throws SQLException {
      final int tablespaceIdx = item_idx % tablespaces.length;
      String sqlStmt = String.format(sql, tablespaces[tablespaceIdx].name);
      LOG.info("DDL thread: Executing statement: " + sqlStmt);
      lstmt.execute(sqlStmt);
    }
  }
}
