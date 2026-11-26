// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.ysqlconnmgr;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.ArrayList;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrPgUnavailable extends BaseYsqlConnMgr {
  private static final String CANNOT_CONNECT_NOW_SQLSTATE = "57P03";
  private static final long POSTMASTER_EXIT_TIMEOUT_MS = 120000;
  private static final String BROKEN_CONTROL_CONNECTION_ERR = "broken control connection";
  private static final int MAX_STALE_CONTROL_CONNECTIONS = 20;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
  }

  private static boolean postmasterRunning() throws Exception {
    try {
      getPostmasterPid();
      return true;
    } catch (RuntimeException e) {
      return false;
    }
  }

  private ConnectionBuilder connMgrConnectionBuilder() {
    return getConnectionBuilder()
        .withTServer(TSERVER_IDX)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR);
  }

  private SQLException connectExpectingFailure(ConnectionBuilder builder) throws Exception {
    try (Connection conn = builder.connect()) {
      fail("Expected the connection to fail while Postgres is stopped");
      return null;
    } catch (SQLException e) {
      LOG.info("Got expected exception", e);
      return e;
    }
  }

  // Killing every master makes the tservers lose their YSQL lease, which stops the postmaster
  // while Connection Manager keeps accepting clients. Those clients must get a retryable error.
  // Control connections pooled before the stop fail on first use with "broken control
  // connection" and are dropped, so skip past them to the first fresh connect attempt.
  @Test
  public void testConnectWhilePostgresIsStopped() throws Exception {
    connMgrConnectionBuilder().connect().close();

    markClusterNeedsRecreation();
    for (HostAndPort master : new ArrayList<>(miniCluster.getMasterHostPorts())) {
      miniCluster.killMasterOnHostPort(master);
    }
    TestUtils.waitFor(() -> !postmasterRunning(), POSTMASTER_EXIT_TIMEOUT_MS);

    ConnectionBuilder builder = connMgrConnectionBuilder();
    builder.setMaxConnectionAttempts(1);
    SQLException error = connectExpectingFailure(builder);
    for (int attempt = 1; attempt < MAX_STALE_CONTROL_CONNECTIONS &&
        error.getMessage().contains(BROKEN_CONTROL_CONNECTION_ERR); ++attempt) {
      error = connectExpectingFailure(builder);
    }
    assertEquals("Unexpected error: " + error.getMessage(),
        CANNOT_CONNECT_NOW_SQLSTATE, error.getSQLState());
  }
}
