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
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestTempNamespaceStickiness extends BaseYsqlConnMgr {
  private static final String TEMP_OBJECT_COUNT =
      "SELECT count(*) FROM pg_depend WHERE refclassid = 'pg_namespace'::regclass "
          + "AND refobjid = pg_my_temp_schema()";

  private Connection routeHolder;

  private enum TempObject {
    TABLE("CREATE TEMP TABLE tmp_t(a int)"),
    FUNCTION("CREATE FUNCTION pg_temp.tmp_f() RETURNS int LANGUAGE sql AS 'SELECT 1'"),
    FUNCTION_VIA_SEARCH_PATH("SET search_path = pg_temp, public",
        "CREATE FUNCTION tmp_f() RETURNS int LANGUAGE sql AS 'SELECT 1'"),
    ENUM_TYPE("CREATE TYPE pg_temp.tmp_e AS ENUM ('a')"),
    DOMAIN("CREATE DOMAIN pg_temp.tmp_d AS int"),
    OPERATOR("CREATE OPERATOR pg_temp.=== (LEFTARG = int, RIGHTARG = int, FUNCTION = int4eq)");

    final String[] sql;

    TempObject(String... sql) {
      this.sql = sql;
    }

    void create(Statement stmt) throws Exception {
      for (String s : sql) {
        stmt.execute(s);
      }
    }
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
  }

  @Before
  public void openRouteHolder() throws Exception {
    routeHolder = cmConn();
  }

  @After
  public void closeRouteHolder() throws Exception {
    if (routeHolder != null) {
      routeHolder.close();
    }
  }

  private Connection cmConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
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

  // For each kind of object: create it in pg_temp. The client is sticky; the next client gets a
  // different backend and sees no temp objects.
  @Test
  public void testCreate() throws Exception {
    for (TempObject object : TempObject.values()) {
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        object.create(stmt);
        waitForStickyConnections(1);
        assertEquals(object.name(), 1, intResult(stmt, TEMP_OBJECT_COUNT));
      }
      waitForStickyConnections(0);
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        assertNotEquals(object.name(), pid, backendPid(stmt));
        assertEquals(object.name(), 0, intResult(stmt, TEMP_OBJECT_COUNT));
      }
    }
  }

  // For each kind of object: BEGIN, create it in pg_temp, ROLLBACK. The client is not sticky, so
  // the next client gets the same backend.
  @Test
  public void testRollback() throws Exception {
    for (TempObject object : TempObject.values()) {
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        stmt.execute("BEGIN");
        object.create(stmt);
        stmt.execute("ROLLBACK");
        assertEquals(object.name(), 0, intResult(stmt, TEMP_OBJECT_COUNT));
      }
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        assertEquals(object.name(), pid, backendPid(stmt));
      }
    }
  }
}
