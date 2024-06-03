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

import static org.yb.AssertionWrappers.assertTrue;

import com.google.common.net.HostAndPort;
import java.net.URL;
import java.sql.Statement;
import java.util.Map;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.YBTestRunner;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value = YBTestRunner.class)
public class TestMasterMetrics extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestMasterMetrics.class);

  protected static final String CREATE_ATTEMPT_METRIC = "Create_Tablet_Attempt";
  protected static final String CREATE_TASK_METRIC = "Create_Tablet_Task";
  protected static final String ALTER_ATTEMPT_METRIC = "Alter_Table_Attempt";
  protected static final String ALTER_TASK_METRIC = "Alter_Table_Task";
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  // Given a metric name count its frequency of occurences.
  private int getMetricsCount(String metric) throws Exception {
    HostAndPort hp = miniCluster.getClient().getLeaderMasterHostAndPort();
    Map<HostAndPort, MiniYBDaemon> masters = miniCluster.getMasters();
    int webPort = masters.get(hp).getWebPort();
    String host = hp.getHost();

    URL url = null;
    BufferedReader br = null;
    try {
      url = new URL(String.format("http://%s:%d/prometheus-metrics", hp.getHost(), webPort));
      br = new BufferedReader(new InputStreamReader(url.openStream()));;
    } catch (Exception ex) {
      LOG.error("Encountered error for reading metrics endpoint" + ex);
      throw new InternalError(ex.getMessage());
    }
    String line = null;
    int count = 0;
    while ((line = br.readLine()) != null) {
      if (line.contains(metric + "_count{")) {
        String inp[] = line.split(" ");
        int val = Integer.parseInt(inp[inp.length - 2].trim());
        count += val;
      }
    }
    return count;
  }

  @Test
  public void testMasterMetrics() throws Exception {

    // number of times create replica should be called for a single table should be more than the
    // replication factor * number of TServers
    long numRpcTasks = getReplicationFactor() * miniCluster.getNumTServers();

    long createTask = getMetricsCount(CREATE_TASK_METRIC);
    long createAttempt = getMetricsCount(CREATE_ATTEMPT_METRIC);
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test(a INT, b INT)");
    }
    assertTrue(numRpcTasks == getMetricsCount(CREATE_TASK_METRIC) - createTask);
    assertTrue(numRpcTasks <= getMetricsCount(CREATE_ATTEMPT_METRIC) - createAttempt);


    long alterTask = getMetricsCount(ALTER_TASK_METRIC);
    long alterAttempt = getMetricsCount(ALTER_ATTEMPT_METRIC);
    try (Statement statement = connection.createStatement()) {
      statement.execute("ALTER TABLE test ADD c FLOAT");
    }
    assertTrue(miniCluster.getNumTServers() <= getMetricsCount(ALTER_TASK_METRIC) - alterTask);
    assertTrue(miniCluster.getNumTServers() <=
      getMetricsCount(ALTER_ATTEMPT_METRIC) - alterAttempt);

    // Truncates use table rewrites, so they should also be counted as create tasks.
    long truncateTask = getMetricsCount(CREATE_TASK_METRIC);
    long truncateAttempt = getMetricsCount(CREATE_ATTEMPT_METRIC);
    try (Statement statement = connection.createStatement()) {
      statement.execute("TRUNCATE TABLE test");
    }
    assertTrue(miniCluster.getNumTServers() <=
      getMetricsCount(CREATE_TASK_METRIC) - truncateTask);
    assertTrue(miniCluster.getNumTServers() <=
      getMetricsCount(CREATE_ATTEMPT_METRIC) - truncateAttempt);

  }
}
