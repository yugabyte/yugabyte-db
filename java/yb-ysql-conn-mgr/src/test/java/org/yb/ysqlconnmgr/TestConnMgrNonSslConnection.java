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
import static org.yb.AssertionWrappers.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.security.cert.X509Certificate;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import org.yb.ysqlconnmgr.PgWireProtocol.PgMessage;

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
  private static final int SSL_PIPELINE_REPLY_TIMEOUT_MS = 15000;

  public TestConnMgrNonSslConnection() {
    // Required for TLS connections to resolve correctly in the test environment;
    // certificates are issued for IP addresses, not hostnames.
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
   * Verifies that SSL connections work through conn mgr with a small readahead buffer.
   *
   * mm_tls_read() used to signal io->on_read even when it was NULL, which crashed Odyssey when a
   * TLS record did not fit in the readahead buffer.
   */
  @Test
  public void testSslWithSmallReadahead() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_conn_mgr_readahead_buffer_size", "64");
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);

    for (int i = 0; i < 5; i++) {
      try (Connection conn = getConnectionBuilder()
               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
               .withSslMode("require")
               .connect();
          Statement stmt = conn.createStatement()) {
        ResultSet rs = stmt.executeQuery("SELECT 1");
        assertTrue(rs.next());
      }
    }
  }

  /**
   * A TLS client sends Parse/Bind/Execute/Sync and a Query in one write, with a 64-byte readahead.
   * Conn mgr pauses the client after the Sync with the readahead full and the rest of the Query
   * buffered by OpenSSL, which does not wake the poller. After the Sync's ReadyForQuery, conn mgr
   * must still read the rest of the Query and reply to it.
   */
  @Test
  public void testSslPipelinedQueryAfterSyncWithSmallReadahead() throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_conn_mgr_readahead_buffer_size", "64");
    tserverFlags.put("ysql_hba_conf_csv", "hostssl all all all trust");
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);

    String literal = String.join("", Collections.nCopies(200, "a"));
    ByteArrayOutputStream pipelined = new ByteArrayOutputStream();
    pipelined.write(PgWireProtocol.buildParse("SELECT 1"));
    pipelined.write(PgWireProtocol.buildBind());
    pipelined.write(PgWireProtocol.buildExecute());
    pipelined.write(PgWireProtocol.buildSync());
    pipelined.write(PgWireProtocol.buildQuery("SELECT '" + literal + "'"));

    try (SSLSocket socket = openSslSocket()) {
      DataInputStream in = new DataInputStream(socket.getInputStream());
      OutputStream out = socket.getOutputStream();
      out.write(PgWireProtocol.buildStartupMessage("yugabyte", "yugabyte", null));
      PgWireProtocol.readUntilReady(in);

      // One write, so the client sends it in a single TLS record.
      out.write(pipelined.toByteArray());
      out.flush();
      assertEquals(Collections.singletonList("1"), readRowsUntilReady(in));
      try {
        assertEquals(Collections.singletonList(literal), readRowsUntilReady(in));
      } catch (SocketTimeoutException e) {
        fail("No reply to the Query pipelined after the Sync");
      }
    }
  }

  private SSLSocket openSslSocket() throws Exception {
    InetSocketAddress target = miniCluster.getYsqlConnMgrContactPoints().get(0);
    Socket plain = new Socket();
    plain.setSoTimeout(SSL_PIPELINE_REPLY_TIMEOUT_MS);
    plain.connect(target);
    DataOutputStream plainOut = new DataOutputStream(plain.getOutputStream());
    plainOut.writeInt(8);
    plainOut.writeInt(80877103); // SSLRequest
    plainOut.flush();
    assertEquals("Server refused SSL", 'S', plain.getInputStream().read());

    SSLContext context = SSLContext.getInstance("TLS");
    context.init(null, new TrustManager[] {new X509TrustManager() {
      @Override
      public void checkClientTrusted(X509Certificate[] chain, String authType) {}

      @Override
      public void checkServerTrusted(X509Certificate[] chain, String authType) {}

      @Override
      public X509Certificate[] getAcceptedIssuers() {
        return new X509Certificate[0];
      }
    }}, null);
    SSLSocket socket = (SSLSocket) context.getSocketFactory().createSocket(
        plain, target.getHostString(), target.getPort(), true);
    socket.startHandshake();
    return socket;
  }

  private static List<String> readRowsUntilReady(DataInputStream in) throws IOException {
    List<String> rows = new ArrayList<>();
    for (;;) {
      PgMessage msg = PgWireProtocol.readMessage(in);
      switch (msg.type) {
        case PgWireProtocol.BE_DATA_ROW:
          rows.add(PgWireProtocol.parseDataRow(msg.body).get(0));
          break;
        case PgWireProtocol.BE_ERROR_RESPONSE:
          fail("Unexpected error: " + PgWireProtocol.parseErrorResponse(msg.body));
          break;
        case PgWireProtocol.BE_READY_FOR_QUERY:
          return rows;
        default:
          break;
      }
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
