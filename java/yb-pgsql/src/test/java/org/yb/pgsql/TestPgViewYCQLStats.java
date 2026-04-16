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

  /** Convenient default cluster for tests to use, cleaned after each test. */
  protected Cluster cluster;

  /** Convenient default session for tests to use, cleaned after each test. */
  protected Session session;

  @Override
  protected void resetSettings() {
    super.resetSettings();
    startCqlProxy = true;
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
