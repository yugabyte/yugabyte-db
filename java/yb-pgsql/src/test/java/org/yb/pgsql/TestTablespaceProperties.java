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
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;
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
import org.yb.client.LeaderStepDownResponse;
import org.yb.client.LocatedTablet;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.MasterDdlOuterClass;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.YBTestRunnerNonTsanOnly;

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestTablespaceProperties extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestTablespaceProperties.class);

  ArrayList<String> tablesWithDefaultPlacement = new ArrayList<String>();
  ArrayList<String> tablesWithCustomPlacement = new ArrayList<String>();

  private static final int MASTER_REFRESH_TABLESPACE_INFO_SECS = 2;

  private static final int MASTER_LOAD_BALANCER_WAIT_TIME_MS = 60 * 1000;

  private List<Map<String, String>> perTserverZonePlacementFlags = Arrays.asList(
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
    builder.perTServerFlags(perTserverZonePlacementFlags);
  }

  @Before
  public void setupTablespaces() throws Exception {
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(
          " CREATE TABLESPACE testTablespace " +
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
    tablesWithDefaultPlacement.addAll(Arrays.asList(defaultTable, defaultIndex,
          defaultIndexCustomTable, tableInDefaultTablegroup));
    tablesWithCustomPlacement.addAll(Arrays.asList(customTable, customIndex,
          customIndexCustomTable, tableInCustomTablegroup));
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

  @Test
  public void testTablespaces() throws Exception {
    // Run sanity tests for tablespaces.
    LOG.info("Running tablespace sanity tests");
    sanityTest();

    // Test with tablespaces disabled.
    LOG.info("Run tests with tablespaces disabled");
    testDisabledTablespaces();

    // Test load balancer functions as expected when
    // tables are placed incorrectly at creation time.
    LOG.info("Run load balancer tablespace placement tests");
    testLBTablespacePlacement();
  }

  public void executeAndAssertErrorThrown(String statement, String err_msg) throws Exception{
    boolean error_thrown = false;
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(statement);
    } catch (PSQLException e) {
      assertTrue(e.getMessage().contains(err_msg));
      error_thrown = true;
    }

    // Verify that error was indeed thrown.
    assertTrue(error_thrown);
  }

  public void negativeTest() throws Exception {
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

    final String not_enough_tservers_in_zone_msg = "Not enough tablet servers in " +
                                                   "cloud3:region1:zone1";
    final String not_enough_tservers_for_rf_msg = "Not enough live tablet servers to create " +
                                                  "table with replication factor 5. 3 tablet " +
                                                  "servers are alive";

    // Test creation of table in invalid tablespace.
    executeAndAssertErrorThrown(
      "CREATE TABLE invalidPlacementTable (a int) TABLESPACE invalid_tblspc",
      not_enough_tservers_in_zone_msg);

    // Test creation of index in invalid tablespace.
    executeAndAssertErrorThrown(
      "CREATE INDEX invalidPlacementIdx ON negativeTestTable(a) TABLESPACE invalid_tblspc",
      not_enough_tservers_in_zone_msg);

    // Test creation of table when the replication factor cannot be satisfied.
    executeAndAssertErrorThrown(
      "CREATE TABLE insufficent_rf_tbl (a int) TABLESPACE insufficient_rf_tblspc",
      not_enough_tservers_for_rf_msg);
  }

  public void sanityTest() throws Exception {
    YBClient client = miniCluster.getClient();

    // Set required YB-Master flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
    }

    negativeTest();

    // Test table creation failures.
    testTableCreationFailure();

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

  public void testDisabledTablespaces() throws Exception {
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

    createTestData("disabled_tablespace_test");

    // Verify that the loadbalancer also placed the tablets of the table based on the
    // tablespace replication info.
    LOG.info("Verify placement of tablets after tablespaces have been disabled");
    verifyDefaultPlacementForAll();
  }

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
    // tablespace replication info.
    LOG.info("Verify whether tablet replicas placed incorrectly at creation time are moved to " +
             "their appropriate placement by the load balancer");
    verifyPlacement();
  }

  public void testTableCreationFailure() throws Exception {
    final YBClient client = miniCluster.getClient();
    int previousBGWait = MiniYBCluster.CATALOG_MANAGER_BG_TASK_WAIT_MS;

    try (Statement stmt = connection.createStatement()) {
      for (HostAndPort hp : miniCluster.getMasters().keySet()) {
        // Increase the interval between subsequent runs of bg thread so that
        // it assigns replicas for tablets of both the tables concurrently.
        assertTrue(client.setFlag(hp, "catalog_manager_bg_task_wait_ms", "10000"));
        assertTrue(client.setFlag(hp, "TEST_skip_placement_validation_createtable_api", "true"));
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
        assertTrue(client.setFlag(hp, "catalog_manager_bg_task_wait_ms",
                                Integer.toString(previousBGWait)));
        assertTrue(client.setFlag(hp, "TEST_skip_placement_validation_createtable_api", "false"));
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
    for (final String table : tablesWithDefaultPlacement) {
      verifyDefaultPlacement(table);
    }
    for (final String table : tablesWithCustomPlacement) {
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

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();

      // Verify that both tablets either belong to zone1 or zone2.
      for (LocatedTablet.Replica replica : replicas) {
        org.yb.CommonNet.CloudInfoPB cloudInfo = replica.getCloudInfo();
        if (cloudInfo.getPlacementCloud().equals("cloud1")) {
          assertTrue(cloudInfo.getPlacementRegion().equals("region1"));
          assertTrue(cloudInfo.getPlacementZone().equals("zone1"));
          continue;
        }
        assertTrue(cloudInfo.getPlacementCloud().equals("cloud2"));
        assertTrue(cloudInfo.getPlacementRegion().equals("region2"));
        assertTrue(cloudInfo.getPlacementZone().equals("zone2"));
      }
    }
  }

  void verifyDefaultPlacement(final String table) throws Exception {
    final YBClient client = miniCluster.getClient();
    client.waitForReplicaCount(getTableFromName(table), 3, 30_000);

    List<LocatedTablet> tabletLocations = getTableFromName(table).getTabletsLocations(30_000);
    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();
      // Verify that tablets can be present in any zone.
      for (LocatedTablet.Replica replica : replicas) {
        org.yb.CommonNet.CloudInfoPB cloudInfo = replica.getCloudInfo();
        if (cloudInfo.getPlacementCloud().equals("cloud1")) {
          assertTrue(cloudInfo.getPlacementRegion().equals("region1"));
          assertTrue(cloudInfo.getPlacementZone().equals("zone1"));
          continue;
        } else if (cloudInfo.getPlacementCloud().equals("cloud2")) {
          assertTrue(cloudInfo.getPlacementRegion().equals("region2"));
          assertTrue(cloudInfo.getPlacementZone().equals("zone2"));
          continue;
        }
        assertTrue(cloudInfo.getPlacementCloud().equals("cloud3"));
        assertTrue(cloudInfo.getPlacementRegion().equals("region3"));
        assertTrue(cloudInfo.getPlacementZone().equals("zone3"));
      }
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
}
