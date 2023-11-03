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
package org.yb.loadtest;

import com.datastax.oss.driver.api.core.ConsistencyLevel;
import com.datastax.oss.driver.api.core.CqlSession;
import com.datastax.oss.driver.api.core.CqlSessionBuilder;
import com.datastax.oss.driver.api.core.config.DefaultDriverOption;
import com.datastax.oss.driver.api.core.config.DriverConfigLoader;
import com.datastax.oss.driver.api.core.config.ProgrammaticDriverConfigLoaderBuilder;
import com.datastax.oss.driver.api.core.cql.PreparedStatement;
import com.datastax.oss.driver.api.core.cql.Row;
import com.datastax.oss.driver.api.querybuilder.SchemaBuilder;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import com.yugabyte.oss.driver.internal.core.loadbalancing.PartitionAwarePolicy;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ColumnSchema;
import org.yb.Common;
import org.yb.CommonTypes;
import org.yb.Schema;
import org.yb.Type;
import org.yb.client.*;
import org.yb.consensus.Metadata;
import org.yb.master.CatalogEntityInfo.PlacementBlockPB;
import org.yb.master.CatalogEntityInfo.PlacementInfoPB;
import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.YBTestRunner;

import java.net.InetSocketAddress;
import java.time.Duration;
import java.util.*;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

@RunWith(value= YBTestRunner.class)
public class TestReadReplica extends BaseMiniClusterTest {

  protected static final Logger LOG = LoggerFactory.getLogger(TestReadReplica.class);
  private static final String PLACEMENT_CLOUD = "testCloud";
  private static final String PLACEMENT_REGION_LIVE = "testRegionLive";
  private static final String PLACEMENT_REGION_READONLY= "testRegionReadOnly";
  private static final String PLACEMENT_ZONE = "testZone";
  private YBTable ybTable = null;
  private LocatedTablet tablet = null;
  private static final String TABLE_NAME = "test_consistency";
  protected static final String DEFAULT_TEST_KEYSPACE = "cql_test_keyspace";
  protected static volatile CqlSession session = null;
  protected static final String TSERVER_READ_METRIC =
    "handler_latency_yb_tserver_TabletServerService_Read";
  protected static final String TSERVER_WRITE_METRIC =
    "handler_latency_yb_tserver_TabletServerService_Write";
  private static final int NUM_OPS = 100;

  protected void createMiniClusterwithReadReplicas() throws Exception {
    final String liveTsPlacement = "live";
    final String readOnlyTsPlacement = "readOnly";
    destroyMiniCluster();

    List<Map<String, String>> perTserverFlags = new ArrayList<>();

    {
      for (int i = 0; i < 3; i++) {
        Map<String, String> livePlacement = getPlacementFlagMapLive(liveTsPlacement, i);
        Map<String, String> readOnlyPlacement = getPlacementFlagMapReadOnly(readOnlyTsPlacement, i);
        perTserverFlags.add(livePlacement);
        perTserverFlags.add(readOnlyPlacement);
      }
    }
    createMiniCluster(
      3,
      perTserverFlags.size(),
      // Master args, used to speed up the test.
      ImmutableMap.of(
        "load_balancer_max_concurrent_adds", "100",
        "load_balancer_max_concurrent_moves", "100",
        "load_balancer_max_concurrent_removals", "100"),
      Collections.emptyMap(),
      cb -> {
        cb.perTServerFlags(perTserverFlags);
      }, Collections.emptyMap());
    YBClient client = miniCluster.getClient();
    List<PlacementBlockPB> placementBlocksLive = new ArrayList<PlacementBlockPB>();
    for(int i = 0 ; i < 3; i++){
      org.yb.CommonNet.CloudInfoPB cloudInfo = org.yb.CommonNet.CloudInfoPB.newBuilder()
        .setPlacementCloud(PLACEMENT_CLOUD)
        .setPlacementRegion(PLACEMENT_REGION_LIVE+i)
        .setPlacementZone(PLACEMENT_ZONE+i)
        .build();
      PlacementBlockPB placementBlock = PlacementBlockPB.newBuilder().
        setCloudInfo(cloudInfo).setMinNumReplicas(1).build();
      placementBlocksLive.add(placementBlock);
    }

    org.yb.CommonNet.CloudInfoPB cloudInfo1 = org.yb.CommonNet.CloudInfoPB.newBuilder()
      .setPlacementCloud(PLACEMENT_CLOUD)
      .setPlacementRegion(PLACEMENT_REGION_READONLY)
      .setPlacementZone(PLACEMENT_ZONE)
      .build();

    PlacementBlockPB placementBlock1 = PlacementBlockPB.newBuilder().
      setCloudInfo(cloudInfo1).setMinNumReplicas(3).build();

    List<PlacementBlockPB> placementBlocksReadOnly =
      new ArrayList<PlacementBlockPB>();
    placementBlocksReadOnly.add(placementBlock1);

    PlacementInfoPB livePlacementInfo =
      PlacementInfoPB.newBuilder().addAllPlacementBlocks(placementBlocksLive).
        setNumReplicas(3).
        setPlacementUuid(ByteString.copyFromUtf8(liveTsPlacement)).build();

    PlacementInfoPB readOnlyPlacementInfo =
      PlacementInfoPB.newBuilder().addAllPlacementBlocks(placementBlocksReadOnly).
        setNumReplicas(3).
        setPlacementUuid(ByteString.copyFromUtf8(readOnlyTsPlacement)).build();

    List<PlacementInfoPB> readOnlyPlacements = Arrays.asList(readOnlyPlacementInfo);
    ModifyClusterConfigReadReplicas readOnlyOperation =
      new ModifyClusterConfigReadReplicas(client, readOnlyPlacements);
    try {
      readOnlyOperation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }

    ModifyClusterConfigLiveReplicas liveOperation =
      new ModifyClusterConfigLiveReplicas(client, livePlacementInfo);
    try {
      liveOperation.doCall();
    } catch (Exception e) {
      LOG.warn("Failed with error:", e);
      assertTrue(false);
    }
  }

  protected void createMiniClusterWithRRandPreferredLeader() throws Exception{
    createMiniClusterwithReadReplicas();
    YBClient client = miniCluster.getClient();
    org.yb.CommonNet.CloudInfoPB leader = org.yb.CommonNet.CloudInfoPB.newBuilder()
      .setPlacementCloud(PLACEMENT_CLOUD)
      .setPlacementRegion(PLACEMENT_REGION_LIVE+1)
      .setPlacementZone(PLACEMENT_ZONE+1)
      .build();

    List<org.yb.CommonNet.CloudInfoPB> leaders = new ArrayList<org.yb.CommonNet.CloudInfoPB>();

    leaders.add(leader);

    ModifyClusterConfigAffinitizedLeaders operation =
      new ModifyClusterConfigAffinitizedLeaders(client, leaders);

    try{
      operation.doCall();
    }
    catch(Exception e){
      assertTrue(false);
    }
  }

  public void setUpTable(CqlSession session) throws Exception {
    // Create a table using YBClient to enforce a single tablet.
    YBClient client = miniCluster.getClient();
    ColumnSchema.ColumnSchemaBuilder hash_column =
      new ColumnSchema.ColumnSchemaBuilder("h", Type.INT32);
    hash_column.hashKey(true);
    ColumnSchema.ColumnSchemaBuilder range_column =
      new ColumnSchema.ColumnSchemaBuilder("r", Type.INT32);
    range_column.rangeKey(true, ColumnSchema.SortOrder.ASC);
    ColumnSchema.ColumnSchemaBuilder regular_column =
      new ColumnSchema.ColumnSchemaBuilder("k", Type.INT32);

    CreateTableOptions options = new CreateTableOptions();
    options.setNumTablets(1);
    options.setTableType(org.yb.CommonTypes.TableType.YQL_TABLE_TYPE);
    ybTable = client.createTable(DEFAULT_TEST_KEYSPACE, TABLE_NAME, new Schema(
      Arrays.asList(hash_column.build(), range_column.build(), regular_column.build())), options);

    // Verify number of replicas.
    List<LocatedTablet> tablets = ybTable.getTabletsLocations(0);
    assertEquals(1, tablets.size());
    tablet = tablets.get(0);

    String stmt = String.format("INSERT INTO %s.%s(h, r, k) VALUES(? , ?, ?);",
      DEFAULT_TEST_KEYSPACE, TABLE_NAME);
    PreparedStatement insert_stmt = session.prepare(stmt);

    // Insert some rows.
    for (int idx = 0; idx < NUM_OPS; idx++) {
      // INSERT: Valid statement with column list.
      session.execute(insert_stmt
        .bind(Integer.valueOf(idx), Integer.valueOf(idx), Integer.valueOf(idx))
        .setConsistencyLevel(ConsistencyLevel.YB_CONSISTENT_PREFIX));
    }
    PreparedStatement select_stmt;
    stmt = String.format("select r from %s.%s where h = ? and k = ?;",
      DEFAULT_TEST_KEYSPACE, TABLE_NAME);
    select_stmt = session.prepare(stmt);
    for (int i = 1; i <= NUM_OPS; i++) {
      Row row = session.execute(select_stmt.bind(Integer.valueOf(i), Integer.valueOf(i))
        .setConsistencyLevel(ConsistencyLevel.YB_CONSISTENT_PREFIX)).one();
    }
  }
  public void createKeyspace(CqlSession cqlSession) throws Exception{
    cqlSession.execute(SchemaBuilder.createKeyspace(DEFAULT_TEST_KEYSPACE)
      .ifNotExists()
      .withSimpleStrategy(1)
      .withDurableWrites(true)
      .build());
    LOG.info("+ Keyspace '{}' created (if needed).", DEFAULT_TEST_KEYSPACE);
  }

  public CqlSession getCassandraClient( List<InetSocketAddress> contact_points) throws Exception {
    CqlSessionBuilder builder;
    builder = CqlSession.builder();
    ProgrammaticDriverConfigLoaderBuilder pBuilder = DriverConfigLoader.programmaticBuilder()
      .withDuration(DefaultDriverOption.CONNECTION_CONNECT_TIMEOUT, Duration.ofSeconds(10))
      .withDuration(DefaultDriverOption.CONNECTION_INIT_QUERY_TIMEOUT, Duration.ofSeconds(10))
      .withDuration(DefaultDriverOption.CONNECTION_SET_KEYSPACE_TIMEOUT, Duration.ofSeconds(10))
      .withDuration(DefaultDriverOption.CONTROL_CONNECTION_TIMEOUT, Duration.ofSeconds(10))
      .withClass(DefaultDriverOption.LOAD_BALANCING_POLICY_CLASS, PartitionAwarePolicy.class);
    builder.withConfigLoader(pBuilder.build());
    builder.addContactPoints(contact_points).withLocalDatacenter(PLACEMENT_REGION_READONLY);
    session = builder.build();
    createKeyspace(session);
    return session;
  }

  @Test()
  public void readfromReadReplica() throws Exception {

    MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS = Integer.MAX_VALUE;

    createMiniClusterwithReadReplicas();
    session = getCassandraClient(miniCluster.getCQLContactPoints());
    setUpTable(session);

    // Verify all reads went to the leader.
    Map<HostAndPort, MiniYBDaemon> tservers = miniCluster.getTabletServers();
    assertEquals(tservers.size(), tablet.getReplicas().size());
    long totalOps = 0;
    for (LocatedTablet.Replica replica : tablet.getReplicas()) {
      String host = replica.getRpcHost();
      int webPort = tservers.get(HostAndPort.fromParts(host, replica.getRpcPort())).getWebPort();
      Metrics metrics = new Metrics(host, webPort, "server");
      long numOpsread = metrics.getHistogram(TSERVER_READ_METRIC).totalCount;
      long numOpswrite = metrics.getHistogram(TSERVER_WRITE_METRIC).totalCount;
      totalOps += numOpsread;
      if (replica.getRole().equals(CommonTypes.PeerRole.LEADER.toString())) {
        assertEquals(0, numOpsread);
        assertEquals(NUM_OPS, numOpswrite);
      }
      else if(replica.getRole().equals(CommonTypes.PeerRole.READ_REPLICA.toString())){
        assertTrue(numOpsread > NUM_OPS/10);
        assertEquals(0, numOpswrite);
      }
      else {
        assertEquals(0, numOpsread);
        assertEquals(0, numOpswrite);
      }
    }
    assertTrue(totalOps >= NUM_OPS);


  }
  @Test()
  public void readfromReadReplicawithpreferredLeader() throws Exception {

    MiniYBCluster.CQL_NODE_LIST_REFRESH_SECS = Integer.MAX_VALUE;

    createMiniClusterWithRRandPreferredLeader();
    session = getCassandraClient(miniCluster.getCQLContactPoints());
    setUpTable(session);

    // Verify all reads went to the leader.
    Map<HostAndPort, MiniYBDaemon> tservers = miniCluster.getTabletServers();
    assertEquals(tservers.size(), tablet.getReplicas().size());
    long totalOps = 0;
    for (LocatedTablet.Replica replica : tablet.getReplicas()) {
      String host = replica.getRpcHost();
      int webPort = tservers.get(HostAndPort.fromParts(host, replica.getRpcPort())).getWebPort();
      Metrics metrics = new Metrics(host, webPort, "server");
      long numOpsread = metrics.getHistogram(TSERVER_READ_METRIC).totalCount;
      long numOpswrite = metrics.getHistogram(TSERVER_WRITE_METRIC).totalCount;
      totalOps += numOpsread;
      if (replica.getRole().equals(CommonTypes.PeerRole.LEADER.toString())) {
        assertEquals(0, numOpsread);
        assertEquals(NUM_OPS, numOpswrite);
      }
      else if(replica.getRole().equals(CommonTypes.PeerRole.READ_REPLICA.toString())){
        assertTrue(numOpsread > NUM_OPS/10);
        assertEquals(0, numOpswrite);
      }
      else {
        assertEquals(0, numOpsread);
        assertEquals(0, numOpswrite);
      }
    }
    assertTrue(totalOps >= NUM_OPS);


  }

  private Map<String, String> getPlacementFlagMapLive(String placementUuid, int i) {
    return ImmutableMap.of(
      "placement_cloud", PLACEMENT_CLOUD,
      "placement_region", PLACEMENT_REGION_LIVE + i,
      "placement_zone", PLACEMENT_ZONE + i,
      "placement_uuid", placementUuid);
  }
  private Map<String, String> getPlacementFlagMapReadOnly(String placementUuid, int i) {
    return ImmutableMap.of(
      "placement_cloud", PLACEMENT_CLOUD,
      "placement_region", PLACEMENT_REGION_READONLY,
      "placement_zone", PLACEMENT_ZONE,
      "placement_uuid", placementUuid);
  }
}
