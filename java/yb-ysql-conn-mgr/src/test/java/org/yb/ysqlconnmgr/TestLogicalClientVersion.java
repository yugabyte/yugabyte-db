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

import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertThrows;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.*;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.stream.Collectors;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.AutoCommit;
import org.yb.pgsql.ConnectionEndpoint;


@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestLogicalClientVersion extends BaseYsqlConnMgr {

  public String LOGICAL_CLIENT_VERSION_TABLE = "pg_yb_logical_client_version";

  public String createLogicalClientSelectQuery() {
    return "SELECT current_version FROM " + LOGICAL_CLIENT_VERSION_TABLE;
  }

  private long getLogicalClientVersion(Statement statement) throws Exception {
    List<Row> rows = getRowList(statement, createLogicalClientSelectQuery());
    assertEquals(1, rows.size());
    return rows.get(0).getLong(0);
  }

  // Read the version without going through Connection Manager, so that the observation
  // itself is not affected by the version matching.
  private long getLogicalClientVersionFromPgEndpoint() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .connect();
        Statement statement = connection.createStatement()) {
      return getLogicalClientVersion(statement);
    }
  }

  @Test
  public void testLogicalClientVersionBump() throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement statement = connection.createStatement()) {

        String query = createLogicalClientSelectQuery();
        List<Row> rows = getRowList(statement, query);

        assertEquals(rows.size(), 1);
        Row ver = rows.get(0);
        Long old_version = rows.get(0).getLong(0);

        // Atler role to bump up the version
        statement.execute("ALTER ROLE yugabyte set timezone = 'GMT'");
        rows = getRowList(statement, query);
        assertEquals(rows.size(), 1);
        Long new_version = rows.get(0).getLong(0);
        assertEquals((long)new_version, (long)old_version+1);

        // Atler database to bump up the version
        statement.execute("ALTER ROLE yugabyte set timezone = 'GMT'");
        rows = getRowList(statement, query);
        assertEquals(rows.size(), 1);
        new_version = rows.get(0).getLong(0);
        assertEquals((long)new_version, (long)old_version + 2);
    }
  }

  // Two logical connections are created named c1 and c2 that will execute txns
  // on same backend B1. Even if c2 has executed "ALTER ROLE SET" command to bump
  // the logical cient version in pg_yb_logical_client_version table, the backend B1
  // was spwaned before hence having old version number.
  @Test
  public void testOnePhysicalConnectionWithTwoLogicalConnections() throws Exception {
    enableVersionMatchingAndRestartCluster();
    try (Connection c1 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection c2 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement s1 = c1.createStatement();
        Statement s2 = c2.createStatement()) {

      String query = createLogicalClientSelectQuery();
      List<Row> rows = getRowList(s1, query);

      assertEquals(rows.size(), 1);
      Row ver = rows.get(0);
      Long old_version = rows.get(0).getLong(0);

      // Executed on backend B1.
      s1.execute("SELECT 1");
      // Executed on backend B1, increase logical_client_version from 1 to 2
      // in pg_yb_logical_client_version catalog table.
      s2.execute("ALTER ROLE yugabyte SET timezone = 'GMT'");

      rows = getRowList(s2, query);
      assertEquals(rows.size(), 1);
      Long new_version = rows.get(0).getLong(0);
      assertEquals((long)new_version, (long)old_version+1);

      // Also executed on backend B1.
      s1.execute("SELECT 1");
    }
  }

  // This test is same as testOnePhysicalConnectionWithTwoLogicalConnections with the
  // difference that now Backend B1 is made sticky to logical connection c2. Due to this
  // new backend will be spwaned for c1 to execute next statement. But since there is bump
  // of version caused by "ALTER ROLE SET" executed by c2, c1 will not get any matching
  // backend hence connecting to backend of higher version.
  @Test
  public void testOnePhysicalConnectionWithTwoLogicalConnectionsSticky() throws Exception {
    enableVersionMatchingAndRestartCluster();
    try (Connection c1 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Connection c2 = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement s1 = c1.createStatement();
        Statement s2 = c2.createStatement()) {

      String query = createLogicalClientSelectQuery();
      List<Row> rows = getRowList(s1, query);

      assertEquals(rows.size(), 1);
      Row ver = rows.get(0);
      Long old_version = rows.get(0).getLong(0);

      // Executed on backend B1.
      s1.execute("SELECT 1");
      // Executed on backend B1, increase logical_client_version from 1 to 2.
      s2.execute("ALTER ROLE yugabyte SET timezone = 'GMT'");
      // Now B1 is sticky with c2.
      s2.execute("BEGIN");

      rows = getRowList(s2, query);
      assertEquals(rows.size(), 1);
      Long new_version = rows.get(0).getLong(0);
      assertEquals((long)new_version, (long)old_version+1);

      // connect to backend of higher version.
      s1.execute("SELECT 1");
    }
  }

  // A failed ALTER ... SET must not bump the logical client version, otherwise the
  // connection which issued it gets killed.
  @Test
  public void testNoVersionBumpOnFailedAlter() throws Exception {
    Map<String, String> tsFlagMap = new HashMap<>();
    tsFlagMap.put("ysql_conn_mgr_alter_guc_adoption_strategy", "connection_static");
    tsFlagMap.put("ysql_conn_mgr_alter_guc_stale_backend_ttl_ms", "0");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tsFlagMap);

    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement statement = connection.createStatement()) {

      long oldVersion = getLogicalClientVersion(statement);

      assertThrows(SQLException.class,
          () -> statement.execute("ALTER ROLE yugabyte SET timezone = 'Invalid/Timezone'"));

      statement.execute("SELECT 1");
      assertEquals(oldVersion, getLogicalClientVersion(statement));
    }
  }

  // The version bump of an ALTER ... SET inside a transaction block happens at commit
  // time, and not at all if the block is rolled back.
  @Test
  public void testVersionBumpInsideTransactionBlock() throws Exception {
    // connection_static with 0 TTL kills an old backend immediately after increment, and
    // transactional ddl is required to actually rollback the ALTER ROLE SET commands
    Map<String, String> tsFlagMap = new HashMap<>();
    tsFlagMap.put("ysql_conn_mgr_alter_guc_adoption_strategy", "connection_static");
    tsFlagMap.put("ysql_conn_mgr_alter_guc_stale_backend_ttl_ms", "0");
    tsFlagMap.put("ysql_yb_ddl_transaction_block_enabled", "true");
    restartClusterWithAdditionalFlags(Collections.emptyMap(), tsFlagMap);

    long oldVersion = getLogicalClientVersionFromPgEndpoint();

    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement statement = connection.createStatement()) {

      statement.execute("BEGIN");
      statement.execute("ALTER ROLE yugabyte SET timezone = 'GMT'");
      statement.execute("SELECT 1");
      assertEquals(oldVersion, getLogicalClientVersionFromPgEndpoint());

      statement.execute("ROLLBACK");
      statement.execute("SELECT 1");
      assertEquals(oldVersion, getLogicalClientVersionFromPgEndpoint());

      statement.execute("BEGIN");
      statement.execute("ALTER ROLE yugabyte SET timezone = 'GMT'");
      statement.execute("SELECT 1");
      assertEquals(oldVersion, getLogicalClientVersionFromPgEndpoint());

      statement.execute("COMMIT");
      assertEquals(oldVersion + 1, getLogicalClientVersionFromPgEndpoint());

      assertThrows(SQLException.class, () -> statement.execute("SELECT 1"));
    }
  }

  // ALTER ROLE ... NOSUPERUSER with other options bumps the version, so pooled backends
  // started before it are not reused by new sessions of the role.
  @Test
  public void testVersionBumpOnAlterRoleSuperuserWithOtherOptions() throws Exception {
    try (Connection adminConn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
            .connect();
        Statement adminStmt = adminConn.createStatement()) {
      adminStmt.execute("CREATE ROLE su_role SUPERUSER LOGIN");

      try (Connection conn = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
              .withUser("su_role")
              .connect();
          Statement stmt = conn.createStatement()) {
        assertTrue(verifySessionParameterValue(stmt, "is_superuser", "on"));
      }

      long oldVersion = getLogicalClientVersion(adminStmt);
      adminStmt.execute("ALTER ROLE su_role NOSUPERUSER CREATEDB");
      assertEquals(oldVersion + 1, getLogicalClientVersionFromPgEndpoint());

      try (Connection conn = getConnectionBuilder()
              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
              .withUser("su_role")
              .connect();
          Statement stmt = conn.createStatement()) {
        assertTrue(verifySessionParameterValue(stmt, "is_superuser", "off"));
        assertThrows(SQLException.class,
            () -> stmt.execute("SET SESSION AUTHORIZATION yugabyte"));
      }
      adminStmt.execute("DROP ROLE su_role");
    }
  }

  private Connection connectToConnMgrAs(String user) throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser(user)
        .connect();
  }

  private void executeOnPgEndpoint(String query) throws Exception {
    try (Connection connection = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .connect();
        Statement statement = connection.createStatement()) {
      statement.execute(query);
    }
  }

  // ALTER ROLE ... NOSUPERUSER issued without going through Connection Manager bumps the
  // version, so pooled backends started before it are not reused by new sessions of the role.
  @Test
  public void testVersionBumpOnAlterRoleSuperuserOnPgEndpoint() throws Exception {
    executeOnPgEndpoint("CREATE ROLE su_pg_role SUPERUSER LOGIN");

    try (Connection conn = connectToConnMgrAs("su_pg_role");
        Statement stmt = conn.createStatement()) {
      assertTrue(verifySessionParameterValue(stmt, "is_superuser", "on"));
    }

    long oldVersion = getLogicalClientVersionFromPgEndpoint();
    executeOnPgEndpoint("ALTER ROLE su_pg_role NOSUPERUSER");
    assertEquals(oldVersion + 1, getLogicalClientVersionFromPgEndpoint());

    try (Connection conn = connectToConnMgrAs("su_pg_role");
        Statement stmt = conn.createStatement()) {
      assertTrue(verifySessionParameterValue(stmt, "is_superuser", "off"));
      assertThrows(SQLException.class,
          () -> stmt.execute("SET SESSION AUTHORIZATION yugabyte"));
    }

    executeOnPgEndpoint("DROP ROLE su_pg_role");
  }

  // ALTER ROLE ... SET issued without going through Connection Manager bumps the version, so
  // new sessions of the role get a backend that has the new default.
  @Test
  public void testVersionBumpOnAlterRoleSetOnPgEndpoint() throws Exception {
    executeOnPgEndpoint("CREATE ROLE guc_pg_role LOGIN");

    try (Connection conn = connectToConnMgrAs("guc_pg_role");
        Statement stmt = conn.createStatement()) {
      assertTrue(verifySessionParameterValue(stmt, "statement_timeout", "0"));
    }

    long oldVersion = getLogicalClientVersionFromPgEndpoint();
    executeOnPgEndpoint("ALTER ROLE guc_pg_role SET statement_timeout = '123s'");
    assertEquals(oldVersion + 1, getLogicalClientVersionFromPgEndpoint());

    try (Connection conn = connectToConnMgrAs("guc_pg_role");
        Statement stmt = conn.createStatement()) {
      assertTrue(verifySessionParameterValue(stmt, "statement_timeout", "123s"));
    }

    executeOnPgEndpoint("DROP ROLE guc_pg_role");
  }

  // ALTER ROLE issued on a node without Connection Manager bumps the version, so pooled
  // backends started before it on another node are not reused by new sessions of the role.
  @Test
  public void testVersionBumpOnAlterRoleFromNodeWithoutConnMgr() throws Exception {
    Map<String, String> plainTserverFlags = getTServerFlags();
    plainTserverFlags.put("enable_ysql_conn_mgr", "false");
    int plainTserverIdx = miniCluster.getNumTServers();
    miniCluster.startTServer(plainTserverFlags);
    markClusterNeedsRecreation();

    try (Connection plainConn = getConnectionBuilder()
            .withTServer(plainTserverIdx)
            .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
            .connect();
        Statement plainStmt = plainConn.createStatement()) {
      plainStmt.execute("CREATE ROLE mixed_role SUPERUSER LOGIN");
      Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 2);

      try (Connection conn = connectToConnMgrAs("mixed_role");
          Statement stmt = conn.createStatement()) {
        assertTrue(verifySessionParameterValue(stmt, "is_superuser", "on"));
        assertTrue(verifySessionParameterValue(stmt, "statement_timeout", "0"));
      }

      long oldVersion = getLogicalClientVersion(plainStmt);
      plainStmt.execute("ALTER ROLE mixed_role NOSUPERUSER");
      assertEquals(oldVersion + 1, getLogicalClientVersion(plainStmt));
      Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 2);

      try (Connection conn = connectToConnMgrAs("mixed_role");
          Statement stmt = conn.createStatement()) {
        assertTrue(verifySessionParameterValue(stmt, "is_superuser", "off"));
        assertThrows(SQLException.class,
            () -> stmt.execute("SET SESSION AUTHORIZATION yugabyte"));
      }

      plainStmt.execute("ALTER ROLE mixed_role SET statement_timeout = '123s'");
      assertEquals(oldVersion + 2, getLogicalClientVersion(plainStmt));
      Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 2);

      try (Connection conn = connectToConnMgrAs("mixed_role");
          Statement stmt = conn.createStatement()) {
        assertTrue(verifySessionParameterValue(stmt, "statement_timeout", "123s"));
      }

      plainStmt.execute("DROP ROLE mixed_role");
    }
  }
}
