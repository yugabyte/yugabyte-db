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
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

import com.google.gson.JsonObject;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * Loading an extension on first use takes over the values a logical client gave its variables
 * while they were placeholders. The client must then stay on that backend, and only then.
 *
 * Each test connects as its own role, so it gets a pool of fresh backends that haven't loaded
 * plpgsql yet.
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestExtensionLoadStickiness extends BaseYsqlConnMgr {
  private static final String LOAD_PLPGSQL = "DO $$ BEGIN NULL; END $$";

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_pg_conf_csv", "plpgsql.print_strict_params=on");
  }

  private void createRole(String role) throws Exception {
    try (Connection conn = connectToPostgres();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE ROLE " + role + " LOGIN");
    }
  }

  private Connection connectAs(String role, String options) throws Exception {
    ConnectionBuilder builder = getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser(role);
    if (options != null) {
      builder = builder.withOptions(options);
    }
    Connection conn = builder.connect();
    try (Statement stmt = conn.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT count(*) FROM pg_settings WHERE name LIKE 'plpgsql.%'")) {
      assertTrue(rs.next());
      assertEquals("plpgsql must not be loaded yet", 0, rs.getInt(1));
    }
    return conn;
  }

  private static String currentSetting(Statement stmt, String name) throws Exception {
    try (ResultSet rs = stmt.executeQuery("SELECT current_setting('" + name + "', true)")) {
      assertTrue(rs.next());
      return rs.getString(1);
    }
  }

  private void assertStickyConnections(String role, int expected) throws Exception {
    Thread.sleep((2 + STATS_UPDATE_INTERVAL) * 1000);
    JsonObject pool = getPool("yugabyte", role);
    assertNotNull(pool);
    assertEquals(expected, pool.get("sticky_connections").getAsInt());
  }

  @Test
  public void testSetBeforeLoad() throws Exception {
    createRole("ext_set");
    try (Connection conn = connectAs("ext_set", null);
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET plpgsql.check_asserts = off");
      stmt.execute("DO $$ BEGIN ASSERT false; END $$");
      assertEquals("off", currentSetting(stmt, "plpgsql.check_asserts"));
      assertStickyConnections("ext_set", 1);
    }
  }

  @Test
  public void testStartupOptionBeforeLoad() throws Exception {
    createRole("ext_startup");
    try (Connection conn = connectAs("ext_startup", "-c plpgsql.check_asserts=off");
         Statement stmt = conn.createStatement()) {
      stmt.execute("DO $$ BEGIN ASSERT false; END $$");
      assertEquals("off", currentSetting(stmt, "plpgsql.check_asserts"));
      assertStickyConnections("ext_startup", 1);
    }
  }

  @Test
  public void testRejectedValueBeforeLoad() throws Exception {
    createRole("ext_bad_value");
    try (Connection conn = connectAs("ext_bad_value", null);
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET plpgsql.check_asserts = 'not_a_bool'");
      // The load drops the value with a WARNING.
      stmt.execute(LOAD_PLPGSQL);
      assertEquals("on", currentSetting(stmt, "plpgsql.check_asserts"));
      assertStickyConnections("ext_bad_value", 1);
    }
  }

  @Test
  public void testMisspelledNameBeforeLoad() throws Exception {
    createRole("ext_bad_name");
    try (Connection conn = connectAs("ext_bad_name", null);
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET plpgsql.check_assert = on");
      // The load removes the placeholder with a WARNING, as plpgsql reserves its prefix.
      stmt.execute(LOAD_PLPGSQL);
      assertNull(currentSetting(stmt, "plpgsql.check_assert"));
      assertStickyConnections("ext_bad_name", 1);
    }
  }

  @Test
  public void testSetLocalBeforeLoad() throws Exception {
    createRole("ext_set_local");
    try (Connection conn = connectAs("ext_set_local", null);
         Statement stmt = conn.createStatement()) {
      stmt.execute("BEGIN");
      stmt.execute("SET LOCAL plpgsql.check_asserts = off");
      stmt.execute("DO $$ BEGIN ASSERT false; END $$");
      stmt.execute("COMMIT");
      assertStickyConnections("ext_set_local", 0);
    }
  }

  @Test
  public void testAlterRoleSetBeforeLoad() throws Exception {
    createRole("ext_alter_role");
    try (Connection conn = connectToPostgres();
         Statement stmt = conn.createStatement()) {
      stmt.execute("ALTER ROLE ext_alter_role SET plpgsql.check_asserts = off");
    }
    try (Connection conn = connectAs("ext_alter_role", null);
         Statement stmt = conn.createStatement()) {
      stmt.execute("DO $$ BEGIN ASSERT false; END $$");
      assertEquals("off", currentSetting(stmt, "plpgsql.check_asserts"));
      assertStickyConnections("ext_alter_role", 0);
    }
  }

  @Test
  public void testLoadWithoutClientValue() throws Exception {
    createRole("ext_no_value");
    try (Connection conn = connectAs("ext_no_value", null);
         Statement stmt = conn.createStatement()) {
      // A value-less placeholder, and one from postgresql.conf, carry nothing of this client.
      stmt.execute("RESET plpgsql.check_asserts");
      stmt.execute(LOAD_PLPGSQL);
      assertEquals("on", currentSetting(stmt, "plpgsql.print_strict_params"));
      assertStickyConnections("ext_no_value", 0);
    }
  }
}
