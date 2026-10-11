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

import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * LOAD through the connection manager. A shared library loaded via LOAD is local to the physical
 * backend that executed it, so the logical connection must become sticky; otherwise later queries
 * could be routed to a backend that never loaded the library.
 *
 * Uses a small per-database pool (two physical backends, multi-route pool off) so that a
 * non-sticky connection visibly rotates between backends.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnectionManagerLoadPasswordcheck extends BaseYsqlConnMgr {
  private static final String LOAD_PASSWORDCHECK = "LOAD 'passwordcheck'";

  private static final String WEAK_PASSWORD = "tooshrt";
  private static final String STRONG_PASSWORD = "A_nice1_pass";

  // Short-lived connections opened after LOAD to force backend reuse/churn in the pool.
  private static final int CHURN_ITERATIONS = 10;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    Map<String, String> poolFlags = new HashMap<>();
    poolFlags.put("ysql_conn_mgr_max_conns_per_db", "2");
    poolFlags.put("ysql_conn_mgr_enable_multi_route_pool", "false");
    builder.addCommonTServerFlags(poolFlags);
  }

  /**
   * LOAD must increment the sticky object count and pin the logical connection to a single backend,
   * without affecting other connections in the pool.
   */
  @Test
  public void testLoadMakesConnectionSticky() throws Exception {
    try (Connection loaderConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection otherConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect()) {

      try (Statement loaderStmt = loaderConn.createStatement();
           Statement otherStmt = otherConn.createStatement()) {
        assertTrue("Loader should not be sticky before LOAD",
            verifySessionParameterValue(loaderStmt,
                "ysql_conn_mgr_sticky_object_count", "0"));
        assertTrue("Other connection should not be sticky before LOAD",
            verifySessionParameterValue(otherStmt,
                "ysql_conn_mgr_sticky_object_count", "0"));

        loaderStmt.execute(LOAD_PASSWORDCHECK);

        assertTrue("Loader should be sticky after LOAD",
            verifySessionParameterValue(loaderStmt,
                "ysql_conn_mgr_sticky_object_count", "1"));
        assertTrue("Other connection should not be affected by LOAD on loader",
            verifySessionParameterValue(otherStmt,
                "ysql_conn_mgr_sticky_object_count", "0"));
      }

      // The loader is pinned to a single physical backend across repeated queries.
      try (Statement loaderStmt = loaderConn.createStatement()) {
        assertConnectionStickyState(loaderStmt, true);
      }
    }
  }

  /**
   * After LOAD, the password-strength hook installed by passwordcheck must remain in effect on the
   * loader connection even after the pool churns through other backends, proving the connection
   * stayed pinned to the backend that ran LOAD.
   *
   * Exercising the weak password on the churn connections in case they accidentally reuse the
   * loader's hook-bearing backend.
   */
  @Test
  public void testLoadPasswordCheckSurvivesPoolChurn() throws Exception {
    final String stickyRole = "regress_load_sticky_user";
    final String churnRole = "regress_load_nonsticky_user";
    try (Connection loaderConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect()) {

      try (Statement loaderStmt = loaderConn.createStatement()) {
        loaderStmt.execute(LOAD_PASSWORDCHECK);
        loaderStmt.execute("CREATE USER " + stickyRole);
        loaderStmt.execute("CREATE USER " + churnRole);
      }

      // Churn the pool so any non-sticky connection would land on a different backend. Each churn
      // connection must be non-sticky and, because it never ran LOAD, must land on a backend that
      // lacks the passwordcheck hook and therefore accepts a weak password. A rejection here would
      // mean the churn connection reused the loader's backend.
      for (int i = 0; i < CHURN_ITERATIONS; i++) {
        try (Connection churnConn = getConnectionBuilder()
                .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                .connect();
            Statement churnStmt = churnConn.createStatement()) {
          assertTrue("Churn connection should not be sticky (it never ran LOAD)",
              verifySessionParameterValue(churnStmt,
                  "ysql_conn_mgr_sticky_object_count", "0"));
          assertTrue("Without LOAD the passwordcheck hook is absent on the churn backend; "
              + "a weak password should be accepted",
              trySetPassword(churnStmt, churnRole, WEAK_PASSWORD));
        }
      }

      try (Statement loaderStmt = loaderConn.createStatement()) {
        // The hook is still loaded on the (sticky) backend, so a weak password is rejected...
        assertTrue("passwordcheck should reject a weak password on the loader connection",
            !trySetPassword(loaderStmt, stickyRole, WEAK_PASSWORD));
        // ...and a strong password is still accepted.
        assertTrue("passwordcheck should accept a strong password on the loader connection",
            trySetPassword(loaderStmt, stickyRole, STRONG_PASSWORD));
      } finally {
        dropRoleQuietly(stickyRole);
        dropRoleQuietly(churnRole);
      }
    }
  }

  private boolean trySetPassword(Statement stmt, String role, String password) {
    try {
      stmt.execute(String.format("ALTER USER %s PASSWORD '%s'", role, password));
      return true;
    } catch (SQLException e) {
      LOG.info("ALTER USER ... PASSWORD rejected: " + e.getMessage());
      return false;
    }
  }

  private void dropRoleQuietly(String role) throws Exception {
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP USER IF EXISTS " + role);
    }
  }
}
