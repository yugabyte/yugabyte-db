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
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * Tests YSQL Connection Manager reading from TLS clients when a TLS record does not fit in the
 * readahead buffer.
 *
 * With TLS, OpenSSL decrypts a whole record and keeps whatever does not fit in the readahead.
 * Those bytes are no longer on the socket, so they do not wake the poller; conn mgr has to notice
 * them itself. A 64-byte readahead makes almost every record overflow it.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestConnMgrTlsReadahead extends BaseYsqlConnMgr {

  public TestConnMgrTlsReadahead() {
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
    flagMap.put("ysql_conn_mgr_readahead_buffer_size", "64");
    return flagMap;
  }

  /**
   * mm_tls_read() used to signal io->on_read even when it was NULL, which crashed Odyssey when a
   * TLS record did not fit in the readahead buffer.
   */
  @Test
  public void testSslWithSmallReadahead() throws Exception {
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
   * A TLS client sends Parse/Bind/Execute/Sync and a Query in one write. Conn mgr pauses the
   * client after the Sync with the readahead full and the rest of the Query buffered by OpenSSL.
   * After the Sync's ReadyForQuery, conn mgr must still read the rest of the Query and reply to it.
   */
  @Test
  public void testSslPipelinedQueryAfterSyncWithSmallReadahead() throws Exception {
    String literal = String.join("", Collections.nCopies(200, "a"));
    try (WireConn conn = rawConnBuilder().ssl().connect()) {
      // The pipeline goes out in one write, so the client sends it in a single TLS record.
      conn.createPipeline()
          .parse("SELECT 1").bind().execute().row("1")
          .sync()
          .query("SELECT '" + literal + "'").row(literal)
          .run();
    }
  }
}
