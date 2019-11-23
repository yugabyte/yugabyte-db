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

import org.junit.After;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.HashSet;
import java.util.Set;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgForeignKey extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgForeignKey.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @After
  public void cleanUpAfter() throws Exception {
    // For this test tear down entire cluster between tests.
    tearDownAfter();
    pgInitialized = false;
  }

  @Test
  public void testPgRegress() throws Exception {
    runPgRegressTest("yb_foreign_key_serial_schedule");
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

      try (Connection connection1 = newConnectionBuilder()
              .setIsolationLevel(isolationLevel)
              .setAutoCommit(AutoCommit.DISABLED)
              .connect();
           Connection connection2 = newConnectionBuilder()
                   .setIsolationLevel(isolationLevel)
                   .setAutoCommit(AutoCommit.DISABLED)
                   .connect()) {

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
}
