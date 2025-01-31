// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.hamcrest.CoreMatchers;
import org.junit.Assume;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;

import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.SystemUtil;
import org.yb.YBParameterizedTestRunner;

import com.google.common.collect.ImmutableMap;

/**
 * Tests for PostgreSQL configuration.
 */
@RunWith(value = YBParameterizedTestRunner.class)
public class TestPgEncryption extends BasePgSQLTest {

  private final ConnectionEndpoint connectionEndpoint;

  @Parameterized.Parameters
  public static List<ConnectionEndpoint> parameters() {
    final String enableYsqlConnMgr = System.getenv("YB_ENABLE_YSQL_CONN_MGR_IN_TESTS");
    if (enableYsqlConnMgr != null && enableYsqlConnMgr.equalsIgnoreCase("true"))
      return Arrays.asList(ConnectionEndpoint.YSQL_CONN_MGR);
    if (SystemUtil.IS_LINUX)
      return Arrays.asList(ConnectionEndpoint.YSQL_CONN_MGR, ConnectionEndpoint.POSTGRES);
    else
      return Arrays.asList(ConnectionEndpoint.POSTGRES);
  }

  public TestPgEncryption(ConnectionEndpoint connectionEndpoint) {
    this.connectionEndpoint = connectionEndpoint;
    useIpWithCertificate = true;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    if (connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR)
      builder.enableYsqlConnMgr(true);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("use_client_to_server_encryption", "true");
    flagMap.put("certs_for_client_dir", certsDir());
    return flagMap;
  }

  private static String certsDir() {
    FileSystem fs = FileSystems.getDefault();
    return fs.getPath(TestUtils.getBinDir()).resolve(fs.getPath("../test_certs")).toString();
  }

  @Test
  public void testSslNoAuth() throws Exception {
    // Client connection with SSL enabled -- should allow connection with or without pass.
    ConnectionBuilder tsConnBldr = getConnectionBuilder().withSslMode("require")
                                   .withConnectionEndpoint(connectionEndpoint);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
      // No-op.
    }
    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      // No-op.
    }

    // Client connection with SSL disabled -- should *not* allow connection with or without pass.
    tsConnBldr = getConnectionBuilder().withSslMode("disable")
                                       .withConnectionEndpoint(connectionEndpoint);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      // With ysql conn mgr endpoint, the checking of ssl connectivity with client happens at
      // ysql conn mgr layer itself and returns required / below error.
      assertThat(
        sqle.getMessage(),
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR ?
        CoreMatchers.containsString("SSL is required") :
        CoreMatchers.containsString("no pg_hba.conf entry for")
      );
    }

    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR ?
        CoreMatchers.containsString("SSL is required") :
        CoreMatchers.containsString("no pg_hba.conf entry for")
      );
    }
  }

  @Test
  public void testSslWithAuth() throws Exception {
    int tserver = spawnTServerWithFlags("ysql_enable_auth", "true");

    // Client connection with SSL enabled -- should only allow connection with pass (+SSL).
    ConnectionBuilder tsConnBldr = getConnectionBuilder().withSslMode("require")
                                                         .withConnectionEndpoint(connectionEndpoint)
                                                         .withTServer(tserver);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
      // No-op.
    }
    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        CoreMatchers.containsString("no password was provided")
      );
    }

    // Client connection with SSL disabled -- should *not* allow connection with or without pass.
    tsConnBldr = getConnectionBuilder().withSslMode("disable").withTServer(tserver)
                                       .withConnectionEndpoint(connectionEndpoint);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR ?
        CoreMatchers.containsString("SSL is required") :
        CoreMatchers.containsString("no pg_hba.conf entry for")
      );
    }

    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR ?
        CoreMatchers.containsString("SSL is required") :
        CoreMatchers.containsString("no pg_hba.conf entry for")
      );
    }
  }

  @Test
  public void testSslWithCustomAuth() throws Exception {
    Assume.assumeFalse("Skipping this test for Ysql Connection Manager as CERT Authentication " +
        "is currently not supported in Ysql Connection Manager.",
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR);

    String sslcertFile = String.format("%s/ysql.crt", certsDir());
    String sslkeyFile = String.format("%s/ysql.key.der", certsDir());
    String sslrootcertFile = String.format("%s/%s", certsDir(), "ca.crt");

    // Using md5 + cert configuration.
    // Additionally we also use ysql_pg_conf just to set a particular cert/key file for testing.
    // Note that as per pgjdbc docs, the key most be in DER format which can be constructed with:
    //   openssl pkcs8 -topk8 -inform PEM -in ysql.key -outform DER -nocrypt -out ysql.key.der
    // (see sslkey entry in https://jdbc.postgresql.org/documentation/head/connect.html for details)
    // This is not required for ysqlsh or psql where the PEM format is supported.
    int tserver = spawnTServerWithFlags(ImmutableMap.of(
        "ysql_hba_conf_csv", "hostssl all all all md5 clientcert=verify-ca",
        "ysql_pg_conf_csv", String.format("ssl_cert_file='%s',ssl_key_file='%s',ssl_ca_file='%s'",
            sslcertFile, sslkeyFile, sslrootcertFile)));

    // Client connection with SSL and cert -- should only allow connection with pass (+cert/SSL).
    ConnectionBuilder tsConnBldr = getConnectionBuilder().withSslMode("require")
                                                         .withSslCert(sslcertFile)
                                                         .withSslKey(sslkeyFile)
                                                         .withSslRootCert(sslrootcertFile)
                                                         .withTServer(tserver);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
     // No-op.
    }
    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        CoreMatchers.containsString("no password was provided")
      );
    }

    // Client connection with SSL verify-full and cert -- should only allow connection with pass
    // (+cert/SSL). 'verify-full' will verify the server host name and root certificate chain to
    // ensure we are connecting to the right server.
    tsConnBldr = getConnectionBuilder().withSslMode("verify-full")
                                       .withSslCert(sslcertFile)
                                       .withSslKey(sslkeyFile)
                                       .withSslRootCert(sslrootcertFile)
                                       .withTServer(tserver);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
      // No-op.
    }
    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        CoreMatchers.containsString("no password was provided")
      );
    }

    // Client connection with SSL enabled but no cert -- should *not* allow connection.
    tsConnBldr = getConnectionBuilder().withSslMode("require").withTServer(tserver);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        CoreMatchers.containsString("connection requires a valid client certificate")
      );
    }
    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        CoreMatchers.containsString("connection requires a valid client certificate")
      );
    }

    // Client connection with SSL disabled -- should *not* allow connection (with or without pass).
    tsConnBldr = getConnectionBuilder().withSslMode("disable").withTServer(tserver);
    try (Connection ignored = tsConnBldr.withUser("yugabyte").withPassword("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        CoreMatchers.containsString("no pg_hba.conf entry for")
      );
    }
    try (Connection ignored = tsConnBldr.withUser("yugabyte").connect()) {
      fail("Expected login attempt to fail");
    } catch (SQLException sqle) {
      assertThat(
        sqle.getMessage(),
        CoreMatchers.containsString("no pg_hba.conf entry for")
      );
    }

  }
}
