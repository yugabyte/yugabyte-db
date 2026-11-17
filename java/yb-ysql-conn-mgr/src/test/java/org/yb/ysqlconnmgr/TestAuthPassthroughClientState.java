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

import static org.junit.Assume.assumeTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.net.InetAddress;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.LogErrorListener;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

// Verifies that one client's state does not carry over to the next when one auth-passthrough
// control backend authenticates many clients in a row with the same Port.
//
// The client's application_name and the host name check_hostname() resolves for it are stored in
// that Port and allocated in the client's login transaction, so each client must start without the
// previous client's values. Host name logins bind their client socket to a chosen loopback address
// to control the host name the server resolves for it.
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestAuthPassthroughClientState extends BaseYsqlConnMgr {

  // Must reverse-resolve to a host name that forward-resolves back to it.
  private static final String NAMED_ADDR = "127.0.0.1";
  // Must not resolve to NAMED_ADDR's host name.
  private static final String OTHER_ADDR = "127.0.0.3";

  private static final String ALLOW_USER = "hba_allow_user";
  private static final String DENY_USER = "hba_deny_user";
  private static final String APP_NAME_USER = "app_name_user";

  private static final String APP_NAME = "passthrough_app_name";

  private static final int NUM_ROUNDS = 5;
  private static final long LOG_WAIT_MS = 10_000;

  // Static: set when the cluster is created, which happens only once for all test methods.
  private static String namedHost;
  private static String otherHost;

  // Returns the forward-confirmed host name of addr, or null if it has none.
  private static String hostNameOf(String addr) throws Exception {
    String name = InetAddress.getByName(addr).getCanonicalHostName();
    return name.equals(addr) ? null : name;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.replicationFactor(1);
    disableWarmupRandomMode(builder);

    try {
      namedHost = hostNameOf(NAMED_ADDR);
      otherHost = hostNameOf(OTHER_ADDR);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }

    String hba = "host all all all trust";
    if (namedHost != null) {
      hba = String.join(",",
          "host all " + ALLOW_USER + " " + namedHost + " trust",
          "host all " + ALLOW_USER + " all reject",
          "host all " + DENY_USER + " " + namedHost + " reject",
          "host all all all trust");
    }

    Map<String, String> flags = new HashMap<>();
    flags.put("enable_ysql_conn_mgr", "true");
    // Auth passthrough: long-lived control backends authenticate the clients.
    flags.put("ysql_conn_mgr_use_auth_backend", "false");
    // A single control backend, so consecutive logins share its Port.
    flags.put("ysql_conn_mgr_control_connection_pool_size", "1");
    flags.put("ysql_hba_conf_csv", hba);
    // Logs each client's application_name when it is authenticated.
    flags.put("ysql_pg_conf_csv", "log_connections=on");
    builder.addCommonTServerFlags(flags);
  }

  @Before
  public void setUpRoles() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withTServer(TSERVER_IDX)
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .connect();
         Statement stmt = conn.createStatement()) {
      for (String user : new String[] {ALLOW_USER, DENY_USER, APP_NAME_USER}) {
        stmt.execute("DROP ROLE IF EXISTS " + user);
        stmt.execute("CREATE ROLE " + user + " WITH LOGIN");
      }
    }
  }

  private static void assumeHostNames() {
    assumeTrue(NAMED_ADDR + " has no forward-confirmed host name", namedHost != null);
    assumeTrue(OTHER_ADDR + " resolves to " + namedHost + ", same as " + NAMED_ADDR,
        !namedHost.equalsIgnoreCase(otherHost));
    LOG.info("Named client {} resolves to {}; other client {} resolves to {}",
        NAMED_ADDR, namedHost, OTHER_ADDR, otherHost);
  }

  private ConnectionBuilder connMgrBuilder(String user) {
    return getConnectionBuilder()
        .withTServer(TSERVER_IDX)
        .withUser(user)
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR);
  }

  // Logs in through builder with the given extra properties and runs a query.
  private static void login(ConnectionBuilder builder, Properties props) throws Exception {
    try (Connection conn = builder.connect(props);
         Statement stmt = conn.createStatement()) {
      stmt.executeQuery("SELECT 1");
    }
  }

  // Returns whether user can log in from fromAddr; a refusal must come from pg_hba.
  private boolean canLogin(String user, String fromAddr) throws Exception {
    Properties props = new Properties();
    props.setProperty("localSocketAddress", fromAddr);
    try {
      login(connMgrBuilder(user), props);
      return true;
    } catch (SQLException e) {
      assertTrue("Expected a pg_hba.conf rejection, got: " + e.getMessage(),
          e.getMessage().contains("pg_hba.conf rejects connection"));
      return false;
    }
  }

  // Repeated logins from the named host. Each login after the first used to match against the
  // previous login's cached name, which was freed with that login's transaction.
  @Test
  public void testRepeatedLoginsFromNamedHost() throws Exception {
    assumeHostNames();
    for (int i = 0; i < NUM_ROUNDS; i++) {
      assertTrue("Round " + i + ": " + ALLOW_USER + " refused from " + namedHost,
          canLogin(ALLOW_USER, NAMED_ADDR));
      assertFalse("Round " + i + ": " + DENY_USER + " admitted from " + namedHost,
          canLogin(DENY_USER, NAMED_ADDR));
    }
  }

  // Logins from the named host right after a login from another address. The other address's
  // failed or non-matching lookup used to stay cached, so the named host matched no host-name rule:
  // the allow rule refused it and the reject rule let it in.
  @Test
  public void testNamedHostAfterOtherAddress() throws Exception {
    assumeHostNames();
    for (int i = 0; i < NUM_ROUNDS; i++) {
      assertFalse("Round " + i + ": " + ALLOW_USER + " admitted from " + OTHER_ADDR,
          canLogin(ALLOW_USER, OTHER_ADDR));
      assertTrue("Round " + i + ": " + ALLOW_USER + " refused from " + namedHost,
          canLogin(ALLOW_USER, NAMED_ADDR));

      assertTrue("Round " + i + ": " + DENY_USER + " refused from " + OTHER_ADDR,
          canLogin(DENY_USER, OTHER_ADDR));
      assertFalse("Round " + i + ": " + DENY_USER + " admitted from " + namedHost,
          canLogin(DENY_USER, NAMED_ADDR));
    }
  }

  // Logins that send no application_name right after one that sent it. The earlier client's
  // application_name, freed with its login transaction, used to stay in the Port and be logged for
  // the later client.
  @Test
  public void testApplicationNameNotCarriedOver() throws Exception {
    AuthorizedLineCollector collector = new AuthorizedLineCollector();
    for (MiniYBDaemon tserver : miniCluster.getTabletServers().values()) {
      tserver.getLogPrinter().addErrorListener(collector);
    }

    for (int i = 0; i < NUM_ROUNDS; i++) {
      login(connMgrBuilder(APP_NAME_USER).withApplicationName(APP_NAME), new Properties());
      String withName = collector.takeLineFor(APP_NAME_USER, LOG_WAIT_MS);
      assertNotNull("Round " + i + ": no connection authorized line for the login with "
          + "application_name", withName);
      assertTrue("Round " + i + ": application_name not logged: " + withName,
          withName.contains("application_name=" + APP_NAME + " "));

      login(connMgrBuilder(APP_NAME_USER), new Properties());
      String withoutName = collector.takeLineFor(APP_NAME_USER, LOG_WAIT_MS);
      assertNotNull("Round " + i + ": no connection authorized line for the login without "
          + "application_name", withoutName);
      assertFalse("Round " + i + ": application_name logged for a client that sent none: "
          + withoutName, withoutName.contains("application_name="));
    }
  }

  // Collects the "connection authorized" lines that auth passthrough logs.
  private static class AuthorizedLineCollector implements LogErrorListener {
    private final List<String> lines = new ArrayList<>();

    @Override
    public synchronized void handleLine(String line) {
      if (line.contains("connection authorized: ") && line.contains("(via Auth Passthrough)")) {
        lines.add(line);
        notifyAll();
      }
    }

    @Override
    public void reportErrorsAtEnd() {}

    // Removes and returns the oldest line logged for user, or null after timeoutMs.
    synchronized String takeLineFor(String user, long timeoutMs) throws InterruptedException {
      String userField = "connection authorized: user=" + user + " ";
      long deadline = System.currentTimeMillis() + timeoutMs;
      while (true) {
        for (Iterator<String> it = lines.iterator(); it.hasNext();) {
          String line = it.next();
          if (line.contains(userField)) {
            it.remove();
            return line;
          }
        }
        long waitMs = deadline - System.currentTimeMillis();
        if (waitMs <= 0) {
          return null;
        }
        wait(waitMs);
      }
    }
  }
}
