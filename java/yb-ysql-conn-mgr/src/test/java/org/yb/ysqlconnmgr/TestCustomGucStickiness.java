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
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;
import com.google.gson.JsonObject;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestCustomGucStickiness extends BaseYsqlConnMgr {

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("ysql_pg_conf_csv", "pgaudit.log='ddl'");
  }

  private void assertPgauditLog(Statement stmt, String expected) throws Exception {
    try (ResultSet rs = stmt.executeQuery("SHOW pgaudit.log")) {
      assertTrue(rs.next());
      assertEquals(expected, rs.getString(1));
    }
  }

  private void assertStickyConnections(String user, int expected) throws Exception {
    Thread.sleep((2 + STATS_UPDATE_INTERVAL) * 1000);
    JsonObject pool = getPool("yugabyte", user);
    assertNotNull(pool);
    assertEquals(expected, pool.get("sticky_connections").getAsInt());
  }

  @Test
  public void testConfigReload() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("SELECT pg_reload_conf()");
      Thread.sleep(2000);
      assertPgauditLog(stmt, "ddl");
      assertStickyConnections("yugabyte", 0);
    }
  }

  @Test
  public void testAlterRoleSet() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE ROLE audit_role LOGIN");
      stmt.execute("ALTER ROLE audit_role SET pgaudit.log = 'write'");
    }

    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser("audit_role").connect();
         Statement stmt = conn.createStatement()) {
      assertPgauditLog(stmt, "write");
      assertStickyConnections("audit_role", 0);
    }
  }

  @Test
  public void testClientSet() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
         Statement stmt = conn.createStatement()) {
      // pgaudit.log is PGC_SUSET; we can SET it since we are logged in as a superuser.
      // Superuser connections are sticky in prod but not in tests.
      stmt.execute("SET pgaudit.log = 'read'");
      assertPgauditLog(stmt, "read");
      assertStickyConnections("yugabyte", 1);
    }
  }

  @Test
  public void testStartupParameter() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withOptions("-c pgaudit.log=read").connect();
         Statement stmt = conn.createStatement()) {
      assertPgauditLog(stmt, "read");
      assertStickyConnections("yugabyte", 1);
    }
  }
}
