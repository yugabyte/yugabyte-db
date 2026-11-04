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

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestSusetGucStickiness extends BaseYsqlConnMgr {

  private Connection connectAsSuperuser() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
        .connect();
  }

  private Connection connectViaConnMgr(String user) throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser(user)
        .connect();
  }

  private void assertValueOnEveryBackend(Statement stmt, String guc, String expected,
                                         boolean expectedSticky) throws Exception {
    Set<Integer> pids = new HashSet<>();
    for (int i = 0; i < 10 || (!expectedSticky && pids.size() < 2 && i < 200); i++) {
      ResultSet rs = stmt.executeQuery(
          "SELECT pg_backend_pid(), current_setting('" + guc + "')");
      assertTrue(rs.next());
      pids.add(rs.getInt(1));
      assertEquals(expected, rs.getString(2));
    }
    if (expectedSticky)
      assertEquals(1, pids.size());
    else
      assertTrue(pids.size() > 1);
  }

  @Test
  public void testSecurityDefinerSetIsSticky() throws Exception {
    try (Connection suConn = connectAsSuperuser();
         Statement suStmt = suConn.createStatement()) {
      suStmt.execute("CREATE ROLE secdef_user LOGIN");
      suStmt.execute(
          "CREATE FUNCTION set_log_min_duration() RETURNS void LANGUAGE plpgsql " +
          "SECURITY DEFINER AS $$ BEGIN SET log_min_duration_statement = 1000; END $$");
    }

    try (Connection conn = connectViaConnMgr("secdef_user");
         Statement stmt = conn.createStatement()) {
      assertConnectionStickyState(stmt, false);
      stmt.execute("SELECT set_log_min_duration()");
      assertValueOnEveryBackend(stmt, "log_min_duration_statement", "1s", true);
    }
  }

  @Test
  public void testGrantedSetIsNotSticky() throws Exception {
    try (Connection suConn = connectAsSuperuser();
         Statement suStmt = suConn.createStatement()) {
      suStmt.execute("CREATE ROLE granted_user LOGIN");
      suStmt.execute("GRANT SET ON PARAMETER log_min_duration_statement TO granted_user");
    }

    try (Connection conn = connectViaConnMgr("granted_user");
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET log_min_duration_statement = 1000");
      assertValueOnEveryBackend(stmt, "log_min_duration_statement", "1s", false);
    }
  }

  @Test
  public void testYbDbAdminSetIsNotSticky() throws Exception {
    try (Connection suConn = connectAsSuperuser();
         Statement suStmt = suConn.createStatement()) {
      suStmt.execute("CREATE ROLE db_admin_user LOGIN");
      suStmt.execute("GRANT yb_db_admin TO db_admin_user");
    }

    try (Connection conn = connectViaConnMgr("db_admin_user");
         Statement stmt = conn.createStatement()) {
      stmt.execute("SET session_replication_role = replica");
      assertValueOnEveryBackend(stmt, "session_replication_role", "replica", false);
    }
  }
}
