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

@RunWith(value = YBTestRunnerYsqlConnMgr.class)
public class TestDropAndRenameRole extends BaseYsqlConnMgr {
  private static final String GET_CURRENT_ROLE_QUERY = "SELECT current_user";

  private static final String TEST_ROLE_DROP = "test_role_drop_fails";
  private static final String TEST_RENAME_ROLE_WITH_NO_PHY_CONN =
        "test_rename_role_with_no_phy_conn";
  private static final String TEST_RENAME_ROLE_WITH_PHY_CONN = "test_rename_role_with_phy_conn";
  private static final String TEST_ROLE_RECREATE_WITH_PHY_CONN =
        "test_role_recreate_with_phy_conn";
  private static final String TEST_ROLE_RECREATE_WITH_NO_PHY_CONN =
        "test_role_recreate_with_no_phy_conn";
  private static final String ROLE_PREFIX_CONCURRENCY_TEST = "test_role_concurrency";

  private static final int NUM_THREADS_ROLE_CONCURRENCY_TESTING = 30;
  private static final int NUM_ROLE_CONCURRENCY_TESTING = 5;
  private static final int NUM_RETRIES = 10;

  protected void waitForRoleInfoToGetUpdatedAcrossCluster() {
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
  }

  private void dropRole(String roleName, boolean shouldSucceed) {
    try (Connection conn = getConnectionBuilder()
            .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR).connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP ROLE " + roleName);
      assertTrue("Executed 'DROP ROLE' query successfully, but it was expected to fail",
          shouldSucceed);
    } catch (Exception e) {
      LOG.info("Got exception", e);
      assertFalse("DROP ROLE query failed", shouldSucceed);
    }
  }

  private void createRole(String roleName) {
    try (Connection conn = getConnectionBuilder().connect();
        Statement stmt = conn.createStatement()) {
      stmt.execute(String.format("CREATE ROLE %s LOGIN", roleName));
      LOG.info("Created the role " + roleName);
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  protected Connection createAndConnectWithRole(String roleName, int tserverIndex)
      throws InterruptedException, SQLException {
    final String expectedErrMsg = String.format("FATAL: role \"%s\" does not exist", roleName);
    Connection conn = null;
    createRole(roleName);
    for (int retry = 1; retry < NUM_RETRIES; ++retry) {
      Thread.sleep(retry * 1000);

      try {
        conn = getConnectionBuilder()
                  .withUser(roleName)
                  .withTServer(tserverIndex)
                  .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                  .connect();
        LOG.info("Created connection to the database successfully");
        return conn;
      } catch (Exception e) {
        assertEquals("Unexpected error message, ", e.getMessage(), expectedErrMsg);
        LOG.error("Got the error while creating the connection", e);
      }
    }

    fail("Unable to connect to the database");
    return null;
  }

  private static void validateRoleName(Connection conn, String expectedRoleName) {
    try (Statement stmt = conn.createStatement();
        ResultSet rs = stmt.executeQuery(GET_CURRENT_ROLE_QUERY)) {
      assertTrue(rs.next());
      assertEquals("Mismatch in expected role name", expectedRoleName, rs.getString(1));
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  private int getRoleOid(String roleName) {
    try (Connection conn = getConnectionBuilder().connect();
        Statement stmt = conn.createStatement()) {
          ResultSet rs = stmt.executeQuery(
            "SELECT oid FROM pg_roles WHERE rolname = '" + roleName + "'");
          assertTrue(rs.next());
          return rs.getInt("oid");
    } catch (Exception e) {
      LOG.error("Unexpected exception", e);
      fail();
    }
    return -1;
  }

  // Test the effect of DROP + CREATE role on an already existing connection
  // using that role.
  // Here this existing connection is made on a different node because if there is
  // a connection using a role on the same node, then the DROP role query on
  // that role will fail.
  private void testRecreateRole(String roleName, Boolean executeQueryBeforeDropRole) {
    try (Connection conn = createAndConnectWithRole(roleName, 1)) {
      int oldRoleOid = getRoleOid(roleName);
      if (executeQueryBeforeDropRole) {
        executeQuery(conn, "SELECT 1", true);
        validateRoleName(conn, roleName);
      }

      // DROP ROLE on a different node should succeed
      dropRole(roleName, true);
      waitForRoleInfoToGetUpdatedAcrossCluster();

      testConnectivityUsingRole(roleName, false, false);

      // Recreate the role
      createRole(roleName);
      waitForRoleInfoToGetUpdatedAcrossCluster();

      int newRoleOid = getRoleOid(roleName);
      assertNotEquals(newRoleOid, oldRoleOid);
      testConnectivityUsingRole(roleName, true, true);

    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  private void testRecreateRoleWithPhysicalConnection() {
    testRecreateRole(TEST_ROLE_RECREATE_WITH_PHY_CONN, true);
  }

  private void testRecreateRoleWithNoPhysicalConnection() {
    testRecreateRole(TEST_ROLE_RECREATE_WITH_NO_PHY_CONN, false);
  }

  @Test
  public void testRecreateRole() {
    testRecreateRoleWithNoPhysicalConnection();
    testRecreateRoleWithPhysicalConnection();
  }

  private void renameRole(String old_role, String new_role) {
    try (Connection conn = getConnectionBuilder().connect();
        Statement stmt = conn.createStatement()) {
      int oldOid = getRoleOid(old_role);
      stmt.execute("ALTER ROLE " + old_role + " RENAME TO " + new_role);
      int newOid = getRoleOid(new_role);
      assertEquals(oldOid, newOid);
    } catch (Exception e) {
      LOG.error("Got exception", e);
      fail();
    }
  }

  private void testConnectivityUsingRole(String roleName, boolean isAccessible,
                                        boolean checkRoleName) {
    try (Connection conn = getConnectionBuilder()
                              .withUser(roleName)
                              .withTServer(1)
                              .withConnectionEndpoint(ConnectionEndpoint.YSQL_CONN_MGR)
                              .connect()) {
      assertTrue("Unexpected behaviour, client is able to connect using role " + roleName,
          isAccessible);

      // validateRoleName() will lead to creation of a physical connection in the pool.
      if (checkRoleName)
        validateRoleName(conn, roleName);

    } catch (Exception e) {
      assertFalse("Unexpected behaviour, client is unable to connect using role " + roleName,
          isAccessible);
    }
  }

  // Test the effect of RENAME role on an already existing connection
  // using that role.
  // Here this existing conneciton is made on a different node because if there is
  // a connection to a database on the same node, then the RENAME role query on
  // that node will fail.
  private void testRenameRole(String roleName, boolean executeQueryBeforeRenameRole) {
    final String renamedRoleName = roleName + "_new";
    try (Connection oldConn = createAndConnectWithRole(roleName, 1)) {
      if (executeQueryBeforeRenameRole)
        executeQuery(oldConn, "SELECT 1", true);

      renameRole(roleName, renamedRoleName);
      waitForRoleInfoToGetUpdatedAcrossCluster();

      if (executeQueryBeforeRenameRole)
        validateRoleName(oldConn, renamedRoleName);

      // New connections using the old role should fail
      testConnectivityUsingRole(roleName, false, false);

      // Connections using the renamed role should succeed
      testConnectivityUsingRole(renamedRoleName, true, true);

      // Recreate role
      createRole(roleName);
      waitForRoleInfoToGetUpdatedAcrossCluster();

      testConnectivityUsingRole(roleName, true, true);

      // Connections using the renamed role sould succeed
      testConnectivityUsingRole(renamedRoleName, true, true);

      // The old connection should be usable
      executeQuery(oldConn, "SELECT 1", true);
      validateRoleName(oldConn, renamedRoleName);
    } catch (Exception e) {
      LOG.error("Got an exception", e);
      fail();
    }
  }

  private void testRenameRoleWithNoPhysicalConnection() {
    testRenameRole(TEST_RENAME_ROLE_WITH_NO_PHY_CONN, false);
  }

  private void testRenameRoleWithPhysicalConnection() {
    testRenameRole(TEST_RENAME_ROLE_WITH_PHY_CONN, true);
  }

  @Test
  public void testRenameWithRecreateRole() throws Exception{
    disableWarmupModeAndRestartCluster();
    testRenameRoleWithNoPhysicalConnection();
    testRenameRoleWithPhysicalConnection();
  }

  class TestConcurrentlyAfterCreateRole extends TestConcurrently {
    public TestConcurrentlyAfterCreateRole(String roleName) {
      super(roleName);
    }

    @Override
    public void run() {
      testConnectivityUsingRole(oidObjName, true, true);
      testSuccess = true;
    }
  }

  class TestConcurrentlyAfterRoleDrop extends TestConcurrently {
    public TestConcurrentlyAfterRoleDrop(String roleName) {
      super(roleName);
    }

    @Override
    public void run() {
      testConnectivityUsingRole(oidObjName, false, false);
      testSuccess = true;
    }
  }

  private void testConcurrentOperations(TestConcurrently testRoleRunnable[], int numThreads) {
    Thread testDb[] = new Thread[numThreads];
    for (int i = 0; i < numThreads; i++)
      testDb[i] = new Thread(testRoleRunnable[i]);

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
      assertTrue(testRoleRunnable[i].testSuccess);
  }

  private void testConcurrentCreateRole(String rolePrefix, int numThreads, int numRoles) {
    TestConcurrently createRoleRunnable[] = new TestConcurrently[numThreads];

    for (int i = 0; i < numRoles; i++)
      createRole(rolePrefix + Integer.toString(i));

    waitForRoleInfoToGetUpdatedAcrossCluster();

    for (int i = 0; i < numThreads; i++)
      createRoleRunnable[i] =
        new TestConcurrentlyAfterCreateRole(rolePrefix + Integer.toString(i % numRoles));

    testConcurrentOperations(createRoleRunnable, numThreads);
  }

  private void testConcurrentDropRole(String rolePrefix, int numThreads, int numRoles) {
    TestConcurrently dropRoleRunnable[] = new TestConcurrently[numThreads];

    for (int i = 0; i < numRoles; i++)
      dropRole(rolePrefix + Integer.toString(i), true);

    for (int i = 0; i < numThreads; i++)
      dropRoleRunnable[i] =
        new TestConcurrentlyAfterRoleDrop(rolePrefix + Integer.toString(i % numRoles));

    testConcurrentOperations(dropRoleRunnable, numThreads);
  }

  @Test
  public void TestConcurrentOperations() throws InterruptedException {
    testConcurrentCreateRole(ROLE_PREFIX_CONCURRENCY_TEST,
        NUM_THREADS_ROLE_CONCURRENCY_TESTING, NUM_ROLE_CONCURRENCY_TESTING);
    BasePgSQLTest.waitForStatsToGetUpdated();
    testConcurrentDropRole(ROLE_PREFIX_CONCURRENCY_TEST,
        NUM_THREADS_ROLE_CONCURRENCY_TESTING, NUM_ROLE_CONCURRENCY_TESTING);
  }
}
