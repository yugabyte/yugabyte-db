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

import static org.yb.AssertionWrappers.*;

import org.junit.*;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.util.Map;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;

@RunWith(value=YBTestRunner.class)
public class TestPgForeignKey extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgForeignKey.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    setFailOnConflictFlags(flagMap);
    return flagMap;
  }

  @After
  public void cleanUpAfter() throws Exception {
    // For this test tear down entire cluster between tests.
    tearDownAfter();
    pgInitialized = false;
  }

  private void checkRows(Statement statement,
                         String table_name,
                         Set<Row> expectedRows) throws Exception {
    try (ResultSet rs = statement.executeQuery("SELECT * FROM " + table_name)) {
      assertEquals(expectedRows, getRowSet(rs));
    }
  }

  @Test
  public void testForeignKeyConflictsWithSerializableIsolation() throws Exception {
    testForeignKeyConflicts(Connection.TRANSACTION_SERIALIZABLE);
  }

  @Test
  public void testForeignKeyConflictsWithSnapshotIsolation() throws Exception {
    testForeignKeyConflicts(Connection.TRANSACTION_REPEATABLE_READ);
  }

  private void testForeignKeyConflicts(int pgIsolationLevel) throws Exception {

    Set<Row> expectedPkRows = new HashSet<>();
    Set<Row> expectedFkRows = new HashSet<>();

    // Set up the tables.
    try (Statement statement = connection.createStatement()) {
      connection.setTransactionIsolation(pgIsolationLevel);

      // Setup pk table.
      statement.execute("create table pk(id int primary key)");
      for (int id = 0; id < 40; id++) {
        statement.execute(String.format("insert into pk(id) values (%d)", id));
        expectedPkRows.add(new Row(id));
      }
      checkRows(statement, "pk", expectedPkRows);

      // Setup fk table.
      statement.execute("create table fk(id int primary key, pkid int references pk(id))");
      for (int id = 1; id < 20; id += 2) {
        statement.execute(String.format("insert into fk(id, pkid) values (%d, %d)", id, id));
        expectedFkRows.add(new Row(id, id));
      }
      checkRows(statement, "fk", expectedFkRows);

      IsolationLevel isolationLevel = IsolationLevel.REPEATABLE_READ;
      if (pgIsolationLevel == Connection.TRANSACTION_SERIALIZABLE) {
        isolationLevel = IsolationLevel.SERIALIZABLE;
      }

      ConnectionBuilder connBldr = getConnectionBuilder()
          .withIsolationLevel(isolationLevel)
          .withAutoCommit(AutoCommit.DISABLED);
      try (Connection connection1 = connBldr.connect();
           Connection connection2 = connBldr.connect()) {

        // Test update/delete conflicts.
        for (int id = 1; id < 20; id += 2) {
          PgTxnState txn1State = new PgTxnState(connection1, "update txn");
          PgTxnState txn2State = new PgTxnState(connection2, "delete txn");

          executeWithTxnState(txn1State,
                              String.format("UPDATE fk SET pkid = %d where id = %d", id - 1, id));
          executeWithTxnState(txn2State,
                              String.format("DELETE FROM pk WHERE id = %d", id - 1));

          commitWithTxnState(txn1State);
          commitWithTxnState(txn2State);
          LOG.info(txn1State.toString());
          LOG.info(txn2State.toString());

          if (txn1State.isSuccess()) {
            assertTrue(txn2State.isFailure());
            expectedFkRows.remove(new Row(id, id));
            expectedFkRows.add(new Row(id, id - 1));
          } else {
            assertTrue(txn1State.isFailure());
            assertTrue(txn2State.isSuccess());
            expectedPkRows.remove(new Row(id - 1));
          }
          checkRows(statement, "pk", expectedPkRows);
          checkRows(statement, "fk", expectedFkRows);
        }

        // Test insert/delete conflicts.
        for (int id = 20; id < 40; id++) {
          PgTxnState txn1State = new PgTxnState(connection1, "insert txn");
          PgTxnState txn2State = new PgTxnState(connection2, "delete txn");

          executeWithTxnState(txn1State,
                              String.format("insert into fk(id, pkid) values (%d, %d)", id, id));
          executeWithTxnState(txn2State,
                              String.format("delete from pk where id = %d", id));

          commitWithTxnState(txn1State);
          commitWithTxnState(txn2State);
          LOG.info(txn1State.toString());
          LOG.info(txn2State.toString());

          if (txn1State.isSuccess()) {
            assertTrue(txn2State.isFailure());
            expectedFkRows.add(new Row(id, id));
          } else {
            assertTrue(txn1State.isFailure());
            assertTrue(txn2State.isSuccess());
            expectedPkRows.remove(new Row(id));
          }
          checkRows(statement, "pk", expectedPkRows);
          checkRows(statement, "fk", expectedFkRows);
        }

      }
    }
  }

  // Ensure that foreign key caching maintains data correctness and referential integrity.
  @Test
  public void testForeignKeyCaching() throws Exception {
    Set<Row> expectedPkRows = new HashSet<>();
    Set<Row> expectedFkPrimaryRows = new HashSet<>();
    Set<Row> expectedFkUniqueRows = new HashSet<>();

    // Set up the tables.
    try (Statement statement = connection.createStatement()) {
      connection.setTransactionIsolation(Connection.TRANSACTION_REPEATABLE_READ);

      // Setup pk table.
      statement.execute(
          "create table pk(b int, a int, y int, x int, primary key(a, b))");
      statement.execute("create unique index on pk(x, y)");

      for (int id = 1; id <= 5; id++) {
        statement.execute(
            String.format("insert into pk(b, a, y, x) values (%d, %d, %d, %d)",
                id * 10, id, id * 1000, id * 100));
        expectedPkRows.add(new Row(id * 10, id, id * 1000, id * 100));
      }
      checkRows(statement, "pk", expectedPkRows);

      // Setup fk table on primary key.
      statement.execute("create table fk_primary(id int primary key, b int, a int, " +
          "foreign key(a, b) references pk(a, b))");

      // Setup fk table on unique index.
      statement.execute("create table fk_unique(id int primary key, y int, x int, " +
          "foreign key(x, y) references pk(x, y))");

      statement.execute("BEGIN");
      // Insert multiple rows using the same referenced row to cause FK caching.
      for (int id = 1; id < 10; id++) {
        statement.execute(String.format("insert into fk_primary(id, b, a) values (%d, 10, 1)", id));
        expectedFkPrimaryRows.add(new Row(id, 10, 1));

        statement.execute(
            String.format("insert into fk_unique(id, y, x) values (%d, 1000, 100)", id));
        expectedFkUniqueRows.add(new Row(id, 1000, 100));
      }
      statement.execute("COMMIT");

      checkRows(statement, "fk_primary", expectedFkPrimaryRows);
      checkRows(statement, "fk_unique", expectedFkUniqueRows);

      // Check that deleting referenced row fails the transaction.
      statement.execute("BEGIN");
      statement.execute("INSERT INTO fk_primary(id, b, a) VALUES(20, 20, 2)");
      runInvalidQuery(statement, "DELETE FROM pk WHERE a=2 AND b=20",
          "Key (a, b)=(2, 20) is still referenced from table");
      statement.execute("ROLLBACK");

      // Check that updating referenced row fails the transaction, take 1.
      statement.execute("BEGIN");
      statement.execute("INSERT INTO fk_primary(id, b, a) VALUES(20, 20, 2)");
      runInvalidQuery(statement, "UPDATE pk SET a=201, b=2001 WHERE a=2 AND b=20",
          "Key (a, b)=(2, 20) is still referenced from table");
      statement.execute("ROLLBACK");

      // Check that updating referenced row fails the transaction, take 2.
      statement.execute("BEGIN");
      statement.execute("INSERT INTO fk_unique(id, y, x) VALUES(2000, 2000, 200)");
      runInvalidQuery(statement, "UPDATE pk SET x=201, y=2001 WHERE x=200 AND y=2000",
          "Key (x, y)=(200, 2000) is still referenced from table");
      statement.execute("ROLLBACK");

      // Check that deleting unrelated rows in pk table remains unaffected by caching.
      statement.execute("insert into pk(b, a, y, x) values (55, 55, 66, 66)");
      statement.execute("insert into pk(b, a, y, x) values (66, 66, 55, 55)");

      statement.execute("BEGIN");
      statement.execute("insert into fk_unique(id, y, x) values (55, 55, 55)");
      // Deleting row with a=55 and b=55 should work.
      statement.execute("delete from pk where a=55 and b=55");
      statement.execute("COMMIT");
    }
  }

  private void testRowLock(int pgIsolationLevel) throws Exception {
    try (Connection extraConnection = getConnectionBuilder().connect();
         Statement stmt = connection.createStatement();
         Statement extraStmt = extraConnection.createStatement()) {
      connection.setTransactionIsolation(pgIsolationLevel);
      extraConnection.setTransactionIsolation(pgIsolationLevel);
      stmt.execute("SET yb_transaction_priority_upper_bound = 0.4");
      extraStmt.execute("SET yb_transaction_priority_lower_bound = 0.5");
      stmt.execute("CREATE TABLE parent(k INT PRIMARY KEY)");
      stmt.execute(
        "CREATE TABLE child(k INT PRIMARY KEY, v INT REFERENCES parent(k) ON DELETE SET NULL)");
      stmt.execute("INSERT INTO parent VALUES (1)");
      extraStmt.execute("BEGIN");
      extraStmt.execute("SELECT * FROM parent WHERE k = 1 FOR UPDATE");

      runInvalidQuery(stmt, "INSERT INTO child VALUES(1, 1)", false,
        "could not serialize access due to concurrent update",
        "current transaction is expired or aborted");
      extraStmt.execute("ROLLBACK");
      assertNoRows(stmt, "SELECT * FROM child");

      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO child VALUES(1, 1)");
      extraStmt.execute("DELETE FROM parent WHERE k = 1");
      runInvalidQuery(stmt,
        "COMMIT",
        "expired or aborted by a conflict");
      assertNoRows(stmt, "SELECT * FROM child");
    }
  }

  @Test
  public void testRowLockSerializableIsolation() throws Exception {
    testRowLock(Connection.TRANSACTION_SERIALIZABLE);
  }

  @Test
  public void testRowLockSnapshotIsolation() throws Exception {
    testRowLock(Connection.TRANSACTION_REPEATABLE_READ);
  }

  private void testInsertConcurrency(int pgIsolationLevel) throws Exception {
    try (Statement stmt = connection.createStatement();
         Connection extraConnection = getConnectionBuilder().connect();
         Statement extraStmt = extraConnection.createStatement()) {
      stmt.execute("CREATE TABLE parent(k int PRIMARY KEY) SPLIT INTO 1 TABLETS");
      stmt.execute("CREATE TABLE child(pk int PRIMARY KEY," +
        "CONSTRAINT parent_fk FOREIGN KEY(pk) REFERENCES parent(k)) SPLIT INTO 1 TABLETS");
      connection.setTransactionIsolation(pgIsolationLevel);
      extraConnection.setTransactionIsolation(pgIsolationLevel);
      stmt.execute("BEGIN");
      stmt.execute("INSERT INTO parent VALUES(1)");
      stmt.execute("INSERT INTO child VALUES(1)");
      extraStmt.execute("BEGIN");
      extraStmt.execute("INSERT INTO parent VALUES(2)");
      extraStmt.execute("INSERT INTO child VALUES(2)");
      stmt.execute("COMMIT");
      extraStmt.execute("COMMIT");
    }
  }

  @Test
  public void testInsertConcurrencySnapshotIsolation() throws Exception {
    testInsertConcurrency(Connection.TRANSACTION_REPEATABLE_READ);
  }
  @Test
  public void testInsertConcurrencySerializableIsolation() throws Exception {
    testInsertConcurrency(Connection.TRANSACTION_SERIALIZABLE);
  }

  @Test
  public void testAddForeignKeyConcurrent() throws Exception {
    try (Statement stmt = connection.createStatement();
         Connection ddlConnection = getConnectionBuilder().connect();
         Statement ddlStmt = ddlConnection.createStatement()) {
      // Create two tables
      ddlStmt.execute("CREATE TABLE parent(k int PRIMARY KEY)");
      ddlStmt.execute("CREATE TABLE child(k int)");

      // Insert data into parent table
      stmt.execute("INSERT INTO parent VALUES (1), (2), (3)");

      // Start inserting data into child table
      stmt.execute("INSERT INTO child VALUES (1)");
      stmt.execute("INSERT INTO child VALUES (2)");

      // On DDL connection, add foreign key constraint
      ddlStmt.execute("ALTER TABLE child ADD CONSTRAINT fk_child_parent " +
                      "FOREIGN KEY (k) REFERENCES parent(k)");

      // Try inserting a valid row on DML connection
      stmt.execute("INSERT INTO child VALUES (3)");

      // Try inserting an invalid row on DML connection
      runInvalidQuery(stmt,
        "INSERT INTO child VALUES (4)",
        "violates foreign key constraint \"fk_child_parent\"");

      // Try inserting an invalid row on the DDL connection
      runInvalidQuery(ddlStmt,
        "INSERT INTO child VALUES (4)",
        "violates foreign key constraint \"fk_child_parent\"");

      // Verify the data
      assertQuery(stmt, "SELECT k FROM child ORDER BY k",
          new Row(1), new Row(2), new Row(3));
    }
  }
}
