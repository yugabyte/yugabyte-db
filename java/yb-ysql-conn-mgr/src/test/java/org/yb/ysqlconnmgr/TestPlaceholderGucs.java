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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * Custom (placeholder) GUCs must not outlive the logical client that created them on a pooled
 * backend: the next logical client should see what a fresh backend shows.
 *
 * All tests use a single backend, so consecutive logical clients share it.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestPlaceholderGucs extends BaseYsqlConnMgr {
  private static final String FUNCTION = "placeholder_set_clause_fn";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
    builder.addCommonTServerFlag("ysql_pg_conf_csv", "myconf.from_file='file_val'");
  }

  @Before
  public void createFunction() throws Exception {
    // LANGUAGE sql, so that no test but the plpgsql one loads plpgsql on the backend.
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.POSTGRES).connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE OR REPLACE FUNCTION " + FUNCTION + "() RETURNS text LANGUAGE sql "
          + "SET app.func = 'v' AS $$ SELECT current_setting('app.func') $$");
    }
  }

  private Connection cmConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
  }

  private Connection cmConn(String options) throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withOptions(options)
        .connect();
  }

  private static int intResult(Statement stmt, String query) throws Exception {
    try (ResultSet rs = stmt.executeQuery(query)) {
      assertTrue(rs.next());
      return rs.getInt(1);
    }
  }

  private static int backendPid(Statement stmt) throws Exception {
    return intResult(stmt, "SELECT pg_backend_pid()");
  }

  private static String currentSetting(Statement stmt, String name) throws Exception {
    try (ResultSet rs = stmt.executeQuery("SELECT current_setting('" + name + "')")) {
      assertTrue(rs.next());
      return rs.getString(1);
    }
  }

  private static void assertUnrecognized(Statement stmt, String name) throws Exception {
    try {
      currentSetting(stmt, name);
      fail(name + " should be unrecognized, as on a fresh backend");
    } catch (SQLException e) {
      assertTrue(e.getMessage(),
          e.getMessage().contains("unrecognized configuration parameter \"" + name + "\""));
    }
  }

  @Test
  public void testSessionPlaceholderRemovedForNextClient() throws Exception {
    try (Connection connA = cmConn();
         Statement a = connA.createStatement();
         Connection connB = cmConn();
         Statement b = connB.createStatement()) {
      a.execute("SET app.tenant_id = 'tenant_a'");
      int pid = backendPid(a);

      assertEquals(pid, backendPid(b));
      assertUnrecognized(b, "app.tenant_id");

      // A's setting is still replayed when A gets the backend back.
      assertEquals("tenant_a", currentSetting(a, "app.tenant_id"));
      assertEquals(pid, backendPid(b));
      assertUnrecognized(b, "app.tenant_id");
    }
  }

  @Test
  public void testUnsetPlaceholdersRemovedForNextClient() throws Exception {
    // Each of these leaves a placeholder without a value in A's session.
    String[] names = {
        "app.set_local", "app.config_local", "app.reset", "app.set_reset", "app.func"};
    try (Connection connA = cmConn();
         Statement a = connA.createStatement();
         Connection connB = cmConn();
         Statement b = connB.createStatement()) {
      connA.setAutoCommit(false);
      a.execute("SET LOCAL app.set_local = 'v'");
      connA.commit();
      connA.setAutoCommit(true);
      a.execute("SELECT set_config('app.config_local', 'v', true)");
      a.execute("RESET app.reset");
      a.execute("SET app.set_reset = 'v'");
      a.execute("RESET app.set_reset");
      try (ResultSet rs = a.executeQuery("SELECT " + FUNCTION + "()")) {
        assertTrue(rs.next());
        assertEquals("v", rs.getString(1));
      }
      int pid = backendPid(a);

      // Within A's own session they behave as in PostgreSQL.
      for (String name : names) {
        assertEquals(name, "", currentSetting(a, name));
      }

      assertEquals(pid, backendPid(b));
      for (String name : names) {
        assertUnrecognized(b, name);
      }
    }
  }

  @Test
  public void testStartupOptionPlaceholderRemovedForNextClient() throws Exception {
    try (Connection connA = cmConn("-c app.startup=startup_val");
         Statement a = connA.createStatement();
         Connection connB = cmConn();
         Statement b = connB.createStatement()) {
      assertEquals("startup_val", currentSetting(a, "app.startup"));
      int pid = backendPid(a);

      assertEquals(pid, backendPid(b));
      assertUnrecognized(b, "app.startup");

      assertEquals("startup_val", currentSetting(a, "app.startup"));
    }
  }

  @Test
  public void testStartupOptionNotLeakedAfterExtensionLoad() throws Exception {
    // A sets an extension's GUC as a startup option before the extension is loaded, so it starts
    // as a placeholder and becomes a real GUC, keeping A's value, when A loads the extension.
    try (Connection connA = cmConn("-c plpgsql.check_asserts=off");
         Statement a = connA.createStatement();
         Connection connB = cmConn();
         Statement b = connB.createStatement()) {
      // pg_settings hides placeholders, so this checks that plpgsql isn't loaded yet.
      assertEquals(0, intResult(a,
          "SELECT count(*) FROM pg_settings WHERE name = 'plpgsql.check_asserts'"));
      a.execute("DO $$ BEGIN ASSERT false; END $$");
      assertEquals("off", currentSetting(a, "plpgsql.check_asserts"));
      int pid = backendPid(a);

      // B must get the built-in default, not A's startup value.
      assertEquals(pid, backendPid(b));
      assertEquals("on", currentSetting(b, "plpgsql.check_asserts"));
      try {
        b.execute("DO $$ BEGIN ASSERT false; END $$");
        fail("ASSERT should fail with plpgsql.check_asserts = on");
      } catch (SQLException e) {
        assertTrue(e.getMessage(), e.getMessage().contains("assertion failed"));
      }

      assertEquals("off", currentSetting(a, "plpgsql.check_asserts"));
    }
  }

  @Test
  public void testConfigFilePlaceholderKept() throws Exception {
    try (Connection connA = cmConn();
         Statement a = connA.createStatement();
         Connection connB = cmConn();
         Statement b = connB.createStatement()) {
      a.execute("SET myconf.from_file = 'session_val'");
      assertEquals("session_val", currentSetting(a, "myconf.from_file"));
      int pid = backendPid(a);

      // A fresh backend has this placeholder too, from postgresql.conf.
      assertEquals(pid, backendPid(b));
      assertEquals("file_val", currentSetting(b, "myconf.from_file"));
    }
  }

  @Test
  public void testResetAllKeepsPlaceholderWithinSession() throws Exception {
    try (Connection conn = cmConn();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET app.same_session = 'v'");
      stmt.execute("RESET ALL");
      assertEquals("", currentSetting(stmt, "app.same_session"));
    }
  }
}
