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
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;
import java.util.List;
import java.util.Map;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.util.PSQLException;

import org.yb.CommonNet.CloudInfoPB;
import org.yb.client.LeaderStepDownResponse;
import org.yb.client.LocatedTablet;
import org.yb.client.ModifyClusterConfigLiveReplicas;
import org.yb.client.ModifyClusterConfigReadReplicas;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.YBTestRunner;

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;

@RunWith(value = YBTestRunner.class)
public class TestTablespaceProperties extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestTablespaceProperties.class);

  ArrayList<String> tablesWithDefaultPlacement = new ArrayList<String>();
  ArrayList<String> tablesWithCustomPlacement = new ArrayList<String>();

  private static final int MASTER_REFRESH_TABLESPACE_INFO_SECS = 2;

  private static final int MASTER_LOAD_BALANCER_WAIT_TIME_MS = 60 * 1000;

  private static final int LOAD_BALANCER_MAX_CONCURRENT = 10;

  private static final String tablespaceName = "testTablespace";

  private static final List<Map<String, String>> perTserverZonePlacementFlags = Arrays.asList(
      ImmutableMap.of(
          "placement_cloud", "cloud1",
          "placement_region", "region1",
          "placement_zone", "zone1"),
      ImmutableMap.of(
          "placement_cloud", "cloud2",
          "placement_region", "region2",
          "placement_zone", "zone2"),
      ImmutableMap.of(
          "placement_cloud", "cloud3",
          "placement_region", "region3",
          "placement_zone", "zone3"));

  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(1500, 1700, 2000, 2000, 2000);
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
    builder.addMasterFlag("vmodule", "sys_catalog=5,cluster_balance=1");
    builder.addMasterFlag("ysql_tablespace_info_refresh_secs",
                          Integer.toString(MASTER_REFRESH_TABLESPACE_INFO_SECS));
    builder.addMasterFlag("auto_create_local_transaction_tables", "true");
    builder.addMasterFlag("TEST_name_transaction_tables_with_tablespace_id", "true");

    // We wait for the load balancer whenever it gets triggered anyways, so there's
    // no concerns about the load balancer taking too many resources.
    builder.addMasterFlag("load_balancer_max_concurrent_tablet_remote_bootstraps",
                          Integer.toString(LOAD_BALANCER_MAX_CONCURRENT));
    builder.addMasterFlag("load_balancer_max_concurrent_tablet_remote_bootstraps_per_table",
                          Integer.toString(LOAD_BALANCER_MAX_CONCURRENT));
    builder.addMasterFlag("load_balancer_max_concurrent_adds",
                          Integer.toString(LOAD_BALANCER_MAX_CONCURRENT));
    builder.addMasterFlag("load_balancer_max_concurrent_removals",
                          Integer.toString(LOAD_BALANCER_MAX_CONCURRENT));
    builder.addMasterFlag("load_balancer_max_concurrent_moves",
                          Integer.toString(LOAD_BALANCER_MAX_CONCURRENT));
    builder.addMasterFlag("load_balancer_max_concurrent_moves_per_table",
                          Integer.toString(LOAD_BALANCER_MAX_CONCURRENT));

    builder.perTServerFlags(perTserverZonePlacementFlags);
  }

  @Before
  public void setupTablespaces() throws Exception {
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute("DROP TABLESPACE IF EXISTS " + tablespaceName);
      setupStatement.execute(
          " CREATE TABLESPACE " + tablespaceName +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}," +
          "{\"cloud\":\"cloud2\",\"region\":\"region2\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");
    }
  }

  private void createTestData (String prefixName) throws Exception {
    // Setup tables.
    String defaultTable = prefixName + "_default_table";
    String customTable = prefixName + "_custom_table";
    String defaultIndex = prefixName + "_default_index";
    String customIndex = prefixName + "_custom_index";
    String defaultTablegroup = prefixName + "_default_tablegroup";
    String customTablegroup = prefixName + "_custom_tablegroup";
    String tableInDefaultTablegroup = prefixName + "_table_in_default_tablegroup";
    String tableInCustomTablegroup = prefixName + "_table_in_custom_tablegroup";
    String defaultIndexCustomTable = prefixName + "_default_idx_on_custom_table";
    String customIndexCustomTable = prefixName + "_custom_idx_on_custom_table";
    try (Statement setupStatement = connection.createStatement()) {
      // Create tablegroups in default and custom tablegroups
      setupStatement.execute("CREATE TABLEGROUP " +  customTablegroup +
          " TABLESPACE testTablespace");
      setupStatement.execute("CREATE TABLEGROUP " +  defaultTablegroup);

      // Create tables in default and custom tablespaces.
      setupStatement.execute(
          "CREATE TABLE " +  customTable + "(a SERIAL) TABLESPACE testTablespace");

      setupStatement.execute(
          "CREATE TABLE " + defaultTable + "(a int CONSTRAINT " + customIndex +
          " UNIQUE USING INDEX TABLESPACE testTablespace)");

      // Create indexes in default and custom tablespaces.
      setupStatement.execute("CREATE INDEX " + defaultIndexCustomTable + " on " +
          customTable + "(a)");

      setupStatement.execute("CREATE INDEX " + customIndexCustomTable + " on " +
          customTable + "(a) TABLESPACE testTablespace");

      setupStatement.execute("CREATE INDEX " + defaultIndex + " on " +
          defaultTable + "(a)");

      // Create tables in tablegroups (in default and custom tablespaces)
      setupStatement.execute(
        "CREATE TABLE " +  tableInDefaultTablegroup + "(a SERIAL) TABLEGROUP " + defaultTablegroup);

      setupStatement.execute(
        "CREATE TABLE " +  tableInCustomTablegroup + "(a SERIAL) TABLEGROUP " + customTablegroup);
    }
    String transactionTableName = getTablespaceTransactionTableName();
    tablesWithDefaultPlacement.clear();
    tablesWithDefaultPlacement.addAll(Arrays.asList(defaultTable, defaultIndex,
          defaultIndexCustomTable, tableInDefaultTablegroup));

    tablesWithCustomPlacement.clear();
    tablesWithCustomPlacement.addAll(Arrays.asList(customTable, customIndex,
          customIndexCustomTable, tableInCustomTablegroup, transactionTableName));
  }

  private void addTserversAndWaitForLB() throws Exception {
    int expectedTServers = miniCluster.getTabletServers().size() + 1;
    miniCluster.startTServer(perTserverZonePlacementFlags.get(1));
    miniCluster.waitForTabletServers(expectedTServers);

    // Wait for loadbalancer to run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Wait for load balancer to become idle.
    assertTrue(miniCluster.getClient().waitForLoadBalance(Long.MAX_VALUE, expectedTServers));
  }

  /**
   * Negative test: Create an index for a table (created inside a tablegroup) and specify its
   * tablespace. This would throw an error as we do not support tablespaces for indexes on
   * Colocated tables.
   */
  @Test
  public void disallowTableSpaceForIndexOnColocatedTables() throws Exception {
    String customTablegroup =  "test_custom_tablegroup";
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute("CREATE TABLEGROUP " +  customTablegroup +
          " TABLESPACE testTablespace");
      setupStatement.execute("CREATE TABLE t (a INT, b FLOAT) TABLEGROUP " + customTablegroup);

      // Create index without specifying tablespace.
      setupStatement.execute("CREATE INDEX t_idx1 ON t(a)");

      // Create an index and also specify its tablespace.
      String errorMsg = "TABLESPACE is not supported for indexes on colocated tables.";
      executeAndAssertErrorThrown("CREATE INDEX t_idx2 ON t(b) TABLESPACE testTablespace",
                                  errorMsg);
    }
  }

  @Test
  public void testTablesOptOutOfColocation() throws Exception {
    final String dbname = "testdatabase";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATED=TRUE", dbname));
    }
    final String colocatedTableName = "colocated_table";
    final String nonColocatedTable = "colocation_opt_out_table";
    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
         Statement stmt = connection2.createStatement()) {
      stmt.execute(String.format("CREATE TABLE %s (h INT PRIMARY KEY, a INT, b FLOAT) " +
                                 "WITH (colocated = false) TABLESPACE testTablespace",
                                 nonColocatedTable));
      stmt.execute(String.format("CREATE TABLE %s (h INT PRIMARY KEY, a INT, b FLOAT)",
                                 colocatedTableName));
    }
    verifyDefaultPlacement(colocatedTableName);
    verifyCustomPlacement(nonColocatedTable);

    // Wait for tablespace info to be refreshed in load balancer.
    Thread.sleep(5 * MASTER_REFRESH_TABLESPACE_INFO_SECS);

    // Verify that load balancer is indeed idle.
    assertTrue(miniCluster.getClient().waitForLoadBalancerIdle(
               MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    verifyDefaultPlacement(colocatedTableName);
    verifyCustomPlacement(nonColocatedTable);
  }

  @Test
  public void sanityTest() throws Exception {
    markClusterNeedsRecreation();
    YBClient client = miniCluster.getClient();

    // Set required YB-Master flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
    }

    createTestData("sanity_test");

    // Verify that the table was created and its tablets were placed
    // according to the tablespace replication info.
    LOG.info("Verify whether tablet replicas were placed correctly at creation time");
    verifyPlacement();

    // Wait for tablespace info to be refreshed in load balancer.
    Thread.sleep(2 * MASTER_REFRESH_TABLESPACE_INFO_SECS);

    addTserversAndWaitForLB();

    // Verify that the loadbalancer also placed the tablets of the table based on the
    // tablespace replication info.
    LOG.info("Verify whether the load balancer maintained the placement of tablet replicas" +
             " after TServers were added");
    verifyPlacement();

    // Trigger a master leader change.
    LeaderStepDownResponse resp = client.masterLeaderStepDown();
    assertFalse(resp.hasError());

    Thread.sleep(10 * MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS);

    LOG.info("Verify that tablets have been placed correctly even after master leader changed");
    verifyPlacement();
  }

  @Test
  public void testDisabledTablespaces() throws Exception {
    // Create some tables with custom and default placements.
    // These tables will be placed according to their tablespaces
    // by the create table path.
    createTestData("pre_disabling_tblspcs");

    // Disable tablespaces
    YBClient client = miniCluster.getClient();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "false"));
    }

    // At this point, since tablespaces are disabled, the LB will detect that the older
    // tables have not been correctly placed. Wait until the load balancer is active.
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Wait for LB to finish its run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerIdle(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Now create some new tables. The table creation path will place these
    // tables according to the cluster config.
    createTestData("disabled_tablespace_test");

    // Verify that both the table creation path and load balancer have placed all the tables
    // based on cluster config.
    LOG.info("Verify placement of tablets after tablespaces have been disabled");
    verifyDefaultPlacementForAll();
  }

  @Test
  public void testLBTablespacePlacement() throws Exception {
    // This test disables using tablespaces at creation time. Thus, the tablets of the table will be
    // incorrectly placed based on cluster config at creation time, and we will rely on the LB to
    // correctly place the table based on its tablespace.
    // Set master flags.
    YBClient client = miniCluster.getClient();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "false"));
    }

    createTestData("test_lb_placement");

    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
    }

    // Since the tablespace-id was not checked during creation, the tablet replicas
    // would have been placed wrongly. This condition will be detected by the load
    // balancer. Wait until it starts running to fix these wrongly placed tablets.
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Wait for LB to finish its run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerIdle(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Verify that the loadbalancer placed the tablets of the table based on the
    // tablespace replication info. Skip checking transaction tables, as they would
    // not have been created when tablespaces were disabled.
    LOG.info("Verify whether tablet replicas placed incorrectly at creation time are moved to " +
             "their appropriate placement by the load balancer");
    verifyPlacement(true /* skipTransactionTables */);
  }

  @Test
  public void negativeTest() throws Exception {
    YBClient client = miniCluster.getClient();

    // Set required YB-Master flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
    }

    // Create tablespaces with invalid placement.
    try (Statement setupStatement = connection.createStatement()) {
      // Create a tablespace specifying a cloud that does not exist.
      setupStatement.execute(
          "CREATE TABLESPACE invalid_tblspc WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud3\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}," +
          "{\"cloud\":\"cloud2\",\"region\":\"region2\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");

      // Create a tablespace wherein the individual min_num_replicas can be
      // satisfied, but the total replication factor cannot.
      setupStatement.execute(
          "CREATE TABLESPACE insufficient_rf_tblspc WITH (replica_placement=" +
          "'{\"num_replicas\":5, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}," +
          "{\"cloud\":\"cloud2\",\"region\":\"region2\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");

      // Create a table that can be used for the index test below.
      setupStatement.execute("CREATE TABLE negativeTestTable (a int)");
    }

    final String not_enough_tservers_msg =
      "Not enough tablet servers in the requested placements";

    // Test creation of table in invalid tablespace.
    executeAndAssertErrorThrown(
      "CREATE TABLE invalidPlacementTable (a int) TABLESPACE invalid_tblspc",
      not_enough_tservers_msg);

    // Test creation of index in invalid tablespace.
    executeAndAssertErrorThrown(
      "CREATE INDEX invalidPlacementIdx ON negativeTestTable(a) TABLESPACE invalid_tblspc",
      not_enough_tservers_msg);

    // Test creation of tablegroup in invalid tablespace.
    executeAndAssertErrorThrown(
      "CREATE TABLEGROUP invalidPlacementTablegroup TABLESPACE invalid_tblspc",
      not_enough_tservers_msg);

    // Test creation of table when the replication factor cannot be satisfied.
    executeAndAssertErrorThrown(
      "CREATE TABLE insufficentRfTable (a int) TABLESPACE insufficient_rf_tblspc",
      not_enough_tservers_msg);

    // Test creation of tablegroup when the replication factor cannot be satisfied.
    executeAndAssertErrorThrown(
      "CREATE TABLEGROUP insufficientRfTablegroup TABLESPACE insufficient_rf_tblspc",
      not_enough_tservers_msg);

    testTableCreationFailure();
  }

  public void executeAndAssertErrorThrown(String statement, String err_msg) throws Exception{
    boolean error_thrown = false;
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(statement);
    } catch (PSQLException e) {
      String actualError = e.getMessage();
      assertTrue("Expected: " + err_msg + " Actual: " + actualError, actualError.contains(err_msg));
      error_thrown = true;
    }

    // Verify that error was indeed thrown.
    assertTrue(error_thrown);
  }

  public void testTableCreationFailure() throws Exception {
    final YBClient client = miniCluster.getClient();
    int previousBGWait = MiniYBCluster.CATALOG_MANAGER_BG_TASK_WAIT_MS;

    try (Statement stmt = connection.createStatement()) {
      for (HostAndPort hp : miniCluster.getMasters().keySet()) {
        // Increase the interval between subsequent runs of bg thread so that
        // it assigns replicas for tablets of both the tables concurrently.
        assertTrue(client.setFlag(hp, "catalog_manager_bg_task_wait_ms", "10000"));
        assertTrue(client.setFlag(
              hp, "TEST_skip_placement_validation_createtable_api", "true", true));
      }
      LOG.info("Increased the delay between successive runs of bg threads.");
      // Create tablespace with valid placement.
      stmt.execute(
          " CREATE TABLESPACE valid_ts " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}," +
          "{\"cloud\":\"cloud2\",\"region\":\"region2\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");
      LOG.info("Created a tablespace with valid placement information.");
      // Create tablespace with invalid placement.
      stmt.execute(
          " CREATE TABLESPACE invalid_ts " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}," +
          "{\"cloud\":\"cloud4\",\"region\":\"region4\",\"zone\":\"zone4\"," +
          "\"min_num_replicas\":1}]}')");
      LOG.info("Created a tablespace with invalid placement information.");

      int count = 2;
      final AtomicBoolean errorsDetectedForValidTable = new AtomicBoolean(false);
      final AtomicBoolean errorsDetectedForInvalidTable = new AtomicBoolean(false);
      final CyclicBarrier barrier = new CyclicBarrier(count);
      final Thread[] threads = new Thread[count];
      final Connection[] connections = new Connection[count];

      // Create connections for concurrent create table.
      for (int i = 0; i < count; ++i) {
        ConnectionBuilder b = getConnectionBuilder();
        b.withTServer(count % miniCluster.getNumTServers());
        connections[i] = b.connect();
      }

      // Enqueue a couple of concurrent table creation jobs.
      // Thread that creates an invalid table.
      threads[0] = new Thread(() -> {
        try (Statement lstmt = connections[0].createStatement()) {
          barrier.await();
          try {
            LOG.info("Creating a table with invalid placement information.");
            lstmt.execute("CREATE TABLE invalidplacementtable (a int) TABLESPACE invalid_ts");
          } catch (Exception e) {
            LOG.error(e.getMessage());
            errorsDetectedForInvalidTable.set(true);
          }
        } catch (InterruptedException | BrokenBarrierException | SQLException throwables) {
          LOG.info("Infrastructure exception, can be ignored", throwables);
        } finally {
          barrier.reset();
        }
      });

      // Thread that creates a valid table.
      threads[1] = new Thread(() -> {
        try (Statement lstmt = connections[1].createStatement()) {
          barrier.await();
          try {
            LOG.info("Creating a table with valid placement information.");
            lstmt.execute("CREATE TABLE validplacementtable (a int) TABLESPACE valid_ts");
          } catch (Exception e) {
            LOG.error(e.getMessage());
            errorsDetectedForValidTable.set(true);
          }
        } catch (InterruptedException | BrokenBarrierException | SQLException throwables) {
          LOG.info("Infrastructure exception, can be ignored", throwables);
        } finally {
          barrier.reset();
        }
      });

      // Wait for join.
      Arrays.stream(threads).forEach(t -> t.start());
      for (Thread t : threads) {
        t.join();
      }

      // Reset the bg threads delay.
      for (HostAndPort hp : miniCluster.getMasters().keySet()) {
        assertTrue(client.setFlag(
              hp, "catalog_manager_bg_task_wait_ms", Integer.toString(previousBGWait)));
        assertTrue(client.setFlag(
              hp, "TEST_skip_placement_validation_createtable_api", "false", true));
      }
      // Verify that the transaction DDL garbage collector removes this table.
      assertTrue(client.waitForTableRemoval(30000, "invalidplacementtable"));

      LOG.info("Valid table created successfully: " + !errorsDetectedForValidTable.get());
      LOG.info("Invalid table created successfully: " + !errorsDetectedForInvalidTable.get());
      assertFalse(errorsDetectedForValidTable.get());
      assertTrue(errorsDetectedForInvalidTable.get());
    }
  }

  // Verify that the tables and indices have been placed in appropriate
  // zones.
  void verifyPlacement() throws Exception {
    verifyPlacement(false /* skipTransactionTables */);
  }

  void verifyPlacement(boolean skipTransactionTables) throws Exception {
    for (final String table : tablesWithDefaultPlacement) {
      verifyDefaultPlacement(table);
    }
    for (final String table : tablesWithCustomPlacement) {
      if (skipTransactionTables && table.contains("transaction")) {
        continue;
      }
      verifyCustomPlacement(table);
    }
  }

  void verifyDefaultPlacementForAll() throws Exception {
    for (final String table : tablesWithDefaultPlacement) {
      verifyDefaultPlacement(table);
    }
    for (final String table : tablesWithCustomPlacement) {
      verifyDefaultPlacement(table);
    }
  }

  void verifyCustomPlacement(final String table) throws Exception {
    final YBClient client = miniCluster.getClient();
    client.waitForReplicaCount(getTableFromName(table), 2, 30_000);

    List<LocatedTablet> tabletLocations = getTableFromName(table).getTabletsLocations(30_000);
    String errorMsg = "Invalid custom placement for table '" + table + "': "
        + getPlacementInfoString(tabletLocations);

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();

      // Verify that both tablets either belong to zone1 or zone2.
      for (LocatedTablet.Replica replica : replicas) {
        final String role = replica.getRole();
        assertFalse(role, role.contains("READ_REPLICA"));
        CloudInfoPB cloudInfo = replica.getCloudInfo();
        if ("cloud1".equals(cloudInfo.getPlacementCloud())) {
          assertEquals(errorMsg, "region1", cloudInfo.getPlacementRegion());
          assertEquals(errorMsg, "zone1", cloudInfo.getPlacementZone());
          continue;
        }
        assertEquals(errorMsg, "cloud2", cloudInfo.getPlacementCloud());
        assertEquals(errorMsg, "region2", cloudInfo.getPlacementRegion());
        assertEquals(errorMsg, "zone2", cloudInfo.getPlacementZone());
      }
    }
  }

  void verifyDefaultPlacement(final String table) throws Exception {
    final YBClient client = miniCluster.getClient();
    client.waitForReplicaCount(getTableFromName(table), 3, 30_000);

    List<LocatedTablet> tabletLocations = getTableFromName(table).getTabletsLocations(30_000);
    String errorMsg = "Invalid default placement for table '" + table + "': "
        + getPlacementInfoString(tabletLocations);

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();
      // Verify that tablets can be present in any zone.
      for (LocatedTablet.Replica replica : replicas) {
        CloudInfoPB cloudInfo = replica.getCloudInfo();
        if ("cloud1".equals(cloudInfo.getPlacementCloud())) {
          assertEquals(errorMsg, "region1", cloudInfo.getPlacementRegion());
          assertEquals(errorMsg, "zone1", cloudInfo.getPlacementZone());
          continue;
        } else if ("cloud2".equals(cloudInfo.getPlacementCloud())) {
          assertEquals(errorMsg, "region2", cloudInfo.getPlacementRegion());
          assertEquals(errorMsg, "zone2", cloudInfo.getPlacementZone());
          continue;
        }
        assertEquals(errorMsg, "cloud3", cloudInfo.getPlacementCloud());
        assertEquals(errorMsg, "region3", cloudInfo.getPlacementRegion());
        assertEquals(errorMsg, "zone3", cloudInfo.getPlacementZone());
      }
    }
  }

  public String getPlacementInfoString(List<LocatedTablet> locatedTablets) {
    StringBuilder sb = new StringBuilder("[");
    for (LocatedTablet tablet : locatedTablets) {
      sb.append("[");
      List<LocatedTablet.Replica> replicas = new ArrayList<>(tablet.getReplicas());
      // Somewhat dirty but would do.
      replicas
          .sort((r1, r2) -> r1.getCloudInfo().toString().compareTo(r2.getCloudInfo().toString()));
      for (LocatedTablet.Replica replica : replicas) {
        if (sb.charAt(sb.length() - 1) != '[') {
          sb.append(", ");
        }
        sb.append("{\n");
        sb.append(Stream.<String>of(replica.getCloudInfo().toString().trim().split("\n"))
            .map((s) -> " " + s)
            .collect(Collectors.joining(",\n")));
        sb.append("\n}");
      }
      sb.append("]");
    }
    sb.append("]");
    return sb.toString();
  }

  public String getTablespaceTransactionTableName() throws Exception {
    try (Statement setupStatement = connection.createStatement()) {
      ResultSet s = setupStatement.executeQuery(
          "SELECT oid FROM pg_catalog.pg_tablespace " +
          "WHERE UPPER(spcname) = UPPER('" + tablespaceName + "')");
      assertTrue(s.next());
      return "transactions_" + s.getInt(1);
    }
  }

  YBTable getTableFromName(final String table) throws Exception {
    final YBClient client = miniCluster.getClient();
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tables =
      client.getTablesList(table).getTableInfoList();
    assertEquals("More than one table found with name " + table, 1, tables.size());
    return client.openTableByUUID(
      tables.get(0).getId().toStringUtf8());
  }


  @Test
  public void readReplicaWithTablespaces() throws Exception {
    markClusterNeedsRecreation();
    final YBClient client = miniCluster.getClient();
    // Set required YB-Master flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
    }

    createTestData("read_replica");
    int expectedTServers = miniCluster.getTabletServers().size() + 1;

    LOG.info("Adding a TServer for read-replica cluster");
    Map<String, String> readReplicaPlacement = ImmutableMap.of(
        "placement_cloud", "cloud1",
        "placement_region", "region1",
        "placement_zone", "zone1",
        "placement_uuid", "readcluster");
    miniCluster.startTServer(readReplicaPlacement);
    miniCluster.waitForTabletServers(expectedTServers);

    CloudInfoPB cloudInfo0 = CloudInfoPB.newBuilder()
            .setPlacementCloud("cloud1")
            .setPlacementRegion("region1")
            .setPlacementZone("zone1")
            .build();

    CatalogEntityInfo.PlacementBlockPB placementBlock0 = CatalogEntityInfo.PlacementBlockPB.
        newBuilder().setCloudInfo(cloudInfo0).setMinNumReplicas(1).build();

    List<CatalogEntityInfo.PlacementBlockPB> placementBlocksLive =
        new ArrayList<CatalogEntityInfo.PlacementBlockPB>();
    placementBlocksLive.add(placementBlock0);

    List<CatalogEntityInfo.PlacementBlockPB> placementBlocksReadOnly =
            new ArrayList<CatalogEntityInfo.PlacementBlockPB>();
    placementBlocksReadOnly.add(placementBlock0);

    CatalogEntityInfo.PlacementInfoPB livePlacementInfo = CatalogEntityInfo.PlacementInfoPB.
        newBuilder().addAllPlacementBlocks(placementBlocksLive).setNumReplicas(1).
        setPlacementUuid(ByteString.copyFromUtf8("")).build();

    CatalogEntityInfo.PlacementInfoPB readOnlyPlacementInfo = CatalogEntityInfo.PlacementInfoPB.
        newBuilder().addAllPlacementBlocks(placementBlocksReadOnly).setNumReplicas(1).
        setPlacementUuid(ByteString.copyFromUtf8("readcluster")).build();

    List<CatalogEntityInfo.PlacementInfoPB> readOnlyPlacements = Arrays.asList(
        readOnlyPlacementInfo);

    ModifyClusterConfigReadReplicas readOnlyOperation =
        new ModifyClusterConfigReadReplicas(client, readOnlyPlacements);
    ModifyClusterConfigLiveReplicas liveOperation =
        new ModifyClusterConfigLiveReplicas(client, livePlacementInfo);

    try {
      LOG.info("Setting read only cluster configuration");
      readOnlyOperation.doCall();

      LOG.info("Setting cluster configuration for live replicas");
      liveOperation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }

    LOG.info("Waiting for the loadbalancer to become active...");

    // Wait for loadbalancer to run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    LOG.info("Waiting for the load balancer to become idle...");
    assertTrue(miniCluster.getClient().waitForLoadBalance(Long.MAX_VALUE, expectedTServers));

    for (final String table : tablesWithDefaultPlacement) {
      verifyPlacementForReadReplica(table);
    }
    for (final String table : tablesWithCustomPlacement) {
      verifyCustomPlacement(table);
    }
  }

  void verifyPlacementForReadReplica(final String table) throws Exception {
    final YBClient client = miniCluster.getClient();
    client.waitForReplicaCount(getTableFromName(table), 1, 30_000);

    List<LocatedTablet> tabletLocations = getTableFromName(table).getTabletsLocations(30_000);
    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      boolean foundReadOnlyReplica = false;
      boolean foundLiveReplica = false;
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();
      // Verify that tablets can be present in any zone.
      for (LocatedTablet.Replica replica : replicas) {
        CloudInfoPB cloudInfo = replica.getCloudInfo();
        final String errorMsg = "Unexpected cloud.region.zone: " + cloudInfo.toString();

        if (replica.getRole().contains("READ_REPLICA")) {
          assertFalse(foundReadOnlyReplica);
          foundReadOnlyReplica = true;
        } else {
          assertFalse(foundLiveReplica);
          foundLiveReplica = true;
        }
        assertEquals(errorMsg, "cloud1", cloudInfo.getPlacementCloud());
        assertEquals(errorMsg, "region1", cloudInfo.getPlacementRegion());
        assertEquals(errorMsg, "zone1", cloudInfo.getPlacementZone());
      }
      // A live replica must be found.
      assertTrue(foundLiveReplica);

      // A read-only replica must be found.
      assertTrue(foundReadOnlyReplica);
    }
  }
}
