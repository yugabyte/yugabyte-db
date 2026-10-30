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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import java.util.stream.IntStream;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNull;
import static org.yb.AssertionWrappers.assertTrue;

/**
 * Regression test for #31741: ALTER TABLE ... DROP COLUMN used to fail with "pg_attribute catalog
 * is missing N attribute(s)" when another session bumped the catalog version while it ran.
 *
 * GRANT and REVOKE take no lock on their target, so they commit while DROP COLUMN holds
 * AccessExclusiveLock, and every commit invalidates the table's relcache entry. DROP COLUMN picks
 * those invalidations up mid-statement, which forces it to rebuild the table's relcache entry from
 * whatever it has written to pg_attribute so far.
 */
@RunWith(value = YBTestRunner.class)
public class TestPgDropColumnConcurrentCatalogInval extends BasePgSQLTest {
  private static final Logger LOG =
      LoggerFactory.getLogger(TestPgDropColumnConcurrentCatalogInval.class);

  /** Each dropped column is one chance to hit the race, so drop many of them per ALTER TABLE. */
  private static final int NUM_COLUMNS = 200;
  private static final int NUM_INVALIDATORS = 3;
  private static final int NUM_ITERATIONS = 3;
  /** Attempts whose DROP COLUMN lost a conflict verify nothing, so allow room to retry them. */
  private static final int MAX_ATTEMPTS = NUM_ITERATIONS * 4;

  private static final String MISSING_ATTRIBUTE_ERROR = "pg_attribute catalog is missing";

  @Override
  protected int getReplicationFactor() {
    return 1;
  }

  @Override
  protected int getInitialNumMasters() {
    return 1;
  }

  @Override
  protected int getInitialNumTServers() {
    return 1;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    toggleDDLMode(flags, /* useLegacy */ false);
    return flags;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flags = super.getMasterFlags();
    toggleDDLMode(flags, /* useLegacy */ false);
    return flags;
  }

  @Test
  public void testDropColumnWithConcurrentGrantRevoke() throws Exception {
    final String columns =
        IntStream.rangeClosed(1, NUM_COLUMNS).mapToObj(i -> "c" + i + " int")
            .collect(Collectors.joining(", "));
    final String dropClauses =
        IntStream.rangeClosed(1, NUM_COLUMNS).mapToObj(i -> "DROP COLUMN c" + i)
            .collect(Collectors.joining(", "));

    int verified = 0;
    for (int attempt = 0; attempt < MAX_ATTEMPTS && verified < NUM_ITERATIONS; ++attempt) {
      try (Statement stmt = connection.createStatement()) {
        stmt.execute("CREATE TABLE t(k int PRIMARY KEY, " + columns + ")");
      }
      try {
        String dropError = dropColumnsUnderGrantRevoke(dropClauses);
        if (dropError != null && !dropError.contains(MISSING_ATTRIBUTE_ERROR)) {
          // DROP COLUMN competes with GRANT/REVOKE for the same pg_class row and can lose. That
          // verifies nothing either way, so retry rather than spend an iteration on it.
          LOG.info("ALTER TABLE lost an unrelated conflict, retrying: " + dropError);
          continue;
        }
        assertNull("ALTER TABLE hit #31741: " + dropError, dropError);
        assertCatalogIsConsistent();
        ++verified;
      } finally {
        try (Statement stmt = connection.createStatement()) {
          stmt.execute("DROP TABLE t");
        } catch (SQLException e) {
          // Never let cleanup replace the assertion failure this test exists to report.
          LOG.error("DROP TABLE t failed", e);
        }
      }
    }

    // Without this the test would pass having proven nothing if every DROP COLUMN lost a conflict.
    assertEquals("only " + verified + " of " + NUM_ITERATIONS + " iterations ran DROP COLUMN to " +
                 "completion in " + MAX_ATTEMPTS + " attempts", NUM_ITERATIONS, verified);
  }

  /**
   * Drops every cN column of table t in one ALTER TABLE while {@link #NUM_INVALIDATORS} sessions
   * loop GRANT/REVOKE on it. Returns the ALTER TABLE error message, or null if it succeeded.
   */
  private String dropColumnsUnderGrantRevoke(String dropClauses) throws Exception {
    final AtomicBoolean stopped = new AtomicBoolean(false);
    final List<Thread> invalidators = new ArrayList<>();
    final List<Connection> invalidatorConnections = new ArrayList<>();
    for (int i = 0; i < NUM_INVALIDATORS; ++i) {
      final Connection conn = getConnectionBuilder().connect();
      invalidatorConnections.add(conn);
      Thread thread = new Thread(() -> {
        try (Statement stmt = conn.createStatement()) {
          while (!stopped.get()) {
            // Both statements race with each other as well as with the DROP COLUMN, so a
            // serialization failure here is expected and says nothing about the bug under test.
            try {
              stmt.execute("GRANT SELECT ON t TO PUBLIC");
              stmt.execute("REVOKE SELECT ON t FROM PUBLIC");
            } catch (SQLException e) {
              LOG.info("GRANT/REVOKE failed: " + e.getMessage());
            }
          }
        } catch (SQLException e) {
          LOG.error("Invalidator thread failed", e);
        }
      });
      thread.start();
      invalidators.add(thread);
    }

    try (Statement stmt = connection.createStatement()) {
      stmt.execute("ALTER TABLE t " + dropClauses);
      return null;
    } catch (SQLException e) {
      return e.getMessage();
    } finally {
      stopped.set(true);
      for (Thread thread : invalidators) {
        thread.join();
      }
      for (Connection conn : invalidatorConnections) {
        conn.close();
      }
    }
  }

  /** pg_class.relnatts must still match the number of pg_attribute rows for the table. */
  private void assertCatalogIsConsistent() throws Exception {
    try (Statement stmt = connection.createStatement();
         ResultSet rs = stmt.executeQuery(
             "SELECT c.relnatts, count(a.*) FROM pg_class c JOIN pg_attribute a" +
             " ON a.attrelid = c.oid AND a.attnum > 0" +
             " WHERE c.relname = 't' GROUP BY c.relnatts")) {
      assertTrue(rs.next());
      assertEquals(NUM_COLUMNS + 1, rs.getInt(1));
      assertEquals(NUM_COLUMNS + 1, rs.getInt(2));
    }
  }
}
