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
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_AUTHENTICATION;
import static org.yb.ysqlconnmgr.PgWireProtocol.BE_NEGOTIATE_PROTOCOL_VERSION;
import static org.yb.ysqlconnmgr.PgWireProtocol.protocolVersion;

import java.util.Arrays;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.RequiresLinux;
import org.yb.ysqlconnmgr.PgWireProtocol.NegotiateProtocolVersion;

/**
 * Protocol version negotiation between a client and YSQL Connection Manager. Connection manager
 * only speaks protocol 3.0, so a StartupMessage asking for a newer 3.x minor version or carrying
 * "_pq_." protocol options must be answered with NegotiateProtocolVersion naming 3.0 and every
 * "_pq_." option, after which startup continues normally. Any other major version is rejected with
 * a FATAL.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestProtocolNegotiation extends BaseYsqlConnMgr {
  // libpq's PG_PROTOCOL_GREASE, sent by default by PG19 beta libpq.
  private static final int GREASE_MINOR = 9999;
  private static final String GREASE_OPTION = "_pq_.test_protocol_negotiation";

  public TestProtocolNegotiation() {
    // The test certificates are issued for IP addresses, not hostnames.
    useIpWithCertificate = true;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("use_client_to_server_encryption", "true");
    flagMap.put("certs_for_client_dir", certsDir());
    // "host" matches both plaintext and TLS connections.
    flagMap.put("ysql_hba_conf_csv", "host all all all trust");
    return flagMap;
  }

  /**
   * Connects and checks that the first reply is NegotiateProtocolVersion(3.0, expectedOptions)
   * when expectNegotiation, and that there is none otherwise. Then runs a query, which also fails
   * if a "_pq_." option reached the session as a setting.
   */
  private WireConn checkStartup(WireConn.Builder builder, boolean expectNegotiation,
                                String... expectedOptions) throws Exception {
    WireConn conn = builder.connect();
    try {
      NegotiateProtocolVersion negotiated = conn.negotiateProtocolVersion();
      if (expectNegotiation) {
        assertEquals(BE_NEGOTIATE_PROTOCOL_VERSION, conn.startupMessages().get(0).type);
        assertEquals(BE_AUTHENTICATION, conn.startupMessages().get(1).type);
        assertEquals(protocolVersion(3, 0), negotiated.newestVersion);
        assertEquals(Arrays.asList(expectedOptions), negotiated.unsupportedOptions);
      } else {
        assertNull(negotiated);
        assertEquals(BE_AUTHENTICATION, conn.startupMessages().get(0).type);
      }

      StringBuilder query = new StringBuilder("SELECT true");
      String[] row = new String[expectedOptions.length + 1];
      Arrays.fill(row, "t");
      for (String option : expectedOptions)
        query.append(", current_setting('").append(option).append("', true) IS NULL");
      conn.createPipeline().query(query.toString()).row(row).run();
      return conn;
    } catch (Throwable t) {
      conn.closeAbruptly();
      throw t;
    }
  }

  @Test
  public void testProtocol30NotNegotiated() throws Exception {
    checkStartup(rawConnBuilder(), false).close();
  }

  @Test
  public void testNewerMinorVersionNegotiatedDown() throws Exception {
    for (int minor : new int[] {1, 2}) {
      checkStartup(rawConnBuilder().protocolVersion(protocolVersion(3, minor)), true).close();
    }
  }

  @Test
  public void testGreasedStartupNegotiated() throws Exception {
    checkStartup(rawConnBuilder()
                     .protocolVersion(protocolVersion(3, GREASE_MINOR))
                     .startupParam(GREASE_OPTION, ""),
                 true, GREASE_OPTION).close();
  }

  @Test
  public void testProtocolOptionsAt30Negotiated() throws Exception {
    // Both options must be named in the reply, since a client treats an omitted one as accepted,
    // and dropping them must not lose the ordinary parameter between them.
    try (WireConn conn = checkStartup(rawConnBuilder()
                                          .startupParam("_pq_.option_a", "x")
                                          .startupParam("application_name", "negotiation_check")
                                          .startupParam("_pq_.option_b", "y"),
                                      true, "_pq_.option_a", "_pq_.option_b")) {
      assertEquals(Arrays.asList("negotiation_check"),
                   conn.startupParameterValues("application_name"));
    }
  }

  @Test
  public void testNegotiationAfterGssEncRequest() throws Exception {
    checkStartup(rawConnBuilder().gssEncRequestFirst().protocolVersion(protocolVersion(3, 2)),
                 true).close();
  }

  @Test
  public void testNegotiationAfterTls() throws Exception {
    checkStartup(rawConnBuilder()
                     .tls()
                     .protocolVersion(protocolVersion(3, GREASE_MINOR))
                     .startupParam(GREASE_OPTION, ""),
                 true, GREASE_OPTION).close();
  }

  @Test
  public void testUnsupportedMajorVersionRejected() throws Exception {
    for (int major : new int[] {2, 4}) {
      ErrorResponse error =
          rawConnBuilder().protocolVersion(protocolVersion(major, 0)).connectExpectingError();
      assertEquals("FATAL", error.severity());
      assertEquals("0A000", error.sqlstate());
      // Odyssey prefixes its own errors with "odyssey: <client id>: ".
      String expected = String.format(
          "unsupported frontend protocol %d.0: server supports 3.0 to 3.0", major);
      assertTrue(error.message(), error.message().endsWith(expected));
    }
  }
}
