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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

/**
 * A role from the client's startup packet (options=-c role=...) keeps the client on the backend
 * where the role was applied, as SET ROLE does, so a later REVOKE does not close the session when
 * it would otherwise move to another backend (#37797).
 */
@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestStartupRoleStickiness extends BaseYsqlConnMgr {

  private static String currentUser(Statement stmt) throws Exception {
    try (ResultSet rs = stmt.executeQuery("SELECT current_user")) {
      assertTrue(rs.next());
      return rs.getString(1);
    }
  }

  @Test
  public void testStartupRoleMakesTheConnectionSticky() throws Exception {
    try (Connection admin = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
         Statement adminStmt = admin.createStatement()) {
      adminStmt.execute("CREATE ROLE startup_login LOGIN");
      adminStmt.execute("CREATE ROLE startup_role");
      adminStmt.execute("GRANT startup_role TO startup_login");
    }

    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withUser("startup_login")
             .withOptions("-c role=startup_role")
             .connect();
         Statement stmt = conn.createStatement()) {
      assertEquals("startup_role", currentUser(stmt));
      assertConnectionStickyState(stmt, true);

      try (Connection admin = getConnectionBuilder()
               .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
           Statement adminStmt = admin.createStatement()) {
        adminStmt.execute("REVOKE startup_role FROM startup_login");
      }

      // As on a direct connection, the session keeps the role it started with.
      for (int i = 0; i < 20; i++) {
        assertEquals("startup_role", currentUser(stmt));
      }
    }
  }

  @Test
  public void testStartupRoleNoneLeavesTheConnectionShared() throws Exception {
    try (Connection conn = getConnectionBuilder()
             .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
             .withOptions("-c role=none")
             .connect();
         Statement stmt = conn.createStatement()) {
      assertConnectionStickyState(stmt, false);
    }
  }
}
