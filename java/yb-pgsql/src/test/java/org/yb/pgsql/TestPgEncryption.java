// Copyright (c) YugabyteDB, Inc.
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

import java.io.File;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import org.apache.commons.io.IOUtils;

import org.hamcrest.CoreMatchers;
import org.junit.Assume;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

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
  private static final Logger LOG = LoggerFactory.getLogger(TestPgEncryption.class);


  private final ConnectionEndpoint connectionEndpoint;

  @Parameterized.Parameters
  public static List<ConnectionEndpoint> parameters() {
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

  @Test
  public void testSslWithCustomAuth() throws Exception {

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
                                                         .withConnectionEndpoint(connectionEndpoint)
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
                                       .withConnectionEndpoint(connectionEndpoint)
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
    tsConnBldr = getConnectionBuilder().withSslMode("require")
                                       .withConnectionEndpoint(connectionEndpoint)
                                       .withTServer(tserver);
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
    tsConnBldr = getConnectionBuilder().withSslMode("disable")
                                       .withConnectionEndpoint(connectionEndpoint)
                                       .withTServer(tserver);
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

  /*
   * SCRAM-SHA-256-PLUS (channel binding) with ysqlsh, since the JDBC driver always uses SCRAM
   * without channel binding. With channel_binding=require, libpq refuses to connect unless the
   * server offered SCRAM-SHA-256-PLUS and the exchange was actually channel-bound, so a successful
   * connection proves PLUS was used. Through the connection manager, the backend checks the
   * binding against the certificate hash forwarded by the connection manager.
   */
  @Test
  public void testScramChannelBinding() throws Exception {
    runScramChannelBindingTest(ImmutableMap.of());
  }

  /*
   * Same as testScramChannelBinding, but the connection manager authenticates through a dedicated
   * auth backend, so the hash is forwarded from od_backend_startup(). The flag is not runtime
   * settable, so it is set only on the tservers this test spawns; other tests keep the default
   * auth passthrough mode.
   */
  @Test
  public void testScramChannelBindingAuthBackend() throws Exception {
    Assume.assumeTrue("Auth backend applies only to the connection manager",
        connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR);
    runScramChannelBindingTest(ImmutableMap.of("ysql_conn_mgr_use_auth_backend", "true"));
  }

  private void runScramChannelBindingTest(Map<String, String> extraFlags) throws Exception {
    final String user = "scram_plus_user";
    final String password = "scram_plus_pass";

    int plusTserver = spawnTServerWithFlags(ImmutableMap.<String, String>builder()
        .put("ysql_hba_conf_csv", "hostssl all all all scram-sha-256")
        .put("ysql_enable_scram_channel_binding", "true")
        .putAll(extraFlags)
        .build());

    int noPlusTserver = spawnTServerWithFlags(ImmutableMap.<String, String>builder()
        .put("ysql_hba_conf_csv", "hostssl all all all scram-sha-256")
        .put("ysql_enable_scram_channel_binding", "false")
        .putAll(extraFlags)
        .build());

    // Tserver 0 has the default (trust) hba; the role is visible from every tserver.
    try (Connection conn = getConnectionBuilder().withSslMode("require")
                                                 .withConnectionEndpoint(connectionEndpoint)
                                                 .withUser("yugabyte")
                                                 .connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET password_encryption = 'scram-sha-256'");
      stmt.execute(String.format("DROP ROLE IF EXISTS %s", user));
      stmt.execute(String.format("CREATE ROLE %s LOGIN PASSWORD '%s'", user, password));
    }

    // spawnTServerWithFlags() does not wait for YSQL to come up, and ysqlsh does not retry.
    waitForYsql(plusTserver, user, password);
    waitForYsql(noPlusTserver, user, password);

    assertYsqlshSucceeds(plusTserver, user, password, "require");
    assertYsqlshFails(plusTserver, user, password + "x", "require",
        "password authentication failed");

    // PLUS is not offered when channel binding is disabled: require fails, prefer falls back to
    // SCRAM-SHA-256.
    assertYsqlshFails(noPlusTserver, user, password, "require",
        "server did not offer an authentication method that supports channel binding");
    assertYsqlshSucceeds(noPlusTserver, user, password, "prefer");
  }

  private void waitForYsql(int tserver, String user, String password) throws Exception {
    ConnectionBuilder builder = getConnectionBuilder().withSslMode("require")
                                                      .withConnectionEndpoint(connectionEndpoint)
                                                      .withTServer(tserver)
                                                      .withUser(user)
                                                      .withPassword(password);
    TestUtils.waitFor(() -> {
      try (Connection ignored = builder.connect()) {
        return true;
      } catch (SQLException e) {
        return false;
      }
    }, 60000);
  }

  private void assertYsqlshSucceeds(int tserver, String user, String password,
                                    String channelBinding) throws Exception {
    YsqlshResult result = runYsqlsh(tserver, user, password, channelBinding);
    assertEquals("ysqlsh with channel_binding=" + channelBinding + " failed: " + result.output,
        0, result.exitCode);
  }

  private void assertYsqlshFails(int tserver, String user, String password,
                                 String channelBinding, String expectedError) throws Exception {
    YsqlshResult result = runYsqlsh(tserver, user, password, channelBinding);
    assertNotEquals("ysqlsh with channel_binding=" + channelBinding + " unexpectedly succeeded",
        0, result.exitCode);
    assertThat(result.output, CoreMatchers.containsString(expectedError));
  }

  private static class YsqlshResult {
    final int exitCode;
    final String output;

    YsqlshResult(int exitCode, String output) {
      this.exitCode = exitCode;
      this.output = output;
    }
  }

  private YsqlshResult runYsqlsh(int tserver, String user, String password,
                                 String channelBinding) throws Exception {
    File ysqlsh = new File(new File(TestUtils.getBuildRootDir(), "postgres/bin"), "ysqlsh");
    InetSocketAddress addr = connectionEndpoint == ConnectionEndpoint.YSQL_CONN_MGR
        ? miniCluster.getYsqlConnMgrContactPoints().get(tserver)
        : miniCluster.getPostgresContactPoints().get(tserver);
    String connStr = String.format(
        "host=%s port=%d user=%s dbname=yugabyte sslmode=require channel_binding=%s",
        addr.getHostString(), addr.getPort(), user, channelBinding);

    ProcessBuilder pb = new ProcessBuilder(
        ysqlsh.toString(), "-v", "ON_ERROR_STOP=1", "-c", "SELECT 1", connStr);
    pb.environment().put("PGPASSWORD", password);
    pb.environment().put("PGCONNECT_TIMEOUT", "30");
    pb.redirectErrorStream(true);

    Process proc = pb.start();
    if (!proc.waitFor(60, TimeUnit.SECONDS)) {
      proc.destroyForcibly();
      fail("ysqlsh timed out: " + connStr);
    }
    String output = IOUtils.toString(proc.getInputStream(), StandardCharsets.UTF_8);
    LOG.info("ysqlsh [{}] exited with {}: {}", connStr, proc.exitValue(), output);
    return new YsqlshResult(proc.exitValue(), output);
  }
}
