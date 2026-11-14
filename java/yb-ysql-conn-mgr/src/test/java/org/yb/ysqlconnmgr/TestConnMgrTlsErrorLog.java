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
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.security.cert.X509Certificate;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.RequiresLinux;

/**
 * Checks the reason conn mgr logs when a TLS client drops its connection with a TCP reset instead
 * of a TLS close_notify. OpenSSL reports that reset only in errno, which conn mgr used to drop and
 * log as "SSL_ERROR_SYSCALL: Success".
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrTlsErrorLog extends BaseYsqlConnMgr {
  private static final int SSL_REQUEST_CODE = 80877103;
  private static final int SOCKET_TIMEOUT_MS = 15000;
  private static final long LOG_WAIT_SECONDS = 30;
  private static final String CONNECTION_RESET = "SSL_ERROR_SYSCALL: Connection reset by peer";

  public TestConnMgrTlsErrorLog() {
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
    // With auth off, this makes the HBA "hostssl all all all trust", which the raw client below
    // needs: it handles only AuthenticationOk.
    flagMap.put("use_client_to_server_encryption", "true");
    flagMap.put("certs_for_client_dir", certsDir());
    return flagMap;
  }

  @Test
  public void testResetAfterStartup() throws Exception {
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);
    tailer.skipToEnd();

    Socket plain = connectWithSslRequest();
    SSLSocket ssl = startTls(plain);
    OutputStream out = ssl.getOutputStream();
    out.write(PgWireProtocol.buildStartupMessage("yugabyte", "yugabyte", null));
    out.flush();
    PgWireProtocol.readUntilReady(new DataInputStream(ssl.getInputStream()));
    reset(plain);

    assertEquals("SSL_read(): " + CONNECTION_RESET, waitForLoggedError(tailer,
        "client disconnected \\(read/write error, addr [^)]*\\): (.*), status OD_ECLIENT_READ"));
  }

  /* The handshake error used to name SSL_connect() too, though conn mgr is the TLS server. */
  @Test
  public void testResetDuringHandshake() throws Exception {
    ConnMgrLogTailer tailer = ConnMgrLogTailer.create(miniCluster, TSERVER_IDX);
    tailer.skipToEnd();

    reset(connectWithSslRequest());

    assertEquals("SSL_accept(): " + CONNECTION_RESET,
        waitForLoggedError(tailer, "\\(tls\\) error: (.*), login time"));
  }

  /** Connects to conn mgr and sends an SSLRequest, which it must accept. */
  private Socket connectWithSslRequest() throws Exception {
    InetSocketAddress target = miniCluster.getYsqlConnMgrContactPoints().get(TSERVER_IDX);
    Socket plain = new Socket();
    plain.setSoTimeout(SOCKET_TIMEOUT_MS);
    plain.connect(target);
    DataOutputStream out = new DataOutputStream(plain.getOutputStream());
    out.writeInt(8);
    out.writeInt(SSL_REQUEST_CODE);
    out.flush();
    assertEquals("Conn mgr refused SSL", 'S', plain.getInputStream().read());
    return plain;
  }

  /*
   * autoClose is false so that closing the plain socket sends no close_notify, as a client that
   * crashes or loses its network would not.
   */
  private static SSLSocket startTls(Socket plain) throws Exception {
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
    InetSocketAddress peer = (InetSocketAddress) plain.getRemoteSocketAddress();
    SSLSocket ssl = (SSLSocket) context.getSocketFactory().createSocket(
        plain, peer.getHostString(), peer.getPort(), false /* autoClose */);
    ssl.startHandshake();
    return ssl;
  }

  /** Closes the connection with a TCP RST. */
  private static void reset(Socket plain) throws IOException {
    plain.setSoLinger(true, 0);
    plain.close();
  }

  /** Waits for a conn mgr log line matching regex and returns its first group. */
  private static String waitForLoggedError(ConnMgrLogTailer tailer, String regex)
      throws Exception {
    String line = tailer.waitForLogRegex(regex, LOG_WAIT_SECONDS, TimeUnit.SECONDS);
    assertNotNull("No conn mgr log line matched " + regex, line);
    Matcher matcher = Pattern.compile(regex).matcher(line);
    assertTrue(matcher.find());
    return matcher.group(1);
  }
}
