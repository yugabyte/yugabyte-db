// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
package org.yb.client;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNotEquals;
import static org.yb.AssertionWrappers.assertNotNull;
import static org.yb.AssertionWrappers.assertTrue;

import java.util.*;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ColumnSchema;
import org.yb.Common.HostPortPB;
import org.yb.Common.TableType;
import org.yb.Schema;
import org.yb.Type;
import org.yb.master.Master;
import org.yb.minicluster.MiniYBCluster;

import com.google.common.net.HostAndPort;

import com.google.protobuf.ByteString;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestYBClient extends BaseYBClientTest {
  private static final Logger LOG = LoggerFactory.getLogger(BaseYBClientTest.class);

  private String tableName;
  
  private static final String PLACEMENT_CLOUD = "testCloud";
  private static final String PLACEMENT_REGION = "testRegion";
  private static final String PLACEMENT_ZONE = "testZone";
  private static final String LIVE_TS = "live";
  private static final String READ_ONLY_TS = "readOnly";
  private static final String READ_ONLY_NEW_TS = "readOnlyNew";

  @After
  public void tearDownAfter() throws Exception {
    // Destroy client and mini cluster after every test, so that a new one is used for the next
    // test.
    destroyClientAndMiniCluster();
  }

  @Before
  public void setTableName() {
    tableName = TestYBClient.class.getName() + "-" + System.currentTimeMillis();
  }

  private void waitForMasterLeader() throws Exception {
    syncClient.waitForMasterLeader(TestUtils.adjustTimeoutForBuildType(10000));
  }

  /**
   * Test load balanced check.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testIsLoadBalanced() throws Exception {
    LOG.info("Starting testIsLoadBalanced");
    IsLoadBalancedResponse resp = syncClient.getIsLoadBalanced(0 /* numServers */);
    assertFalse(resp.hasError());
  }

  /**
   * Test that we can create and destroy client objects (to catch leaking resources).
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testClientCreateDestroy() throws Exception {
    LOG.info("Starting testClientCreateDestroy");
    YBClient myClient = null;
    for (int i = 0 ; i < 1000; i++) {
      AsyncYBClient aClient = new AsyncYBClient.AsyncYBClientBuilder(masterAddresses)
                                .build();
      myClient = new YBClient(aClient);
      myClient.close();
      myClient = null;
    }
  }

  /**
   * Test Waiting for load balance, with simulated errors.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testWaitForLoadBalance() throws Exception {
    syncClient.injectWaitError();
    boolean isBalanced = syncClient.waitForLoadBalance(Long.MAX_VALUE, 0);
    assertTrue(isBalanced);
  }

  /**
   * Test Master Configuration Change operation going from A,B,C to D,E,F.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testAllMasterChangeConfig() throws Exception {
    waitForMasterLeader();
    LOG.info("Starting testAllChangeMasterConfig");
    int numBefore = miniCluster.getNumMasters();
    ListMastersResponse listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numBefore);
    HostAndPort[] newHp = new HostAndPort[3];
    for (int i = 0; i < 3; i++) {
      newHp[i] = miniCluster.startShellMaster();
    }

    ChangeConfigResponse resp;
    HostAndPort oldHp;
    for (int i = 0; i < 3; i++) {
      LOG.info("Add server {}", newHp[i].toString());
      resp = syncClient.changeMasterConfig(newHp[i].getHostText(), newHp[i].getPort(), true);
      assertFalse(resp.hasError());
      oldHp = miniCluster.getMasterHostPort(i);
      LOG.info("Remove server {}", oldHp.toString());
      resp = syncClient.changeMasterConfig(oldHp.getHostText(), oldHp.getPort(), false);
      assertFalse(resp.hasError());
    }

    listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numBefore);
  }

  /**
   * Test for Master Configuration Change operations.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testChangeMasterConfig() throws Exception {
    // TODO: See if TestName @Rule can be made to work instead of explicit test names.
    waitForMasterLeader();
    LOG.info("Starting testChangeMasterConfig");
    int numBefore = miniCluster.getNumMasters();
    ListMastersResponse listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numBefore);
    HostAndPort newHp = miniCluster.startShellMaster();
    ChangeConfigResponse resp = syncClient.changeMasterConfig(
        newHp.getHostText(), newHp.getPort(), true);
    assertFalse(resp.hasError());
    int numAfter = miniCluster.getNumMasters();
    assertEquals(numAfter, numBefore + 1);
    listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numAfter);
  }

  /**
   * Test for Master Configuration Change operation using host/port.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testChangeMasterConfigWithHostPort() throws Exception {
    int numBefore = miniCluster.getNumMasters();
    List<HostAndPort> hostports = miniCluster.getMasterHostPorts();
    HostAndPort leaderHp = BaseYBClientTest.findLeaderMasterHostPort();
    HostAndPort nonLeaderHp = null;
    for (HostAndPort hp : hostports) {
      if (!hp.equals(leaderHp)) {
        nonLeaderHp = hp;
        break;
      }
    }
    LOG.info("Using host/port {}/{}.",nonLeaderHp.getHostText(), nonLeaderHp.getPort());
    ChangeConfigResponse resp = syncClient.changeMasterConfig(
        nonLeaderHp.getHostText(), nonLeaderHp.getPort(), false, true);
    assertFalse(resp.hasError());
    ListMastersResponse listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numBefore - 1);
  }

  /**
   * Test for Master Configuration Change which triggers a leader step down operation.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testChangeMasterConfigOfLeader() throws Exception {
    waitForMasterLeader();
    LOG.info("Starting testChangeMasterConfigOfLeader");
    int numBefore = miniCluster.getNumMasters();
    ListMastersResponse listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numBefore);
    HostAndPort leaderHp = BaseYBClientTest.findLeaderMasterHostPort();
    ChangeConfigResponse resp = syncClient.changeMasterConfig(
        leaderHp.getHostText(), leaderHp.getPort(), false);
    assertFalse(resp.hasError());
    listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numBefore - 1);
  }

  /**
   * Test for Master leader step down operation.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testLeaderStepDown() throws Exception {
    LOG.info("Starting testLeaderStepDown");
    String leaderUuid = syncClient.getLeaderMasterUUID();
    assertNotNull(leaderUuid);
    ListMastersResponse listResp = syncClient.listMasters();
    int numBefore = listResp.getMasters().size();
    LeaderStepDownResponse resp = syncClient.masterLeaderStepDown();
    assertFalse(resp.hasError());
    // We might have failed due to election taking too long in this instance: https://goo.gl/xYmZDb.
    Thread.sleep(6000);
    TestUtils.waitFor(
        () -> {
          return syncClient.getLeaderMasterUUID() != null;
        }, 20000);

    String newLeaderUuid = syncClient.getLeaderMasterUUID();
    assertNotNull(newLeaderUuid);
    listResp = syncClient.listMasters();
    assertEquals(listResp.getMasters().size(), numBefore);
    // NOTE: This assert could intermittently fail. We will use this test for creating a
    // reproducible test case for JIRA ENG-49.
    LOG.info("New leader uuid " + newLeaderUuid + ", old leader uuid " + leaderUuid);
    assertNotEquals(newLeaderUuid, leaderUuid);
  }

  /**
   * Test for changing the universe config.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testChangeMasterClusterConfig() throws Exception {
    LOG.info("Starting testChangeMasterClusterConfig");
    GetMasterClusterConfigResponse resp = syncClient.getMasterClusterConfig();
    assertFalse(resp.hasError());
    // Config starts at 0 and gets bumped with every write.
    assertEquals(0, resp.getConfig().getVersion());
    assertEquals(0, resp.getConfig().getServerBlacklist().getHostsList().size());

    // Prepare some hosts.
    HostPortPB host1 = HostPortPB.newBuilder().setHost("host1").setPort(0).build();
    HostPortPB host2 = HostPortPB.newBuilder().setHost("host2").setPort(0).build();
    List<HostPortPB> hosts = new ArrayList<HostPortPB>();
    hosts.add(host1);
    hosts.add(host2);
    // Add the hosts to the config.
    ModifyMasterClusterConfigBlacklist operation =
        new ModifyMasterClusterConfigBlacklist(syncClient, hosts, true);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    // Check the new config info.
    resp = syncClient.getMasterClusterConfig();
    assertFalse(resp.hasError());
    assertEquals(1, resp.getConfig().getVersion());
    assertEquals(2, resp.getConfig().getServerBlacklist().getHostsList().size());
    // Modify the blacklist again and remove host2 by issuing a remove with a list of just one
    // element: host2.
    hosts.remove(host1);
    operation = new ModifyMasterClusterConfigBlacklist(syncClient, hosts, false);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    // Check the config one last time.
    resp = syncClient.getMasterClusterConfig();
    assertFalse(resp.hasError());
    assertEquals(2, resp.getConfig().getVersion());
    List<HostPortPB> responseHosts = resp.getConfig().getServerBlacklist().getHostsList();
    assertEquals(1, responseHosts.size());
    HostPortPB responseHost = responseHosts.get(0);
    assertEquals(host1.getHost(), responseHost.getHost());
    assertEquals(host1.getPort(), responseHost.getPort());
  }
  
  /**
   * Test for live and read only replica correct load balancing.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testTableCreateCorrectReplicaConfiguration() throws Exception {
    // Destroy the cluster so we can create a new one.
    destroyMiniCluster();
    
    List<String> livePlacement = Arrays.asList(
        "--placement_cloud=" + PLACEMENT_CLOUD, "--placement_region=" + PLACEMENT_REGION, 
        "--placement_zone=" + PLACEMENT_ZONE, "--placement_uuid=" + LIVE_TS);
    
    List<String> readOnlyPlacement = Arrays.asList(
        "--placement_cloud=" + PLACEMENT_CLOUD, "--placement_region=" + PLACEMENT_REGION, 
        "--placement_zone=" + PLACEMENT_ZONE, "--placement_uuid=" + READ_ONLY_TS);
    
    List<List<String>> tserverArgs = new ArrayList<List<String>>();
    // Create a live and read only cluster with 3 masters and 3 tservers in the same az. Although this is not
    // the most common use case, it is the most pathological and should be no different from different
    // azs.
    for (int i = 0; i < 3; i++) {
      tserverArgs.add(livePlacement);
      tserverArgs.add(readOnlyPlacement);
    }
    
    // Master args, used to speed up the test.
    List<String> masterArgs = Arrays.asList("--load_balancer_max_concurrent_adds=100", 
                                            "--load_balancer_max_concurrent_moves=100",
                                            "--load_balancer_max_concurrent_removals=100");
    
    createMiniCluster(3, masterArgs, tserverArgs);
    
    // Create the cluster config pb to be sent to the masters
    org.yb.Common.CloudInfoPB cloudInfo0 = org.yb.Common.CloudInfoPB.newBuilder()
        .setPlacementCloud(PLACEMENT_CLOUD)
        .setPlacementRegion(PLACEMENT_REGION)
        .setPlacementZone(PLACEMENT_ZONE)
        .build();
    
    Master.PlacementBlockPB placementBlock0 = 
        Master.PlacementBlockPB.newBuilder().setCloudInfo(cloudInfo0).setMinNumReplicas(3).build();
    
    List<Master.PlacementBlockPB> placementBlocksLive = new ArrayList<Master.PlacementBlockPB>();
    placementBlocksLive.add(placementBlock0);
    
    List<Master.PlacementBlockPB> placementBlocksReadOnly = new ArrayList<Master.PlacementBlockPB>();
    placementBlocksReadOnly.add(placementBlock0);
    
    Master.PlacementInfoPB livePlacementInfo = 
        Master.PlacementInfoPB.newBuilder().addAllPlacementBlocks(placementBlocksLive).
        setPlacementUuid(ByteString.copyFromUtf8(LIVE_TS)).build();
    
    Master.PlacementInfoPB readOnlyPlacementInfo = 
        Master.PlacementInfoPB.newBuilder().addAllPlacementBlocks(placementBlocksReadOnly).
        setPlacementUuid(ByteString.copyFromUtf8(READ_ONLY_TS)).build();
    
    List<Master.PlacementInfoPB> readOnlyPlacements = Arrays.asList(readOnlyPlacementInfo);
    ModifyClusterConfigReadReplicas readOnlyOperation =
        new ModifyClusterConfigReadReplicas(syncClient, readOnlyPlacements);
    try {
      readOnlyOperation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    
    ModifyClusterConfigLiveReplicas liveOperation =
        new ModifyClusterConfigLiveReplicas(syncClient, livePlacementInfo);
    try {
      liveOperation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    
    // Create a table with 8 tablets
    List<ColumnSchema> columns = new ArrayList<>(hashKeySchema.getColumns());
    Schema newSchema = new Schema(columns);
    CreateTableOptions tableOptions = new CreateTableOptions().setNumTablets(8);
    YBTable table = syncClient.createTable(
        DEFAULT_KEYSPACE_NAME, "CreateTableTest", newSchema, tableOptions);
    
    // Ensure that each live tserver has 8 live replicas each and each read only tserver has 8 read only replicas
    // each.
    Map<String, List<List<Integer>>> placementUuidMap = 
        table.getMemberTypeCountsForEachTSType(DEFAULT_TIMEOUT_MS);
    List<List<Integer>> liveTsList = placementUuidMap.get(LIVE_TS);
    List<List<Integer>> readOnlyTsList = placementUuidMap.get(READ_ONLY_TS);
    
    LOG.info(liveTsList.get(0).toString());
    assertTrue(liveTsList.get(0).equals(Arrays.asList(8, 8, 8)));
    assertTrue(liveTsList.get(1).equals(Arrays.asList(0, 0, 0)));
    
    assertTrue(readOnlyTsList.get(0).equals(Arrays.asList(0, 0, 0)));
    assertTrue(readOnlyTsList.get(1).equals(Arrays.asList(8, 8, 8)));
    
    // Create another live and readOnly node.
    miniCluster.startTServer(tserverArgs.get(0));
    miniCluster.startTServer(tserverArgs.get(1));
    Thread.sleep(2 * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);
    miniCluster.waitForTabletServers(8);

    // Test that now each live tsever has 6 live replicas and each read only tserver has 6 read only replicas.
    placementUuidMap = table.getMemberTypeCountsForEachTSType(DEFAULT_TIMEOUT_MS);
    Map<String, List<List<Integer>>> expectedMap = new HashMap<String, List<List<Integer>>>();
    List<List<Integer>> expectedLiveTsList = Arrays.asList(Arrays.asList(6, 6, 6, 6), 
                                                           Arrays.asList(0, 0, 0, 0));
    List<List<Integer>> expectedReadOnlyTsList = Arrays.asList(Arrays.asList(0, 0, 0, 0), 
                                                            Arrays.asList(6, 6, 6, 6));
    expectedMap.put(LIVE_TS, expectedLiveTsList);
    expectedMap.put(READ_ONLY_TS, expectedReadOnlyTsList);
    
    assertTrue(syncClient.waitForExpectedReplicaMap(30000, table, expectedMap));
    
    // Now we create a new read only cluster with 3 nodes and RF=3 with uuid readOnlyNew in the same zone.
    List<String> readOnlyPlacementNew = Arrays.asList(
        "--placement_cloud=" + PLACEMENT_CLOUD, "--placement_region=" + PLACEMENT_REGION, 
        "--placement_zone=" + PLACEMENT_ZONE, "--placement_uuid=" + READ_ONLY_NEW_TS);
    
    for (int i = 0; i < 3; i++) {
      miniCluster.startTServer(readOnlyPlacementNew);
    }
    Thread.sleep(2 * MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS * 1000);
    miniCluster.waitForTabletServers(11);
    
    List<Master.PlacementBlockPB> placementBlocksreadOnlyNew = 
        new ArrayList<Master.PlacementBlockPB>();
    placementBlocksreadOnlyNew.add(placementBlock0);
    
    Master.PlacementInfoPB readOnlyPlacementInfoNew = 
        Master.PlacementInfoPB.newBuilder().
        addAllPlacementBlocks(placementBlocksreadOnlyNew).
        setPlacementUuid(ByteString.copyFromUtf8(READ_ONLY_NEW_TS)).build();
    
    List<Master.PlacementInfoPB> readOnlyPlacementsNew = 
        Arrays.asList(readOnlyPlacementInfoNew);
    ModifyClusterConfigReadReplicas readOnlyOperationNew =
        new ModifyClusterConfigReadReplicas(syncClient, readOnlyPlacementsNew);
    try {
      readOnlyOperationNew.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    
    // We make sure that each tserver is this new zone has 8 read only replicas each, and 
    // that none of the other tservers have changed replica counts.
    List<List<Integer>> expectedReadOnlyNewTsList = Arrays.asList(Arrays.asList(0, 0, 0), 
                                                               Arrays.asList(8, 8, 8));
    expectedMap.put(READ_ONLY_NEW_TS, expectedReadOnlyNewTsList);
    
    assertTrue(syncClient.waitForExpectedReplicaMap(60000, table, expectedMap));
  }

  /**
   * Test for changing the universe config's replication factor.
   * @throws Exception
   */
  @Test(timeout = 100000)
  public void testChangeClusterConfigRF() throws Exception {
    GetMasterClusterConfigResponse resp = syncClient.getMasterClusterConfig();
    assertFalse(resp.hasError());
    // Config starts at 0 and gets bumped with every write.
    assertEquals(0, resp.getConfig().getVersion());
    assertEquals(0, resp.getConfig().getReplicationInfo().getLiveReplicas().getNumReplicas());

    // Change the RF of the config.
    ModifyClusterConfigReplicationFactor operation =
        new ModifyClusterConfigReplicationFactor(syncClient, 5);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    // Check the new config info.
    resp = syncClient.getMasterClusterConfig();
    assertFalse(resp.hasError());
    assertEquals(1, resp.getConfig().getVersion());
    assertEquals(5, resp.getConfig().getReplicationInfo().getLiveReplicas().getNumReplicas());

    // Reduce the RF of the config.
    operation = new ModifyClusterConfigReplicationFactor(syncClient, 3);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    // Check the new config info.
    resp = syncClient.getMasterClusterConfig();
    assertFalse(resp.hasError());
    assertEquals(2, resp.getConfig().getVersion());
    assertEquals(3, resp.getConfig().getReplicationInfo().getLiveReplicas().getNumReplicas());
  }
  
  @Test(timeout = 100000)
  public void testAffinitizedLeaders() throws Exception {
    destroyMiniCluster();
    List<List<String>> tserverArgs = new ArrayList<List<String>>();
    tserverArgs.add(Arrays.asList(
        "--placement_cloud=testCloud", "--placement_region=testRegion", "--placement_zone=testZone0"));
    tserverArgs.add(Arrays.asList(
        "--placement_cloud=testCloud", "--placement_region=testRegion", "--placement_zone=testZone1"));
    tserverArgs.add(Arrays.asList(
        "--placement_cloud=testCloud", "--placement_region=testRegion", "--placement_zone=testZone2"));
    createMiniCluster(3, tserverArgs);
    LOG.info("created mini cluster");
    
    List<org.yb.Common.CloudInfoPB> leaders = new ArrayList<org.yb.Common.CloudInfoPB>();
    
    org.yb.Common.CloudInfoPB.Builder cloudInfoBuilder = org.yb.Common.CloudInfoPB.newBuilder().
    setPlacementCloud("testCloud").setPlacementRegion("testRegion");
    
    org.yb.Common.CloudInfoPB ci0 = cloudInfoBuilder.setPlacementZone("testZone0").build();
    org.yb.Common.CloudInfoPB ci1 = cloudInfoBuilder.setPlacementZone("testZone1").build();
    org.yb.Common.CloudInfoPB ci2 = cloudInfoBuilder.setPlacementZone("testZone2").build();
    
    // First, making the first two zones affinitized leaders.
    leaders.add(ci0);
    leaders.add(ci1);
    
    ModifyClusterConfigAffinitizedLeaders operation =
        new ModifyClusterConfigAffinitizedLeaders(syncClient, leaders);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    
    List<ColumnSchema> columns = new ArrayList<>(hashKeySchema.getColumns());
    Schema newSchema = new Schema(columns);
    CreateTableOptions tableOptions = new CreateTableOptions().setNumTablets(8);
    YBTable table = syncClient.createTable(DEFAULT_KEYSPACE_NAME, "AffinitizedLeaders", newSchema, tableOptions);
    
    assertTrue(syncClient.waitForAreLeadersOnPreferredOnlyCondition(DEFAULT_TIMEOUT_MS));
    
    leaders.clear();
    //Now make only the third zone an affinitized leader.
    leaders.add(ci2);
    
    operation = new ModifyClusterConfigAffinitizedLeaders(syncClient, leaders);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    
    assertTrue(syncClient.waitForAreLeadersOnPreferredOnlyCondition(DEFAULT_TIMEOUT_MS));
    
    // Now have no affinitized leaders, should balance 2, 3, 3.
    leaders.clear();
    operation = new ModifyClusterConfigAffinitizedLeaders(syncClient, leaders);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    
    assertTrue(syncClient.waitForAreLeadersOnPreferredOnlyCondition(DEFAULT_TIMEOUT_MS));
    
    // Now balance all affinitized leaders, should take no balancing steps.
    leaders.add(ci0);
    leaders.add(ci1);
    leaders.add(ci2);
    operation = new ModifyClusterConfigAffinitizedLeaders(syncClient, leaders);
    try {
      operation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
    
    assertTrue(syncClient.waitForAreLeadersOnPreferredOnlyCondition(DEFAULT_TIMEOUT_MS));
  }

  /**
   * Test creating, opening and deleting a redis table through a YBClient.
   */
  @Test(timeout = 100000)
  public void testRedisTable() throws Exception {
    LOG.info("Starting testRedisTable");
    String redisTableName = YBClient.REDIS_DEFAULT_TABLE_NAME;
    YBTable table = syncClient.createRedisTable(redisTableName);
    assertFalse(syncClient.getTablesList().getTablesList().isEmpty());
    assertTrue(syncClient.getTablesList().getTablesList().contains(redisTableName));
    assertEquals(TableType.REDIS_TABLE_TYPE, table.getTableType());

    table = syncClient.openTable(YBClient.REDIS_KEYSPACE_NAME, redisTableName);
    assertEquals(redisSchema.getColumnCount(), table.getSchema().getColumnCount());
    assertEquals(TableType.REDIS_TABLE_TYPE, table.getTableType());
    assertEquals(YBClient.REDIS_KEYSPACE_NAME, table.getKeyspace());

    syncClient.deleteTable(YBClient.REDIS_KEYSPACE_NAME, redisTableName);
    assertFalse(syncClient.getTablesList().getTablesList().contains(tableName));
  }

  /**
   * Test creating a CQL keyspace through a YBClient.
   */
  @Test(timeout = 100000)
  public void testKeySpace() throws Exception {
    LOG.info("Starting KeySpaceTable");
    // Check that we can create a keyspace.
    CreateKeyspaceResponse resp = syncClient.createKeyspace("testKeySpace");
    assertFalse(resp.hasError());
 }

  /**
   * Test creating and deleting a table through a YBClient.
   */
  @Test(timeout = 100000)
  public void testCreateDeleteTable() throws Exception {
    LOG.info("Starting testCreateDeleteTable");
    // Check that we can create a table.
    syncClient.createTable(DEFAULT_KEYSPACE_NAME, tableName, basicSchema, new CreateTableOptions());
    assertFalse(syncClient.getTablesList().getTablesList().isEmpty());
    assertTrue(syncClient.getTablesList().getTablesList().contains(tableName));

    // Check that we can delete it.
    syncClient.deleteTable(DEFAULT_KEYSPACE_NAME, tableName);
    assertFalse(syncClient.getTablesList().getTablesList().contains(tableName));

    // Check that we can re-recreate it, with a different schema.
    List<ColumnSchema> columns = new ArrayList<>(basicSchema.getColumns());
    columns.add(new ColumnSchema.ColumnSchemaBuilder("one more", Type.STRING).build());
    Schema newSchema = new Schema(columns);
    syncClient.createTable(DEFAULT_KEYSPACE_NAME, tableName, newSchema);

    // Check that we can open a table and see that it has the new schema.
    YBTable table = syncClient.openTable(DEFAULT_KEYSPACE_NAME, tableName);
    assertEquals(newSchema.getColumnCount(), table.getSchema().getColumnCount());
  }
}
