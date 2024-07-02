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
import static org.junit.Assume.*;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Map;

import com.yugabyte.util.PSQLException;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestAlterTableWithConcurrentTxn extends BasePgSQLTest {
  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("TEST_yb_test_table_rewrite_keep_old_table", "true");
    return flagMap;
  }

  private static final Logger LOG = LoggerFactory.getLogger(TestAlterTableWithConcurrentTxn.class);

  // When a transaction is performed on a table that is altered,
  // "need_global_cache_refresh" is set to true and raise
  // transaction conflict error in YBPrepareCacheRefreshIfNeeded().
  private static final String[] TRANSACTION_ABORT_ERRORS =
      { "expired or aborted by a conflict", "Transaction aborted: kAborted" };
  private static final String SCHEMA_VERSION_MISMATCH_ERROR =
      "schema version mismatch for table";
  private static final String NO_SEQUENCE_FOUND_ERROR =
      "no owned sequence found";
  private static final String NO_TUPLE_FOUND_ERROR =
       "could not find tuple for parent";
  private static final String NO_ERROR = "";
  private static final boolean executeDmlBeforeAlter = true;
  private static final boolean executeDmlAfterAlter = false;
  private static final boolean cacheMetadataSetTrue = true;
  private static enum Dml { INSERT, SELECT }
  private static enum AlterCommand {
      ADD_CONSTRAINT, DROP_CONSTRAINT,
      ADD_COLUMN, DROP_COLUMN, ADD_INDEX,
      ENABLE_TRIG, DISABLE_TRIG, CHANGE_OWNER,
      COLUMN_DEFAULT, SET_NOT_NULL, DROP_NOT_NULL,
      DROP_IDENTITY, ADD_IDENTITY,
      ENABLE_ROW_SECURITY, DISABLE_ROW_SECURITY,
      ATTACH_PARTITION, DETACH_PARTITION,
      ADD_FOREIGN_KEY, DROP_FOREIGN_KEY,
      ADD_PRIMARY_KEY, DROP_PRIMARY_KEY,
      ADD_COLUMN_WITH_VOLATILE_DEFAULT, ALTER_TYPE}

  private void prepareAndPopulateTable(AlterCommand alterCommand, String tableName)
      throws Exception {
    // Separate connection is used to create and load the table to avoid
    // caching the table before running the DML transaction.
    try (Connection connection = getConnectionBuilder().connect();
         Statement statement = connection.createStatement()) {

      // Create other resources before table creation
      if (alterCommand == AlterCommand.CHANGE_OWNER) {
        Row row = getSingleRow(statement,
            "SELECT COUNT(*) FROM pg_roles WHERE rolname='r1'");
        if (row.getLong(0) == 0) {
          statement.execute("CREATE USER r1");
        }
        row = getSingleRow(statement,
            "SELECT COUNT(*) FROM pg_roles WHERE rolname='r2'");
        if (row.getLong(0) == 0) {
          statement.execute("CREATE USER r2");
        }
        statement.execute("SET ROLE r1");
      }

      // Create table
      String createTableQuery = "CREATE TABLE " + tableName;
      if (alterCommand == AlterCommand.ADD_PRIMARY_KEY) {
        createTableQuery += " (a INT";
      } else {
        createTableQuery += " (a INT PRIMARY KEY";
      }
      if (alterCommand == AlterCommand.DROP_CONSTRAINT) {
        createTableQuery += " CONSTRAINT positive CHECK (a > 0)";
      }
      if (alterCommand != AlterCommand.ADD_COLUMN) {
        createTableQuery += ", b TEXT";
      }
      if (alterCommand == AlterCommand.DROP_NOT_NULL) {
        createTableQuery += " NOT NULL";
      }
      if (alterCommand == AlterCommand.ADD_IDENTITY) {
        createTableQuery += ", c INT NOT NULL";
      } else if (alterCommand == AlterCommand.DROP_IDENTITY) {
        createTableQuery += ", c INT GENERATED ALWAYS AS IDENTITY";
      }
      if (alterCommand == AlterCommand.ALTER_TYPE) {
        createTableQuery += ", d TEXT";
      }
      createTableQuery += ")";
      if (alterCommand == AlterCommand.ATTACH_PARTITION ||
          alterCommand == AlterCommand.DETACH_PARTITION) {
        createTableQuery += " PARTITION BY LIST (a)";
      }
      LOG.info(createTableQuery);
      statement.execute(createTableQuery);

      // Create other resources after table creation
      if (alterCommand == AlterCommand.ENABLE_TRIG || alterCommand == AlterCommand.DISABLE_TRIG) {
        statement.execute("CREATE TABLE IF NOT EXISTS dummyTableForTrig (z int)");
        Row row = getSingleRow(statement,
            "SELECT COUNT(*) FROM pg_proc WHERE proname='trig_fn'");
        if (row.getLong(0) == 0) {
          statement.execute("CREATE FUNCTION trig_fn() RETURNS TRIGGER AS $$ " +
              "BEGIN INSERT INTO dummyTableForTrig VALUES (10); " +
              "RETURN NEW; END; $$ LANGUAGE 'plpgsql'");
        }
        statement.execute(
            "CREATE TRIGGER trig AFTER INSERT ON " + tableName +
            " FOR EACH STATEMENT EXECUTE PROCEDURE trig_fn()");
      } else if (alterCommand == AlterCommand.ATTACH_PARTITION ||
          alterCommand == AlterCommand.DETACH_PARTITION) {
        statement.execute(
            "CREATE TABLE " + tableName + "_p1 PARTITION OF " +
            tableName + " FOR VALUES IN (1, 3)");
        if (alterCommand == AlterCommand.ATTACH_PARTITION) {
          statement.execute("CREATE TABLE " + tableName + "_p2 (a INT NOT NULL, b TEXT)");
        } else {
          statement.execute(
              "CREATE TABLE " + tableName + "_p2 PARTITION OF " + tableName +
              " FOR VALUES IN (2, 4)");
        }
      } else if (alterCommand == AlterCommand.ADD_FOREIGN_KEY) {
        statement.execute("CREATE TABLE " + tableName + "_f (a INT)");
      } else if (alterCommand == AlterCommand.DROP_FOREIGN_KEY) {
        statement.execute("CREATE TABLE " + tableName + "_f (a INT, " +
            "CONSTRAINT c FOREIGN KEY (a) REFERENCES " + tableName + "(a))");
      }

      // Populate the table
      switch (alterCommand) {
        case DROP_COLUMN:
        case ADD_CONSTRAINT:
        case DROP_CONSTRAINT:
        case ADD_INDEX:
        case CHANGE_OWNER:
        case COLUMN_DEFAULT:
        case SET_NOT_NULL:
        case DROP_NOT_NULL:
        case ENABLE_ROW_SECURITY:
        case DISABLE_ROW_SECURITY:
        case DROP_IDENTITY:
        case ATTACH_PARTITION:
        case DETACH_PARTITION:
        case ADD_PRIMARY_KEY:
        case DROP_PRIMARY_KEY:
        case ADD_COLUMN_WITH_VOLATILE_DEFAULT: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo')");
          break;
        }
        case ADD_IDENTITY: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo', 1)");
          break;
        }
        case ADD_COLUMN: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1)");
          break;
        }
        case ENABLE_TRIG:
        case DISABLE_TRIG: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo')");
          statement.execute("INSERT INTO dummyTableForTrig VALUES (1)");
          break;
        }
        case ADD_FOREIGN_KEY:
        case DROP_FOREIGN_KEY: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo')");
          statement.execute("INSERT INTO " + tableName + "_f VALUES (1)");
          break;
        }
        case ALTER_TYPE: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo', 'bar')");
          break;
        }
        default: {
          throw new Exception("Alter command type " + alterCommand + " not supported");
        }
      }
    }
  }

  private String getAlterSql(AlterCommand alterCommand, String tableName) throws Exception {
    // Set test flag to skip dropping old tables for table rewrite operations.
    String rewriteTestFlag = "SET yb_test_table_rewrite_keep_old_table=true;";
    switch (alterCommand) {
      case DROP_COLUMN: {
        return "ALTER TABLE " + tableName + " DROP COLUMN b";
      }
      case ADD_COLUMN: {
        return "ALTER TABLE " + tableName + " ADD COLUMN b TEXT";
      }
      case ADD_CONSTRAINT: {
        return "ALTER TABLE " + tableName + " ADD CONSTRAINT positive CHECK (a > 0)";
      }
      case DROP_CONSTRAINT: {
        return "ALTER TABLE " + tableName + " DROP CONSTRAINT positive";
      }
      case ADD_INDEX: {
        return "ALTER TABLE " + tableName + " ADD CONSTRAINT " + tableName + "_id UNIQUE (a)";
      }
      case ENABLE_TRIG: {
        return "ALTER TABLE " + tableName + " ENABLE TRIGGER trig";
      }
      case DISABLE_TRIG: {
        return "ALTER TABLE " + tableName + " DISABLE TRIGGER trig";
      }
      case CHANGE_OWNER: {
        return "ALTER TABLE " + tableName + " OWNER to r2";
      }
      case COLUMN_DEFAULT: {
        return "ALTER TABLE " + tableName + " ALTER COLUMN a SET DEFAULT 100";
      }
      case SET_NOT_NULL: {
        return "ALTER TABLE " + tableName + " ALTER COLUMN a SET NOT NULL";
      }
      case DROP_NOT_NULL: {
        return "ALTER TABLE " + tableName + " ALTER COLUMN b DROP NOT NULL";
      }
      case ENABLE_ROW_SECURITY: {
        return "ALTER TABLE " + tableName + " ENABLE ROW LEVEL SECURITY";
      }
      case DISABLE_ROW_SECURITY: {
        return "ALTER TABLE " + tableName + " DISABLE ROW LEVEL SECURITY";
      }
      case ADD_IDENTITY: {
        return "ALTER TABLE " + tableName + " ALTER COLUMN c ADD GENERATED ALWAYS AS IDENTITY";
      }
      case DROP_IDENTITY: {
        return "ALTER TABLE " + tableName + " ALTER COLUMN c DROP IDENTITY";
      }
      case ATTACH_PARTITION: {
        return rewriteTestFlag + "ALTER TABLE " + tableName + " ATTACH PARTITION " +
            tableName + "_p2 FOR VALUES IN (2)";
      }
      case DETACH_PARTITION: {
        return "ALTER TABLE " + tableName + " DETACH PARTITION " + tableName + "_p2";
      }
      case ADD_FOREIGN_KEY: {
        return "ALTER TABLE " + tableName + "_f ADD CONSTRAINT c " +
            "FOREIGN KEY (a) REFERENCES " + tableName +"(a)";
      }
      case DROP_FOREIGN_KEY: {
        return "ALTER TABLE " + tableName + "_f DROP CONSTRAINT c";
      }
      case ADD_PRIMARY_KEY: {
        return rewriteTestFlag + "ALTER TABLE " + tableName + " ADD PRIMARY KEY (a)";
      }
      case DROP_PRIMARY_KEY: {
        return rewriteTestFlag + "ALTER TABLE " + tableName + " DROP CONSTRAINT " + tableName
          + "_pkey";
      }
      case ADD_COLUMN_WITH_VOLATILE_DEFAULT: {
        return rewriteTestFlag + "ALTER TABLE " + tableName
          + " ADD COLUMN d float DEFAULT random()";
      }
      case ALTER_TYPE: {
        return rewriteTestFlag + "ALTER TABLE " + tableName
          + " ALTER COLUMN d TYPE int USING length(d)";
      }
      default: {
        throw new Exception("Alter command type " + alterCommand + " not supported");
      }
    }
  }

  private String getDmlSql(Dml dmlToExecute, AlterCommand alterCommand,
                           String tableName, boolean withCachedMetadata,
                           boolean executeDmlBeforeAlter) throws Exception {
    boolean useOriginalSchema = withCachedMetadata || executeDmlBeforeAlter;
    switch (dmlToExecute) {
      case INSERT: {
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
              return "INSERT INTO " + tableName + " VALUES (2, 'foo')";
            } else {
              return "INSERT INTO " + tableName + " VALUES (2)";
            }
          }
          case ADD_CONSTRAINT:
          case ADD_INDEX:
          case DROP_CONSTRAINT:
          case ENABLE_TRIG:
          case DISABLE_TRIG:
          case CHANGE_OWNER:
          case COLUMN_DEFAULT:
          case SET_NOT_NULL:
          case DROP_NOT_NULL:
          case ENABLE_ROW_SECURITY:
          case DISABLE_ROW_SECURITY:
          case ADD_PRIMARY_KEY:
          case DROP_PRIMARY_KEY: {
            return "INSERT INTO " + tableName + " VALUES (2, 'foo')";
          }
          case ADD_FOREIGN_KEY:
          case DROP_FOREIGN_KEY: {
            if (tableName.contains("_f"))
              return "INSERT INTO " + tableName + " VALUES (1)";
            else
              return "INSERT INTO " + tableName + " VALUES (2, 'bar')";
          }
          case ATTACH_PARTITION: {
            if (tableName.contains("p")) {
              return "INSERT INTO " + tableName + " VALUES (2)";
            } else {
              return "INSERT INTO " + tableName + " VALUES (3)";
            }
          }
          case DETACH_PARTITION: {
            if (tableName.contains("p")) {
              return "INSERT INTO " + tableName + " VALUES (2)";
            } else {
              return "INSERT INTO " + tableName + " VALUES (4)";
            }
          }
          case ADD_IDENTITY: {
            if (useOriginalSchema) {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar', 2)";
            } else {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar')";
            }
          }
          case DROP_IDENTITY: {
            if (useOriginalSchema) {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar')";
            } else {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar', 2)";
            }
          }
          case ADD_COLUMN_WITH_VOLATILE_DEFAULT: {
            if (useOriginalSchema) {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar')";
            } else {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar', 2.0)";
            }
          }
          case ALTER_TYPE: {
            if (useOriginalSchema) {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar', 'foobar')";
            } else {
              return "INSERT INTO " + tableName + " VALUES (2, 'bar', 6)";
            }
          }
          default: {
            throw new Exception("Alter command type " + alterCommand + " not supported");
          }
        }
      }
      case SELECT: {
        switch (alterCommand) {
          case ADD_COLUMN:
          case DROP_COLUMN:
          case ADD_CONSTRAINT:
          case DROP_CONSTRAINT:
          case ADD_INDEX:
          case ENABLE_TRIG:
          case DISABLE_TRIG:
          case CHANGE_OWNER:
          case COLUMN_DEFAULT:
          case SET_NOT_NULL:
          case DROP_NOT_NULL:
          case ENABLE_ROW_SECURITY:
          case DISABLE_ROW_SECURITY:
          case ADD_IDENTITY:
          case DROP_IDENTITY:
          case ADD_FOREIGN_KEY:
          case DROP_FOREIGN_KEY:
          case ADD_PRIMARY_KEY:
          case DROP_PRIMARY_KEY:
          case ADD_COLUMN_WITH_VOLATILE_DEFAULT:
          case ALTER_TYPE:
          case ATTACH_PARTITION:
            return "SELECT a FROM " + tableName + " WHERE a = 1";
          case DETACH_PARTITION:
            return "SELECT a FROM " + tableName + " WHERE a = 2";
          default:
            throw new Exception("Alter command type " + alterCommand + " not supported");
        }
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
                                                   String... expectedErrorMessages)
      throws Exception {
    String tableName = dmlToExecute + "OnTable" +
        (executeDmlBeforeAlter ? "Before" : "After") + "AlterTable" + alterCommand + "On" +
        (withCachedMetadata ? "PgCached" : "NonPgCached");
    prepareAndPopulateTable(alterCommand, tableName);
    String dependentTableName = "";
    if (alterCommand == AlterCommand.ATTACH_PARTITION ||
        alterCommand == AlterCommand.DETACH_PARTITION) {
      dependentTableName = tableName + "_p2";
    } else if (alterCommand == AlterCommand.ADD_FOREIGN_KEY ||
        alterCommand == AlterCommand.DROP_FOREIGN_KEY) {
      dependentTableName = tableName + "_f";
    }

    try (Connection txnConnection1 = getConnectionBuilder().connect();
         Statement txnStmt1 = txnConnection1.createStatement();
         Connection txnConnection2 = getConnectionBuilder().connect();
         Statement txnStmt2 = txnConnection2.createStatement();
         Connection ddlConnection = getConnectionBuilder().connect();
         Statement ddlStmt = ddlConnection.createStatement()) {

      if (withCachedMetadata) {
        // Running a simple select on statement to load table's metadata into the PgGate cache.
        txnStmt1.execute("SELECT * FROM " + tableName);
        if (!dependentTableName.isEmpty()) {
          if (alterCommand == AlterCommand.DETACH_PARTITION) {
            // For partitioned table, the pg_inherits cache is only populated upon INSERTs, so
            // run an INSERT statement to populate the cache. A SELECT will only populate the
            // relcache entry for the child directly, just like a SELECT to a regular table. To
            // populate the inheritance hierarcy into the cache (which is needed by this test), we
            // should run an INSERT.
            txnStmt2.execute("INSERT INTO " + dependentTableName + " VALUES (2)");
          } else {
            txnStmt2.execute("SELECT * FROM " + dependentTableName);
          }
        }
      }

      // Using original schema in case of cached metadata to cause transaction conflict
      String dmlQuery = getDmlSql(
          dmlToExecute, alterCommand, tableName, withCachedMetadata, executeDmlBeforeAlter);
      String alterQuery = getAlterSql(alterCommand, tableName);

      if (dependentTableName.isEmpty()) {
        txnStmt1.execute("BEGIN");
        if (executeDmlBeforeAlter) {
          // execute DML in txnConnection1
          txnStmt1.execute(dmlQuery);
          // execute ALTER in ddlConnection
          ddlStmt.execute(alterQuery);
          // execute COMMIT in txnConnection1
          if (expectedErrorMessages.length == 1 && expectedErrorMessages[0].isEmpty()) {
            txnStmt1.execute("COMMIT");
          } else {
            runInvalidQuery(txnStmt1, "COMMIT", expectedErrorMessages);
          }
        } else {
          // execute ALTER in ddlConnection
          ddlStmt.execute(alterQuery);
          // execute DML in txnConnection1
          if (expectedErrorMessages.length == 1 && expectedErrorMessages[0].isEmpty()) {
            txnStmt1.execute(dmlQuery);
          } else {
            runInvalidQuery(txnStmt1, dmlQuery, expectedErrorMessages);
          }
          // execute COMMIT in txnConnection1
          txnStmt1.execute("COMMIT");
        }
      } else {
        String[] expectedErrorMessagesOnDependentTable = expectedErrorMessages.clone();
        if (alterCommand == AlterCommand.ATTACH_PARTITION) {
          if (executeDmlBeforeAlter) {
            if (dmlToExecute == Dml.INSERT) {
              expectedErrorMessagesOnDependentTable = TRANSACTION_ABORT_ERRORS;
            } else {
              expectedErrorMessagesOnDependentTable = new String[]{ NO_ERROR };
            }
          } else {
            if (withCachedMetadata) {
              expectedErrorMessagesOnDependentTable = new String[]{ SCHEMA_VERSION_MISMATCH_ERROR };
            } else {
              expectedErrorMessagesOnDependentTable = new String[]{ NO_ERROR };
            }
          }
        } else if (alterCommand == AlterCommand.DETACH_PARTITION) {
          if (!executeDmlBeforeAlter) {
            if (!withCachedMetadata && dmlToExecute == Dml.INSERT) {
              expectedErrorMessagesOnDependentTable = new String[]{ NO_ERROR };
            }
          }
        }

        String dmlQueryOnDependentTable = getDmlSql(
            dmlToExecute, alterCommand, dependentTableName,
            withCachedMetadata, executeDmlBeforeAlter);

        txnStmt1.execute("BEGIN");
        txnStmt2.execute("BEGIN");
        if (executeDmlBeforeAlter) {
          // execute DML on main table in txnConnection1
          txnStmt1.execute(dmlQuery);
          // execute DML on dependent table in txnConnection2
          txnStmt2.execute(dmlQueryOnDependentTable);
          // execute ALTER in ddlConnection
          ddlStmt.execute(alterQuery);
          // execute COMMIT in txnConnection1 & 2
          if (expectedErrorMessages.length == 1 && expectedErrorMessages[0].isEmpty()) {
            txnStmt1.execute("COMMIT");
            if (expectedErrorMessagesOnDependentTable.length == 1 &&
                expectedErrorMessagesOnDependentTable[0].isEmpty()) {
              txnStmt2.execute("COMMIT");
            } else {
              runInvalidQuery(txnStmt2, "COMMIT", expectedErrorMessagesOnDependentTable);
            }
          } else {
            runInvalidQuery(txnStmt1, "COMMIT", expectedErrorMessages);
            runInvalidQuery(txnStmt2, "COMMIT", expectedErrorMessagesOnDependentTable);
          }
        } else {
          // execute ALTER in ddlConnection
          ddlStmt.execute(alterQuery);
          // execute DML in txnConnection1 & 2
          if (expectedErrorMessages.length == 1 && expectedErrorMessages[0].isEmpty()) {
            txnStmt1.execute(dmlQuery);
            if (expectedErrorMessagesOnDependentTable.length == 1 &&
                expectedErrorMessagesOnDependentTable[0].equals(NO_ERROR)) {
              txnStmt2.execute(dmlQueryOnDependentTable);
            } else {
              runInvalidQuery(txnStmt2,
                              dmlQueryOnDependentTable,
                              expectedErrorMessagesOnDependentTable);
            }
          } else {
            runInvalidQuery(txnStmt1, dmlQuery, expectedErrorMessages);
            if (expectedErrorMessagesOnDependentTable.length == 1 &&
                expectedErrorMessagesOnDependentTable[0].equals(NO_ERROR)) {
              txnStmt2.execute(dmlQueryOnDependentTable);
            } else {
              runInvalidQuery(txnStmt2,
                              dmlQueryOnDependentTable,
                              expectedErrorMessagesOnDependentTable);
            }
          }
          // execute COMMIT in txnConnection1 & 2
          txnStmt1.execute("COMMIT");
          txnStmt2.execute("COMMIT");
        }
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

      runInvalidQuery(txnStmt1, "COMMIT", TRANSACTION_ABORT_ERRORS);
      runInvalidQuery(txnStmt2, "COMMIT", TRANSACTION_ABORT_ERRORS);
      runInvalidQuery(txnStmt3, "COMMIT", TRANSACTION_ABORT_ERRORS);

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
      Row row = getSingleRow(txnStmt2, "SELECT a FROM " + tableName);
      assertEquals(1, row.elems.size());
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
          "column \"c\" of relation \"" + tableName + "\" contains null values");

      Thread.sleep(20000); // 20 seconds
      assertQuery(stmt, "SELECT COUNT(*) FROM " + tableName, new Row(1));
      stmt.execute("INSERT INTO " + tableName + " VALUES (2, 2)");
      assertQuery(stmt, "SELECT COUNT(*) FROM " + tableName, new Row(2));
    }
  }

  /**
   * Test dimensions:
   * -- DDL types ALTER TABLE
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
  public void testDmlTransactionBeforeAlterOnCurrentResource() throws Exception {
    // Scenario 1. Execute DML before DDL.
    // a) For INSERT DML type:
    //    Transaction should conflict since insert operation
    //    takes a distributed transaction lock.
    // b) For SELECT DML type:
    //    Transaction should not conflict because select operation just
    //    picks a read time and doesn't take a distributed transaction lock.
    // Note when performing DML before DDL, there is no need to separately cache
    // the table metadata since caching will happen when executing DML query.
    for (AlterCommand alterType : AlterCommand.values()) {

      String[] expectedErrorOnInsert =  TRANSACTION_ABORT_ERRORS;
      if (alterType == AlterCommand.ATTACH_PARTITION) {
        expectedErrorOnInsert = new String[]{ NO_ERROR };
      }

      LOG.info("Run INSERT txn before ALTER " + alterType);
      runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, alterType, !cacheMetadataSetTrue,
          executeDmlBeforeAlter, expectedErrorOnInsert);
      LOG.info("Run SELECT txn before ALTER " + alterType);
      runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, alterType, !cacheMetadataSetTrue,
          executeDmlBeforeAlter, NO_ERROR);
    }
  }

  @Test
  public void testDmlTransactionAfterAlterOnCurrentResourceWithCachedMetadata() throws Exception {
    // Scenario 2. Execute any DML type after DDL.
    // a) For PG metadata cached table:
    //    Transaction should conflict since we are using
    //    the original schema for DML operation.
    for (AlterCommand alterType : AlterCommand.values()) {

      String expectedErrorOnInsert;
      if (alterType == AlterCommand.DROP_IDENTITY) {
        expectedErrorOnInsert = NO_SEQUENCE_FOUND_ERROR;
      } else if (alterType == AlterCommand.ATTACH_PARTITION) {
        expectedErrorOnInsert = NO_ERROR;
      } else {
        expectedErrorOnInsert = SCHEMA_VERSION_MISMATCH_ERROR;
      }
      String expectedErrorOnSelect;
      if (alterType == AlterCommand.ATTACH_PARTITION) {
        expectedErrorOnSelect = NO_ERROR;
      } else {
        expectedErrorOnSelect = SCHEMA_VERSION_MISMATCH_ERROR;
      }

      LOG.info("Run INSERT txn after ALTER " + alterType + " cache set " + cacheMetadataSetTrue);
      runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, alterType, cacheMetadataSetTrue,
          executeDmlAfterAlter, expectedErrorOnInsert);
      LOG.info("Run SELECT txn after ALTER " + alterType + " cache set " + cacheMetadataSetTrue);
      runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, alterType, cacheMetadataSetTrue,
          executeDmlAfterAlter, expectedErrorOnSelect);
    }
  }

  @Test
  public void testDmlTransactionAfterAlterOnCurrentResourceWithoutCachedMetadata()
      throws Exception {
    // Scenario 2. Execute any DML type after DDL.
    // b) For non PG metadata cached table:
    //    Transaction should not conflict because new schema is used for
    //    DML operation and the original schema is not already cached.

    // The test fails with Connection Manager as it is expected that a new
    // session would latch onto a new physical connection. Instead, two logical
    // connections use the same physical connection, leading to unexpected
    // results as per the expectations of the test.
    assumeFalse(BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED, isTestRunningWithConnectionManager());

    for (AlterCommand alterType : AlterCommand.values()) {
      LOG.info("Run INSERT txn after ALTER " + alterType + " cache set " + !cacheMetadataSetTrue);
      runDmlTxnWithAlterOnCurrentResource(Dml.INSERT, alterType, !cacheMetadataSetTrue,
          executeDmlAfterAlter, NO_ERROR);
      LOG.info("Run SELECT txn after ALTER " + alterType + " cache set " + !cacheMetadataSetTrue);
      runDmlTxnWithAlterOnCurrentResource(Dml.SELECT, alterType, !cacheMetadataSetTrue,
          executeDmlAfterAlter, NO_ERROR);
    }
  }

  @Test
  public void testMultipleDmlTransactionWithAlterOnCurrentResource() throws Exception {

    // The test fails with Connection Manager as it is expected that each new
    // session would latch onto a new physical connection. Instead, any two
    // logical connections share the same physical connection, leading to
    // unexpected results as per the expectations of the test.
    assumeFalse(BasePgSQLTest.UNIQUE_PHYSICAL_CONNS_NEEDED, isTestRunningWithConnectionManager());

    LOG.info("Run multiple transactions before altering the resource");
    runMultipleTxnsBeforeAlterTable();
    LOG.info("Run multiple transactions before and after altering the resource");
    runMultipleTxnsBeforeAndAfterAlterTable();
    LOG.info("Run alter with roll back and ensure table doesn't get deleted afterwards");
    runAlterWithRollbackToEnsureNoTableDeletion();
  }

  @Test
  public void testDmlTransactionWithAlterOnDifferentResource() throws Exception {
    LOG.info("Run insert transaction before/after altering another resource");
    AlterCommand addColumn = AlterCommand.ADD_COLUMN;
    runInsertTxnWithAlterOnUnrelatedResource(addColumn,
        cacheMetadataSetTrue, executeDmlBeforeAlter);
    runInsertTxnWithAlterOnUnrelatedResource(addColumn,
        cacheMetadataSetTrue, executeDmlAfterAlter);

    LOG.info("Run insert transaction before/after altering another resource");
    AlterCommand dropColumn = AlterCommand.DROP_COLUMN;
    runInsertTxnWithAlterOnUnrelatedResource(dropColumn,
        cacheMetadataSetTrue, executeDmlBeforeAlter);
    runInsertTxnWithAlterOnUnrelatedResource(dropColumn,
        cacheMetadataSetTrue, executeDmlAfterAlter);
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
