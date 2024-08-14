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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.yugabyte.util.PSQLException;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestYbAsh extends BasePgSQLTest {
  private static final int ASH_SAMPLING_INTERVAL = 1000;

  private static final int ASH_SAMPLE_SIZE = 500;

  private static final String ASH_VIEW = "yb_active_session_history";

  private void setAshConfigAndRestartCluster(
      int sampling_interval, int sample_size) throws Exception {
    Map<String, String> flagMap = super.getTServerFlags();
    if (isTestRunningWithConnectionManager()) {
      flagMap.put("allowed_preview_flags_csv",
         "ysql_yb_ash_enable_infra,ysql_yb_enable_ash,enable_ysql_conn_mgr");
    } else {
      flagMap.put("allowed_preview_flags_csv", "ysql_yb_ash_enable_infra,ysql_yb_enable_ash");
    }
    flagMap.put("ysql_yb_ash_enable_infra", "true");
    flagMap.put("ysql_yb_enable_ash", "true");
    flagMap.put("ysql_yb_ash_sampling_interval_ms", String.valueOf(sampling_interval));
    flagMap.put("ysql_yb_ash_sample_size", String.valueOf(sample_size));
    // flagMap.put("create_initial_sys_catalog_snapshot", "true");
    Map<String, String> masterFlagMap = super.getMasterFlags();
    restartClusterWithFlags(masterFlagMap, flagMap);
  }

  private void executePgSleep(Statement statement, long seconds) throws Exception {
    statement.execute("SELECT pg_sleep(" + seconds + ")");
  }

  /**
   * We should get an error if we try to query the ASH view without
   * enabling ASH
   */
  @Test
  public void testAshViewWithoutEnablingAsh() throws Exception {
    // We need to restart the cluster because ASH may already have been enabled
    restartCluster();
    try (Statement statement = connection.createStatement()) {
      runInvalidQuery(statement, "SELECT * FROM " + ASH_VIEW,
          "ysql_yb_ash_enable_infra gflag must be enabled");
    }
  }

  /**
   * The circular buffer should be empty if the cluster is idle. The query to check
   * that the circular buffer is empty might get sampled and put in the buffer, so
   * we exclude those samples. We also exlude constant query_ids which can mean that
   * these are background tasks, or query_id before the actual query_id is set.
   */
  @Test
  public void testEmptyCircularBuffer() throws Exception {
    setAshConfigAndRestartCluster(ASH_SAMPLING_INTERVAL, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      String query = "SELECT COUNT(*) FROM " + ASH_VIEW + " JOIN pg_stat_statements "
          + "ON query_id = queryid WHERE query NOT LIKE '%" + ASH_VIEW + "%' AND query_id < 0 "
          + "AND query_id > 6";
      assertOneRow(statement, query, 0);
      Thread.sleep(2 * ASH_SAMPLING_INTERVAL);
      assertOneRow(statement, query, 0);
    }
  }

  /**
   * Query the pg_sleep function and check if the appropriate number
   * of PgSleep wait events are present in the circular buffer
   */
  @Test
  public void testNonEmptyCircularBuffer() throws Exception {
    setAshConfigAndRestartCluster(ASH_SAMPLING_INTERVAL, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      long sleep_time = TimeUnit.MILLISECONDS.toSeconds(5 * ASH_SAMPLING_INTERVAL);
      String wait_event_name = "PgSleep";
      executePgSleep(statement, sleep_time);
      // We should get atleast (sleep_time - 1) 'PgSleep' wait events, it is
      // possible that one sampling event occurs just before the sleep begins and then
      // 4 sampling events occur and one sampling event occurs after the sleep is over.
      long res = getSingleRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE wait_event='" + wait_event_name + "'").getLong(0);
      assertGreaterThanOrEqualTo(res, sleep_time - 1);
    }
  }

  /**
   * No events should be sampled if the sample size is 0
   */
  @Test
  public void testZeroSampleSize() throws Exception {
    setAshConfigAndRestartCluster(ASH_SAMPLING_INTERVAL, 0);
    try (Statement statement = connection.createStatement()) {
      long sleep_time = TimeUnit.MILLISECONDS.toSeconds(2 * ASH_SAMPLING_INTERVAL);
      executePgSleep(statement, sleep_time);
      assertOneRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW, 0);
    }
  }

  /**
   * Verify that we are getting some tserver samples. We decrease the sampling
   * interval so that we increase the probability of catching tserver samples
   */
  @Test
  public void testTServerSamples() throws Exception {
    setAshConfigAndRestartCluster(100, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table(k INT, v TEXT)");
      for (int i = 0; i < 1000; ++i) {
        statement.execute(String.format("INSERT INTO test_table VALUES(%d, 'v-%d')", i, i));
        statement.execute(String.format("SELECT v FROM test_table WHERE k=%d", i));
      }
      int res = getSingleRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE wait_event_component='TServer'").getLong(0).intValue();
      assertGreaterThan(res, 0);
    }
  }

  /**
   * Query id of utility statements shouldn't be zero
   */
  @Test
  public void testUtilityStatementsQueryId() throws Exception {
    setAshConfigAndRestartCluster(100, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table1(k INT)");
      statement.execute("ALTER TABLE test_table1 ADD value TEXT");
      statement.execute("ALTER TABLE test_table1 RENAME TO test_table2");
      for (int i = 0; i < 10; ++i) {
        statement.execute(String.format("INSERT INTO test_table2 VALUES(%d, 'v-%d')", i, i));
      }
      statement.execute("TRUNCATE TABLE test_table2");
      statement.execute("DROP TABLE test_table2");
      // TODO: remove wait_event_component='YSQL' once all tserver RPCs are instrumented
      assertOneRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW + " WHERE query_id = 0 " +
          "AND wait_event_component='YSQL'", 0);
    }
  }

  /**
   * Sanity check that nested queries work with ASH enabled and
   * nested queries only get tracked when pg_stat_statements tracks nested queries.
   */
  @Test
  public void testNestedQueriesWithAsh() throws Exception {
    setAshConfigAndRestartCluster(100, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      String tableName = "test_table";

      // Queries inside extension scripts
      statement.execute("DROP EXTENSION IF EXISTS pg_stat_statements");
      statement.execute("CREATE EXTENSION pg_stat_statements");

      // Queries inside triggers
      statement.execute("CREATE TABLE " + tableName + "(k INT, v INT)");
      statement.execute("CREATE FUNCTION trigger_fn() " +
          "RETURNS TRIGGER AS $$ BEGIN UPDATE test_table SET v = 1 " +
          "WHERE k = 1; RETURN NEW; END; $$ LANGUAGE plpgsql");
      statement.execute("CREATE TRIGGER trig AFTER INSERT ON test_table " +
          "FOR EACH STATEMENT EXECUTE PROCEDURE trigger_fn()");

      for (int i = 0; i < 10; ++i) {
        statement.execute(String.format("INSERT INTO %s VALUES(%d, %d)",
            tableName, i, i));
      }

      // Queries inside functions
      String get_nested_query_id = "SELECT queryid FROM pg_stat_statements " +
          "WHERE query = 'INSERT INTO " + tableName + " SELECT i, i FROM " +
          "generate_series(i, j) as i'";

      statement.execute("TRUNCATE " + tableName);

      statement.execute("CREATE FUNCTION insert_into_table(i INT, j INT) " +
          "RETURNS void AS $$ INSERT INTO " + tableName + " SELECT i, i FROM " +
          "generate_series(i, j) as i $$ LANGUAGE SQL");

      // Track all queries inside pg_stat_statements
      statement.execute("SET pg_stat_statements.track = 'ALL'");

      // Run the nested query
      statement.execute(String.format("SELECT insert_into_table(1, 100000)"));

      long nested_query_id = getSingleRow(statement, get_nested_query_id).getLong(0).longValue();
      String nested_query_id_samples_count = "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE query_id = " + nested_query_id ;
      String nested_query_id_samples_count_last_second = "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE query_id = " + nested_query_id + " AND sample_time >= current_timestamp - " +
          "interval '1 second'" ;

      // Verify that there are samples of the nested query
      assertGreaterThan(getSingleRow(statement, nested_query_id_samples_count).getLong(0), 0L);

      // Track only top level queries inside pg_stat_statements
      statement.execute("SET pg_stat_statements.track = 'TOP'");

      // sleep for one second so that the circular buffer doesn't contain samples of this query id
      // for the last one second
      executePgSleep(statement, 1);

      // reset pg_stat_statements so that the nested query is no longer there
      statement.execute("SELECT pg_stat_statements_reset()");

      // Rerun the nested query, now pg_stat_statements should not track it
      statement.execute(String.format("SELECT insert_into_table(100001, 200000)"));

      // Make sure that the nested query doesn't show up in pg_stat_statements
      ResultSet rs = statement.executeQuery(get_nested_query_id);
      assertFalse(rs.next());

      // Make sure there are no ASH samples with the nested query id
      assertEquals(getSingleRow(statement, nested_query_id_samples_count_last_second).
          getLong(0).longValue(), 0L);
    }
  }

  /**
   * Aux info of samples from postgres should be null.
   */
  @Test
  public void testPgAuxInfo() throws Exception {
    setAshConfigAndRestartCluster(10, ASH_SAMPLE_SIZE);
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table(k INT, v TEXT)");
      for (int i = 0; i < 10000; ++i) {
        statement.execute(String.format("INSERT INTO test_table VALUES(%d, 'v-%d')", i, i));
        statement.execute(String.format("SELECT v FROM test_table WHERE k=%d", i));
      }
      int res = getSingleRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE wait_event_component='YSQL' AND wait_event_aux IS NOT NULL")
          .getLong(0).intValue();
      assertEquals(res, 0);
    }
  }

  /**
   * Verify that catalog requests are sampled
   */
  @Test
  public void testCatalogRequests() throws Exception {
    // Use small sampling interval so that we are more likely to catch catalog requests
    setAshConfigAndRestartCluster(5, ASH_SAMPLE_SIZE);
    int catalog_request_query_id = 5;
    String catalog_read_wait_event = "CatalogRead";
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table(k INT, v TEXT)");
      for (int i = 0; i < 100; ++i) {
        statement.execute(String.format("INSERT INTO test_table VALUES(%d, 'v-%d')", i, i));
        statement.execute(String.format("SELECT v FROM test_table WHERE k=%d", i));
      }
      int res1 = getSingleRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE query_id = " + catalog_request_query_id + " OR " +
          "wait_event = '" + catalog_read_wait_event + "'").getLong(0).intValue();
      assertGreaterThan(res1, 0);
    }
  }

  /**
   * Test that we don't capture more than 'ysql_yb_ash_sample_size' number of samples
   */
  @Test
  public void testSampleSize() throws Exception {
    final int sample_size = 3;
    setAshConfigAndRestartCluster(ASH_SAMPLING_INTERVAL, sample_size);
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table(k INT, v TEXT)");
    }
    final int NUM_THREADS = 5;
    final int NUM_INSERTS_PER_THREAD = 100;
    ExecutorService ecs = Executors.newFixedThreadPool(NUM_THREADS);
    List<Future<?>> futures = new ArrayList<>();
    for (int i = 1; i <= NUM_THREADS; ++i) {
      final int threadIndex = i;
      Future<?> future = ecs.submit(() -> {
        try (Statement statement = connection.createStatement()) {
          for (int j = 0; j < NUM_INSERTS_PER_THREAD; ++j) {
            statement.execute(String.format("INSERT INTO test_table VALUES(%d, 'v-%d')",
                threadIndex, j));
          }
        } catch (Exception e) {
          fail(e.getMessage());
        }
      });
      futures.add(future);
    }
    for (Future<?> future : futures) {
      future.get();
    }
    ecs.shutdown();
    ecs.awaitTermination(30, TimeUnit.SECONDS);
    try (Statement statement = connection.createStatement()) {
      ResultSet rs = statement.executeQuery("SELECT sample_time, wait_event_component, " +
          "count(*) FROM " + ASH_VIEW + " GROUP BY sample_time, wait_event_component");
      while (rs.next()) {
        assertLessThanOrEqualTo(rs.getLong("count"), Long.valueOf(sample_size));
      }
      rs = statement.executeQuery("SELECT sample_weight FROM " + ASH_VIEW);
      while (rs.next()) {
        assertGreaterThanOrEqualTo(rs.getDouble("sample_weight"), Double.valueOf(1.0));
      }
    }
  }

  /**
   * Verify that we see the YSQL backend's pid in ASH
   */
  @Test
  public void testYsqlPids() throws Exception {
    setAshConfigAndRestartCluster(100, ASH_SAMPLE_SIZE);

    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test_table(k INT, v TEXT)");
      for (int i = 0; i < 100; ++i) {
        statement.execute(String.format("INSERT INTO test_table VALUES(%d, 'v-%d')", i, i));
      }
      int pid = getSingleRow(statement, "SELECT pg_backend_pid()").getInt(0);
      int res = getSingleRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE pid = " + pid).getLong(0).intValue();
      assertGreaterThan(res, 0);
    }

    try (Statement statement = connection.createStatement()) {
      for (int i = 0; i < 100; ++i) {
        statement.execute(String.format("SELECT * FROM test_table WHERE k = %d", i));
      }
      int pid = getSingleRow(statement, "SELECT pg_backend_pid()").getInt(0);
      int res = getSingleRow(statement, "SELECT COUNT(*) FROM " + ASH_VIEW +
          " WHERE pid = " + pid).getLong(0).intValue();
      assertGreaterThan(res, 0);
    }
  }
}
