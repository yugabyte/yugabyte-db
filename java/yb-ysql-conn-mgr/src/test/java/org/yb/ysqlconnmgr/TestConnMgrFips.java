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
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLException;

import org.junit.Assume;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import org.yb.ysqlconnmgr.TestConnMgrSslSettings.TlsProbeFactory;

/**
 * Tests that the client to YSQL Connection Manager TLS hop runs on the OpenSSL FIPS provider when
 * openssl_require_fips is set.
 *
 * FIPS does not reach conn mgr through a GUC. The tserver activates it in-process
 * (OpenSSLInitializer in secure_stream.cc) and hands it to child processes only through the
 * OPENSSL_CONF/OPENSSL_MODULES environment variables that rpc::SetOpenSSLEnv() sets on the
 * subprocess. A pooler started without that call links the same libcrypto but initializes it from
 * the stock config, so it serves the client hop from the default provider while the rest of the
 * universe is in FIPS mode.
 *
 * The two tests below cover that from both ends -- the environment conn mgr was started with, and
 * the crypto its TLS listener will actually agree to.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrFips extends BaseYsqlConnMgr {

  private static final String TLS_1_3 = "TLSv1.3";

  /*
   * TLS 1.3 suites that separate the two providers. The FIPS provider implements no ChaCha20, so
   * OpenSSL drops TLS_CHACHA20_POLY1305_SHA256 from the suites it will negotiate; AES-128-GCM is
   * FIPS approved and survives. Confirmed against the thirdparty OpenSSL 3.5 in this tree:
   * `openssl ciphers -s -tls1_3` lists three suites normally and only the two AES-GCM ones with
   * OPENSSL_CONF pointing at openssl-config/openssl-fips.cnf.
   */
  private static final String JSSE_CHACHA20 = "TLS_CHACHA20_POLY1305_SHA256";
  private static final String JSSE_AES_128 = "TLS_AES_128_GCM_SHA256";

  public TestConnMgrFips() {
    // Certificates in test_certs are issued for IP addresses, not hostnames.
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
    flagMap.put("openssl_require_fips", "true");
    return flagMap;
  }

  /**
   * Connects to conn mgr over TLS, offering only the given protocols and cipher suites.
   *
   * TODO: TlsProbeFactory is shared with TestConnMgrSslSettings; move it to BaseYsqlConnMgr or its
   * own file if more tests start using it.
   */
  private Connection connectOffering(String[] protocols, String[] cipherSuites) throws Exception {
    TlsProbeFactory.offer(protocols, cipherSuites);
    Properties props = new Properties();
    props.setProperty("sslfactory", TlsProbeFactory.class.getName());
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withSslMode("require")
        .connect(props);
  }

  private static void assumeSuiteSupported(String suite) throws Exception {
    Assume.assumeTrue("Runtime JDK does not support " + suite,
        Arrays.asList(SSLContext.getDefault().getSupportedSSLParameters().getCipherSuites())
            .contains(suite));
  }

  private static boolean hasSslCause(Throwable t) {
    for (; t != null; t = t.getCause()) {
      if (t instanceof SSLException) {
        return true;
      }
    }
    return false;
  }

  /** Reads the NUL-separated /proc/<pid>/environ of a process owned by this user. */
  private static Map<String, String> readEnviron(int pid) throws IOException {
    Map<String, String> env = new HashMap<>();
    byte[] raw = Files.readAllBytes(Paths.get("/proc", Integer.toString(pid), "environ"));
    for (String entry : new String(raw, StandardCharsets.UTF_8).split("\0")) {
      int eq = entry.indexOf('=');
      if (eq > 0) {
        env.put(entry.substring(0, eq), entry.substring(eq + 1));
      }
    }
    return env;
  }

  /*
   * Checks the environment conn mgr was actually started with. This is the plumbing itself rather
   * than its effect, and it is the assertion that names the cause when the handshake test below
   * starts failing.
   */
  @Test
  public void testConnMgrStartedWithFipsOpenSslConfig() throws Exception {
    int pid = getOdysseyPidForHost(getPgHost(TSERVER_IDX));
    Map<String, String> env = readEnviron(pid);
    String opensslConf = env.get("OPENSSL_CONF");
    LOG.info("Odyssey pid {} OPENSSL_CONF={} OPENSSL_MODULES={}",
        pid, opensslConf, env.get("OPENSSL_MODULES"));

    assertTrue("conn mgr was started without OPENSSL_CONF, so its libcrypto loaded the stock "
            + "config and the client hop runs on the default provider; "
            + "YsqlConnMgrWrapper::Start() has to call rpc::SetOpenSSLEnv()",
        opensslConf != null);
    assertTrue("OPENSSL_CONF should name the generated FIPS config, got: " + opensslConf,
        opensslConf.endsWith("openssl-fips.cnf"));
    assertEquals("conn mgr must be told FIPS is required so it fails closed",
        "true", env.get("YB_YSQL_CONN_MGR_REQUIRE_FIPS"));
  }

  /*
   * Checks the effect: the suites conn mgr's TLS listener will agree to are the ones the FIPS
   * provider implements. A pooler on the default provider negotiates ChaCha20-Poly1305 happily,
   * so the rejection below is what distinguishes the two providers from the client side.
   */
  @Test
  public void testFipsRejectsChaCha20OnClientHop() throws Exception {
    assumeSuiteSupported(JSSE_CHACHA20);
    assumeSuiteSupported(JSSE_AES_128);

    try (Connection ignored =
        connectOffering(new String[] {TLS_1_3}, new String[] {JSSE_CHACHA20})) {
      fail("conn mgr negotiated " + JSSE_CHACHA20 + ", which the FIPS provider does not "
          + "implement; its TLS listener is running on the default provider");
    } catch (SQLException e) {
      // Any other failure (refused connection, auth) would say nothing about the provider.
      assertTrue("expected a TLS handshake failure, got: " + e, hasSslCause(e));
      LOG.info("Handshake rejected as expected: {}", e.getMessage());
    }

    try (Connection conn = connectOffering(new String[] {TLS_1_3}, new String[] {JSSE_AES_128});
        Statement stmt = conn.createStatement()) {
      assertTrue("a FIPS approved suite must still connect", stmt.executeQuery("SELECT 1").next());
      assertEquals("Negotiated cipher suite", JSSE_AES_128,
          TlsProbeFactory.negotiatedCipherSuite());
    }
  }

  /*
   * The control for the test above. With FIPS off, SetOpenSSLEnv() sets nothing, so the same
   * pooler binary initializes libcrypto from the stock config and serves the client hop from the
   * default provider.
   * ChaCha20 negotiating here is what makes the rejection above evidence about which provider is
   * active.
   */
  @Test
  public void testChaCha20NegotiatesWithoutFips() throws Exception {
    assumeSuiteSupported(JSSE_CHACHA20);
    try {
      Map<String, String> tserverFlags = new HashMap<>();
      tserverFlags.put("openssl_require_fips", "false");
      restartClusterWithAdditionalFlags(Collections.emptyMap(), tserverFlags);

      int pid = getOdysseyPidForHost(getPgHost(TSERVER_IDX));
      Map<String, String> env = readEnviron(pid);
      assertTrue("with FIPS off, nothing should put OPENSSL_CONF in conn mgr's environment",
          env.get("OPENSSL_CONF") == null);
      assertEquals("false", env.get("YB_YSQL_CONN_MGR_REQUIRE_FIPS"));

      try (Connection conn = connectOffering(new String[] {TLS_1_3}, new String[] {JSSE_CHACHA20});
          Statement stmt = conn.createStatement()) {
        assertTrue(stmt.executeQuery("SELECT 1").next());
        assertEquals("Negotiated cipher suite", JSSE_CHACHA20,
            TlsProbeFactory.negotiatedCipherSuite());
      }
    } finally {
      // This test is the only one here that does not want the FIPS cluster, so give the next test
      // a fresh one rather than depending on the order JUnit picks.
      markClusterNeedsRecreation();
    }
  }
}
