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

  private static final String TABLE = "test";

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

    createMiniCluster(3, tserverArgs);
    pgInitialized = false;
    initPostgresBefore();

    // Create a tablespace with replication info.
    // TODO: Add negative test cases.
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(
          " CREATE TABLESPACE testTablespace " +
          "  WITH (replication_factor=2," +
          "    placement='cloud1.region1.zone1=1,cloud2.region2.zone2=1'" +
          " );");
      setupStatement.execute(
          "CREATE TABLE " + TABLE + "(a int) TABLESPACE testTablespace;");
      setupStatement.execute(
          "INSERT INTO " + TABLE + " values(5);");
    }

    // Verify that the table was created and its tablets were placed
    // according to the tablespace replication info.
    verifyPlacement();

    // Add 2 tservers, one in zone 2 and the other in zone 3.
    int expectedTServers = miniCluster.getTabletServers().size() + 2;
    miniCluster.startTServer(zone2Placement);
    miniCluster.startTServer(zone3Placement);
    miniCluster.waitForTabletServers(expectedTServers);

    // Wait for loadbalancer to run.
    boolean isBalanced = miniCluster.getClient().waitForLoadBalance(Long.MAX_VALUE, 0);
    assertTrue(isBalanced);

    // Verify that the loadbalancer also placed the tablets of the table based on the
    // tablespace replication info.
    verifyPlacement();

    /* More tests:
     * Drop tablespace with and without tables mapped to it.
     * Test tablespace with user specified if appropriate.
     * Perform sanity crud on table associated with tablespace.
     * Associate table with tablespace with no options and it should
     * be placed according to cluster level config.
     * Fix any pg tests that now fail due to supporting tablespace
     * features.
     * Remove tserver and see everything works as expected.
     * Add negative tests for wrong formats of placement options, invalid
     * replication factor inputs, min_replication_factor per block being
     * higher than total replication factor etc.
     * Some of the above should be Pg tests?
     */
  }

  // Verify that 'TABLE' has been placed only in zone 1 and zone 2.
  void verifyPlacement() throws Exception {
    // Open table.
    final YBClient client = miniCluster.getClient();
    List<Master.ListTablesResponsePB.TableInfo> tables =
      client.getTablesList(TABLE).getTableInfoList();
    assertEquals(1, tables.size());
    final YBTable ybtable = client.openTableByUUID(
      tables.get(0).getId().toStringUtf8());

    // Get tablets for table.
    for (LocatedTablet tablet : ybtable.getTabletsLocations(30000)) {
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
}
