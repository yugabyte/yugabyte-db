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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLWarning;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import com.google.common.net.HostAndPort;
import com.google.gson.JsonObject;

// Checks that pgaudit configured the ways production does it (server config, ALTER ROLE SET,
// ALTER DATABASE SET) audits every statement of every logical session while Conn Mgr shares
// backends between clients, without making connections sticky. Audit lines are read from client
// NOTICEs; STATEMENT_ID is ignored since it is a per-backend counter.
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestPgauditBackendSharing extends BaseYsqlConnMgr {

  private static final String AUDITED_QUERY = "SELECT pg_backend_pid() FROM audit_t";
  private static final String AUDIT_SETTINGS =
      "pgaudit.log='read',pgaudit.log_client=on,pgaudit.log_level='notice'";

  private String auditRole;

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
  }

  private Connection connect(String db, String user) throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withDatabase(db).withUser(user).connect();
  }

  private void execAsSuperuser(String db, String... sqls) throws Exception {
    try (Connection conn = connect(db, "yugabyte");
         Statement stmt = conn.createStatement()) {
      for (String sql : sqls) {
        stmt.execute(sql);
      }
    }
  }

  private void createRoleAndTable(String db) throws Exception {
    execAsSuperuser("yugabyte", "CREATE ROLE " + auditRole + " LOGIN");
    execAsSuperuser(db,
        "CREATE TABLE IF NOT EXISTS audit_t (k int)",
        "INSERT INTO audit_t VALUES (1)",
        "GRANT SELECT ON audit_t TO " + auditRole);
  }

  private List<Connection> connectClients(String db, int numClients) throws Exception {
    List<Connection> conns = new ArrayList<>();
    for (int i = 0; i < numClients; i++) {
      conns.add(connect(db, auditRole));
    }
    return conns;
  }

  private static void closeAll(List<Connection> conns) throws Exception {
    for (Connection conn : conns) {
      conn.close();
    }
  }

  private int runAudited(Statement stmt, int expectedAuditLines) throws Exception {
    int pid;
    try (ResultSet rs = stmt.executeQuery(AUDITED_QUERY)) {
      assertTrue(rs.next());
      pid = rs.getInt(1);
    }
    List<String> lines = new ArrayList<>();
    for (SQLWarning w = stmt.getWarnings(); w != null; w = w.getNextWarning()) {
      if (w.getMessage().startsWith("AUDIT:")) {
        lines.add(w.getMessage());
      }
    }
    assertEquals("Audit lines on backend " + pid + ": " + lines, expectedAuditLines, lines.size());
    for (String line : lines) {
      assertTrue("Unexpected audit line: " + line, line.contains(",READ,SELECT,"));
    }
    return pid;
  }

  // Each client holds a transaction open, so each runs on its own backend.
  private Set<Integer> runOnOwnBackends(List<Connection> conns, int expectedAuditLines)
      throws Exception {
    Set<Integer> pids = new HashSet<>();
    for (Connection conn : conns) {
      conn.setAutoCommit(false);
      try (Statement stmt = conn.createStatement()) {
        pids.add(runAudited(stmt, expectedAuditLines));
      }
    }
    assertEquals(conns.size(), pids.size());
    for (Connection conn : conns) {
      conn.commit();
      conn.setAutoCommit(true);
    }
    return pids;
  }

  // Autocommit statements, so the clients share backends in whatever order Conn Mgr picks.
  private void runShared(List<Connection> conns, int expectedAuditLines) throws Exception {
    for (int round = 0; round < 3; round++) {
      for (Connection conn : conns) {
        try (Statement stmt = conn.createStatement()) {
          runAudited(stmt, expectedAuditLines);
        }
      }
    }
  }

  // Must run while the clients are connected: closing a sticky client closes its backend, which
  // drops it from the count.
  private void assertNoStickyConnections(String db) throws Exception {
    Thread.sleep((2 + STATS_UPDATE_INTERVAL) * 1000);
    JsonObject pool = getPool(db, auditRole);
    assertNotNull(pool);
    assertEquals(0, pool.get("sticky_connections").getAsInt());
  }

  private void setPgConfAndWait(String pgConfCsv, String expectedPgauditLog) throws Exception {
    for (HostAndPort tserver : miniCluster.getTabletServers().keySet()) {
      setServerFlag(tserver, "ysql_pg_conf_csv", pgConfCsv);
    }
    TestUtils.waitFor(() -> {
      try (Connection conn = connect("yugabyte", "yugabyte");
           Statement stmt = conn.createStatement();
           ResultSet rs = stmt.executeQuery("SHOW pgaudit.log")) {
        return rs.next() && expectedPgauditLog.equals(rs.getString(1));
      }
    }, 30000);
    // The postmaster signals its children after reloading; give them time to receive it.
    Thread.sleep(1000);
  }

  @Test
  public void testRoleSettings() throws Exception {
    auditRole = "audit_role_settings";
    createRoleAndTable("yugabyte");
    execAsSuperuser("yugabyte",
        "ALTER ROLE " + auditRole + " SET pgaudit.log = 'read'",
        "ALTER ROLE " + auditRole + " SET pgaudit.log_client = on",
        "ALTER ROLE " + auditRole + " SET pgaudit.log_level = 'notice'");

    List<Connection> conns = connectClients("yugabyte", 3);
    try {
      runOnOwnBackends(conns, 1);
      runShared(conns, 1);
      assertNoStickyConnections("yugabyte");
    } finally {
      closeAll(conns);
    }
  }

  @Test
  public void testDatabaseSettings() throws Exception {
    auditRole = "audit_db_settings";
    String db = "audit_db";
    execAsSuperuser("yugabyte", "CREATE DATABASE " + db);
    createRoleAndTable(db);
    execAsSuperuser("yugabyte",
        "ALTER DATABASE " + db + " SET pgaudit.log = 'read'",
        "ALTER DATABASE " + db + " SET pgaudit.log_client = on",
        "ALTER DATABASE " + db + " SET pgaudit.log_level = 'notice'");

    List<Connection> conns = connectClients(db, 3);
    try {
      runOnOwnBackends(conns, 1);
      runShared(conns, 1);
      assertNoStickyConnections(db);
    } finally {
      closeAll(conns);
    }
  }

  // Turns audit logging on and off through ysql_pg_conf_csv while clients stay connected. As on
  // PG, the change must reach every existing session, on pooled backends that existed before the
  // change and on ones forked after it.
  @Test
  public void testServerConfigChangeAtRuntime() throws Exception {
    auditRole = "audit_server_config";
    createRoleAndTable("yugabyte");

    List<Connection> conns = connectClients("yugabyte", 5);
    try {
      List<Connection> early = conns.subList(0, 3);
      Set<Integer> oldPids = runOnOwnBackends(early, 0);

      setPgConfAndWait(AUDIT_SETTINGS, "read");
      // The first 3 clients take the 3 idle backends from before the change; the other 2 force
      // new backends.
      Set<Integer> pids = runOnOwnBackends(conns, 1);
      assertTrue("Old backends " + oldPids + " not reused: " + pids, pids.containsAll(oldPids));
      runShared(conns, 1);
      assertNoStickyConnections("yugabyte");

      setPgConfAndWait("", "none");
      runOnOwnBackends(conns, 0);
      runShared(conns, 0);
      assertNoStickyConnections("yugabyte");
    } finally {
      closeAll(conns);
      for (HostAndPort tserver : miniCluster.getTabletServers().keySet()) {
        setServerFlag(tserver, "ysql_pg_conf_csv", "");
      }
    }
  }
}
