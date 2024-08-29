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

import static org.junit.Assume.assumeFalse;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.pgsql.BasePgSQLTest;
import org.yb.pgsql.ConnectionEndpoint;
import org.yb.util.BuildTypeUtil;

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestDropAndRenameDb extends BaseYsqlConnMgr {
  private static final String GET_CURRENT_DB_QUERY = "SELECT current_database()";

  private static final String TEST_DB_DROP = "test_db_drop_database_fails";
  private static final String TEST_DB_RENAME_DB_WITH_NO_PHY_CONN =
      "test_db_rename_db_with_no_phy_conn";
  private static final String TEST_DB_RENAME_DB_WITH_PHY_CONN = "test_db_rename_db_with_phy_conn";
  private static final String TEST_DB_RECREATE_WITH_PHY_CONN = "test_db_recreate_with_phy_conn";
  private static final String TEST_DB_RECREATE_WITH_NO_PHY_CONN =
      "test_db_recreate_with_no_phy_conn";
  private static final String DB_PREFIX_CONCURRENCY_TEST = "test_db_concurrency";
  private static final String TEST_DB_RENAME_DB_FAILS = "test_db_name_rename_fails";
  private static final String TEST_DB_MOVE_DB_FAILS = "test_db_name_move_db_fails";

  private static final int NUM_THREADS_DB_CONCURRENCY_TESTING = 30;
  private static final int NUM_DB_CONCURRENCY_TESTING = 5;
  private static final int NUM_RETRIES = 10;

  protected void waitForDbInfoToGetUpdatedAcrossCluster() {
    try {
      Thread.sleep(MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS * 4);
    } catch (Exception e) {
      LOG.error("Unexpected error", e);
      fail();
    }
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);

    builder.addCommonTServerFlag("ysql_conn_mgr_stats_interval",
        Integer.toString(BasePgSQLTest.CONNECTIONS_STATS_UPDATE_INTERVAL_SECS));

    builder.addCommonTServerFlag (
        "TEST_ysql_conn_mgr_dowarmup_all_pools_random_attach", "true");
  }

  private void executeDDL(String query, boolean shouldSucceed) {
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute(query);
      assertTrue("Executed '" + query +"' successfully, but it was expected to fail",
          shouldSucceed);
    } catch (Exception e) {
      LOG.info("Got exception", e);
      assertFalse(query + " query failed", shouldSucceed);
    }
  }

  private void dropDatabase(String dbName, boolean shouldSucceed) {
    executeDDL("DROP DATABASE " + dbName, shouldSucceed);
  }

  private void createDb(String dbName) {
    try (Connection conn = getConnectionBuilder().connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s", dbName));
      LOG.info("Created the database " + dbName);
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  protected Connection createAndConnectOnDb(String dbName, int tserverIndex)
      throws InterruptedException, SQLException {
    final String expectedErrMsg = String.format("FATAL: database \"%s\" does not exist", dbName);
    Connection conn = null;
    createDb(dbName);
    for (int retry = 1; retry < NUM_RETRIES; ++retry) {
      Thread.sleep(retry * 1000);

      try {
        conn = getConnectionBuilder()
                  .withDatabase(dbName)
                  .withTServer(tserverIndex)
                  .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                  .connect();
        LOG.info("Created connection to the database successfully");
        return conn;
      } catch (Exception e) {
        assertEquals("Unexpected error message, ", e.getMessage(), expectedErrMsg);
        LOG.error("Got the error while creating the connection", e);
      }

      if (conn != null)
        conn.close();
    }

    fail("Unable to connect to the database");
    return null;
  }

  private static void validateDbName(Connection conn, String expectedDbName) {
    try (Statement stmt = conn.createStatement();
        ResultSet rs = stmt.executeQuery(GET_CURRENT_DB_QUERY)) {
      assertTrue(rs.next());
      assertEquals("Mismatch in expected db name", expectedDbName, rs.getString(1));
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  // GH #20581: DROP DATABASE DB_1 query should fail if there is a client connection to the database
  // DB_1 on the same node.
  // This test ensures that:
  // a. DROP DATABASE DB_1 fails when there is a logical connection to the database
  //    DB_1 but no physical connection.
  // b. DROP DATABASE DB_1 should succeed when there is no logical connection to the database
  //    DB_1 but there is a physical connection to the database.
  @Test
  public void testDropDbFail() throws InterruptedException {
    // Create a logical connection and try dropping the database
    try (Connection conn = createAndConnectOnDb(TEST_DB_DROP, 0) ;
        Statement stmt = conn.createStatement()) {
      dropDatabase(TEST_DB_DROP, false);

      // Run a query on the logical connection so that the pool has atleast 1
      // physical connection.
      stmt.execute("SELECT 1");
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }

    BasePgSQLTest.waitForStatsToGetUpdated();
    dropDatabase(TEST_DB_DROP, true);
  }

  private static void executeQuery(Connection conn, String query, boolean shouldSucceed) {
    try (Statement stmt = conn.createStatement()) {
      ResultSet rs = stmt.executeQuery(query);
      assertEquals(shouldSucceed, rs.next());
      assertTrue("Expected the query to fail", shouldSucceed);
    } catch (Exception e) {
      LOG.info("Got an exception while executing the query", e);
      assertFalse("Expected the query to succeed", shouldSucceed);
    }
  }

  private int getDbOid(String dbName) {
    try (Connection conn = getConnectionBuilder().connect();
        Statement stmt = conn.createStatement()) {
          ResultSet rs = stmt.executeQuery(
            "SELECT oid FROM pg_database WHERE datname = '" + dbName + "'");
          assertTrue(rs.next());
          return rs.getInt("oid");
    } catch (Exception e) {
      LOG.error("Unexpected exception", e);
      fail();
    }
    return -1;
  }

  // Test the effect of DROP + CREATE database on an already existing connection
  // to that database.
  // Here this existing conneciton is made on a different node because if there is
  // a connection to a database on the same node, then the DROP database query on
  // that database will fail.
  private void testRecreateDb(String dbName, Boolean executeQueryBeforeDropDb) {
    try (Connection conn = createAndConnectOnDb(dbName, 1)) {
      int oldDbOid = getDbOid(dbName);
      if (executeQueryBeforeDropDb) {
        executeQuery(conn, "SELECT 1", true);
        validateDbName(conn, dbName);
      }

      // DROP DATABASE on a different node should succeed
      dropDatabase(dbName, true);
      testConnectivityToDb(dbName, false, false);

      // Recreate the db
      createDb(dbName);
      waitForDbInfoToGetUpdatedAcrossCluster();

      int newDbOid = getDbOid(dbName);
      assertNotEquals(newDbOid, oldDbOid);
      testConnectivityToDb(dbName, true, true);

      // TODO(janand) gh#21574 client is able to execute the query on detective env
      // executeQuery(conn, GET_CURRENT_DB_QUERY, false);
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  private void testRecreateDbWithPhysicalConnection() {
    testRecreateDb(TEST_DB_RECREATE_WITH_PHY_CONN, true);
  }

  private void testRecreateDbWithNoPhysicalConnection() {
    testRecreateDb(TEST_DB_RECREATE_WITH_NO_PHY_CONN, false);
  }

  @Test
  public void testRecreateDb() {
    testRecreateDbWithNoPhysicalConnection();
    testRecreateDbWithPhysicalConnection();
  }

  private void renameDb(String oldDb, String newDb, boolean shouldSucceed) {
    executeDDL("ALTER DATABASE " + oldDb + " RENAME TO " + newDb, shouldSucceed);
  }

  private void renameDb(String oldDb, String newDb) {
    renameDb(oldDb, newDb, true);
  }

  private void testConnectivityToDb(String dbName, boolean isAccessible, boolean checkDbName) {
    try (Connection conn = getConnectionBuilder()
                              .withDatabase(dbName)
                              .withTServer(1)
                              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .connect()) {
      assertTrue("Unexpected behaviour, client is able to connect to database " + dbName,
          isAccessible);

      // validateDbName() will lead to creation of a physical connection in the pool.
      if (checkDbName)
        validateDbName(conn, dbName);

    } catch (Exception e) {
      assertFalse("Unexpected behaviour, client is unable to connect to database " + dbName,
          isAccessible);
    }
  }

  // Test the effect of RENAME database on an already existing connection
  // to that database.
  // Here this existing conneciton is made on a different node because if there is
  // a connection to a database on the same node, then the RENAME database query on
  // that database will fail.
  private void testRenameDb(String dbName, boolean executeQueryBeforeRenameDb) {
    final String renamedDbName = dbName + "_new";
    try (Connection oldConn = createAndConnectOnDb(dbName, 1)) {
      if (executeQueryBeforeRenameDb)
        executeQuery(oldConn, "SELECT 1", true);

      renameDb(dbName, renamedDbName);
      waitForDbInfoToGetUpdatedAcrossCluster();

      if (executeQueryBeforeRenameDb)
        validateDbName(oldConn, renamedDbName);

      // New connections to the old db should fail
      testConnectivityToDb(dbName, false, false);

      // Connections to the renamed db should succeed
      testConnectivityToDb(renamedDbName, true, true);

      // Recreate db
      createDb(dbName);
      waitForDbInfoToGetUpdatedAcrossCluster();

      testConnectivityToDb(dbName, true,true);

      // Connections to the renamed db sould succeed
      testConnectivityToDb(renamedDbName, true, true);

      // The old connection should be us able
      executeQuery(oldConn, "SELECT 1", true);
      validateDbName(oldConn, renamedDbName);
    } catch (Exception e) {
      LOG.error("Got an exception", e);
      fail();
    }
  }

  private void testRenameDbWithNoPhysicalConnection() {
    testRenameDb(TEST_DB_RENAME_DB_WITH_NO_PHY_CONN, false);
  }

  private void testRenameDbWithPhysicalConnection() {
    testRenameDb(TEST_DB_RENAME_DB_WITH_PHY_CONN, true);
  }

  @Test
  public void testRenameWithRecreateDb() {
    assumeFalse(BaseYsqlConnMgr.DISABLE_TEST_WITH_ASAN, BuildTypeUtil.isASAN());
    testRenameDbWithNoPhysicalConnection();
    testRenameDbWithPhysicalConnection();
  }

  class TestConcurrentlyOnCreateDb extends TestConcurrently {
    public TestConcurrentlyOnCreateDb(String dbName) {
      super(dbName);
    }

    @Override
    public void run() {
      testConnectivityToDb(dbName, true, true);
      testSuccess = true;
    }
  }

  class TestConcurrentlyOnDbDrop extends TestConcurrently {
    public TestConcurrentlyOnDbDrop(String dbName) {
      super(dbName);
    }

    @Override
    public void run() {
      testConnectivityToDb(dbName, false, false);
      testSuccess = true;
    }
  }

  private void testConcurrentOperations(TestConcurrently testDbRunnable[], int numThreads) {
    Thread testDb[] = new Thread[numThreads];
    for (int i = 0; i < numThreads; i++)
      testDb[i] = new Thread(testDbRunnable[i]);

    for (int i = 0; i < numThreads; i++)
      testDb[i].start();

    try {
      for (int i = 0; i < numThreads; i++)
        testDb[i].join();
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }

    for (int i = 0; i < numThreads; i++)
      assertTrue(testDbRunnable[i].testSuccess);
  }

  private void testConcurrentCreateDb(String dbPrefix, int numThreads, int numDbs) {
    TestConcurrently createDbRunnable[] = new TestConcurrently[numThreads];

    for (int i = 0; i < numDbs; i++)
      createDb(dbPrefix + Integer.toString(i));

    waitForDbInfoToGetUpdatedAcrossCluster();

    for (int i = 0; i < numThreads; i++)
      createDbRunnable[i] = new TestConcurrentlyOnCreateDb(dbPrefix + Integer.toString(i % numDbs));

    testConcurrentOperations(createDbRunnable, numThreads);
  }

  private void testConcurrentDropDb(String dbPrefix, int numThreads, int numDbs) {
    TestConcurrently dropDbRunnable[] = new TestConcurrently[numThreads];

    for (int i = 0; i < numDbs; i++)
      dropDatabase(dbPrefix + Integer.toString(i), true);

    for (int i = 0; i < numThreads; i++)
      dropDbRunnable[i] = new TestConcurrentlyOnDbDrop(dbPrefix + Integer.toString(i % numDbs));

    testConcurrentOperations(dropDbRunnable, numThreads);
  }

  @Test
  public void TestConcurrentOperations() throws InterruptedException {
    testConcurrentCreateDb(DB_PREFIX_CONCURRENCY_TEST,
        NUM_THREADS_DB_CONCURRENCY_TESTING, NUM_DB_CONCURRENCY_TESTING);
    BasePgSQLTest.waitForStatsToGetUpdated();
    testConcurrentDropDb(DB_PREFIX_CONCURRENCY_TEST,
        NUM_THREADS_DB_CONCURRENCY_TESTING, NUM_DB_CONCURRENCY_TESTING);
  }

  @Test
  public void testRenameDbFails() throws SQLException, InterruptedException {
    String newDb = TEST_DB_RENAME_DB_FAILS + "_new";
    try (Connection conn = createAndConnectOnDb(TEST_DB_RENAME_DB_FAILS, 0)) {
      renameDb(TEST_DB_RENAME_DB_FAILS, newDb, false);
    }

    BasePgSQLTest.waitForStatsToGetUpdated();
    renameDb(TEST_DB_RENAME_DB_FAILS, newDb, true);

    testConnectivityToDb(TEST_DB_RENAME_DB_FAILS, false, false);
    testConnectivityToDb(newDb, true, true);
  }
}
