// Copyright (c) YugabyteDB, Inc.
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

import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;

import java.sql.Connection;
import java.sql.Statement;
import java.util.*;
import java.util.stream.Collectors;

import org.junit.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.CommonNet;
import org.yb.CommonNet.CloudInfoPB;
import org.yb.client.*;
import org.yb.minicluster.MiniYBCluster;

/**
 * Tests that both TestTablespaceProperties and TestTablespacePropertiesWithPackedRowV2 run.
 * They cover the master reading tablespace placement out of the YSQL catalog (#29498 crashed
 * there under packed rows v2) and moving user data between tablespaces. The rest of
 * TestTablespaceProperties exercises placement rules that do not depend on the row format and
 * runs once.
 */
public abstract class BaseTablespacePropertiesTest extends BaseTablespaceTest {
  private static final Logger LOG = LoggerFactory.getLogger(BaseTablespacePropertiesTest.class);

  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(1500, 1700, 2000, 2000, 2000);
  }

  @Before
  public void setupTablespaces() {
    customTablespace.dropIfExists(connection);
    customTablespace.create(connection);
  }

  /*
   * Basic Tests
   */
  @Test
  public void sanityTest() throws Exception {
    YBClient client = miniCluster.getClient();

    // Set required YB-Master flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
    }

    createTestData("sanity_test");

    // Verify that the table was created and its tablets were placed
    // according to the tablespace replication info.
    LOG.info("Verify whether tablet replicas were placed correctly at creation time");
    verifyPlacement(false /* skipTransactionTables */);

    // Wait for tablespace info to be refreshed in load balancer.
    Thread.sleep(2 * 1000 * MASTER_REFRESH_TABLESPACE_INFO_SECS);

    addTserversAndWaitForLB();

    // Verify that the loadbalancer also placed the tablets of the table based on the
    // tablespace replication info.
    LOG.info(
        "Verify whether the load balancer maintained the placement of tablet replicas"
            + " after TServers were added");
    verifyPlacement(false /* skipTransactionTables */);

    // Trigger a master leader change.
    LeaderStepDownResponse resp = client.masterLeaderStepDown();
    assertFalse(resp.hasError());

    Thread.sleep(10 * MiniYBCluster.TSERVER_HEARTBEAT_INTERVAL_MS);

    LOG.info("Verify that tablets have been placed correctly even after master leader changed");
    verifyPlacement(false /* skipTransactionTables */);
  }

  @Test
  public void testAlterTablespacePlacementMovesExistingObjects() throws Exception {
    Tablespace alteredTablespace = new Tablespace(tablespaceName, Arrays.asList(2, 3));
    String tableName = "alter_tsp_base";
    String indexName = "alter_tsp_idx";
    String matViewName = "alter_tsp_mv";
    String tablegroupName = "alter_tsp_tgroup";
    String tablegroupTableName = "alter_tsp_tgroup_tbl";
    String partitionedTableName = "alter_tsp_part";
    String partitionName1 = "alter_tsp_part_p1";
    String partitionName2 = "alter_tsp_part_p2";
    // The partitioned parent does not own tablets, so verify its partitions.
    List<String> relations = Arrays.asList(
        tableName, indexName, matViewName, tablegroupTableName, partitionName1, partitionName2);

    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(String.format(
          "CREATE TABLE %s (a int) TABLESPACE %s", tableName, tablespaceName));
      setupStatement.execute(String.format(
          "CREATE INDEX %s ON %s(a) TABLESPACE %s", indexName, tableName, tablespaceName));
      setupStatement.execute(String.format(
          "CREATE MATERIALIZED VIEW %s TABLESPACE %s AS SELECT * FROM %s",
          matViewName, tablespaceName, tableName));
      setupStatement.execute(String.format(
          "CREATE TABLEGROUP %s TABLESPACE %s", tablegroupName, tablespaceName));
      setupStatement.execute(String.format(
          "CREATE TABLE %s (a int) TABLEGROUP %s", tablegroupTableName, tablegroupName));
      setupStatement.execute(String.format(
          "CREATE TABLE %s (a int) PARTITION BY RANGE (a) TABLESPACE %s",
          partitionedTableName, tablespaceName));
      setupStatement.execute(String.format(
          "CREATE TABLE %s PARTITION OF %s FOR VALUES FROM (0) TO (100) TABLESPACE %s",
          partitionName1, partitionedTableName, tablespaceName));
      setupStatement.execute(String.format(
          "CREATE TABLE %s PARTITION OF %s FOR VALUES FROM (100) TO (200) TABLESPACE %s",
          partitionName2, partitionedTableName, tablespaceName));
    }

    try {
      verifyPlacement(relations, customTablespace);

      try (Statement alterStatement = connection.createStatement()) {
        alterStatement.execute(String.format(
            "ALTER TABLESPACE %s SET (replica_placement='%s')",
            tablespaceName, alteredTablespace.toJson()));
      }

      Thread.sleep(2 * 1000 * MASTER_REFRESH_TABLESPACE_INFO_SECS);
      assertTrue(miniCluster.getClient().waitForLoadBalance(
          MASTER_LOAD_BALANCER_WAIT_TIME_MS, miniCluster.getTabletServers().size()));
      verifyPlacement(relations, alteredTablespace);

      try (Statement resetStatement = connection.createStatement()) {
        resetStatement.execute(String.format(
            "ALTER TABLESPACE %s RESET (replica_placement)", tablespaceName));
      }

      Thread.sleep(2 * 1000 * MASTER_REFRESH_TABLESPACE_INFO_SECS);
      assertTrue(miniCluster.getClient().waitForLoadBalance(
          MASTER_LOAD_BALANCER_WAIT_TIME_MS, miniCluster.getTabletServers().size()));
      verifyPlacement(relations, defaultTablespace);
    } finally {
      try (Statement cleanupStatement = connection.createStatement()) {
        cleanupStatement.execute(String.format("DROP TABLE IF EXISTS %s", tablegroupTableName));
        cleanupStatement.execute(String.format("DROP TABLEGROUP IF EXISTS %s", tablegroupName));
        cleanupStatement.execute(String.format("DROP MATERIALIZED VIEW IF EXISTS %s", matViewName));
        cleanupStatement.execute(String.format(
            "DROP TABLE IF EXISTS %s CASCADE", partitionedTableName));
        cleanupStatement.execute(String.format("DROP TABLE IF EXISTS %s CASCADE", tableName));
        cleanupStatement.execute(String.format(
            "ALTER TABLESPACE %s SET (replica_placement='%s')",
            tablespaceName, customTablespace.toJson()));
      }
    }
  }

  @Test
  public void testTableRewrite() throws Exception {
    final String dbname = "testdatabase";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATED=TRUE", dbname));
    }
    final String nonColocatedTable = "colocation_opt_out_table";
    final String nonColocatedIndex = "colocation_opt_out_idx";
    final String regularTable = "testtable";
    final String regularIndex = "testindex";
    final String tablespaceClause = "TABLESPACE " + tablespaceName;

    String[] databases = {dbname, "yugabyte"};
    String[] tables = {nonColocatedTable, regularTable};
    String[] indexes = {nonColocatedIndex, regularIndex};

    for (int i = 0; i < databases.length; i++) {
      try (Connection connection2 = getConnectionBuilder().withDatabase(databases[i]).connect();
        Statement stmt = connection2.createStatement()) {
        stmt.execute(
          String.format(
            "CREATE TABLE %s (h INT, a INT) WITH (colocated = false) %s",
            tables[i], tablespaceClause));
        stmt.execute(
          String.format(
            "CREATE INDEX %s ON %s (a) %s",
            indexes[i], tables[i], tablespaceClause));
        // Perform table rewrite.
        stmt.execute(
          String.format(
            "ALTER TABLE %s ADD PRIMARY KEY (h)",
            tables[i], tables[i]));
      }
      verifyTablePlacement(tables[i], customTablespace);
      verifyTablePlacement(indexes[i], customTablespace);
    }

    // Master failover can outlast the default 30s ysql lease in slow builds; an expired lease
    // kills all pg sessions, including the test connection.
    YBClient client = miniCluster.getClient();
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "master_ysql_operation_lease_ttl_ms", "90000"));
    }
    // Wait for tservers to refresh their leases with the new TTL.
    Thread.sleep(5000);

    findAndKillMasterLeader();
    waitForMasterLeader(MASTER_LEADER_TIMEOUT_MS);

    // Test that ALTER ... SET TABLESPACE works after table rewrite + master failover.
    Tablespace ts2 = new Tablespace("tablespace_2", Collections.singletonList(1));
    ts2.create(connection);

    for (int i = 0; i < databases.length; i++) {
      try (Connection connection2 = getConnectionBuilder().withDatabase(databases[i]).connect();
        Statement stmt = connection2.createStatement()) {
      stmt.execute(String.format("ALTER TABLE %s SET TABLESPACE %s", tables[i], ts2.name));
      stmt.execute(String.format("ALTER INDEX %s SET TABLESPACE %s", indexes[i], ts2.name));
      }
      // Wait for load balancer to run.
      waitForLoadBalancer();

      verifyTablePlacement(tables[i], ts2);
      verifyTablePlacement(indexes[i], ts2);
    }
  }

  @Test
  public void readReplicaWithTablespaces() throws Exception {
    final YBClient client = miniCluster.getClient();
    // Set required YB-Master flags.
    for (HostAndPort hp : miniCluster.getMasters().keySet()) {
      assertTrue(client.setFlag(hp, "enable_ysql_tablespaces_for_placement", "true"));
    }

    createTestData("read_replica");
    int expectedTServers = miniCluster.getTabletServers().size() + 1;

    LOG.info("Adding a TServer for read-replica cluster");
    PlacementBlock zone1 = new PlacementBlock(1);
    Map<String, String> readReplicaPlacement = new HashMap<>(zone1.toPlacementMap());
    readReplicaPlacement.put("placement_uuid", "readcluster");
    miniCluster.startTServer(readReplicaPlacement);
    miniCluster.waitForTabletServers(expectedTServers);

    CloudInfoPB cloudInfo0 =
        CloudInfoPB.newBuilder()
            .setPlacementCloud("cloud1")
            .setPlacementRegion("region1")
            .setPlacementZone("zone1")
            .build();

    CommonNet.PlacementBlockPB placementBlock0 =
        CommonNet.PlacementBlockPB.newBuilder()
            .setCloudInfo(cloudInfo0)
            .setMinNumReplicas(1)
            .build();

    List<CommonNet.PlacementBlockPB> placementBlocksLive =
        new ArrayList<CommonNet.PlacementBlockPB>();
    placementBlocksLive.add(placementBlock0);

    List<CommonNet.PlacementBlockPB> placementBlocksReadOnly =
        new ArrayList<CommonNet.PlacementBlockPB>();
    placementBlocksReadOnly.add(placementBlock0);

    CommonNet.PlacementInfoPB livePlacementInfo =
        CommonNet.PlacementInfoPB.newBuilder()
            .addAllPlacementBlocks(placementBlocksLive)
            .setNumReplicas(1)
            .setPlacementUuid(ByteString.copyFromUtf8(""))
            .build();

    CommonNet.PlacementInfoPB readOnlyPlacementInfo =
        CommonNet.PlacementInfoPB.newBuilder()
            .addAllPlacementBlocks(placementBlocksReadOnly)
            .setNumReplicas(1)
            .setPlacementUuid(ByteString.copyFromUtf8("readcluster"))
            .build();

    List<CommonNet.PlacementInfoPB> readOnlyPlacements =
        Arrays.asList(readOnlyPlacementInfo);

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

    waitForLoadBalancer(expectedTServers);

    for (final String table : tablesWithDefaultPlacement) {
      verifyPlacementForReadReplica(table);
    }

    verifyPlacement(tablesWithCustomPlacement, customTablespace);

    try (Statement alterStatement = connection.createStatement()) {
      alterStatement.execute(String.format(
          "ALTER TABLESPACE %s SET (read_replica_placement='%s')",
          tablespaceName, new Tablespace(
              "read_replica_tablespace", Collections.singletonList(1)).toJson()));
    }
    waitForLoadBalancer(expectedTServers);
    List<String> customTablesWithoutTransactionTable = tablesWithCustomPlacement.stream()
        .filter(table -> !table.contains("transaction"))
        .collect(Collectors.toList());
    for (final String table : customTablesWithoutTransactionTable) {
      verifyPlacementForReadReplica(table, customTablespace.numReplicas, 1);
    }

    try (Statement alterStatement = connection.createStatement()) {
      alterStatement.execute(String.format(
          "ALTER TABLESPACE %s RESET (read_replica_placement)", tablespaceName));
    }
    waitForLoadBalancer(expectedTServers);
    verifyPlacement(tablesWithCustomPlacement, customTablespace);

    // Alter tables with custom tablespaces to move to pg_default.
    List<String> movedTables = moveCreatedTestDataToDefaultTablespace("read_replica");

    waitForLoadBalancer();
    for (final String table : movedTables) {
      verifyPlacementForReadReplica(table);
    }
  }

  void verifyPlacementForReadReplica(final String table) throws Exception {
    verifyPlacementForReadReplica(table, 1 /* expectedLiveReplicas */,
        1 /* expectedReadReplicas */);
  }

  void verifyPlacementForReadReplica(
      final String table, int expectedLiveReplicas, int expectedReadReplicas) throws Exception {
    LOG.info("Verifying placement for read replica for table " + table);
    final YBClient client = miniCluster.getClient();
    client.waitForReplicaCount(
        getTableFromName(table), expectedLiveReplicas + expectedReadReplicas, DEFAULT_TIMEOUT_MS);

    List<LocatedTablet> tabletLocations =
        getTableFromName(table).getTabletsLocations(DEFAULT_TIMEOUT_MS);

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();

      long readReplicas =
          replicas.stream().filter(replica -> replica.getRole().contains("READ_REPLICA")).count();

      long liveReplicas = replicas.size() - readReplicas;

      assertEquals(expectedLiveReplicas, liveReplicas);
      assertEquals(expectedReadReplicas, readReplicas);
    }
  }
}
