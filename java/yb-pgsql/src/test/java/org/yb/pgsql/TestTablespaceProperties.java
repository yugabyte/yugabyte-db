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

import org.yb.minicluster.MiniYBDaemon;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.LeaderStepDownResponse;
import org.yb.client.LocatedTablet.Replica;
import org.yb.client.LocatedTablet;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.Master;
import org.yb.minicluster.MiniYBCluster;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.util.*;
import java.sql.Statement;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThanOrEqualTo;
import static org.yb.AssertionWrappers.assertTrue;
import com.google.common.net.HostAndPort;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestTablespaceProperties extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  ArrayList<String> tablesWithDefaultPlacement = new ArrayList<String>();
  ArrayList<String> tablesWithCustomPlacement = new ArrayList<String>();

  private static final int MASTER_REFRESH_TABLESPACE_INFO_SECS = 2;

  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(800, 1000, 1500, 1500, 1500);
  }

  void setupCluster() throws Exception {
    destroyMiniCluster();
    createMiniCluster(3, masterArgs, getTserverArgs(), true /* enable_pg_transactions */);

    pgInitialized = false;
    initPostgresBefore();
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

  private List<List<String>> getTserverArgs() {
    List<String> zone1Placement = Arrays.asList(
            "--placement_cloud=cloud1", "--placement_region=region1",
            "--placement_zone=zone1");

    List<String> zone2Placement = Arrays.asList(
            "--placement_cloud=cloud2", "--placement_region=region2",
            "--placement_zone=zone2");

    List<String> zone3Placement = Arrays.asList(
            "--placement_cloud=cloud3", "--placement_region=region3",
            "--placement_zone=zone3");

    List<List<String>> tserverArgs = new ArrayList<List<String>>();
    tserverArgs.add(zone1Placement);
    tserverArgs.add(zone2Placement);
    tserverArgs.add(zone3Placement);
    return tserverArgs;
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
    int expectedTServers = miniCluster.getTabletServers().size() + 2;
    List<List<String>> tserverArgs = getTserverArgs();
    miniCluster.startTServer(tserverArgs.get(1));
    miniCluster.startTServer(tserverArgs.get(2));
    miniCluster.waitForTabletServers(expectedTServers);

    // Wait for loadbalancer to run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(30000 /* timeoutMs */));

    // Wait for load balancer to become idle.
    assertTrue(miniCluster.getClient().waitForLoadBalance(Long.MAX_VALUE, expectedTServers));
  }

  @Test
  public void testTablespaces() throws Exception {
    // First setup tablespaces.
    setupCluster();

    // Run sanity tests for tablespaces.
    sanityTest();

    // Test with tablespaces disabled.
    testDisabledTablespaces();

    // Test load balancer functions as expected when
    // tables are placed incorrectly at creation time.
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
    verifyPlacement();

    // Wait for tablespace info to be refreshed in load balancer.
    Thread.sleep(2 * MASTER_REFRESH_TABLESPACE_INFO_SECS);

    addTserversAndWaitForLB();

    // Verify that the loadbalancer also placed the tablets of the table based on the
    // tablespace replication info.
    verifyPlacement();

    // Trigger a master leader change.
    LeaderStepDownResponse resp = client.masterLeaderStepDown();
    assertFalse(resp.hasError());

    Thread.sleep(5 * MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS);

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
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(30000 /* timeoutMs */));

    // Wait for LB to finish its run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerIdle(200000 /* timeoutMs */));

    createTestData("disabled_tablespace_test");

    // Verify that the loadbalancer also placed the tablets of the table based on the
    // tablespace replication info.
    verifyDefaultPlacementForAll();
  }

  public void testLBTablespacePlacement() throws Exception {
    // This test disables setting the tablespace id at creation time. Thus, the
    // tablets of the table will be incorrectly placed based on cluster config
    // at creation time, and we will rely on the LB to correctly place the table
    // based on its tablespace.
    // Set master flags.
    YBClient client = miniCluster.getClient();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
      assertTrue(client.setFlag(hp, "TEST_disable_setting_tablespace_id_at_creation", "true"));
      assertTrue(client.setFlag(hp, "v", "3"));
    }
    createTestData("test_lb_placement");

    // Since the tablespace-id was not checked during creation, the tablet replicas
    // would have been placed wrongly. This condition will be detected by the load
    // balancer. Wait until it starts running to fix these wrongly placed tablets.
    assertTrue(miniCluster.getClient().waitForLoadBalancerActive(30000 /* timeoutMs */));

    // Wait for LB to finish its run.
    assertTrue(miniCluster.getClient().waitForLoadBalancerIdle(200000 /* timeoutMs */));

    // Verify that the loadbalancer placed the tablets of the table based on the
    // tablespace replication info.
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
