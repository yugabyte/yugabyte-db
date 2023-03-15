// Copyright (c) YugaByte, Inc.
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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.OptionalInt;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgSavepoints extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSavepoints.class);
  private static final int LARGE_BATCH_ROW_THRESHOLD = 100;
  private static final List<IsolationLevel> isoLevels = Arrays.asList(
    IsolationLevel.REPEATABLE_READ,
    IsolationLevel.SERIALIZABLE,
    IsolationLevel.READ_COMMITTED);

  private void createTable() throws SQLException {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE t (k INT, v INT)");
    }
  }

  private OptionalInt getSingleValue(Connection c, int k) throws SQLException {
    String query = String.format("SELECT * FROM t WHERE k = %d", k);
    try (ResultSet rs = c.createStatement().executeQuery(query)) {
      if (!rs.next()) {
        return OptionalInt.empty();
      }
      Row row = Row.fromResultSet(rs);
      assertFalse("Found more than one result", rs.next());
      return OptionalInt.of(row.getInt(1));
    }
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("txn_max_apply_batch_records", String.format("%d", LARGE_BATCH_ROW_THRESHOLD));
    return flags;
  }

  @Test
  public void testSavepointCreationInternal() throws Exception {
    createTable();

    for (IsolationLevel isoLevel: isoLevels) {
      try (Connection conn = getConnectionBuilder()
                                  .withIsolationLevel(isoLevel)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect()) {
        Statement statement = conn.createStatement();
        statement.execute("INSERT INTO t VALUES (1, 2)");
        statement.execute("SAVEPOINT a");
        statement.execute("INSERT INTO t VALUES (3, 4)");
        statement.execute("SAVEPOINT b");

        assertEquals(getSingleValue(conn, 1), OptionalInt.of(2));
        assertEquals(getSingleValue(conn, 3), OptionalInt.of(4));
        conn.rollback();
      }
    };
  }

  @Test
  public void testSavepointRollback() throws Exception {
    createTable();

    for (IsolationLevel isoLevel: isoLevels) {
      try (Connection conn = getConnectionBuilder()
                                  .withIsolationLevel(isoLevel)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect()) {
        Statement statement = conn.createStatement();
        statement.execute("INSERT INTO t VALUES (1, 2)");
        statement.execute("SAVEPOINT a");
        statement.execute("INSERT INTO t VALUES (3, 4)");
        statement.execute("ROLLBACK TO a");

        assertEquals(getSingleValue(conn, 1), OptionalInt.of(2));
        assertEquals(getSingleValue(conn, 3), OptionalInt.empty());
        conn.rollback();
      }
    }
  }

  @Test
  public void testSavepointUpdateAbortedRow() throws Exception {
    createTable();

    for (IsolationLevel isoLevel: isoLevels) {
      try (Connection conn = getConnectionBuilder()
                                  .withIsolationLevel(isoLevel)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect()) {
        Statement statement = conn.createStatement();
        statement.execute("INSERT INTO t VALUES (1, 2)");
        statement.execute("SAVEPOINT a");
        statement.execute("INSERT INTO t VALUES (3, 4)");
        statement.execute("ROLLBACK TO a");
        statement.execute("UPDATE t SET v = 5 WHERE k = 3");
        assertEquals(statement.getUpdateCount(), 0);

        assertEquals(getSingleValue(conn, 1), OptionalInt.of(2));
        assertEquals(getSingleValue(conn, 3), OptionalInt.empty());
        conn.rollback();
      }
    }
  }

  @Test
  public void testIgnoresIntentOfRolledBackSavepointSameTxn() throws Exception {
    createTable();

    for (IsolationLevel isoLevel: isoLevels) {
      try (Connection conn = getConnectionBuilder()
                                  .withIsolationLevel(isoLevel)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect()) {
        Statement statement = conn.createStatement();
        statement.execute("SAVEPOINT a");
        statement.execute("SAVEPOINT b");
        statement.execute("INSERT INTO t VALUES (3, 4)");
        statement.execute("RELEASE SAVEPOINT b");
        statement.execute("ROLLBACK TO a");

        assertEquals(getSingleValue(conn, 3), OptionalInt.empty());
        conn.rollback();
      }
    }
  }

  @Test
  public void testIgnoresLockOnlyConflictOfCommittedTxn() throws Exception {
    createTable();

    getConnectionBuilder().connect().createStatement()
        .execute("INSERT INTO t SELECT generate_series(1, 10), 0");

    Connection conn1 = getConnectionBuilder()
                                  .withIsolationLevel(IsolationLevel.REPEATABLE_READ)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect();
    Connection conn2 = getConnectionBuilder()
                                  .withIsolationLevel(IsolationLevel.REPEATABLE_READ)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect();

    Statement s1 = conn1.createStatement();
    Statement s2 = conn2.createStatement();

    s1.execute("SELECT * FROM t");
    s2.execute("SELECT * FROM t");

    s1.execute("SAVEPOINT a");
    s1.execute("UPDATE t SET v=1 WHERE k=1");
    s1.execute("ROLLBACK TO a");
    s1.execute("SELECT * FROM t WHERE k=1 FOR UPDATE");
    s1.execute("UPDATE t SET v=1 WHERE k=3");
    conn1.commit();

    s2.execute("UPDATE t SET v=2 WHERE k=1");
    conn2.commit();

    assertEquals(OptionalInt.of(2), getSingleValue(conn1, 1));
  }

  @Test
  public void testStackedSavepoints() throws Exception {
    createTable();

    for (IsolationLevel isoLevel: isoLevels) {
      try (Connection conn = getConnectionBuilder()
                                  .withIsolationLevel(isoLevel)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect()) {
        Statement statement = conn.createStatement();
        statement.execute("INSERT INTO t VALUES (1, 2)");
        statement.execute("SAVEPOINT a");
        statement.execute("INSERT INTO t VALUES (3, 4)");
        statement.execute("SAVEPOINT a");
        statement.execute("INSERT INTO t VALUES (5, 6)");

        // At this point we should have all three values
        assertEquals(OptionalInt.of(2), getSingleValue(conn, 1));
        assertEquals(OptionalInt.of(4), getSingleValue(conn, 3));
        assertEquals(OptionalInt.of(6), getSingleValue(conn, 5));

        statement.execute("ROLLBACK TO a");
        // At this point we rolled back to the second savepoint and should have only two of the
        // values
        assertEquals(OptionalInt.of(2), getSingleValue(conn, 1));
        assertEquals(OptionalInt.of(4), getSingleValue(conn, 3));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 5));


        statement.execute("INSERT INTO t VALUES (5, 6)");
        statement.execute("RELEASE a");
        // After releasing a, future references to "a" should now refer to the first savepoint. So
        // rolling back to a at this point should leave us with only the first row still in the db.
        statement.execute("ROLLBACK TO a");
        assertEquals(OptionalInt.of(2), getSingleValue(conn, 1));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 3));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 5));

        // At this point, the first savepoint named "a" should still be referenceable, and we ought
        // to still roll back to it. So inserting a new row, and then rolling back to a, should
        // again leave us with only the first row visible
        statement.execute("INSERT INTO t VALUES (5, 6)");
        statement.execute("INSERT INTO t VALUES (7, 8)");
        statement.execute("ROLLBACK TO a");
        assertEquals(OptionalInt.of(2), getSingleValue(conn, 1));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 3));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 5));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 7));

        conn.commit();
        // We should have only committed the first inserted value.
        assertEquals(OptionalInt.of(2), getSingleValue(conn, 1));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 3));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 5));
        assertEquals(OptionalInt.empty(), getSingleValue(conn, 7));
        statement.execute("truncate t");
        conn.commit();
      }
    }
  }

  @Test
  public void testSavepointCommitWithAbort() throws Exception {
    createTable();

    for (IsolationLevel isoLevel: isoLevels) {
      try (Connection conn = getConnectionBuilder()
                                  .withIsolationLevel(isoLevel)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect()) {
        Statement statement = conn.createStatement();
        statement.execute("INSERT INTO t VALUES (1, 2)");
        statement.execute("SAVEPOINT a");
        statement.execute("INSERT INTO t VALUES (3, 4)");
        statement.execute("ROLLBACK TO a");
        statement.execute("INSERT INTO t VALUES (5, 6)");
        conn.commit();

        assertEquals(getSingleValue(conn, 1), OptionalInt.of(2));
        assertEquals(getSingleValue(conn, 3), OptionalInt.empty());
        assertEquals(getSingleValue(conn, 5), OptionalInt.of(6));
        statement.execute("truncate t");
        conn.commit();
      }
    }
  }

  @Test
  public void testSavepointLargeApplyWithAborts() throws Exception {
    final int NUM_BATCHES_TO_WRITE = 5;
    final int ROW_MOD_TO_ABORT = 7;
    createTable();

    for (IsolationLevel isoLevel: isoLevels) {
      try (Connection conn = getConnectionBuilder()
                                  .withIsolationLevel(isoLevel)
                                  .withAutoCommit(AutoCommit.DISABLED)
                                  .connect()) {
        Statement statement = conn.createStatement();
        for (int i = 0; i < NUM_BATCHES_TO_WRITE; ++i) {
          for (int j = 0; j < LARGE_BATCH_ROW_THRESHOLD; ++j) {
            int val = i * LARGE_BATCH_ROW_THRESHOLD + j;
            if (val % ROW_MOD_TO_ABORT == 0) {
              statement.execute("SAVEPOINT a");
            }
            statement.execute(String.format("INSERT INTO t VALUES (%d, %d)", val, val));
            if (val % ROW_MOD_TO_ABORT == 0) {
              statement.execute("ROLLBACK TO a");
            }
          }
        }
        conn.commit();

        for (int i = 0; i < NUM_BATCHES_TO_WRITE; ++i) {
          for (int j = 0; j < LARGE_BATCH_ROW_THRESHOLD; ++j) {
            int val = i * NUM_BATCHES_TO_WRITE + j;
            if (val % ROW_MOD_TO_ABORT == 0) {
              assertEquals(getSingleValue(conn, val), OptionalInt.empty());
            } else {
              assertEquals(getSingleValue(conn, val), OptionalInt.of(val));
            }
          }
        }
        statement.execute("truncate t");
        conn.commit();
      }
    }
  }

  @Test
  public void testSavepointLargeApplyWithAbortsAndNodeKills() throws Exception {
    // TODO(savepoints): Add test which forces node restarts to ensure transaction loader is
    // properly loading savepoint metadata.
  }

  @Test
  public void testSavepointCanBeDisabled() throws Exception {
    Map<String, String> extra_tserver_flags = new HashMap<String, String>();
    extra_tserver_flags.put("enable_pg_savepoints", "false");
    restartClusterWithFlags(Collections.emptyMap(), extra_tserver_flags);
    createTable();
    try (Statement statement = connection.createStatement()) {
        statement.execute("INSERT INTO t VALUES (1, 2)");
        runInvalidQuery(statement,  "SAVEPOINT a", /*matchAll*/ true,
                        "SAVEPOINT <transaction> not supported due to setting of flag " +
                        "--enable_pg_savepoints",
                        "Hint: The flag may have been set to false because savepoints do not " +
                        "currently work with xCluster replication");
    }
    // Revert back to old set of flags for other test methods
    restartClusterWithFlags(Collections.emptyMap(), Collections.emptyMap());
  }
}
