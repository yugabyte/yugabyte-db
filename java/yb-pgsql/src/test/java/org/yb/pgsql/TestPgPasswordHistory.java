// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package org.yb.pgsql;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.client.TestUtils;

import com.yugabyte.util.PSQLException;

/**
 * Concurrent ALTER ROLE against password history. Transactional DDL is required so the first
 * ALTER stays uncommitted until commit(). Debug builds otherwise leave DDL outside the
 * enclosing transaction.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgPasswordHistory extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgPasswordHistory.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "yb_allow_prehashed_passwords=false");
    appendToYsqlPgConf(flagMap, "yb_password_reuse_history=3");
    toggleDDLMode(flagMap, /* useLegacy */ false);
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    toggleDDLMode(flagMap, /* useLegacy */ false);
    return flagMap;
  }

  /**
   * Two ALTER ROLEs on one role, issued by the role itself, so each one checks reuse and
   * records. An admin reset records without that check. The pg_authid row conflict aborts the
   * loser, so only the winner is recorded and that password still cannot be reused.
   */
  @Test
  public void testConcurrentAlterRolePassword() throws Exception {
    final String role = "phconc";
    final String loserSql = "ALTER USER " + role + " PASSWORD 'pw_loser'";

    try (Connection setup = getConnectionBuilder().connect();
         Statement s = setup.createStatement()) {
      s.execute("CREATE USER " + role + " PASSWORD 'pw_one'");
      assertEquals(1, historyLength(s, role));
    }

    AtomicReference<SQLException> loserError = new AtomicReference<>();
    try (Connection winner = getConnectionBuilder().withUser(role).connect();
         Connection loser = getConnectionBuilder().withUser(role).connect();
         Connection watch = getConnectionBuilder().connect();
         Statement winnerStmt = winner.createStatement()) {
      // DDL is not retried, so the overlapping ALTER aborts instead of running again.
      try (Statement loserSetup = loser.createStatement()) {
        loserSetup.execute("SET statement_timeout = '120s'");
      }
      loser.setAutoCommit(false);
      winner.setAutoCommit(false);
      winnerStmt.execute("ALTER USER " + role + " PASSWORD 'pw_winner'");

      CountDownLatch alterStarted = new CountDownLatch(1);
      Thread loserThread = new Thread(() -> {
        try (Statement loserStmt = loser.createStatement()) {
          // Open the transaction before the ALTER so the two overlap.
          loserStmt.execute("SELECT 1");
          alterStarted.countDown();
          loserStmt.execute(loserSql);
        } catch (SQLException e) {
          loserError.set(e);
        }
      });
      loserThread.start();
      assertTrue(alterStarted.await(60, TimeUnit.SECONDS));
      try {
        TestUtils.waitFor(() ->
            loserError.get() != null || alterIsRunning(watch, loserSql), 60000);
      } finally {
        winner.commit();
      }
      loserThread.join(120000);
      assertFalse(loserThread.isAlive());
    }

    SQLException error = loserError.get();
    assertNotNull(error);
    LOG.info("Concurrent ALTER failed with: {}", error.getMessage());
    assertTrue(error.getMessage().contains(
        "could not serialize access due to concurrent update"));

    try (Connection check = getConnectionBuilder().connect();
         Connection asRole = getConnectionBuilder().withUser(role).connect();
         Statement s = check.createStatement();
         Statement roleStmt = asRole.createStatement()) {
      // CREATE recorded pw_one; only the committed ALTER adds a row.
      // The count stays on the superuser connection: the role cannot read the catalog.
      assertEquals(2, historyLength(s, role));
      try {
        roleStmt.execute("ALTER USER " + role + " PASSWORD 'pw_winner'");
        fail("winner password was not recorded");
      } catch (PSQLException e) {
        assertTrue(e.getMessage().contains(
            "password matches a recently used password"));
      }
      roleStmt.execute(loserSql);
      assertEquals(3, historyLength(s, role));
    }
  }

  private static int historyLength(Statement s, String role) throws SQLException {
    ResultSet rs = s.executeQuery(
        "SELECT count(*) FROM pg_catalog.pg_yb_password_history h " +
        "JOIN pg_authid a ON a.oid = h.pwdhstrole WHERE a.rolname = '" + role + "'");
    assertTrue(rs.next());
    return rs.getInt(1);
  }

  private static boolean alterIsRunning(Connection watch, String sql) throws SQLException {
    try (Statement s = watch.createStatement();
         ResultSet rs = s.executeQuery(
             "SELECT 1 FROM pg_stat_activity " +
             "WHERE pid <> pg_backend_pid() AND query = '" + sql.replace("'", "''") + "'")) {
      return rs.next();
    }
  }
}
