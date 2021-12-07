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

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.Statement;

import com.yugabyte.util.PSQLException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestAlterTableWithConcurrentTxn extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestAlterTableWithConcurrentTxn.class);

  // When a transaction is performed on a table that is altered,
  // "need_global_cache_refresh" is set to true and raise
  // transaction conflict error in YBPrepareCacheRefreshIfNeeded().
  private static final String TRANSACTION_CONFLICT_ERROR =
      "expired or aborted by a conflict";
  private static final String SCHEMA_VERSION_MISMATCH_ERROR =
      "schema version mismatch for table";
  private static final String NO_ERROR = "";
  private static final boolean executeDmlBeforeAlter = true;
  private static final boolean executeDmlAfterAlter = false;
  private static enum Dml { INSERT, SELECT }
  private static enum AlterCommand { ADD_COLUMN, DROP_COLUMN }

  private void prepareAndPopulateTable(AlterCommand alterCommand, String tableName)
      throws Exception {
    // Separate connection is used to create and load the table to avoid
    // caching the table before running the DML transaction.
    try (Connection connection = getConnectionBuilder().connect();
         Statement statement = connection.createStatement()) {

      String createTableQuery = "CREATE TABLE " + tableName + " (a INT PRIMARY KEY";
      if (alterCommand == AlterCommand.DROP_COLUMN) {
        createTableQuery += ", b TEXT";
      }
      createTableQuery += ")";
      LOG.info(createTableQuery);
      statement.execute(createTableQuery);

      switch (alterCommand) {
        case DROP_COLUMN: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo')");
          break;
        }
        case ADD_COLUMN: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1)");
          break;
        }
        default: {
          throw new Exception("Alter command type " + alterCommand + " not supported");
        }
      }
    }
  }

  private String getAlterSql(AlterCommand alterCommand, String tableName) throws Exception {
    switch (alterCommand) {
      case DROP_COLUMN: {
        return "ALTER TABLE " + tableName + " DROP COLUMN b";
      }
      case ADD_COLUMN: {
        return "ALTER TABLE " + tableName + " ADD COLUMN b TEXT";
      }
      default: {
        throw new Exception("Alter command type " + alterCommand + " not supported");
      }
    }
  }

  private String getDmlSql(Dml dmlToExecute, AlterCommand alterCommand,
                             String tableName, boolean withCachedMetadata,
                             boolean executeDmlBeforeAlter) throws Exception {
    switch (dmlToExecute) {
      case INSERT: {
        boolean useOriginalSchema = withCachedMetadata || executeDmlBeforeAlter;
        switch (alterCommand) {
          case ADD_COLUMN: {
            if (useOriginalSchema) {
              return "INSERT INTO " + tableName + " VALUES (2)";
            } else {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar')";
            }
          }
          case DROP_COLUMN: {
            if (useOriginalSchema) {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar')";
            } else {
              return "INSERT INTO " + tableName + " VALUES (2)";
            }
          }
          default: {
            throw new Exception("Alter command type " + alterCommand + " not supported");
          }
        }
      }
      case SELECT: {
        return "SELECT a FROM " + tableName + " WHERE a = 1";
      }
      default: {
        throw new Exception("DML type " + dmlToExecute + " not supported");
      }
    }
  }

  private void runDmlTxnWithAlterOnCurrentResource(Dml dmlToExecute,
                                                   AlterCommand alterCommand,
                                                   boolean withCachedMetadata,
                                                   boolean executeDmlBeforeAlter,
                                                   String expectedErrorMessage) throws Exception {
    String tableName = dmlToExecute + "OnSameTable" +
        (executeDmlBeforeAlter ? "Before" : "After") + "AlterTable" + alterCommand + "On" +
        (withCachedMetadata ? "PgCached" : "NonPgCached") + "Table";
    prepareAndPopulateTable(alterCommand, tableName);

    try (Connection txnConnection = getConnectionBuilder().connect();
         Statement txnStmt = txnConnection.createStatement();
         Connection ddlConnection = getConnectionBuilder().connect();
         Statement ddlStmt = ddlConnection.createStatement()) {

      if (withCachedMetadata) {
        // Running a simple select on statement to load table's metadata into the PgGate cache.
        txnStmt.execute("SELECT * FROM " + tableName);
      }

      txnStmt.execute("BEGIN");

      // Using original schema in case of cached metadata to cause transaction conflict
      String dmlQuery = getDmlSql(
          dmlToExecute, alterCommand, tableName, withCachedMetadata, executeDmlBeforeAlter);
      String alterQuery = getAlterSql(alterCommand, tableName);

      if (executeDmlBeforeAlter) {
        // execute DML in txnConnection
        txnStmt.execute(dmlQuery);
        // execute ALTER in ddlConnection
        ddlStmt.execute(alterQuery);
        // execute COMMIT in txnConnection
        if (expectedErrorMessage.isEmpty()) {
          txnStmt.execute("COMMIT");
        } else {
          runInvalidQuery(txnStmt, "COMMIT", expectedErrorMessage);
        }
      } else {
        // execute ALTER in ddlConnection
        ddlStmt.execute(alterQuery);
        // execute DML in txnConnection
        if (expectedErrorMessage.isEmpty()) {
          txnStmt.execute(dmlQuery);
        } else {
          runInvalidQuery(txnStmt, dmlQuery, expectedErrorMessage);
        }
        // execute COMMIT in txnConnection
        txnStmt.execute("COMMIT");
      }
    }
  }

  private void runInsertTxnWithAlterOnUnrelatedResource(AlterCommand alterCommand,
                                                        boolean withCachedMetadata,
                                                        boolean executeDmlBeforeAlter)
      throws Exception {
    String tableName = "insertOnDifferentTable" +
        (executeDmlBeforeAlter ? "Before" : "After") + "AlterTable" + alterCommand + "On" +
        (withCachedMetadata ? "PgCached" : "NonPgCached") + "Table";
    String tableName2 = "dummyTableFor" + tableName;
    prepareAndPopulateTable(alterCommand, tableName);
    prepareAndPopulateTable(alterCommand, tableName2);

    try (Connection txnConnection = getConnectionBuilder().connect();
         Statement txnStmt = txnConnection.createStatement();
         Connection ddlConnection = getConnectionBuilder().connect();
         Statement ddlStmt = ddlConnection.createStatement()) {

      if (withCachedMetadata) {
        // Running a simple select on statement to load table's metadata into the PgGate cache.
        txnStmt.execute("SELECT * FROM " + tableName);
        ddlStmt.execute("SELECT * FROM " + tableName2);
      }

      txnStmt.execute("BEGIN");

      String alterQuery = getAlterSql(alterCommand, tableName2);

      if (executeDmlBeforeAlter) {
        txnStmt.execute("INSERT INTO " + tableName + " VALUES (5)");
        ddlStmt.execute(alterQuery);
      } else {
        ddlStmt.execute(alterQuery);
        txnStmt.execute("INSERT INTO " + tableName + " VALUES (5)");
      }

      txnStmt.execute("COMMIT");
      assertQuery(txnStmt, "SELECT COUNT(*) FROM " + tableName, new Row(2));
    }
  }

  private void runMultipleTxnsBeforeAlterTable() throws Exception {
    AlterCommand dropColumn = AlterCommand.DROP_COLUMN;

    String tableName = "runMultipleTxnsBeforeAlterTable";
    // table gets populated with one row
    prepareAndPopulateTable(dropColumn, tableName);

    try (Connection txnConnection = getConnectionBuilder().connect();
         Statement txnStmt1 = txnConnection.createStatement();
         Connection ddlConnection = getConnectionBuilder().connect();
         Statement ddlStmt = ddlConnection.createStatement();
         Connection txnConnection2 = getConnectionBuilder().connect();
         Statement txnStmt2 = txnConnection2.createStatement();
         Connection txnConnection3 = getConnectionBuilder().connect();
         Statement txnStmt3 = txnConnection3.createStatement()) {

      txnStmt1.execute("BEGIN");
      txnStmt1.execute("INSERT INTO " + tableName + " VALUES (10, 20)");

      txnStmt2.execute("BEGIN");
      txnStmt2.execute("DELETE FROM " + tableName + " WHERE a = 1");

      txnStmt3.execute("BEGIN");
      txnStmt3.execute("UPDATE " + tableName + " SET a = 10 WHERE a = 2");
      txnStmt3.execute("INSERT INTO " + tableName + " VALUES (3, 4)");

      ddlStmt.execute(getAlterSql(dropColumn, tableName));

      runInvalidQuery(txnStmt1, "COMMIT", TRANSACTION_CONFLICT_ERROR);
      runInvalidQuery(txnStmt2, "COMMIT", TRANSACTION_CONFLICT_ERROR);
      runInvalidQuery(txnStmt3, "COMMIT", TRANSACTION_CONFLICT_ERROR);

      assertQuery(txnStmt1, "SELECT COUNT(*) FROM " + tableName, new Row(1));
    }
  }

  private void runMultipleTxnsBeforeAndAfterAlterTable() throws Exception {
    AlterCommand dropColumn = AlterCommand.DROP_COLUMN;

    String tableName = "runMultipleTxnsBeforeAndAfterAlterTable";
    // table gets populated with one row
    prepareAndPopulateTable(dropColumn, tableName);
    String tableName2 = "dummyTableForRunMultipleTxnsBeforeAndAfterAlterTable";
    prepareAndPopulateTable(dropColumn, tableName2);

    try (Connection txnConnection = getConnectionBuilder().connect();
         Statement txnStmt1 = txnConnection.createStatement();
         Connection ddlConnection = getConnectionBuilder().connect();
         Statement ddlStmt = ddlConnection.createStatement();
         Connection txnConnection2 = getConnectionBuilder().connect();
         Statement txnStmt2 = txnConnection2.createStatement()) {

      txnStmt1.execute("BEGIN");
      txnStmt1.execute("INSERT INTO " + tableName2 + " VALUES (10, 20)");

      txnStmt2.execute("BEGIN");
      txnStmt2.execute("DELETE FROM " + tableName2 + " WHERE a = 50");
      txnStmt2.execute("INSERT INTO " + tableName2 + " VALUES (11, 20)");

      ddlStmt.execute(getAlterSql(dropColumn, tableName));

      runInvalidQuery(txnStmt1, "INSERT INTO " + tableName + " VALUES (0, 0)",
          "has more expressions than target columns");
      txnStmt1.execute("COMMIT");
      // only one column (the new schema) is expected
      ResultSet rs = txnStmt2.executeQuery("SELECT a FROM " + tableName);
      ResultSetMetaData rsmd = rs.getMetaData();
      assertEquals(1, rsmd.getColumnCount());
      txnStmt2.execute("COMMIT");

      assertQuery(txnStmt1, "SELECT COUNT(*) FROM " + tableName, new Row(1));
      assertQuery(txnStmt1, "SELECT COUNT(*) FROM " + tableName2, new Row(2));
    }
  }

  private void runAlterWithRollbackToEnsureNoTableDeletion() throws Exception {
    String tableName = "runAlterWithRollbackToEnsureNoTableDeletion";

    try (Connection connection = getConnectionBuilder().connect();
         Statement stmt = connection.createStatement()) {

      stmt.execute("CREATE TABLE " + tableName + " (a INT PRIMARY KEY, b INT)");
      stmt.execute("INSERT INTO " + tableName + " VALUES (1, 1)");

      runInvalidQuery(stmt, "ALTER TABLE " + tableName + " ADD COLUMN c INT NOT NULL",
          "column \"c\" contains null values");

      Thread.sleep(20000); // 20 seconds
      assertQuery(stmt, "SELECT COUNT(*) FROM " + tableName, new Row(1));
      stmt.execute("INSERT INTO " + tableName + " VALUES (2, 2)");
      assertQuery(stmt, "SELECT COUNT(*) FROM " + tableName, new Row(2));
    }
  }

  /**
   * Test dimensions:
   * -- DDL types (ALTER TABLE [ADD/DROP] COLUMN)
   * -- DML types INSERT vs. SELECT
   * -- Perform DML before vs. after DDL
   * -- PG metadata cached vs. non-cached
   *
   * Test restrictions:
   * -- Always use filled (pre-populated) tables
   * -- Always use a primary key when creating the table
   * -- Always use snapshot isolation
   * -- DML operation always affects exactly one row
   */
  @Test
  public void testDmlTransactionWithAlterOnCurrentResource() throws Exception {
    AlterCommand addCol = AlterCommand.ADD_COLUMN;
    AlterCommand dropCol = AlterCommand.DROP_COLUMN;
    boolean withCachedMetadata = true;

    // Scenario 1. Execute DML before DDL.
    // a) For INSERT DML type:
    //    Transaction should conflict since insert operation
    //    takes a distributed transaction lock.
    // b) For SELECT DML type:
    //    Transaction should not conflict because select operation just
    //    picks a read time and doesn't take a distributed transaction lock.
    // Note when performing DML before DDL, there is no need to separately cache
    // the table metadata since caching will happen when executing DML query.
    LOG.info("Run INSERT txn before ALTER " + addCol);
    runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, addCol, !withCachedMetadata,
        executeDmlBeforeAlter, TRANSACTION_CONFLICT_ERROR);
    LOG.info("Run INSERT txn before ALTER " + dropCol);
    runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, dropCol, !withCachedMetadata,
        executeDmlBeforeAlter, TRANSACTION_CONFLICT_ERROR);
    LOG.info("Run SELECT txn before ALTER " + addCol);
    runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, addCol, !withCachedMetadata,
        executeDmlBeforeAlter, NO_ERROR);
    LOG.info("Run SELECT txn before ALTER " + dropCol);
    runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, dropCol, !withCachedMetadata,
        executeDmlBeforeAlter, NO_ERROR);

    // Scenario 2. Execute any DML type after DDL.
    // a) For PG metadata cached table:
    //    Transaction should conflict since we are using
    //    the original schema for DML operation.
    // b) For non PG metadata cached table:
    //    Transaction should not conflict because new schema is used for
    //    DML operation and the original schema is not already cached.
    LOG.info("Run " + Dml.INSERT + " transaction after ALTER " + addCol + " on PG cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, addCol, withCachedMetadata,
        executeDmlAfterAlter, SCHEMA_VERSION_MISMATCH_ERROR);
    LOG.info("Run " + Dml.SELECT + " transaction after ALTER " + addCol + " on PG cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, addCol, withCachedMetadata,
        executeDmlAfterAlter, SCHEMA_VERSION_MISMATCH_ERROR);
    LOG.info("Run " + Dml.INSERT + " transaction after ALTER " + dropCol + " on PG cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, dropCol, withCachedMetadata,
        executeDmlAfterAlter, SCHEMA_VERSION_MISMATCH_ERROR);
    LOG.info("Run " + Dml.SELECT + " transaction after ALTER " + dropCol + " on PG cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, dropCol, withCachedMetadata,
        executeDmlAfterAlter, SCHEMA_VERSION_MISMATCH_ERROR);
    LOG.info("Run " + Dml.INSERT + " transaction after ALTER " + addCol + " on non cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, addCol, !withCachedMetadata,
        executeDmlAfterAlter, NO_ERROR);
    LOG.info("Run " + Dml.SELECT + " transaction after ALTER " + addCol + " on non cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, addCol, !withCachedMetadata,
        executeDmlAfterAlter, NO_ERROR);
    LOG.info("Run " + Dml.INSERT + " transaction after ALTER " + dropCol + " on non cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, dropCol, !withCachedMetadata,
        executeDmlAfterAlter, NO_ERROR);
    LOG.info("Run " + Dml.SELECT + " transaction after ALTER " + dropCol + " on non cached table");
    runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, dropCol, !withCachedMetadata,
        executeDmlAfterAlter, NO_ERROR);

    LOG.info("Run multiple transactions before altering the resource");
    runMultipleTxnsBeforeAlterTable();
    LOG.info("Run multiple transactions before and after altering the resource");
    runMultipleTxnsBeforeAndAfterAlterTable();
    LOG.info("Run alter with roll back and ensure table doesn't get deleted afterwards");
    runAlterWithRollbackToEnsureNoTableDeletion();
  }

  @Test
  public void testDmlTransactionWithAlterOnDifferentResource() throws Exception {
    boolean withCachedMetadata = true;
    LOG.info("Run insert transaction before/after altering another resource");
    AlterCommand addColumn = AlterCommand.ADD_COLUMN;
    runInsertTxnWithAlterOnUnrelatedResource(addColumn, withCachedMetadata, executeDmlBeforeAlter);
    runInsertTxnWithAlterOnUnrelatedResource(addColumn, withCachedMetadata, executeDmlAfterAlter);

    LOG.info("Run insert transaction before/after altering another resource");
    AlterCommand dropColumn = AlterCommand.DROP_COLUMN;
    runInsertTxnWithAlterOnUnrelatedResource(dropColumn, withCachedMetadata, executeDmlBeforeAlter);
    runInsertTxnWithAlterOnUnrelatedResource(dropColumn, withCachedMetadata, executeDmlAfterAlter);
  }

  @Test
  public void testTransactionConflictErrorCode() throws Exception {
    try (Connection conn1 = getConnectionBuilder().connect();
         Statement stmt1 = conn1.createStatement();
         Connection conn2 = getConnectionBuilder().connect();
         Statement stmt2 = conn2.createStatement();
         Connection conn3 = getConnectionBuilder().connect();
         Statement stmt3 = conn3.createStatement()) {

      stmt1.execute("CREATE TABLE p (a INT)");
      stmt1.execute("INSERT INTO p VALUES (1)");

      LOG.info("Test write before ALTER");
      stmt2.execute("SELECT * FROM p");
      stmt2.execute("BEGIN");
      stmt2.execute("INSERT INTO p VALUES (2)");
      stmt3.execute("ALTER TABLE p ADD COLUMN b TEXT");
      try {
        stmt2.execute("COMMIT");
        fail("Write before ALTER did not fail");
      } catch (PSQLException e) {
        assertEquals(SERIALIZATION_FAILURE_PSQL_STATE, e.getSQLState());
      }

      LOG.info("Test write after ALTER");
      stmt2.execute("SELECT * FROM p");
      stmt2.execute("BEGIN");
      stmt3.execute("ALTER TABLE p DROP COLUMN b");
      try {
        stmt2.execute("INSERT INTO p VALUES (2)");
        fail("Write after ALTER did not fail");
      } catch (PSQLException e) {
        assertEquals(SERIALIZATION_FAILURE_PSQL_STATE, e.getSQLState());
      }
      stmt2.execute("COMMIT");

      // Only testing select after ALTER because
      // select before ALTER is treated as no op and
      // thus does not cause transaction to abort
      LOG.info("Test select after ALTER");
      stmt2.execute("SELECT * FROM p");
      stmt2.execute("BEGIN");
      stmt3.execute("ALTER TABLE p ADD COLUMN b TEXT");
      try {
        stmt2.execute("SELECT * FROM p");
        fail("Read after ALTER did not fail");
      } catch (PSQLException e) {
        assertEquals(SERIALIZATION_FAILURE_PSQL_STATE, e.getSQLState());
      }
      stmt2.execute("COMMIT");
    }
  }

  @Test
  public void testTransactionConflictErrorCodeOnOtherResource() throws Exception {
    try (Connection conn1 = getConnectionBuilder().connect();
        Statement stmt1 = conn1.createStatement();
        Connection conn2 = getConnectionBuilder().connect();
        Statement stmt2 = conn2.createStatement();
        Connection conn3 = getConnectionBuilder().connect();
        Statement stmt3 = conn3.createStatement()) {

      LOG.info("Test select after ALTER on different resource");
      stmt1.execute("CREATE TABLE test (a INT PRIMARY KEY, b INT, c INT)");
      stmt1.execute("CREATE USER foo");

      stmt2.execute("BEGIN");
      stmt2.execute("INSERT INTO test VALUES (1,1,1)");

      stmt3.execute("ALTER USER foo RENAME TO bar");
      waitForTServerHeartbeat();
      try {
        stmt2.execute("INSERT INTO test VALUES (2,2,2)");
        fail("Insert after ALTER on different resource did not fail");
      } catch (PSQLException e) {
        assertEquals(SERIALIZATION_FAILURE_PSQL_STATE, e.getSQLState());
      }
      stmt2.execute("COMMIT");
    }
  }
}
