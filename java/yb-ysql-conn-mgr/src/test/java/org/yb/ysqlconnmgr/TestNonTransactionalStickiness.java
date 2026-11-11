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
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import com.google.gson.JsonObject;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.RequiresLinux;

@RequiresLinux
@RunWith(value = YBTestRunner.class)
public class TestNonTransactionalStickiness extends BaseYsqlConnMgr {
  private static final String SEQUENCE = "nontxn_seq";
  private static final String ORIGIN = "nontxn_origin";
  private static final String ROLE = "nontxn_role";
  private static final String WEAK_PASSWORD = "tooshrt";
  private static final String PROCEDURE = "nontxn_proc";
  private static final String ORIGIN_USER = "nontxn_origin_user";
  private static final String ORIGIN_USER_PASSWORD = "pwd";
  private static final String ORIGIN_FUNCTIONS =
      "pg_replication_origin_session_setup(text), "
      + "yb_replication_origin_session_setup_shared(text), "
      + "pg_replication_origin_session_is_setup()";

  private Connection routeHolder;

  private enum Source {
    PREPARE("PREPARE nontxn_stmt AS SELECT 1",
        "EXECUTE 'PREPARE nontxn_stmt AS SELECT 1'") {
      @Override
      boolean hasState(Statement stmt) throws Exception {
        return intResult(stmt,
            "SELECT count(*) FROM pg_prepared_statements WHERE name = 'nontxn_stmt'") > 0;
      }
    },
    REPLICATION_ORIGIN(
        "SELECT pg_replication_origin_session_setup('" + ORIGIN + "')",
        "PERFORM pg_replication_origin_session_setup('" + ORIGIN + "')") {
      @Override
      boolean hasState(Statement stmt) throws Exception {
        return originIsSetup(stmt);
      }
    },
    REPLICATION_ORIGIN_SHARED(
        "SELECT yb_replication_origin_session_setup_shared('" + ORIGIN + "')",
        "PERFORM yb_replication_origin_session_setup_shared('" + ORIGIN + "')") {
      @Override
      boolean hasState(Statement stmt) throws Exception {
        return originIsSetup(stmt);
      }
    },
    LOAD("LOAD 'passwordcheck'", "EXECUTE 'LOAD ''passwordcheck'''") {
      @Override
      boolean hasState(Statement stmt) throws Exception {
        try {
          stmt.execute(String.format("ALTER ROLE %s PASSWORD '%s'", ROLE, WEAK_PASSWORD));
          return false;
        } catch (SQLException e) {
          assertTrue(e.getMessage(), e.getMessage().contains("password is too short"));
          return true;
        }
      }
    },
    HLL_MAX_SPARSE("SELECT hll_set_max_sparse(128)", "PERFORM hll_set_max_sparse(128)") {
      @Override
      boolean hasState(Statement stmt) throws Exception {
        return intResult(stmt, "SELECT hll_set_max_sparse(-1)") != -1;
      }
    },
    SESSION_SEQUENCE("SELECT nextval('" + SEQUENCE + "')",
        "PERFORM nextval('" + SEQUENCE + "')") {
      @Override
      boolean hasState(Statement stmt) throws Exception {
        try {
          stmt.executeQuery("SELECT lastval()").close();
          return true;
        } catch (SQLException e) {
          assertTrue(e.getMessage(), e.getMessage().contains("not yet defined"));
          return false;
        }
      }
    };

    final String sql;
    final String plpgsql;

    Source(String sql, String plpgsql) {
      this.sql = sql;
      this.plpgsql = plpgsql;
    }

    abstract boolean hasState(Statement stmt) throws Exception;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    disableWarmupRandomMode(builder);
    builder.addCommonTServerFlags(SINGLE_BACKEND_FLAGS);
    builder.addCommonTServerFlag("ysql_conn_mgr_sequence_support_mode", "session");
  }

  @Before
  public void createObjects() throws Exception {
    try (Connection conn = pgConn();
         Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE EXTENSION IF NOT EXISTS hll");
      stmt.execute("CREATE SEQUENCE IF NOT EXISTS " + SEQUENCE);
      stmt.execute(String.format(
          "SELECT pg_replication_origin_create('%s') "
              + "WHERE pg_replication_origin_oid('%s') IS NULL",
          ORIGIN, ORIGIN));
      if (intResult(stmt,
          "SELECT count(*) FROM pg_roles WHERE rolname = '" + ROLE + "'") == 0) {
        stmt.execute("CREATE ROLE " + ROLE);
      }
      if (intResult(stmt,
          "SELECT count(*) FROM pg_roles WHERE rolname = '" + ORIGIN_USER + "'") == 0) {
        stmt.execute(String.format("CREATE ROLE %s LOGIN PASSWORD '%s'",
            ORIGIN_USER, ORIGIN_USER_PASSWORD));
      }
      stmt.execute(String.format("GRANT EXECUTE ON FUNCTION %s TO %s",
          ORIGIN_FUNCTIONS, ORIGIN_USER));
    }
    routeHolder = cmConn();
  }

  @After
  public void closeRouteHolder() throws Exception {
    if (routeHolder != null) {
      routeHolder.close();
    }
  }

  private static int intResult(Statement stmt, String query) throws Exception {
    try (ResultSet rs = stmt.executeQuery(query)) {
      assertTrue(rs.next());
      return rs.getInt(1);
    }
  }

  private static boolean originIsSetup(Statement stmt) throws Exception {
    try (ResultSet rs = stmt.executeQuery("SELECT pg_replication_origin_session_is_setup()")) {
      assertTrue(rs.next());
      return rs.getBoolean(1);
    }
  }

  private Connection cmConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .connect();
  }

  private Connection pgConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.POSTGRES)
        .connect();
  }

  private Connection originUserConn() throws Exception {
    return getConnectionBuilder()
        .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
        .withUser(ORIGIN_USER)
        .withPassword(ORIGIN_USER_PASSWORD)
        .connect();
  }

  private void waitForOriginUserStickyConnections(int expected) throws Exception {
    TestUtils.waitFor(() -> {
      JsonObject pool = getPool("yugabyte", ORIGIN_USER);
      return pool != null && pool.get("sticky_connections").getAsInt() == expected;
    }, 30000);
  }

  private static int backendPid(Statement stmt) throws Exception {
    return intResult(stmt, "SELECT pg_backend_pid()");
  }

  private void assertNextClientHasNoState(Source source, int pid) throws Exception {
    waitForStickyConnections(0);
    try (Connection conn = cmConn();
         Statement stmt = conn.createStatement()) {
      assertNotEquals(source.name(), pid, backendPid(stmt));
      assertFalse(source.name(), source.hasState(stmt));
    }
  }

  private void assertNextOriginUserClientHasNoState(Source source, int pid) throws Exception {
    waitForOriginUserStickyConnections(0);
    try (Connection conn = originUserConn();
         Statement stmt = conn.createStatement()) {
      assertNotEquals(source.name(), pid, backendPid(stmt));
      assertFalse(source.name(), source.hasState(stmt));
    }
  }

  // For each source: BEGIN, the source, ROLLBACK. The client stays sticky and keeps the state;
  // a client connecting after it disconnects does not see the state.
  @Test
  public void testRollback() throws Exception {
    for (Source source : Source.values()) {
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        stmt.execute("BEGIN");
        stmt.execute(source.sql);
        stmt.execute("ROLLBACK");
        waitForStickyConnections(1);
        assertTrue(source.name(), source.hasState(stmt));
      }
      assertNextClientHasNoState(source, pid);
    }
  }

  // For each source: BEGIN, SAVEPOINT, the source, ROLLBACK TO SAVEPOINT, COMMIT. The client
  // stays sticky; a client connecting after it disconnects does not see the state.
  @Test
  public void testRollbackToSavepoint() throws Exception {
    for (Source source : Source.values()) {
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        stmt.execute("BEGIN");
        stmt.execute("SAVEPOINT s");
        stmt.execute(source.sql);
        stmt.execute("ROLLBACK TO SAVEPOINT s");
        stmt.execute("COMMIT");
        waitForStickyConnections(1);
        assertTrue(source.name(), source.hasState(stmt));
      }
      assertNextClientHasNoState(source, pid);
    }
  }

  // For each source: BEGIN, DECLARE a WITH HOLD cursor, SAVEPOINT, the source, ROLLBACK TO
  // SAVEPOINT, COMMIT. The sticky object count is still 1 for the cursor, the client stays
  // sticky and keeps the state; a client connecting after it disconnects does not see it.
  @Test
  public void testTransactionalObjectWithRollbackToSavepoint() throws Exception {
    for (Source source : Source.values()) {
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        stmt.execute("BEGIN");
        stmt.execute("DECLARE nontxn_cursor CURSOR WITH HOLD FOR SELECT 1");
        stmt.execute("SAVEPOINT s");
        stmt.execute(source.sql);
        stmt.execute("ROLLBACK TO SAVEPOINT s");
        stmt.execute("COMMIT");
        waitForStickyConnections(1);
        assertTrue(source.name(), verifySessionParameterValue(stmt,
            "ysql_conn_mgr_sticky_object_count", "1"));
        assertTrue(source.name(), source.hasState(stmt));
        assertEquals(source.name(), 1, intResult(stmt,
            "SELECT count(*) FROM pg_cursors WHERE name = 'nontxn_cursor'"));
      }
      assertNextClientHasNoState(source, pid);
    }
  }

  // For each source: a DO block runs the source and then raises an error. The client stays
  // sticky; a client connecting after it disconnects does not see the state.
  @Test
  public void testErrorLaterInStatement() throws Exception {
    for (Source source : Source.values()) {
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        try {
          stmt.execute(String.format(
              "DO $$ BEGIN %s; RAISE EXCEPTION 'nontxn error'; END $$", source.plpgsql));
          fail("DO block should fail");
        } catch (SQLException e) {
          assertTrue(e.getMessage(), e.getMessage().contains("nontxn error"));
        }
        waitForStickyConnections(1);
        assertTrue(source.name(), source.hasState(stmt));
      }
      assertNextClientHasNoState(source, pid);
    }
  }

  // For each source: BEGIN, the source, then disconnect with the transaction open. The next
  // client gets a different backend and does not see the state.
  @Test
  public void testDisconnectInTransaction() throws Exception {
    for (Source source : Source.values()) {
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        stmt.execute("BEGIN");
        stmt.execute(source.sql);
      }
      assertNextClientHasNoState(source, pid);
    }
  }

  // For each source: CALL a procedure that runs the source, commits and runs another statement.
  // The client stays sticky; a client connecting after it disconnects does not see the state.
  @Test
  public void testProcedureCommit() throws Exception {
    for (Source source : Source.values()) {
      try (Connection conn = pgConn();
           Statement stmt = conn.createStatement()) {
        stmt.execute(String.format(
            "CREATE OR REPLACE PROCEDURE %s() LANGUAGE plpgsql AS "
                + "$$ BEGIN %s; COMMIT; PERFORM 1; END $$",
            PROCEDURE, source.plpgsql));
      }
      int pid;
      try (Connection conn = cmConn();
           Statement stmt = conn.createStatement()) {
        pid = backendPid(stmt);
        stmt.execute("CALL " + PROCEDURE + "()");
        waitForStickyConnections(1);
        assertTrue(source.name(), source.hasState(stmt));
      }
      assertNextClientHasNoState(source, pid);
    }
  }

  // As a non-superuser granted the origin functions, for both origin setups: BEGIN, setup,
  // ROLLBACK, then BEGIN, setup and disconnect. The client is sticky after the ROLLBACK, and
  // each time the next client gets a different backend and does not see the origin.
  @Test
  public void testNonSuperuserReplicationOrigin() throws Exception {
    try (Connection holder = originUserConn()) {
      for (Source source :
          new Source[] {Source.REPLICATION_ORIGIN, Source.REPLICATION_ORIGIN_SHARED}) {
        int pid;
        try (Connection conn = originUserConn();
             Statement stmt = conn.createStatement()) {
          pid = backendPid(stmt);
          stmt.execute("BEGIN");
          stmt.execute(source.sql);
          stmt.execute("ROLLBACK");
          waitForOriginUserStickyConnections(1);
          assertTrue(source.name(), source.hasState(stmt));
        }
        assertNextOriginUserClientHasNoState(source, pid);

        try (Connection conn = originUserConn();
             Statement stmt = conn.createStatement()) {
          pid = backendPid(stmt);
          stmt.execute("BEGIN");
          stmt.execute(source.sql);
        }
        assertNextOriginUserClientHasNoState(source, pid);
      }
    }
  }
}
