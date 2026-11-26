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

import static org.hamcrest.MatcherAssert.assertThat;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;
import static org.yb.ysqlconnmgr.PgWireProtocol.*;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * Basic cert authentication through YSQL Connection Manager.
 *
 * ysql.crt in test_certs has CN=yugabyte, so hostssl ... cert maps that
 * certificate to the yugabyte role.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestCertAuth extends BaseYsqlConnMgr {

  private static final int SOCKET_TIMEOUT_MS = 10000;

  public TestCertAuth() {
    // Certificates are issued for IP addresses, not hostnames.
    useIpWithCertificate = true;
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

  @Override
  public ConnectionBuilder connectionBuilderForVerification(ConnectionBuilder builder) {
    return builder
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withSslMode("require")
        .withSslCert(String.format("%s/ysql.crt", certsDir()))
        .withSslKey(String.format("%s/ysql.key.der", certsDir()))
        .withSslRootCert(String.format("%s/ca.crt", certsDir()));
  }

  private ConnectionBuilder sslClientCertBuilder() {
    return connectionBuilderForVerification(getConnectionBuilder());
  }

  private void logHbaFiles() throws Exception {
    for (MiniYBDaemon tserver : miniCluster.getTabletServers().values()) {
      Path hbaFile = Paths.get(tserver.getDataDirPath(), "pg_data", "ysql_hba.conf");
      LOG.info("ysql_hba.conf on tserver {}:\n{}", tserver.getLocalhostIP(),
          new String(Files.readAllBytes(hbaFile), StandardCharsets.UTF_8));
    }
  }

  private void restartWithHba(String hba, boolean enableAuthBackend) throws Exception {
    restartWithHbaAndIdent(hba, null, enableAuthBackend);
  }

  private void restartWithHbaAndIdent(String hba, String ident, boolean enableAuthBackend)
      throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_hba_conf_csv", hba);
    if (ident != null) {
      tserverFlags.put("ysql_ident_conf_csv", ident);
    }
    tserverFlags.put("ysql_conn_mgr_use_auth_backend",
        enableAuthBackend ? "true" : "false");
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);
    logHbaFiles();
  }

  /*
   * restartClusterWithAdditionalFlags() restarts the cluster but does not recreate the catalog, so
   * a role created by one test method is still there when the next one runs. Recreate rather than
   * create, so the test methods do not depend on the order JUnit happens to run them in.
   */
  private static void recreateRole(Statement stmt, String role) throws Exception {
    stmt.execute(String.format("DROP ROLE IF EXISTS %s", role));
    stmt.execute(String.format("CREATE ROLE %s LOGIN", role));
  }

  @Test
  public void testCertAuthSucceedsWithMatchingCnAuthBackend() throws Exception {
    runCertAuthSucceedsWithMatchingCn(true);
  }

  @Test
  public void testCertAuthSucceedsWithMatchingCnAuthPassthrough() throws Exception {
    runCertAuthSucceedsWithMatchingCn(false);
  }

  private void runCertAuthSucceedsWithMatchingCn(boolean enableAuthBackend) throws Exception {
    restartWithHba("hostssl all all all cert", enableAuthBackend);

    try (Connection conn = sslClientCertBuilder().withUser("yugabyte").connect();
         Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT current_user");
      assertTrue(rs.next());
      assertEquals("yugabyte", rs.getString(1));
      recreateRole(stmt, "otheruser");
    }

    /*
     * Client certificate are generated with CN=yugabyte, so any other user
     * should fail authentication.
     */
    try (Connection ignored = sslClientCertBuilder().withUser("otheruser").connect()) {
      fail("Expected cert authentication to fail when CN does not match the role");
    } catch (SQLException e) {
      assertThat(e.getMessage(),
          CoreMatchers.containsString("certificate authentication failed"));
    }

    try (Connection ignored = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withSslMode("require")
             .withUser("yugabyte")
             .connect()) {
      fail("Expected cert authentication to fail without a client certificate");
    } catch (SQLException e) {
      assertThat(e.getMessage(),
          CoreMatchers.containsString("connection requires a valid client certificate"));
    }
  }

  @Test
  public void testClientCertVerifyCaAuthBackend() throws Exception {
    runClientCertVerifyCa(true);
  }

  @Test
  public void testClientCertVerifyCaAuthPassthrough() throws Exception {
    runClientCertVerifyCa(false);
  }

  private void runClientCertVerifyCa(boolean enableAuthBackend) throws Exception {
    restartWithHba("hostssl all all all trust clientcert=verify-ca", enableAuthBackend);

    try (Connection conn = sslClientCertBuilder().withUser("yugabyte").connect();
         Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT 1");
      assertTrue(rs.next());
      assertEquals(1, rs.getInt(1));
      recreateRole(stmt, "otheruser");
    }

    /*
     * Client certificate are generated with CN=yugabyte, but with verify-ca
     * the common name is not used to authenticate the user.
     */
    try (Connection conn = sslClientCertBuilder().withUser("otheruser").connect();
         Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT current_user");
      assertTrue(rs.next());
      assertEquals("otheruser", rs.getString(1));
    }

    try (Connection ignored = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withSslMode("require")
             .withUser("yugabyte")
             .connect()) {
      fail("Expected clientcert=verify-ca to reject a connection without a client certificate");
    } catch (SQLException e) {
      assertThat(e.getMessage(),
          CoreMatchers.containsString("connection requires a valid client certificate"));
    }
  }

  /*
   * Postgres rejects yb_ycm_internal_client_cert unless the peer is the connection
   * manager; the value is unused for that check, so a dummy suffices.
   */
  @Test
  public void testDirectClientCannotForgetClientCertAuthBackend() throws Exception {
    runDirectClientCannotForgeClientCert(true);
  }

  @Test
  public void testDirectClientCannotForgetClientCertAuthPassthrough() throws Exception {
    runDirectClientCannotForgeClientCert(false);
  }

  private void runDirectClientCannotForgeClientCert(boolean enableAuthBackend) throws Exception {
    restartWithHba("hostssl all all all cert", enableAuthBackend);

    InetSocketAddress addr = miniCluster.getPostgresContactPoints().get(TSERVER_IDX);
    LOG.info("Connecting raw socket directly to PostgreSQL at " + addr);

    try (Socket socket = new Socket()) {
      socket.setTcpNoDelay(true);
      socket.setSoTimeout(SOCKET_TIMEOUT_MS);
      socket.connect(addr);

      DataOutputStream out = new DataOutputStream(socket.getOutputStream());
      DataInputStream in = new DataInputStream(socket.getInputStream());

      out.write(buildStartupMessage("yugabyte", "yugabyte",
          Collections.singletonMap("yb_ycm_internal_client_cert", "1")));
      out.flush();

      PgMessage response = readMessage(in);
      assertEquals("Expected an ErrorResponse when a direct client supplies " +
                   "yb_ycm_internal_client_cert",
          BE_ERROR_RESPONSE, response.type);
      assertThat(new String(response.body, StandardCharsets.UTF_8),
          CoreMatchers.containsString(
              "yb_ycm_internal_client_cert must only be provided when " +
              "the client is the connection manager"));
    }
  }

  /*
   * Auth passthrough control backends are long lived and authenticate one client after another, so
   * the certificate of the client that just authenticated must not still be on the Port when the
   * next one arrives. If it is, a client that presented no certificate inherits the previous
   * client's identity and satisfies a cert rule it should have failed.
   *
   * A control connection pool of one makes the reuse deterministic. Without it consecutive clients
   * can land on different control backends and the stale state is simply never observed, so the
   * test would pass whether or not the certificate is cleared.
   */
  @Test
  public void testForwardedCertIsNotReusedByNextClient() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_hba_conf_csv", "hostssl all all all cert");
    // Not runtime settable, and false is already the default; set it so the mode under test does
    // not silently change if that default ever does.
    tserverFlags.put("ysql_conn_mgr_use_auth_backend", "false");
    tserverFlags.put("ysql_conn_mgr_control_connection_pool_size", "1");
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);
    logHbaFiles();

    // Authenticate with a certificate, leaving one on the control backend's Port.
    try (Connection conn = sslClientCertBuilder()
             .withTServer(TSERVER_IDX)
             .withUser("yugabyte")
             .connect();
         Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT current_user");
      assertTrue(rs.next());
      assertEquals("yugabyte", rs.getString(1));
    }

    /*
     * The next client presents none and must be rejected on its own merits. Retried a few times
     * because which control backend serves a request is not guaranteed even with a pool of one.
     */
    for (int attempt = 1; attempt <= 5; attempt++) {
      try (Connection ignored = getConnectionBuilder()
               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
               .withTServer(TSERVER_IDX)
               .withSslMode("require")
               .withUser("yugabyte")
               .connect()) {
        fail("Expected a client presenting no certificate to be rejected on attempt " + attempt
            + "; the previous client's certificate must not be reused");
      } catch (SQLException e) {
        assertThat(e.getMessage(),
            CoreMatchers.containsString("connection requires a valid client certificate"));
      }
    }
  }

  /*
   * verify-full additionally requires the certificate's common name to match the role being
   * assumed. The test certificate has CN=yugabyte, so it authenticates that role and no other,
   * where verify-ca above lets any role through.
   */
  @Test
  public void testClientCertVerifyFullAuthBackend() throws Exception {
    runClientCertVerifyFull(true);
  }

  @Test
  public void testClientCertVerifyFullAuthPassthrough() throws Exception {
    runClientCertVerifyFull(false);
  }

  private void runClientCertVerifyFull(boolean enableAuthBackend) throws Exception {
    restartWithHba("hostssl all all all trust clientcert=verify-full", enableAuthBackend);

    try (Connection conn = sslClientCertBuilder().withUser("yugabyte").connect();
         Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT current_user");
      assertTrue(rs.next());
      assertEquals("yugabyte", rs.getString(1));
      recreateRole(stmt, "otheruser");
    }

    /*
     * The hba method is trust, so the connection would be let in on the strength of the certificate
     * alone; the CN mismatch is what rejects it, reported through the trust method's message.
     */
    try (Connection ignored = sslClientCertBuilder().withUser("otheruser").connect()) {
      fail("Expected clientcert=verify-full to reject a certificate whose CN is not the role");
    } catch (SQLException e) {
      assertThat(e.getMessage(),
          CoreMatchers.containsString("\"trust\" authentication failed for user \"otheruser\""));
    }
  }

  /*
   * clientname=DN matches the certificate's full RFC 2253 subject through a pg_ident map, not
   * against the CN. The test cert's subject is "CN=yugabyte,O=YugaByte"; a map routing that DN to
   * dbadmin must let the dbadmin role in -- which a plain "cert" (clientname=CN) rule would have
   * rejected because the CN is yugabyte, not dbadmin. Roles absent from the map must still be
   * rejected. This exercises the DN derivation on the Postgres side (yb_be_tls_set_peer_cert_info
   * -> X509_NAME_print_ex with XN_FLAG_RFC2253) end-to-end through the forwarded cert.
   */
  @Test
  public void testCertAuthDnMapAuthBackend() throws Exception {
    runCertAuthDnMap(true);
  }

  @Test
  public void testCertAuthDnMapAuthPassthrough() throws Exception {
    runCertAuthDnMap(false);
  }

  private void runCertAuthDnMap(boolean enableAuthBackend) throws Exception {
    /*
     * Two ident lines for the same DN: one to dbadmin (the test's actual assertion), one to
     * yugabyte (so the framework's post-restart health check, which reconnects as yugabyte, can
     * still succeed). The system-username must be double-quoted in pg_ident because the DN's
     * comma is otherwise treated as a multi-value separator ("multiple values in ident field").
     * CSV "" escapes to a literal " in the written ysql_ident.conf line.
     */
    String ident = "\"certmap    \"\"/^CN=yugabyte,O=YugaByte$\"\"    dbadmin\","
                 + "\"certmap    \"\"/^CN=yugabyte,O=YugaByte$\"\"    yugabyte\"";
    restartWithHbaAndIdent(
        "hostssl all all all cert clientname=DN map=certmap",
        ident, enableAuthBackend);

    try (Connection conn = sslClientCertBuilder().withUser("yugabyte").connect();
         Statement stmt = conn.createStatement()) {
      recreateRole(stmt, "dbadmin");
      recreateRole(stmt, "dnrejected");
    }

    /*
     * dbadmin authenticating with this cert is the DN-vs-CN differentiator: a plain
     * "hostssl all all all cert" rule (which matches on CN) would reject dbadmin because the
     * cert's CN is "yugabyte", not "dbadmin". The DN rule accepts it because the map routes the
     * DN CN=yugabyte,O=YugaByte to dbadmin.
     */
    try (Connection conn = sslClientCertBuilder().withUser("dbadmin").connect();
         Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT current_user");
      assertTrue(rs.next());
      assertEquals("dbadmin", rs.getString(1));
    }

    /* dnrejected is a real role but is not in the map: the DN rule must reject it. */
    try (Connection ignored = sslClientCertBuilder().withUser("dnrejected").connect()) {
      fail("Expected clientname=DN to reject a role not present in the ident map");
    } catch (SQLException e) {
      assertThat(e.getMessage(),
          CoreMatchers.containsString("certificate authentication failed"));
    }
  }
}
