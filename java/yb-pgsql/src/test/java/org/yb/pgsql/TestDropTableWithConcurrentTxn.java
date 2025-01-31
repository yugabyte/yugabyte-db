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

import static org.junit.Assume.assumeFalse;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Collections;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestDropTableWithConcurrentTxn extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestDropTableWithConcurrentTxn.class);

  // When a transaction is performed on a table that is deleted,
  // "need_global_cache_refresh" is set to true and raise
  // transaction conflict error in YBPrepareCacheRefreshIfNeeded().
  private static final String[] TRANSACTION_ABORT_ERRORS =
      { "expired or aborted by a conflict", "Transaction aborted: kAborted" };
  private static final String RESOURCE_NONEXISTING_ERROR =
      "does not exist";
  private static final String SCHEMA_VERSION_MISMATCH_ERROR =
      "schema version mismatch for table";
  private static final String NO_ERROR = "";
  private static final boolean executeDmlBeforeDrop = true;
  private static final boolean executeDmlAfterDrop = false;
  private enum Resource { TABLE, INDEX, VIEW }
  private enum Dml { INSERT, SELECT }

  private void prepareResources(Resource resourceToDrop, String tableName) throws Exception {
    // Separate connection is used to create and load the table to avoid
    // caching the table before running the DML transaction.
    try (Connection connection = getConnectionBuilder().connect();
        Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE " + tableName + " (a INT PRIMARY KEY, b TEXT)");
      switch (resourceToDrop) {
        case TABLE: {
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo')");
          break;
        }
        case INDEX: {
          String indexName = tableName + "_index";
          statement.execute("CREATE INDEX " + indexName + " ON " + tableName + " (b)");
          statement.execute("INSERT INTO " + tableName + " VALUES (1, 'foo')");
          break;
        }
        case VIEW: {
          String viewName = tableName + "_view";
          statement.execute("CREATE VIEW " + viewName + " AS SELECT * FROM " + tableName);
          statement.execute("INSERT INTO " + viewName + " VALUES (1, 'foo')");
          break;
        }
        default: {
          throw new Exception("Resource type " + resourceToDrop + " not supported");
        }
      }
    }
  }

  private String getDropQuery(Resource resourceToDrop, String tableName) throws Exception {
    switch (resourceToDrop) {
      case TABLE: {
        return "DROP TABLE " + tableName;
      }
      case INDEX: {
        return "DROP INDEX " + tableName + "_index";
      }
      case VIEW: {
        return "DROP VIEW " + tableName + "_view";
      }
      default: {
        throw new Exception("Resource type " + resourceToDrop + " not supported");
      }
    }
  }

  private String getDmlQuery(Dml dmlToExecute, Resource resourceToDrop, String tableName)
      throws Exception {
    switch (dmlToExecute) {
      case INSERT: {
        switch (resourceToDrop) {
          case VIEW: {
            return "INSERT INTO " + tableName + "_view VALUES (2, 'bar')";
          }
          case TABLE:
          case INDEX: {
            return "INSERT INTO " + tableName + " VALUES (2, 'bar')";
          }
          default: {
            throw new Exception("Resource type " + resourceToDrop + " not supported");
          }
        }
      }
      case SELECT: {
        switch (resourceToDrop) {
          case VIEW: {
            return "SELECT * FROM " + tableName + "_view";
          }
          case TABLE:
          case INDEX: {
            return "SELECT * FROM " + tableName;
          }
          default: {
            throw new Exception("Resource type " + resourceToDrop + " not supported");
          }
        }
      }
      default: {
        throw new Exception("Resource type " + resourceToDrop + " not supported");
      }
    }
  }

  private void runDmlTxnWithDropOnCurrentResource(Dml dmlToExecute,
                                                  Resource resourceToDrop,
                                                  boolean withCachedMetadata,
                                                  boolean executeDmlBeforeDrop,
                                                  String... expectedErrorMessages)
      throws Exception {
    String tableName = dmlToExecute + "OnCurrentResource" +
        (executeDmlBeforeDrop ? "Before" : "After") + "DropOn" +
        (withCachedMetadata ? "PgCached" : "NonPgCached") + resourceToDrop;
    prepareResources(resourceToDrop, tableName);
    String resource = ((Resource.VIEW).equals(resourceToDrop)) ? tableName + "_view" : tableName;

    try (Connection txnConnection = getConnectionBuilder().connect();
        Statement statement1 = txnConnection.createStatement();
        Connection ddlConnection = getConnectionBuilder().connect();
        Statement statement2 = ddlConnection.createStatement()) {

      if (withCachedMetadata) {
        // Run query to load table and index metadata into the PgGate cache.
        statement1.execute("SELECT * FROM " + resource + " WHERE b like 'bar'");
      }

      statement1.execute("BEGIN");

      String dmlQuery = getDmlQuery(dmlToExecute, resourceToDrop, tableName);
      String dropQuery = getDropQuery(resourceToDrop, tableName);

      if (executeDmlBeforeDrop) {
        // execute DML in connection 1
        statement1.execute(dmlQuery);
        // execute DROP in connection 2
        statement2.execute(dropQuery);
        // execute COMMIT in connection 1
        if (expectedErrorMessages.length == 1 && expectedErrorMessages[0].isEmpty()) {
          statement1.execute("COMMIT");
        } else {
          runInvalidQuery(statement1, "COMMIT", expectedErrorMessages);
        }
      } else {
        // execute DROP in connection 2
        statement2.execute(dropQuery);
        // execute DML in connection 1
        if (expectedErrorMessages.length == 1 && expectedErrorMessages[0].isEmpty()) {
          statement1.execute(dmlQuery);
        } else {
          runInvalidQuery(statement1, dmlQuery, expectedErrorMessages);
        }
        // execute COMMIT in connection 1
        statement1.execute("COMMIT");
      }
    }
  }

  private void runInsertTxnWithDropOnUnrelatedResource(Resource resourceToDrop,
                                                       boolean withCachedMetadata,
                                                       boolean executeDmlBeforeDrop)
      throws Exception {
    String tableName = "insert" +
        (executeDmlBeforeDrop ? "Before" : "After") + "DropOn" +
        (withCachedMetadata ? "PgCached" : "NonPgCached") + "Unrelated" + resourceToDrop;
    String tableName2 = "dummyTableFor" + tableName;
    String resource = ((Resource.VIEW).equals(resourceToDrop)) ? tableName + "_view" : tableName;
    prepareResources(resourceToDrop, tableName);
    prepareResources(resourceToDrop, tableName2);

    try (Connection txnConnection = getConnectionBuilder().connect();
        Statement statement1 = txnConnection.createStatement();
        Connection ddlConnection = getConnectionBuilder().connect();
        Statement statement2 = ddlConnection.createStatement()) {

      if (withCachedMetadata) {
        // Run query to load table and index metadata into the PgGate cache.
        statement1.execute("SELECT * FROM " + resource + " WHERE b like 'bar'");
      }

      statement1.execute("BEGIN");

      String dropQuery = getDropQuery(resourceToDrop, tableName2);

      if (executeDmlBeforeDrop) {
        statement1.execute("INSERT INTO " + resource + " VALUES (5, 'foo')");
        statement2.execute(dropQuery);
      } else {
        statement2.execute(dropQuery);
        statement1.execute("INSERT INTO " + resource + " VALUES (5, 'foo')");
      }

      statement1.execute("COMMIT");
      ResultSet rs = statement1.executeQuery("SELECT * FROM " + resource);
      assertTrue(rs.next());
      assertEquals(5, rs.getInt(1));
    }
  }

  private void runMultipleTxnsBeforeDropTable() throws Exception {
    String tableName = "runMultipleTxnsBeforeDropTable";
    prepareResources(Resource.TABLE, tableName);

    try (Connection txnConnection = getConnectionBuilder().connect();
        Statement statement1 = txnConnection.createStatement();
        Connection ddlConnection = getConnectionBuilder().connect();
        Statement statement2 = ddlConnection.createStatement();
        Connection connection3 = getConnectionBuilder().connect();
        Statement statement3 = connection3.createStatement();
        Connection connection4 = getConnectionBuilder().connect();
        Statement statement4 = connection4.createStatement()) {

      statement1.execute("BEGIN");
      statement1.execute("INSERT INTO " + tableName + " VALUES (10, 'x')");

      statement3.execute("BEGIN");
      statement3.execute("INSERT INTO " + tableName + " VALUES (3, 'z')");

      statement4.execute("BEGIN");
      statement4.execute("INSERT INTO " + tableName + " VALUES (4, 'w')");
      statement4.execute("SELECT * FROM " + tableName);

      statement2.execute("DROP TABLE " + tableName);

      runInvalidQuery(statement1, "COMMIT", TRANSACTION_ABORT_ERRORS);
      runInvalidQuery(statement3, "COMMIT", TRANSACTION_ABORT_ERRORS);
      runInvalidQuery(statement4, "COMMIT", TRANSACTION_ABORT_ERRORS);
    }
  }

  private void runDmlTxnInSameConnectionAsDropTable() throws Exception {
    String tableName = "runDmlTxnInSameConnectionAsDropTable";
    prepareResources(Resource.TABLE, tableName);

    try (Connection connection = getConnectionBuilder().connect();
        Statement statement = connection.createStatement()) {

      statement.execute("BEGIN");
      statement.execute("INSERT INTO " + tableName + " VALUES (10, 'x')");
      statement.execute("SELECT * FROM " + tableName);

      statement.execute("DROP TABLE " + tableName);

      runInvalidQuery(statement, "COMMIT", TRANSACTION_ABORT_ERRORS);
    }
  }

  /*
  Test dimensions:
  -- DDL types (DROP TABLE)
  -- DML types INSERT vs. SELECT
  -- Perform DML before vs. after DDL
  -- PG metadata cached vs. non-cached

  Test restrictions:
  -- Always use filled (pre-populated) tables
  -- Always use a primary key when creating the table
  -- Always use snapshot isolation
  -- DML operation always affects exactly one row
  */
  public void testDmlTxnDropInternal() throws Exception {
    boolean withCachedMetadata = true;

    //------------------------------------------------------------------------------------------
    // Testing DROP TABLE
    //------------------------------------------------------------------------------------------
    Resource tableDrop = Resource.TABLE;
    LOG.info("Run INSERT transactions BEFORE drop");
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, tableDrop, !withCachedMetadata,
        executeDmlBeforeDrop, TRANSACTION_ABORT_ERRORS);
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, tableDrop, withCachedMetadata,
        executeDmlBeforeDrop, TRANSACTION_ABORT_ERRORS);
    LOG.info("Run INSERT transactions AFTER drop");
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, tableDrop, !withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, tableDrop, withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    LOG.info("Run SELECT transactions AFTER drop");
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, tableDrop, !withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, tableDrop, withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    // For SELECT before DDL on either cached or non-cached table
    // transaction should not conflict because select operation just
    // picks a read time and doesn't take a distributed transaction lock.
    LOG.info("Run SELECT transactions BEFORE drop");
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, tableDrop, !withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, tableDrop, withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);

    LOG.info("Run INSERT transaction BEFORE drop on another resource");
    runInsertTxnWithDropOnUnrelatedResource(tableDrop, !withCachedMetadata, executeDmlBeforeDrop);
    LOG.info("Run INSERT transaction AFTER drop on another resource");
    runInsertTxnWithDropOnUnrelatedResource(tableDrop, !withCachedMetadata, executeDmlAfterDrop);
    runInsertTxnWithDropOnUnrelatedResource(tableDrop, withCachedMetadata, executeDmlAfterDrop);
    LOG.info("Run multiple transaction BEFORE drop");
    runMultipleTxnsBeforeDropTable();
    runDmlTxnInSameConnectionAsDropTable();

    //------------------------------------------------------------------------------------------
    // Testing DROP INDEX
    //------------------------------------------------------------------------------------------
    Resource indexDrop = Resource.INDEX;
    LOG.info("Run INSERT transactions BEFORE drop");
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, indexDrop, !withCachedMetadata,
        executeDmlBeforeDrop, TRANSACTION_ABORT_ERRORS);
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, indexDrop, withCachedMetadata,
        executeDmlBeforeDrop, TRANSACTION_ABORT_ERRORS);
    LOG.info("Run INSERT transaction AFTER drop");
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, indexDrop, withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    // Postgres load relation's index info on cache refresh, as a result even without explicit index
    // usage the error will be the same as with index usage.
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, indexDrop, !withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    LOG.info("Run SELECT transaction AFTER drop");
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, indexDrop, withCachedMetadata,
        executeDmlAfterDrop, SCHEMA_VERSION_MISMATCH_ERROR);
    // For SELECT before DDL on either cached or non-cached table
    // transaction should not conflict because select operation just
    // picks a read time and doesn't take a distributed transaction lock.
    LOG.info("Run SELECT transaction BEFORE drop");
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, indexDrop, !withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, indexDrop, withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);
    LOG.info("Run SELECT transaction AFTER drop");
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, indexDrop, !withCachedMetadata,
        executeDmlAfterDrop, NO_ERROR);

    LOG.info("Run INSERT transaction BEFORE drop on another resource");
    runInsertTxnWithDropOnUnrelatedResource(indexDrop, !withCachedMetadata, executeDmlBeforeDrop);
    LOG.info("Run INSERT transaction AFTER drop on another resource");
    runInsertTxnWithDropOnUnrelatedResource(indexDrop, !withCachedMetadata, executeDmlAfterDrop);
    runInsertTxnWithDropOnUnrelatedResource(indexDrop, withCachedMetadata, executeDmlAfterDrop);

    //------------------------------------------------------------------------------------------
    // Testing DROP VIEW
    //------------------------------------------------------------------------------------------
    Resource viewDrop = Resource.VIEW;
    LOG.info("Run INSERT transaction BEFORE drop");
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, viewDrop, !withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, viewDrop, withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);
    LOG.info("Run SELECT transaction BEFORE drop");
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, viewDrop, !withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, viewDrop, withCachedMetadata,
        executeDmlBeforeDrop, NO_ERROR);
    LOG.info("Run INSERT transaction AFTER drop");
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, viewDrop, !withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.INSERT, viewDrop, withCachedMetadata,
        executeDmlAfterDrop, NO_ERROR);
    LOG.info("Run SELECT transaction AFTER drop");
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, viewDrop, !withCachedMetadata,
        executeDmlAfterDrop, RESOURCE_NONEXISTING_ERROR);
    runDmlTxnWithDropOnCurrentResource(Dml.SELECT, viewDrop, withCachedMetadata,
        executeDmlAfterDrop, NO_ERROR);

    LOG.info("Run INSERT transaction BEFORE drop on another resource");
    runInsertTxnWithDropOnUnrelatedResource(viewDrop, !withCachedMetadata, executeDmlBeforeDrop);
    LOG.info("Run INSERT transaction AFTER drop on another resource");
    runInsertTxnWithDropOnUnrelatedResource(viewDrop, !withCachedMetadata, executeDmlAfterDrop);
    runInsertTxnWithDropOnUnrelatedResource(viewDrop, withCachedMetadata, executeDmlAfterDrop);
  }

  @Test
  public void testDmlTxnDrop() throws Exception {
    assumeFalse(BasePgSQLTest.CANNOT_GURANTEE_EXPECTED_PHYSICAL_CONN_FOR_CACHE,
                isTestRunningWithConnectionManager());
    testDmlTxnDropInternal();
  }

  @Test
  public void testDmlTxnDropWithReadCommitted() throws Exception {
    assumeFalse(BasePgSQLTest.CANNOT_GURANTEE_EXPECTED_PHYSICAL_CONN_FOR_CACHE,
                isTestRunningWithConnectionManager());
    restartClusterWithFlags(Collections.emptyMap(),
                            Collections.singletonMap("yb_enable_read_committed_isolation",
                                                     "true"));
    testDmlTxnDropInternal();
  }
}
