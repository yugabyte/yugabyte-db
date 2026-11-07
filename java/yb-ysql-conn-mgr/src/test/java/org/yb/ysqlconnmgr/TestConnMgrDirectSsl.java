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
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;

import javax.net.ssl.SSLHandshakeException;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrDirectSsl extends BaseYsqlConnMgr {
  private static final int SOCKET_TIMEOUT_MS = 10000;

  public TestConnMgrDirectSsl() {
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
    flagMap.put("ysql_hba_conf_csv", "hostssl all all all trust");
    return flagMap;
  }

  // Opens TLS right after connect with ALPN "postgresql", like libpq's sslnegotiation=direct.
  // Only hostssl is allowed, so the login succeeds only if the connection counts as SSL.
  @Test
  public void testDirectSslWithPostgresqlAlpn() throws Exception {
    try (WireConn conn = rawConnBuilder()
             .socketTimeoutMs(SOCKET_TIMEOUT_MS)
             .directSsl("postgresql")
             .connect()) {
      assertEquals("postgresql", conn.sslApplicationProtocol());
    }
  }

  // Direct TLS without ALPN. The handshake finishes, then the server closes the connection.
  @Test
  public void testDirectSslWithoutAlpnRejected() throws Exception {
    try (WireConn conn = rawConnBuilder()
             .socketTimeoutMs(SOCKET_TIMEOUT_MS)
             .directSsl()
             .connect()) {
      fail("Direct SSL connection without ALPN was accepted");
    } catch (IOException e) {
      LOG.info("Direct SSL connection without ALPN rejected as expected", e);
    }
  }

  // Direct TLS offering only ALPN "h2". The server fails the handshake.
  @Test
  public void testDirectSslWithOtherAlpnRejected() throws Exception {
    try (WireConn conn = rawConnBuilder()
             .socketTimeoutMs(SOCKET_TIMEOUT_MS)
             .directSsl("h2")
             .connect()) {
      fail("Direct SSL connection with ALPN h2 was accepted");
    } catch (SSLHandshakeException e) {
      assertTrue("Unexpected handshake error: " + e,
          e.getMessage().contains("no_application_protocol"));
    }
  }

  // SSLRequest followed by TLS still works, and plain text gets rejected by the hostssl rule.
  @Test
  public void testSslRequestStillWorks() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withSslMode("require")
             .connect();
         Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery("SELECT 1");
      assertTrue(rs.next());
    }

    ErrorResponse error = rawConnBuilder()
        .socketTimeoutMs(SOCKET_TIMEOUT_MS)
        .connectExpectingError();
    assertTrue("Unexpected error for plain text connection: " + error,
        error.toString().contains("no pg_hba.conf entry"));
  }
}
