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

import java.sql.Statement;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.LeaderStepDownResponse;
import org.yb.client.LocatedTablet;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.Master;
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
    return getPerfMaxRuntime(1000, 1200, 1500, 1500, 1500);
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
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
    String defaultIndexCustomTable = prefixName + "_default_idx_on_custom_table";
    String customIndexCustomTable = prefixName + "_custom_idx_on_custom_table";
    try (Statement setupStatement = connection.createStatement()) {
      // Create tables in default and custom tablespaces.
      setupStatement.execute(
          "CREATE TABLE " +  customTable + "(a int) TABLESPACE testTablespace");

      setupStatement.execute(
          "CREATE TABLE " + defaultTable + "(a int)");

      // Create indexes in default and custom tablespaces.
      setupStatement.execute("CREATE INDEX " + customIndex + " on " +
          defaultTable + "(a) TABLESPACE testTablespace");

      setupStatement.execute("CREATE INDEX " + defaultIndexCustomTable + " on " +
          customTable + "(a)");

      setupStatement.execute("CREATE INDEX " + customIndexCustomTable + " on " +
          customTable + "(a) TABLESPACE testTablespace");

      setupStatement.execute("CREATE INDEX " + defaultIndex + " on " +
          defaultTable + "(a)");
    }
    tablesWithDefaultPlacement.addAll(Arrays.asList(defaultTable, defaultIndex,
          defaultIndexCustomTable));
    tablesWithCustomPlacement.addAll(Arrays.asList(customTable, customIndex,
          customIndexCustomTable));
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

  public void sanityTest() throws Exception {
    YBClient client = miniCluster.getClient();

    // Set required YB-Master flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "ysql_tablespace_info_refresh_secs",
            Integer.toString(MASTER_REFRESH_TABLESPACE_INFO_SECS)));
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
      assertTrue(client.setFlag(hp, "v", "3"));
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

  public void testDisabledTablespaces() throws Exception {
    YBClient client = miniCluster.getClient();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "false"));
      assertTrue(client.setFlag(hp, "v", "3"));
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
      assertTrue(client.setFlag(hp, "v", "3"));
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
    List<LocatedTablet> tabletLocations = fetchTablets(table);

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();
      // Replication factor should be 2.
      assertEquals("Mismatch of replication factor for table:" + table, replicas.size(), 2);

      // Verify that both tablets either belong to zone1 or zone2.
      for (LocatedTablet.Replica replica : replicas) {
        org.yb.Common.CloudInfoPB cloudInfo = replica.getCloudInfo();
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
    List<LocatedTablet> tabletLocations = fetchTablets(table);

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();
      // Replication factor should be 3.
      assertEquals("Mismatch of replication factor for table:" + table, 3, replicas.size());

      // Verify that tablets can be present in any zone.
      for (LocatedTablet.Replica replica : replicas) {
        org.yb.Common.CloudInfoPB cloudInfo = replica.getCloudInfo();
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

  List<LocatedTablet> fetchTablets(final String table) throws Exception {
    final YBClient client = miniCluster.getClient();
    List<Master.ListTablesResponsePB.TableInfo> tables =
      client.getTablesList(table).getTableInfoList();
    assertEquals(1, tables.size());
    final YBTable ybtable = client.openTableByUUID(
      tables.get(0).getId().toStringUtf8());
    return ybtable.getTabletsLocations(30000);
  }
}
