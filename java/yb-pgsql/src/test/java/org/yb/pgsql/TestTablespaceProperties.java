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
import org.yb.client.LocatedTablet.Replica;
import org.yb.client.LocatedTablet;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.Master;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.util.*;
import java.sql.Statement;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertGreaterThanOrEqualTo;
import static org.yb.AssertionWrappers.assertTrue;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestTablespaceProperties extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  private static final String CUSTOM_PLACEMENT_TABLE = "test1";

  private static final String DEFAULT_PLACEMENT_TABLE = "test2";

  private static final String CUSTOM_PLACEMENT_INDEX = "idx1";

  private static final String DEFAULT_PLACEMENT_INDEX = "idx2";

  private static final String CUSTOM_PLACEMENT_INDEX2 = "idx3";

  private static final String DEFAULT_PLACEMENT_INDEX2 = "idx4";

  private static final int MASTER_REFRESH_TABLESPACE_INFO_SECS = 2;

  @Override
  public int getTestMethodTimeoutSec() {
    return 300;
  }

  @Test
  public void testTablespaces() throws Exception {
    // Destroy the cluster so we can create a new one.
    destroyMiniCluster();

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

    List<String> masterArgs = Arrays.asList(
        "--ysql_tablespace_info_refresh_secs=" + MASTER_REFRESH_TABLESPACE_INFO_SECS,
        "--enable_ysql_tablespaces_for_placement=true");
    createMiniCluster(3, masterArgs, tserverArgs, true /* enable_pg_transactions */);
    pgInitialized = false;
    initPostgresBefore();

    // Create a tablespace with replication info.
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(
          " CREATE TABLESPACE testTablespace " +
          "  WITH (replica_placement=" +
          "'[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_number_of_replicas\":1}," +
          "{\"cloud\":\"cloud2\",\"region\":\"region2\",\"zone\":\"zone2\"," +
          "\"min_number_of_replicas\":1}]')");

      // Create tables in default and custom tablespaces.
      setupStatement.execute(
          "CREATE TABLE " + CUSTOM_PLACEMENT_TABLE + "(a int) TABLESPACE testTablespace");
      setupStatement.execute(
          "CREATE TABLE " + DEFAULT_PLACEMENT_TABLE + "(a int)");

      // Create indexes in default and custom tablespaces.
      setupStatement.execute("CREATE INDEX " + CUSTOM_PLACEMENT_INDEX + " on " +
          DEFAULT_PLACEMENT_TABLE + "(a) TABLESPACE testTablespace");

      setupStatement.execute("CREATE INDEX " + DEFAULT_PLACEMENT_INDEX + " on " +
          CUSTOM_PLACEMENT_TABLE + "(a)");

      setupStatement.execute("CREATE INDEX " + CUSTOM_PLACEMENT_INDEX2 + " on " +
          CUSTOM_PLACEMENT_TABLE + "(a) TABLESPACE testTablespace");

      setupStatement.execute("CREATE INDEX " + DEFAULT_PLACEMENT_INDEX2 + " on " +
          DEFAULT_PLACEMENT_TABLE + "(a)");
    }

    // Verify that the table was created and its tablets were placed
    // according to the tablespace replication info.
    verifyPlacement();

    // Wait for tablespace info to be refreshed in load balancer.
    Thread.sleep(MASTER_REFRESH_TABLESPACE_INFO_SECS);

    // Add 2 tservers, one in zone 2 and the other in zone 3.
    int expectedTServers = miniCluster.getTabletServers().size() + 2;
    miniCluster.startTServer(zone2Placement);
    miniCluster.startTServer(zone3Placement);
    miniCluster.waitForTabletServers(expectedTServers);

    // Wait for loadbalancer to run.
    final boolean isBalanced = miniCluster.getClient().waitForLoadBalance(Long.MAX_VALUE, 0);
    assertTrue(isBalanced);

    // Verify that the loadbalancer also placed the tablets of the table based on the
    // tablespace replication info.
    verifyPlacement();
  }

  // Verify that the tables and indices have been placed in appropriate
  // zones.
  void verifyPlacement() throws Exception {
    verifyCustomPlacement(CUSTOM_PLACEMENT_TABLE);
    verifyCustomPlacement(CUSTOM_PLACEMENT_INDEX);
    verifyCustomPlacement(CUSTOM_PLACEMENT_INDEX2);
    verifyDefaultPlacement(DEFAULT_PLACEMENT_TABLE);
    verifyDefaultPlacement(DEFAULT_PLACEMENT_INDEX);
    verifyDefaultPlacement(DEFAULT_PLACEMENT_INDEX2);
  }

  void verifyCustomPlacement(final String table) throws Exception {
    List<LocatedTablet> tabletLocations = fetchTablets(table);

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();
      // Replication factor should be 2.
      assertEquals(2, replicas.size());

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
      assertEquals(3, replicas.size());

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
