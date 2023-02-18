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
package org.yb.loadtester;

import java.util.List;

import com.datastax.driver.core.*;
import com.datastax.driver.core.querybuilder.QueryBuilder;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.AssertionWrappers;
import org.yb.util.YBTestRunnerNonTsanOnly;
import org.yb.minicluster.MiniYBCluster;

import static junit.framework.TestCase.assertTrue;
import static junit.framework.TestCase.assertEquals;

/**
 * This is an integration test that is specific to RF=1 case, so that we can default to
 * creating and deleting RF=1 cluster in the setup phase.
 */

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestRF1Cluster extends TestClusterBase {
  private static final Logger LOG = LoggerFactory.getLogger(TestRF1Cluster.class);

  @Override
  public void setUpBefore() throws Exception {
    createMiniCluster(1, 1);
    updateConfigReplicationFactor(1);
  }

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 10 minutes.
  public void testRF1toRF3() throws Exception {
    performRFChange(1, 3);
  }

  @Test(timeout = TEST_TIMEOUT_SEC * 1000) // 10 minutes.
  public void testRF1LoadBalance() throws Exception {
    // Wait for load tester to generate traffic.
    loadTesterRunnable.waitNumOpsIncrement(NUM_OPS_INCREMENT);
    LOG.info("WaitOps Done.");

    // Now perform a tserver expand.
    addNewTServers(2);
    LOG.info("Add Tservers Done.");

    // Wait for load to balance across the three tservers.
    assertTrue(client.waitForLoadBalance(LOADBALANCE_TIMEOUT_MS, 3));

    // Wait for load to balancer to become idle.
    assertTrue(client.waitForLoadBalancerIdle(LOADBALANCE_TIMEOUT_MS));

    // Wait for the partition metadata to refresh.
    Thread.sleep(2 * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);
    LOG.info("Load Balance Done.");
  }

  private boolean verifyNumRowsInTable(
          ConsistencyLevel consistencyLevel, String tableName, int numRows) {
    Statement statement = QueryBuilder.select()
            .from(DEFAULT_TEST_KEYSPACE, tableName)
            .setConsistencyLevel(consistencyLevel);
    return numRows == session.execute(statement).all().size();
  }

  private void internalTestDefaultTTL() throws Exception {
    // A set of write/read ops to stabilize the table.
    for (int i = 0; i < 10; ++i) {
      session.execute(String.format("INSERT INTO employee(id, name, age, lang) " +
          "VALUES(%d, 'John+%d', %d, 'Go')", 100 + i, i, i));
      session.execute("SELECT * FROM employee");
      session.execute(String.format("DELETE FROM employee WHERE id = %d", 100 + i));
    }

    long start_time = System.currentTimeMillis();
    int num_rows = 3;
    for (int i = 0; i < num_rows; ++i) {
      session.execute(String.format(
          "INSERT INTO employee(id, name, age, lang) VALUES(%d, 'John+%d', %d, 'Go')", i, i, i));
    }

    LOG.info("Select from table and verify inserted rows. Time after start (ms): " +
        (System.currentTimeMillis() - start_time));
    ResultSet rs = session.execute("SELECT * FROM employee");
    List<Row> rows = rs.all();
    LOG.info("Row count: " + rows.size());
    assertEquals(num_rows, rows.size());

    long sleep_time = 5000 + start_time - System.currentTimeMillis();
    if (sleep_time > 0) {
      LOG.info("Sleep time (ms): " + sleep_time);
      Thread.sleep(sleep_time);
    }

    LOG.info("Select from table and verify TTL has expired - read by default.");
    rs = session.execute("SELECT * FROM employee");
    rows = rs.all();
    LOG.info("Row count: " + rows.size());
    assertEquals(0, rows.size());

    // Test all available Consistency Levels.
    for (ConsistencyLevel level : ConsistencyLevel.values()) {
      LOG.info("Test expired TTL with Consistency: " + level);
      assertTrue(verifyNumRowsInTable(level, "employee", 0));
    }
  }

  @Test
  public void testDefaultTTLWithChangedRF() throws Exception {
    LOG.info("Create table and insert values with default TTL for RF = 1");
    session.execute("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, " +
        "lang varchar) WITH default_time_to_live = 4");

    internalTestDefaultTTL();

    LOG.info("Change replication factor from 1 to 3");
    performRFChange(1, 3);

    LOG.info("Insert values with default TTL for RF = 3");
    internalTestDefaultTTL();

    LOG.info("Change replication factor from 3 to 1");
    performRFChange(3, 1);

    LOG.info("Insert values with default TTL for reduced RF = 1");
    internalTestDefaultTTL();

    session.execute("DROP TABLE employee");
  }

  @Test
  public void testDefaultTTLWithFixedRF() throws Exception {
    for (int rf = 1; rf <= 5; rf += 2) {
      // Destroy existing cluster and recreate it.
      destroyMiniCluster();
      createMiniCluster(rf, rf);
      updateConfigReplicationFactor(rf);
      setUpCqlClient();

      LOG.info("Create table and insert values with default TTL for RF = " + rf);
      session.execute("CREATE TABLE employee (id int PRIMARY KEY, name varchar, age int, " +
          "lang varchar) WITH default_time_to_live = 4");

      internalTestDefaultTTL();
    }
  }
}
