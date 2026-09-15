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

import java.io.IOException;
import java.net.Socket;
import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.security.NoSuchAlgorithmException;
import java.security.Principal;
import java.security.PrivateKey;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;

import javax.net.ssl.KeyManager;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509KeyManager;
import javax.net.ssl.X509TrustManager;

import com.yugabyte.ssl.WrappedFactory;

import org.hamcrest.CoreMatchers;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.yb.YBParameterizedTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * Tests that the SSL settings YSQL Connection Manager reads out of ysql_pg.conf are actually
 * applied to its TLS listener.
 *
 * Every test runs twice, once against conn mgr and once against PostgreSQL directly. The
 * PostgreSQL run is the reference: a setting conn mgr has to honor is one PostgreSQL already
 * honors, so a test that passes against one endpoint and fails against the other localizes the
 * defect to the pooler.
 *
 * The client connects through JDBC with {@link TlsProbeFactory} as its sslfactory, which is what
 * lets a test choose which protocols and cipher suites the client offers and then read back what
 * the handshake settled on. Asking the server (pg_stat_ssl, ssl_version()) would not work here,
 * because through the pooler that describes the conn mgr to PostgreSQL connection rather than this
 * one.
 */
@RequiresLinux
@RunWith(value = YBParameterizedTestRunner.class)
public class TestConnMgrSslSettings extends BaseYsqlConnMgr {

  /**
   * A client key manager that never presents a certificate, but records whether the server asked
   * for one and which certificate authorities it named in its CertificateRequest.
   */
  public static class RecordingKeyManager implements X509KeyManager {
    private static volatile boolean asked;
    private static volatile Principal[] issuers;

    static void reset() {
      asked = false;
      issuers = null;
    }

    @Override
    public String chooseClientAlias(String[] keyType, Principal[] issuers, Socket socket) {
      RecordingKeyManager.asked = true;
      RecordingKeyManager.issuers = issuers;
      return null; // Present no client certificate.
    }

    @Override
    public String[] getClientAliases(String keyType, Principal[] issuers) { return null; }
    @Override
    public String[] getServerAliases(String keyType, Principal[] issuers) { return null; }
    @Override
    public String chooseServerAlias(String keyType, Principal[] issuers, Socket socket) {
      return null;
    }
    @Override
    public X509Certificate[] getCertificateChain(String alias) { return null; }
    @Override
    public PrivateKey getPrivateKey(String alias) { return null; }
  }

  /*
   * An sslfactory for the JDBC driver that restricts what the client offers during the TLS
   * handshake and remembers what the handshake settled on. The driver exposes no connection
   * property for either, and it instantiates this class reflectively, so what the client offers
   * travels through static state and the tests below must not run concurrently.
   */
  public static class TlsProbeFactory extends WrappedFactory {
    private static String[] offeredProtocols;
    private static String[] offeredCipherSuites;
    private static volatile SSLSocket lastSocket;

    /**
     * JDK 8 ships TLSv1.3 but leaves it out of the "TLS" context's active protocols, and
     * setEnabledProtocols() cannot put it back: getEnabledProtocols() then reports a version the
     * ClientHello never offers, so a TLSv1.3 connection fails as though the server had refused it.
     * A context obtained as "TLSv1.3" enables it, and still lets a test pin a connection to
     * TLSv1.2. JDK 8u252 and older have no TLSv1.3 at all, hence the fallback.
     */
    static SSLContext newContext() throws NoSuchAlgorithmException {
      try {
        return SSLContext.getInstance(TLS_1_3);
      } catch (NoSuchAlgorithmException e) {
        return SSLContext.getInstance("TLS");
      }
    }

    public TlsProbeFactory() throws Exception {
      SSLContext ctx = newContext();
      // These tests are about what the pooler agrees to, not about who signed its certificate.
      // The key manager presents no certificate; it is here only to record whether conn mgr asked
      // for one, and which certificate authorities it named.
      ctx.init(new KeyManager[]{new RecordingKeyManager()},
          new TrustManager[]{new X509TrustManager() {
        public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
        public void checkClientTrusted(X509Certificate[] chain, String authType) {}
        public void checkServerTrusted(X509Certificate[] chain, String authType) {}
      }}, new SecureRandom());
      factory = ctx.getSocketFactory();
    }

    /**
     * Restricts what the next connection offers. Passing null for either argument leaves the JDK
     * defaults in place.
     */
    static void offer(String[] protocols, String[] cipherSuites) {
      offeredProtocols = protocols;
      offeredCipherSuites = cipherSuites;
      lastSocket = null;
    }

    static String negotiatedProtocol() {
      return lastSocket.getSession().getProtocol();
    }

    static String negotiatedCipherSuite() {
      return lastSocket.getSession().getCipherSuite();
    }

    @Override
    public Socket createSocket(Socket s, String host, int port, boolean autoClose)
        throws IOException {
      // MakeSSL starts the handshake as soon as this returns, so restrict the socket now.
      SSLSocket socket = (SSLSocket) super.createSocket(s, host, port, autoClose);
      if (offeredProtocols != null) {
        socket.setEnabledProtocols(offeredProtocols);
      }
      if (offeredCipherSuites != null) {
        // JSSE puts the suites in the ClientHello in the order given here, which is what makes the
        // ssl_prefer_server_ciphers test meaningful.
        socket.setEnabledCipherSuites(offeredCipherSuites);
      }
      lastSocket = socket;
      return socket;
    }
  }

  private static final String TLS_1_2 = "TLSv1.2";
  private static final String TLS_1_3 = "TLSv1.3";

  // Two suites the test_certs RSA server certificates can be used with, as OpenSSL spells them for
  // ssl_ciphers and as JSSE spells them for the client side and the assertions.
  private static final String OPENSSL_AES_128 = "ECDHE-RSA-AES128-GCM-SHA256";
  private static final String OPENSSL_AES_256 = "ECDHE-RSA-AES256-GCM-SHA384";
  private static final String JSSE_AES_128 = "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
  private static final String JSSE_AES_256 = "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
  // Client certificates from test_certs, issued by the same CA and differing only in that
  // ysql_revoked is listed in that CA's ca.crl.
  private static final String VALID_CLIENT_CERT = "ysql";
  private static final String REVOKED_CLIENT_CERT = "ysql_revoked";

  private final ConnectionEndpoint connectionEndpoint;

  public TestConnMgrSslSettings(ConnectionEndpoint connectionEndpoint) {
    this.connectionEndpoint = connectionEndpoint;
    // Certificates in test_certs are issued for IP addresses, not hostnames.
    useIpWithCertificate = true;
  }

  @Parameterized.Parameters
  public static List<ConnectionEndpoint> connectionEndpoints() {
    return Arrays.asList(ConnectionEndpoint.POSTGRES, ConnectionEndpoint.YSQL_CONN_MGR);
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

  /** Restarts the cluster with the given ssl_* GUCs written into ysql_pg.conf. */
  private void restartWithSslGucs(String pgConfCsv) throws Exception {
    Map<String, String> tserverFlags = new HashMap<>();
    tserverFlags.put("ysql_pg_conf_csv", pgConfCsv);
    LOG.info("Restarting cluster with ysql_pg_conf_csv: {}", pgConfCsv);
    restartClusterWithAdditionalFlags(new HashMap<>(), tserverFlags);
  }

  /** Connects to the endpoint under test over TLS, offering only the given protocols. */
  private Connection connectOffering(String... protocols) throws Exception {
    return connectOffering(protocols, null);
  }

  /**
   * Connects to the endpoint under test over TLS, offering only the given protocols and cipher
   * suites, in the order given.
   */
  private Connection connectOffering(String[] protocols, String[] cipherSuites) throws Exception {
    TlsProbeFactory.offer(protocols, cipherSuites);
    Properties props = new Properties();
    props.setProperty("sslfactory", TlsProbeFactory.class.getName());
    return getConnectionBuilder()
        .withConnectionEndpoint(connectionEndpoint)
        .withSslMode("require")
        .connect(props);
  }

  /**
   * Connects to the endpoint under test over TLS, presenting the named client certificate from
   * test_certs.
   */
  private Connection connectWithClientCert(String certPrefix) throws Exception {
    // pgjdbc requires the key in PKCS#8 DER form, which is why test_certs ships a .key.der.
    return getConnectionBuilder()
        .withConnectionEndpoint(connectionEndpoint)
        .withSslMode("require")
        .withSslCert(String.format("%s/%s.crt", certsDir(), certPrefix))
        .withSslKey(String.format("%s/%s.key.der", certsDir(), certPrefix))
        .withSslRootCert(String.format("%s/ca.crt", certsDir()))
        .connect();
  }

  private void assertClientCertAccepted(String certPrefix) throws Exception {
    try (Connection conn = connectWithClientCert(certPrefix);
         Statement stmt = conn.createStatement()) {
      assertTrue(stmt.executeQuery("SELECT 1").next());
    }
  }

  /*
   * The reason cannot be asserted on: the server closes the connection as it sends the alert, so
   * the client may report "SSL error: Broken pipe" or a generic "connection attempt failed". What
   * pins the rejection to revocation is the company this assertion keeps in each test below.
   */
  private void assertClientCertRejected(String certPrefix) throws Exception {
    try (Connection ignored = connectWithClientCert(certPrefix)) {
      fail("Expected " + connectionEndpoint + " to reject client certificate " + certPrefix);
    } catch (SQLException e) {
      LOG.info("Handshake rejected as expected: {}", e.getMessage());
      assertThat(e.getMessage(), CoreMatchers.anyOf(
          CoreMatchers.containsString("SSL error"),
          CoreMatchers.containsString("connection attempt failed")));
    }
  }

  /**
   * What a {@link TlsProbeFactory} connection offers when a test does not restrict it. This is the
   * context's enabled set rather than its supported set, which on JDK 8 names TLSv1.3 even where
   * no connection can offer it.
   */
  private static String[] activeProtocols() throws Exception {
    SSLContext ctx = TlsProbeFactory.newContext();
    ctx.init(null, null, null);
    return ctx.getDefaultSSLParameters().getProtocols();
  }

  /** TLSv1.2 and TLSv1.3, minus whatever the runtime JDK cannot offer. */
  private static String[] probeProtocols() throws Exception {
    List<String> active = Arrays.asList(activeProtocols());
    List<String> offered = new ArrayList<>();
    for (String protocol : new String[] {TLS_1_2, TLS_1_3}) {
      if (active.contains(protocol)) {
        offered.add(protocol);
      }
    }
    return offered.toArray(new String[0]);
  }

  /**
   * The base class probes for readiness with a connection that offers whatever TLS versions the
   * JDK enables by default, which on JDK 8 is TLSv1.2 alone even though TLSv1.3 is supported.
   * That probe cannot reach a pooler running under ssl_min_protocol_version='TLSv1.3', so the
   * restart times out before any assertion in this class runs. Offer both versions explicitly,
   * against whichever endpoint the run is exercising.
   */
  @Override
  public void verifyClusterAcceptsConnMgrConnections() throws Exception {
    LOG.info("Waiting for {} to accept connections over TLS", connectionEndpoint);
    final String[] protocols = probeProtocols();
    TestUtils.waitFor(() -> {
        try {
          connectOffering(protocols, null).close();
          return true;
        } catch (Exception e) {
          return false;
        }
      },
      10000);
  }

  /*
   * Validates min and max protocol version settings with conn mgr
   */
  @Test
  public void testSslMinMaxProtocolVersion() throws Exception {
    restartWithSslGucs("ssl_min_protocol_version='TLSv1.3'");

    try (Connection ignored = connectOffering(TLS_1_2)) {
      fail("Expected " + connectionEndpoint + " to refuse a " + TLS_1_2 + " client");
    } catch (SQLException e) {
      LOG.info("Handshake rejected as expected: {}", e.getMessage());
      assertThat(e.getMessage(), CoreMatchers.containsString("SSL error"));
    }

    try (Connection conn = connectOffering(TLS_1_3);
        Statement stmt = conn.createStatement()) {
      assertTrue(stmt.executeQuery("SELECT 1").next());
      assertEquals("Negotiated protocol", TLS_1_3, TlsProbeFactory.negotiatedProtocol());
    }

    restartWithSslGucs("ssl_min_protocol_version='TLSv1.2',ssl_max_protocol_version='TLSv1.2'");

    try (Connection ignored = connectOffering(TLS_1_3)) {
      fail("Expected " + connectionEndpoint + " to refuse a " + TLS_1_3 + " client");
    } catch (SQLException e) {
      LOG.info("Handshake rejected as expected: {}", e.getMessage());
      assertThat(e.getMessage(), CoreMatchers.containsString("SSL error"));
    }

    try (Connection conn = connectOffering(TLS_1_2);
        Statement stmt = conn.createStatement()) {
      assertTrue(stmt.executeQuery("SELECT 1").next());
      assertEquals("Negotiated protocol", TLS_1_2, TlsProbeFactory.negotiatedProtocol());
    }

  }

  /*
   * Validates that an empty ssl_min_protocol_version stops YSQL outright instead of quietly
   * lowering the TLS floor. PostgreSQL rejects the value because the GUC is declared over
   * ssl_protocol_versions_info + 1 and so has no {"", PG_TLS_ANY} entry. Conn mgr has to refuse it
   * for the same reason: an empty value reaches machinarium as a non-NULL empty string, which
   * yb_mm_tls_protocol_to_pg_enum() maps to PG_TLS_ANY, and be_tls_init() reads that as "impose no
   * minimum" without reporting anything. ValidatePgSslMinProtocolVersion() in ysql_conn_mgr_conf.cc
   * is what keeps conn mgr from coming up in that state.
   *
   * The tserver outlives both children failing, so the restart itself may or may not surface an
   * error; what has to hold either way is that neither the postmaster nor odyssey is running.
   */
  @Test
  public void testEmptySslMinProtocolVersionBlocksStartup() throws Exception {
    // Check that the processes are running before the restart.
    int odysseyPid = getOdysseyPid();
    int postmasterPid = getPostmasterPid();
    try {
      try {
        restartWithSslGucs("ssl_min_protocol_version=''");
      } catch (Exception e) {
        // Restart may or may not surface the child failure; the process checks below are the
        // real assertion. Swallow so we always get to them.
        LOG.info("Restart reported failure: {}", e.getMessage());
      }

      // Odyssey: ValidatePgSslMinProtocolVersion() rejects the value at config-generation time
      // in the wrapper, so the odyssey binary is never exec'd -- getOdysseyPid() must throw.
      try {
        getOdysseyPid();
        fail("Expected Odyssey to be not running");
      } catch (Exception e) {
        LOG.info("Odyssey is not running as expected: {}", e.getMessage());
      }

      // Postmaster: PgWrapper's supervisor re-forks postgres every ~100ms after each FATAL, so
      // "process exists right now" is racy against the retry cycle. The invariant to assert is
      // that no single postmaster survives -- sample twice with a gap and require the pid to
      // either be absent, or differ across samples (churn = not stably up).
      Integer firstPid = null;
      try {
        firstPid = getPostmasterPid();
      } catch (Exception e) {
        LOG.info("Postmaster not running on first sample: {}", e.getMessage());
      }
      Thread.sleep(2000);
      Integer secondPid = null;
      try {
        secondPid = getPostmasterPid();
      } catch (Exception e) {
        LOG.info("Postmaster not running on second sample: {}", e.getMessage());
      }
      if (firstPid != null && firstPid.equals(secondPid)) {
        fail("Expected Postmaster to not stay running, but pid " + firstPid
            + " persisted across 2s");
      }
      LOG.info("Postmaster is not stably running as expected (first={}, second={})",
          firstPid, secondPid);
    } finally {
      restartWithSslGucs("");
    }
  }

  /*
   * Validates that an explicitly empty ssl_max_protocol_version is accepted and leaves the ceiling
   * unset. The two GUCs are asymmetric: ssl_min_protocol_version is defined over
   * ssl_protocol_versions_info + 1, which skips the {"", PG_TLS_ANY} entry, so an empty min is
   * rejected, while ssl_max_protocol_version keeps that entry and PG_TLS_ANY is its boot value.
   * be_tls_init() reads PG_TLS_ANY as "never call SSL_CTX_set_max_proto_version()", and conn mgr
   * has to map an empty value the same way.
   */
  @Test
  public void testEmptySslMaxProtocolVersion() throws Exception {
    restartWithSslGucs("ssl_max_protocol_version=''");

    try (Connection conn = connectOffering(TLS_1_3);
        Statement stmt = conn.createStatement()) {
      assertEquals("Negotiated protocol", TLS_1_3, TlsProbeFactory.negotiatedProtocol());

      try (ResultSet rs = stmt.executeQuery("SHOW ssl_max_protocol_version")) {
        assertTrue("SHOW ssl_max_protocol_version returned no row", rs.next());
        assertEquals("PG spells PG_TLS_ANY as the empty label", "", rs.getString(1));
      }
    }
  }

  /*
   * Validates that only the cipher suites listed in ssl_ciphers can be used. The GUC maps to
   * SSL_CTX_set_cipher_list(), which governs TLSv1.2 and older, so the client is pinned to TLSv1.2.
   *
   * The accepted handshake also carries the client certificate check. be_tls_init() asks for a
   * client certificate whenever a root store is configured, without ever requiring one: it sets
   * SSL_VERIFY_PEER and SSL_VERIFY_CLIENT_ONCE but not SSL_VERIFY_FAIL_IF_NO_PEER_CERT, because
   * PostgreSQL decides whether a certificate was required from pg_hba.conf. Conn mgr does not run
   * that check yet, so both halves matter: the CertificateRequest must name the configured root
   * CA, and a client that answers it with no certificate must still be let through. Being pinned
   * to TLSv1.2 is what makes the certificate authorities readable here, as TLSv1.2 puts them
   * directly in the CertificateRequest while TLSv1.3 encodes them as an extension.
   */
  @Test
  public void testSslCiphers() throws Exception {
    restartWithSslGucs("ssl_ciphers='" + OPENSSL_AES_128 + "'");

    try (Connection ignored =
        connectOffering(new String[] {TLS_1_2}, new String[] {JSSE_AES_256})) {
      fail("Expected " + connectionEndpoint + " to refuse a " + TLS_1_2 + " client offering "
          + JSSE_AES_256);
    } catch (SQLException e) {
      LOG.info("Handshake rejected as expected: {}", e.getMessage());
      assertThat(e.getMessage(), CoreMatchers.containsString("SSL error"));
    }

    RecordingKeyManager.reset();

    try (Connection conn = connectOffering(new String[] {TLS_1_2}, new String[] {JSSE_AES_128});
        Statement stmt = conn.createStatement()) {
      assertTrue("a client presenting no certificate must still connect",
          stmt.executeQuery("SELECT 1").next());
      assertEquals("Negotiated cipher suite", JSSE_AES_128,
          TlsProbeFactory.negotiatedCipherSuite());
    }

    assertTrue(connectionEndpoint + " should have requested a client certificate",
        RecordingKeyManager.asked);
    Principal[] issuers = RecordingKeyManager.issuers;
    LOG.info("CertificateRequest named issuers: {}", Arrays.toString(issuers));
    assertTrue("CertificateRequest should have named the configured root CA; "
            + "be_tls_init() calls SSL_CTX_set_client_CA_list(), which machinarium never did",
        issuers != null && issuers.length > 0);
  }

  /*
   * Validates ssl_prefer_server_ciphers. Conn mgr lists AES-128 ahead of AES-256 while the client
   * offers them the other way round, so the setting alone decides which suite wins.
   */
  @Test
  public void testSslPreferServerCiphers() throws Exception {
    final String[] protocols = {TLS_1_2};
    final String[] clientOrder = {JSSE_AES_256, JSSE_AES_128};
    final String serverOrder = "ssl_ciphers='" + OPENSSL_AES_128 + ":" + OPENSSL_AES_256 + "'";

    restartWithSslGucs(serverOrder + ",ssl_prefer_server_ciphers=on");

    try (Connection ignored = connectOffering(protocols, clientOrder)) {
      assertEquals("Server order should decide", JSSE_AES_128,
          TlsProbeFactory.negotiatedCipherSuite());
    }

    restartWithSslGucs(serverOrder + ",ssl_prefer_server_ciphers=off");

    try (Connection ignored = connectOffering(protocols, clientOrder)) {
      assertEquals("Client order should decide", JSSE_AES_256,
          TlsProbeFactory.negotiatedCipherSuite());
    }

    /*
     * ssl_prefer_server_ciphers is the only SSL setting conn mgr parses itself, and its value
     * comes back from ysql_pg.conf, whose content is partly operator supplied through
     * --ysql_pg_conf_csv. PostgreSQL's parse_bool_with_len() accepts any unambiguous
     * abbreviation, so conn mgr has to read the same spellings: "t" is the shortest form of true,
     * and "of" the shortest of off, since a lone "o" cannot tell the two apart.
     */
    restartWithSslGucs(serverOrder + ",ssl_prefer_server_ciphers=t");

    try (Connection ignored = connectOffering(protocols, clientOrder)) {
      assertEquals("'t' should mean on, so server order should decide", JSSE_AES_128,
          TlsProbeFactory.negotiatedCipherSuite());
    }

    restartWithSslGucs(serverOrder + ",ssl_prefer_server_ciphers=of");

    try (Connection ignored = connectOffering(protocols, clientOrder)) {
      assertEquals("'of' should mean off, so client order should decide", JSSE_AES_256,
          TlsProbeFactory.negotiatedCipherSuite());
    }
  }

  /*
   * Validates ssl_crl_file. The server solicits a client certificate even though cert
   * authentication is not offered, and verifies whatever is presented, so a revoked certificate is
   * turned away once the CRL is loaded. Three connections make the case that the CRL is what
   * turned it away: the revoked certificate is refused, the valid one from the same CA is not, and
   * the revoked one is fine again with no CRL configured.
   */
  @Test
  public void testSslCrlFile() throws Exception {
    restartWithSslGucs("ssl_crl_file='" + certsDir() + "/ca.crl'");

    assertClientCertRejected(REVOKED_CLIENT_CERT);
    assertClientCertAccepted(VALID_CLIENT_CERT);

    restartWithSslGucs("");

    assertClientCertAccepted(REVOKED_CLIENT_CERT);
  }

  /*
   * Validates ssl_crl_dir, which holds the same CRL as ssl_crl_file above under the hashed name
   * OpenSSL looks a directory up by.
   */
  @Test
  public void testSslCrlDir() throws Exception {
    restartWithSslGucs(String.format("ssl_crl_dir='%s/crl'", certsDir()));

    assertClientCertRejected(REVOKED_CLIENT_CERT);
    assertClientCertAccepted(VALID_CLIENT_CERT);
  }
}
