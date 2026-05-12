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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

import java.util.Collections;
import java.util.Map;
import com.datastax.driver.core.*;
import com.datastax.driver.core.QueryOptions;
import com.datastax.driver.core.SocketOptions;
import com.datastax.driver.core.ConsistencyLevel;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgViewYCQLStats extends BasePgSQLTest {

  private static final Logger LOG = LoggerFactory.getLogger(TestPgViewYCQLStats.class);
  protected int cqlClientTimeoutMs = 120 * 1000;

  /*
   * ycql_stat_statements reflects this(local) tablet server's CQL proxy only, while the Java
   * driver load-balances across tservers. So SUM(calls) for a fingerprint is in (0, N] on this node
   * where N is the number of executions issued in the test (cluster-wide), never more than N.
   */
  private static void assertLocalCallsPositiveAndAtMost(
      long sumCalls, int maxClusterWideExecutions, String label) {
    assertTrue(label + ": expected SUM(calls) > 0 on local TS, got " + sumCalls, sumCalls > 0);
    assertTrue(label + ": expected SUM(calls) <= " + maxClusterWideExecutions
        + " on local TS (CQL LB spreads load across tservers), got " + sumCalls,
        sumCalls <= maxClusterWideExecutions);
  }

  /** Convenient default cluster for tests to use, cleaned after each test. */
  protected Cluster cluster;

  /** Convenient default session for tests to use, cleaned after each test. */
  protected Session session;

  @Override
  protected void resetSettings() {
    super.resetSettings();
    startCqlProxy = true;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_yb_enable_ash", "true");
    flagMap.put("ysql_yb_ash_sampling_interval_ms", "10");
    flagMap.put("ysql_yb_ash_sample_size", "500");
    return flagMap;
  }

  public Cluster.Builder getDefaultClusterBuilder() {
    // Set default consistency level to strong consistency
    QueryOptions queryOptions = new QueryOptions();
    queryOptions.setConsistencyLevel(ConsistencyLevel.YB_STRONG);
    // Set a long timeout for CQL queries since build servers might be really slow (especially Mac
    // Mini).
    SocketOptions socketOptions = new SocketOptions();
    socketOptions.setReadTimeoutMillis(cqlClientTimeoutMs);
    socketOptions.setConnectTimeoutMillis(cqlClientTimeoutMs);
    return Cluster.builder()
              .addContactPointsWithPorts(miniCluster.getCQLContactPoints())
              .withQueryOptions(queryOptions)
              .withSocketOptions(socketOptions);
  }

  /** Create a CQL client  */
  public void setUpCqlClient() throws Exception {
    LOG.info("setUpCqlClient is running");

    if (miniCluster == null) {
      final String errorMsg =
          "Mini-cluster must already be running by the time setUpCqlClient is invoked";
      LOG.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }

    try {
      cluster = getDefaultClusterBuilder().build();
      session = cluster.connect();
      LOG.info("Connected to cluster: " + cluster.getMetadata().getClusterName());
    } catch (Exception ex) {
      LOG.error("Error while setting up a CQL client", ex);
      throw ex;
    }

    final int numTServers = miniCluster.getTabletServers().size();
    final int expectedNumPeers = Math.max(0, Math.min(numTServers - 1, 2));
    LOG.info("Waiting until system.peers contains at least " + expectedNumPeers + " entries (" +
        "number of tablet servers: " + numTServers + ")");
    int attemptsMade = 0;
    boolean waitSuccessful = false;

    while (attemptsMade < 30) {
      int numPeers = session.execute("SELECT peer FROM system.peers").all().size();
      if (numPeers >= expectedNumPeers) {
        waitSuccessful = true;
        break;
      }
      LOG.info("system.peers still contains only " + numPeers + " entries, waiting");
      attemptsMade++;
      Thread.sleep(1000);
    }
    if (waitSuccessful) {
      LOG.info("Succeeded waiting for " + expectedNumPeers + " peers to show up in system.peers");
    } else {
      LOG.warn("Timed out waiting for " + expectedNumPeers + " peers to show up in system.peers");
    }
  }

  @Test
  public void testYCQLStats() throws Exception {
    setUpCqlClient();
    session.execute("create keyspace k1").one();
    session.execute("use k1").one();
    session.execute("create table table1(col1 int, col2 int, primary key (col1))").one();
    PreparedStatement ps = session.prepare("select col1 from table1");
    for (int i = 0; i < 10; i++) {
      session.execute(ps.bind()).iterator();
    }
    for (int i = 0; i < 10; i++) {
      session.execute("select col2 from table1").one();
    }
    try (Statement statement = connection.createStatement()) {
      statement.execute("create extension yb_ycql_utils");
      int count_prepared = getSingleRow(statement, "SELECT COUNT(*) FROM ycql_stat_statements"  +
          " WHERE is_prepared='t' and query like '%select col1%'").getLong(0).intValue();
      int count_unprepared = getSingleRow(statement, "SELECT COUNT(*) FROM ycql_stat_statements"  +
          " WHERE is_prepared='f' and query like '%select col2%'").getLong(0).intValue();

      assertEquals(count_prepared, 1);
      assertEquals(count_unprepared, 1);

    }
    session.execute("drop table table1").one();
  }

  @Test
  public void testMixedBatchStatements() throws Exception {
    setUpCqlClient();
    session.execute("CREATE KEYSPACE IF NOT EXISTS kb1 WITH replication = " +
        "{'class': 'SimpleStrategy', 'replication_factor': 1}");
    session.execute("USE kb1");
    session.execute("CREATE TABLE IF NOT EXISTS batch_mix " +
        "(k INT PRIMARY KEY, v TEXT, v2 INT)");

    for (int i = 0; i < 30; i++) {
      session.execute("INSERT INTO batch_mix (k, v, v2) VALUES (" +
          i + ", 'init', 0)");
    }

    PreparedStatement insertPs = session.prepare(
        "INSERT INTO batch_mix (k, v, v2) VALUES (?, ?, ?)");
    PreparedStatement updatePs = session.prepare(
        "UPDATE batch_mix SET v = ? WHERE k = ?");
    PreparedStatement deletePs = session.prepare(
        "DELETE FROM batch_mix WHERE k = ?");

    for (int i = 0; i < 10; i++) {
      BatchStatement batch = new BatchStatement();
      batch.add(insertPs.bind(100 + i, "batch", i)); // k, v, v2
      batch.add(updatePs.bind("upd_" + i, i));  // v, k
      batch.add(deletePs.bind(20 + i));  // k
      session.execute(batch);
    }

    Thread.sleep(5000);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS yb_ycql_utils");

      long entryCount = getSingleRow(stmt,
          "SELECT COUNT(*) FROM ycql_stat_statements " +
          "WHERE query LIKE '%INSERT INTO batch_mix%' " +
          "AND query LIKE '%UPDATE batch_mix%' " +
          "AND query LIKE '%DELETE FROM batch_mix%'").getLong(0);
      assertTrue("Expected batch entries in ycql_stat_statements, got " +
          entryCount, entryCount > 0);

      Row metricsRow = getSingleRow(stmt,
          "SELECT SUM(calls)::bigint, SUM(total_time), MAX(max_time) " +
          "FROM ycql_stat_statements " +
          "WHERE query LIKE '%INSERT INTO batch_mix%' " +
          "AND query LIKE '%UPDATE batch_mix%' " +
          "AND query LIKE '%DELETE FROM batch_mix%'");
      long totalCalls = metricsRow.getLong(0);
      double totalTime = metricsRow.getDouble(1);
      double maxTime = metricsRow.getDouble(2);
      // One ycql_stat_statements row per mixed-batch fingerprint; calls += 1 per BATCH execution
      // (10 iterations), not per child (not 30).
      assertLocalCallsPositiveAndAtMost(totalCalls, 10, "mixed batch (INSERT+UPDATE+DELETE)");
      assertTrue("Expected total_time > 0, got " + totalTime, totalTime > 0);
      assertTrue("Expected max_time > 0, got " + maxTime, maxTime > 0);
    }

    session.execute("DROP TABLE batch_mix");
  }

  @Test
  public void testSingleQueryBatchMatchesNonBatch() throws Exception {
    setUpCqlClient();
    session.execute("CREATE KEYSPACE IF NOT EXISTS kb2 WITH replication = " +
        "{'class': 'SimpleStrategy', 'replication_factor': 1}");
    session.execute("USE kb2");
    session.execute("CREATE TABLE IF NOT EXISTS sq_test " +
        "(k INT PRIMARY KEY, v TEXT)");

    for (int i = 0; i < 50; i++) {
      session.execute("INSERT INTO sq_test (k, v) VALUES (" + i + ", 'init')");
    }

    PreparedStatement insertPs = session.prepare(
        "INSERT INTO sq_test (k, v) VALUES (?, ?)");
    PreparedStatement updatePs = session.prepare(
        "UPDATE sq_test SET v = ? WHERE k = ?");
    PreparedStatement deletePs = session.prepare(
        "DELETE FROM sq_test WHERE k = ?");

    for (int i = 0; i < 5; i++) {
      session.execute(insertPs.bind(100 + i, "prep_val"));
      BatchStatement batch = new BatchStatement();
      batch.add(insertPs.bind(200 + i, "batch_val"));
      session.execute(batch);
    }

    for (int i = 0; i < 5; i++) {
      session.execute(updatePs.bind("prep_upd_" + i, i));
      BatchStatement batch = new BatchStatement();
      batch.add(updatePs.bind("batch_upd_" + i, 10 + i));
      session.execute(batch);
    }

    for (int i = 0; i < 5; i++) {
      session.execute(deletePs.bind(20 + i));
      BatchStatement batch = new BatchStatement();
      batch.add(deletePs.bind(30 + i));
      session.execute(batch);
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS yb_ycql_utils");

      // INSERT: single-query batch queryid should match prepared queryid.
      long insertDistinctIds = getSingleRow(stmt,
          "SELECT COUNT(DISTINCT queryid) FROM ycql_stat_statements " +
          "WHERE query LIKE '%INSERT INTO sq_test (k, v) VALUES (?, ?)%'")
          .getLong(0);
      assertEquals("INSERT: all entries should share one queryid",
          1L, insertDistinctIds);

      Row insertMetrics = getSingleRow(stmt,
          "SELECT SUM(calls)::bigint, SUM(total_time), " +
          "MAX(max_time), MIN(min_time) " +
          "FROM ycql_stat_statements " +
          "WHERE query LIKE '%INSERT INTO sq_test (k, v) VALUES (?, ?)%'");
      assertLocalCallsPositiveAndAtMost(insertMetrics.getLong(0).longValue(), 10,
          "INSERT prepared + single-query batch");
      assertTrue("INSERT total_time > 0", insertMetrics.getDouble(1) > 0);
      assertTrue("INSERT max_time > 0", insertMetrics.getDouble(2) > 0);
      assertTrue("INSERT min_time > 0", insertMetrics.getDouble(3) > 0);

      // UPDATE: single-query batch queryid should match prepared queryid.
      long updateDistinctIds = getSingleRow(stmt,
          "SELECT COUNT(DISTINCT queryid) FROM ycql_stat_statements " +
          "WHERE query LIKE '%UPDATE sq_test SET v = ? WHERE k = ?%'")
          .getLong(0);
      assertEquals("UPDATE: all entries should share one queryid",
          1L, updateDistinctIds);

      Row updateMetrics = getSingleRow(stmt,
          "SELECT SUM(calls)::bigint, SUM(total_time), " +
          "MAX(max_time), MIN(min_time) " +
          "FROM ycql_stat_statements " +
          "WHERE query LIKE '%UPDATE sq_test SET v = ? WHERE k = ?%'");
      assertLocalCallsPositiveAndAtMost(updateMetrics.getLong(0).longValue(), 10,
          "UPDATE prepared + single-query batch");
      assertTrue("UPDATE total_time > 0", updateMetrics.getDouble(1) > 0);
      assertTrue("UPDATE max_time > 0", updateMetrics.getDouble(2) > 0);
      assertTrue("UPDATE min_time > 0", updateMetrics.getDouble(3) > 0);

      // DELETE: single-query batch queryid should match prepared queryid.
      long deleteDistinctIds = getSingleRow(stmt,
          "SELECT COUNT(DISTINCT queryid) FROM ycql_stat_statements " +
          "WHERE query LIKE '%DELETE FROM sq_test WHERE k = ?%'")
          .getLong(0);
      assertEquals("DELETE: all entries should share one queryid",
          1L, deleteDistinctIds);

      Row deleteMetrics = getSingleRow(stmt,
          "SELECT SUM(calls)::bigint, SUM(total_time), " +
          "MAX(max_time), MIN(min_time) " +
          "FROM ycql_stat_statements " +
          "WHERE query LIKE '%DELETE FROM sq_test WHERE k = ?%'");
      assertLocalCallsPositiveAndAtMost(deleteMetrics.getLong(0).longValue(), 10,
          "DELETE prepared + single-query batch");
      assertTrue("DELETE total_time > 0", deleteMetrics.getDouble(1) > 0);
      assertTrue("DELETE max_time > 0", deleteMetrics.getDouble(2) > 0);
      assertTrue("DELETE min_time > 0", deleteMetrics.getDouble(3) > 0);
    }

    session.execute("DROP TABLE sq_test");
  }

  @Test
  public void testBatchIsPreparedFlag() throws Exception {
    setUpCqlClient();
    session.execute("CREATE KEYSPACE IF NOT EXISTS kb3 WITH replication = " +
        "{'class': 'SimpleStrategy', 'replication_factor': 1}");
    session.execute("USE kb3");

    // All-prepared batch: every child is a bound prepared statement.
    session.execute("CREATE TABLE IF NOT EXISTS batch_allprep " +
        "(k INT PRIMARY KEY, v TEXT)");
    for (int i = 0; i < 10; i++) {
      session.execute("INSERT INTO batch_allprep (k, v) VALUES (" +
          i + ", 'init')");
    }

    PreparedStatement psInsertAP = session.prepare(
        "INSERT INTO batch_allprep (k, v) VALUES (?, ?)");
    PreparedStatement psDeleteAP = session.prepare(
        "DELETE FROM batch_allprep WHERE k = ?");
    for (int i = 0; i < 5; i++) {
      BatchStatement allPrepBatch = new BatchStatement();
      allPrepBatch.add(psInsertAP.bind(100 + i, "allprep"));
      allPrepBatch.add(psDeleteAP.bind(i));
      session.execute(allPrepBatch);
    }

    // Mixed batch: one prepared child, one unprepared (SimpleStatement) child.
    session.execute("CREATE TABLE IF NOT EXISTS batch_mixprep " +
        "(k INT PRIMARY KEY, v TEXT)");
    for (int i = 0; i < 10; i++) {
      session.execute("INSERT INTO batch_mixprep (k, v) VALUES (" +
          i + ", 'init')");
    }

    PreparedStatement psInsertMP = session.prepare(
        "INSERT INTO batch_mixprep (k, v) VALUES (?, ?)");
    for (int i = 0; i < 5; i++) {
      BatchStatement mixedBatch = new BatchStatement();
      mixedBatch.add(psInsertMP.bind(100 + i, "mixed"));
      mixedBatch.add(new SimpleStatement(
          "DELETE FROM batch_mixprep WHERE k = ?", i));
      session.execute(mixedBatch);
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS yb_ycql_utils");

      long allPrepTrue = getSingleRow(stmt,
          "SELECT COUNT(*) FROM ycql_stat_statements " +
          "WHERE is_prepared = 't' " +
          "AND query LIKE '%INSERT INTO batch_allprep%' " +
          "AND query LIKE '%DELETE FROM batch_allprep%'").getLong(0);
      assertTrue("All-prepared batch should have is_prepared=true entries, got " +
          allPrepTrue, allPrepTrue > 0);

      long allPrepFalse = getSingleRow(stmt,
          "SELECT COUNT(*) FROM ycql_stat_statements " +
          "WHERE is_prepared = 'f' " +
          "AND query LIKE '%INSERT INTO batch_allprep%' " +
          "AND query LIKE '%DELETE FROM batch_allprep%'").getLong(0);
      assertEquals("All-prepared batch should have no is_prepared=false entries",
          0L, allPrepFalse);

      long mixedFalse = getSingleRow(stmt,
          "SELECT COUNT(*) FROM ycql_stat_statements " +
          "WHERE is_prepared = 'f' " +
          "AND query LIKE '%INSERT INTO batch_mixprep%' " +
          "AND query LIKE '%DELETE FROM batch_mixprep%'").getLong(0);
      assertTrue("Mixed batch should have is_prepared=false entries, got " +
          mixedFalse, mixedFalse > 0);

      long mixedTrue = getSingleRow(stmt,
          "SELECT COUNT(*) FROM ycql_stat_statements " +
          "WHERE is_prepared = 't' " +
          "AND query LIKE '%INSERT INTO batch_mixprep%' " +
          "AND query LIKE '%DELETE FROM batch_mixprep%'").getLong(0);
      assertEquals("Mixed batch should have no is_prepared=true entries",
          0L, mixedTrue);

      long allPrepCalls = getSingleRow(stmt,
          "SELECT COALESCE(SUM(calls), 0)::bigint FROM ycql_stat_statements " +
          "WHERE is_prepared = 't' " +
          "AND query LIKE '%INSERT INTO batch_allprep%' " +
          "AND query LIKE '%DELETE FROM batch_allprep%'").getLong(0);
      assertLocalCallsPositiveAndAtMost(allPrepCalls, 5, "all-prepared batch");

      long mixedPrepCalls = getSingleRow(stmt,
          "SELECT COALESCE(SUM(calls), 0)::bigint FROM ycql_stat_statements " +
          "WHERE is_prepared = 'f' " +
          "AND query LIKE '%INSERT INTO batch_mixprep%' " +
          "AND query LIKE '%DELETE FROM batch_mixprep%'").getLong(0);
      assertLocalCallsPositiveAndAtMost(mixedPrepCalls, 5, "mixed-prepared batch");
    }

    session.execute("DROP TABLE batch_allprep");
    session.execute("DROP TABLE batch_mixprep");
  }

  @Test
  public void testAshJoinYcqlStatStatementsForBatch() throws Exception {
    setUpCqlClient();
    session.execute("CREATE KEYSPACE IF NOT EXISTS kb_ash WITH replication = " +
        "{'class': 'SimpleStrategy', 'replication_factor': 1}");
    session.execute("USE kb_ash");
    session.execute("CREATE TABLE IF NOT EXISTS ash_batch " +
        "(k INT PRIMARY KEY, v TEXT, v2 INT)");

    for (int i = 0; i < 500; i++) {
      session.execute("INSERT INTO ash_batch (k, v, v2) VALUES (" +
          i + ", 'init', 0)");
    }

    PreparedStatement insertPs = session.prepare(
        "INSERT INTO ash_batch (k, v, v2) VALUES (?, ?, ?)");
    PreparedStatement updatePs = session.prepare(
        "UPDATE ash_batch SET v = ? WHERE k = ?");
    PreparedStatement deletePs = session.prepare(
        "DELETE FROM ash_batch WHERE k = ?");

    // Execute many batches so that total CQL processing time far exceeds the
    // ASH sampling interval (10ms). Each batch triggers multiple write RPCs
    // with YB_STRONG consistency, keeping the CQL thread active long enough
    // for ASH to reliably capture samples.
    for (int i = 0; i < 500; i++) {
      BatchStatement batch = new BatchStatement();
      batch.add(insertPs.bind(1000 + i, "batch", i)); // k, v, v2
      batch.add(updatePs.bind("upd_" + i, i % 500)); // v, k
      batch.add(deletePs.bind(500 + i)); // k
      session.execute(batch);
    }

    Thread.sleep(5000);

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS yb_ycql_utils");

      long queryid = getSingleRow(stmt,
          "SELECT DISTINCT queryid FROM ycql_stat_statements " +
          "WHERE query LIKE '%INSERT INTO ash_batch%' " +
          "AND query LIKE '%UPDATE ash_batch%' " +
          "AND query LIKE '%DELETE FROM ash_batch%'").getLong(0);

      long joinCount = getSingleRow(stmt,
          "SELECT COUNT(*) FROM yb_active_session_history ash " +
          "JOIN ycql_stat_statements css ON ash.query_id = css.queryid " +
          "WHERE css.queryid = " + queryid).getLong(0);
      assertTrue("Expected ASH entries joinable with ycql_stat_statements " +
          "for batch queryid " + queryid + ", got " + joinCount,
          joinCount > 0);
    }

    session.execute("DROP TABLE ash_batch");
  }

  public void cleanUpAfter() throws Exception {
    if (session != null) {
      session.close();
    }
    session = null;
    if (cluster != null) {
      cluster.close();
    }
    cluster = null;
    super.cleanUpAfter();
  }
}
