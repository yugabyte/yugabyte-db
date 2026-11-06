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

import static org.yb.AssertionWrappers.assertFalse;

import java.sql.Connection;
import java.sql.Statement;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;

import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

import com.google.gson.JsonObject;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestCancelBackendReuse extends BaseYsqlConnMgr {
  private static final int CANCEL_DELAY_MS = 4000;
  private static final long WAIT_TIMEOUT_MS = 30000;

  private final ExecutorService executor = Executors.newCachedThreadPool();

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlag("ysql_conn_mgr_max_conns_per_db", "1");
    builder.addCommonTServerFlag("ysql_conn_mgr_enable_multi_route_pool", "false");
    builder.addCommonTServerFlag("TEST_ysql_conn_mgr_cancel_delay_ms",
        Integer.toString(CANCEL_DELAY_MS));
  }

  @After
  public void shutdownExecutor() {
    executor.shutdownNow();
  }

  private Connection connMgrConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
  }

  private void waitForPoolCount(String field, int expected) throws Exception {
    TestUtils.waitFor(() -> {
      JsonObject pool = getPool("yugabyte", "yugabyte");
      return pool != null && pool.get(field).getAsInt() == expected;
    }, WAIT_TIMEOUT_MS);
  }

  @Test
  public void testDelayedCancelDoesNotHitNextClient() throws Exception {
    try (Connection a = connMgrConn();
         Connection b = connMgrConn();
         Statement aStmt = a.createStatement();
         Statement bStmt = b.createStatement()) {
      Future<?> aResult = executor.submit(() -> {
        aStmt.execute("SELECT pg_sleep(3)");
        return null;
      });
      waitForPoolCount("active_logical_connections", 1);

      Future<?> bResult = executor.submit(() -> {
        bStmt.execute("SELECT pg_sleep(4)");
        return null;
      });
      waitForPoolCount("queued_logical_connections", 1);

      assertFalse(aResult.isDone());
      Future<?> cancel = executor.submit(() -> {
        aStmt.cancel();
        return null;
      });

      aResult.get(WAIT_TIMEOUT_MS, TimeUnit.MILLISECONDS);
      bResult.get(WAIT_TIMEOUT_MS, TimeUnit.MILLISECONDS);
      cancel.get(WAIT_TIMEOUT_MS, TimeUnit.MILLISECONDS);
    }
  }
}
