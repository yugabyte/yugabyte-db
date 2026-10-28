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
import static org.yb.AssertionWrappers.fail;

import com.google.common.net.HostAndPort;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * Verifies that the YSQL Connection Manager auth-passthrough control backend re-reads
 * pg_hba.conf (ysql_hba.conf) on SIGHUP.
 *
 * Unlike a regular backend, which authenticates exactly once right after fork, the
 * auth-passthrough control backend is long-lived and authenticates every logical client. It must
 * therefore pick up HBA changes on SIGHUP; otherwise it keeps using the stale rules inherited at
 * fork time. Changing {@code ysql_hba_conf_csv} at runtime regenerates ysql_hba.conf and SIGHUPs
 * postgres, which forwards the signal to the (reused) control backend.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestAuthPassthroughHbaReload extends BaseYsqlConnMgr {

  private static final String BLOCKED_USER = "blocked_user";
  private static final String ALLOWED_USER = "allowed_user";

  // Permissive baseline: everyone may connect without a password.
  private static final String INITIAL_HBA = "host all all all trust";

  // Reject only blocked_user; everyone else remains trusted. HBA is evaluated top-down and the
  // first matching rule wins, so the reject line must precede the catch-all trust line.
  private static final String UPDATED_HBA =
      "host all " + BLOCKED_USER + " all reject,host all all all trust";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
    // Keep control-backend behavior deterministic so the same backend is reused across the
    // runtime HBA change (rather than a fresh one that would trivially read the new file).
    disableWarmupRandomMode(builder);

    Map<String, String> flags = new HashMap<>();
    flags.put("enable_ysql_conn_mgr", "true");
    // false => auth-passthrough mode: reuse long-lived control backends to authenticate clients.
    flags.put("ysql_conn_mgr_use_auth_backend", "false");
    flags.put("ysql_hba_conf_csv", INITIAL_HBA);
    builder.addCommonTServerFlags(flags);
  }

  @Before
  public void setupRoles() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withTServer(TSERVER_IDX)
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("DROP ROLE IF EXISTS " + BLOCKED_USER);
      stmt.execute("DROP ROLE IF EXISTS " + ALLOWED_USER);
      stmt.execute("CREATE ROLE " + BLOCKED_USER + " WITH LOGIN");
      stmt.execute("CREATE ROLE " + ALLOWED_USER + " WITH LOGIN");
    }
  }

  private void setHbaOnAllTServers(String hba) throws Exception {
    for (HostAndPort tserver : miniCluster.getTabletServers().keySet()) {
      setServerFlag(tserver, "ysql_hba_conf_csv", hba);
    }
  }

  private boolean canLogin(String user) throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withTServer(TSERVER_IDX)
             .withUser(user)
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect();
         Statement stmt = conn.createStatement()) {
      stmt.executeQuery("SELECT 1");
      return true;
    } catch (SQLException e) {
      LOG.info("Login for user \"{}\" failed: {}", user, e.getMessage());
      return false;
    }
  }

  private void assertLoginSucceeds(String user) throws Exception {
    assertTrue("Expected login to succeed for user \"" + user + "\"", canLogin(user));
  }

  private void assertLoginRejectedByHba(String user) throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withTServer(TSERVER_IDX)
             .withUser(user)
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect()) {
      fail("Expected login to be rejected by pg_hba.conf for user \"" + user + "\"");
    } catch (SQLException e) {
      LOG.info("Login for user \"{}\" rejected as expected: {}", user, e.getMessage());
      // The rejection must come from the reloaded HBA, not some unrelated failure.
      assertTrue("Expected an HBA-based rejection, got: " + e.getMessage(),
          e.getMessage().contains("pg_hba.conf"));
    }
  }

  @Test
  public void testControlBackendReloadsHbaOnSighup() throws Exception {
    // Warm up the control backend and confirm both users connect under the permissive baseline.
    assertLoginSucceeds(BLOCKED_USER);
    assertLoginSucceeds(ALLOWED_USER);

    // Push a runtime HBA change that rejects only blocked_user. This rewrites ysql_hba.conf and
    // SIGHUPs postgres, which forwards SIGHUP to the reused auth-passthrough control backend.
    setHbaOnAllTServers(UPDATED_HBA);

    // The reload must take effect on the long-lived control backend: blocked_user is now rejected
    // while allowed_user still authenticates.
    TestUtils.waitFor(() -> !canLogin(BLOCKED_USER), 30000);
    assertLoginRejectedByHba(BLOCKED_USER);
    assertLoginSucceeds(ALLOWED_USER);

    // Reload the other way: restore the permissive HBA and confirm blocked_user is allowed again,
    // proving the control backend reloads in both directions.
    setHbaOnAllTServers(INITIAL_HBA);
    TestUtils.waitFor(() -> canLogin(BLOCKED_USER), 30000);
    assertLoginSucceeds(ALLOWED_USER);
  }
}
