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

import static org.yb.AssertionWrappers.fail;
import static org.yb.AssertionWrappers.assertTrue;

import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Pattern;
import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

import static org.hamcrest.MatcherAssert.assertThat;

/**
 * Tests that YSQL Connection Manager with TLS enabled correctly forwards non-SSL connection
 * requests to PostgreSQL for authentication, instead of prematurely rejecting them.
 *
 * With tls "allow" in Odyssey, the SSL/non-SSL decision is delegated to PostgreSQL's HBA rules,
 * matching the behavior of a direct PostgreSQL connection.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrNonSslConnection extends BaseYsqlConnMgr {

  public TestConnMgrNonSslConnection() {
    // Required for TLS connections to resolve correctly in the test environment;
    // certificates are issued for IP addresses, not hostnames.
    useIpWithCertificate = true;
  }

  private static String certsDir() {
    FileSystem fs = FileSystems.getDefault();
    return fs.getPath(TestUtils.getBinDir()).resolve(fs.getPath("../test_certs")).toString();
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("use_client_to_server_encryption", "true");
    flagMap.put("certs_for_client_dir", certsDir());
    return flagMap;
  }

  /**
   * Verifies that a non-SSL connection to the YSQL Connection Manager succeeds when the HBA
   * configuration includes a "host" (non-SSL) rule.
   *
   * Before the fix, conn mgr with tls "require" would reject the connection at the TLS handshake
   * stage with "SSL is required", regardless of HBA rules. After the fix (tls "allow"), conn mgr
   * passes the connection to PostgreSQL, which allows it per the "host" HBA rule.
   */
  @Test
  public void testNonSslConnectionAllowedByHba() throws Exception {
    // Restart with a permissive HBA that allows both SSL and non-SSL connections.
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_hba_conf_csv", "host all all all trust");
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);

    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withSslMode("disable")
             .connect();
        Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT 1");
      rs.next();
      LOG.info("Non-SSL connection via conn mgr succeeded as expected with 'host' HBA rule");
    }
  }

  /**
   * Verifies that SSL connections continue to work through YSQL Connection Manager when TLS
   * is enabled on the cluster.
   */
  @Test
  public void testSslConnectionWorksThroughConnMgr() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withSslMode("require")
             .connect();
        Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT 1");
      rs.next();
      LOG.info("SSL connection via conn mgr succeeded as expected");
    }
  }

  /**
   * Verifies that when HBA only allows SSL connections (hostssl rule), a non-SSL connection via
   * YSQL Connection Manager is rejected by PostgreSQL (with "no pg_hba.conf entry"), NOT by conn
   * mgr itself with "SSL is required".
   *
   * This confirms that after the fix the authentication decision is correctly delegated to
   * PostgreSQL rather than being short-circuited by conn mgr.
   */
  @Test
  public void testNonSslConnectionRejectedByPgNotConnMgr() throws Exception {
    // Restart with an HBA that only allows SSL connections.
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_hba_conf_csv", "hostssl all all all trust");
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);

    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withSslMode("disable")
             .connect()) {
      fail("Expected non-SSL connection to be rejected by PostgreSQL HBA rules");
    } catch (SQLException e) {
      LOG.info("Non-SSL connection correctly rejected. Error: {}", e.getMessage());
      // PostgreSQL should reject the connection via HBA, NOT conn mgr with "SSL is required".
      // This confirms conn mgr is forwarding the connection to PG for authentication decisions.
      assertThat(e.getMessage(),
          CoreMatchers.containsString("no pg_hba.conf entry for host \""));
      assertThat(e.getMessage(), CoreMatchers.containsString(
          "\", user \"yugabyte\", database \"yugabyte\", no encryption"));
      // Host must be a numeric IPv4 address (not "???" from a bad raddr.salen).
      assertTrue("Expected numeric IP in HBA reject message, got: " + e.getMessage(),
          Pattern.compile("no pg_hba\\.conf entry for host \"\\d{1,3}(?:\\.\\d{1,3}){3}\"")
              .matcher(e.getMessage())
              .find());
    }
  }

  @Test
  public void testPreferSslModeForNonSslConnection() throws Exception {
    // Restart with a permissive HBA that allows both SSL and non-SSL connections.
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("use_client_to_server_encryption", "false");
    tserverFlags.put("certs_for_client_dir", "");
    tserverFlags.put("ysql_hba_conf_csv",
      "hostnossl all all all trust, host all foo all trust");
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);

    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withSslMode("disable")
             .connect()) {
      Statement stmt = conn.createStatement();
      stmt.execute(
          "CREATE ROLE foo WITH LOGIN PASSWORD '123';");
      try(Connection conn2 = getConnectionBuilder()
          .withUser("foo")
          .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
          .withSslMode("disable")
          .connect()) {
        LOG.info("Non-SSL connection via conn mgr succeeded with disabled " +
         "ssl mode and hostnossl hba entry");
      }

      // With prefer, client will try to make SSL connection first,
      // and then fall back to non-SSL.
      // This test is to validate, client is authenitcated via
      // {hostnossl all all all trust} hba entry rather than {host all foo all md5}
      // which requires password.
      try(Connection conn3 = getConnectionBuilder()
          .withUser("foo")
          .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
          .withSslMode("prefer")
          .connect()) {
        LOG.info("Non-SSL connection via conn mgr succeeded with prefer "+
         "ssl mode and hostnossl hba entry");
      }
    }
  }
}
