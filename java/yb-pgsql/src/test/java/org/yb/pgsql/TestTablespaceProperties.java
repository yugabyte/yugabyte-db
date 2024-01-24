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

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;

import com.yugabyte.util.PSQLException;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.*;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import org.junit.*;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.CommonNet.CloudInfoPB;
import org.yb.YBTestRunner;
import org.yb.client.*;
import org.yb.master.CatalogEntityInfo;
import org.yb.minicluster.MiniYBCluster;

@RunWith(value = YBTestRunner.class)
public class TestTablespaceProperties extends BaseTablespaceTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestTablespaceProperties.class);

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
  public void testDisabledTablespaces() throws Exception {
    markClusterNeedsRecreation();
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
    waitForLoadBalancer();

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
    markClusterNeedsRecreation();
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
    waitForLoadBalancer();

    // Verify that the loadbalancer placed the tablets of the table based on the
    // tablespace replication info. Skip checking transaction tables, as they would
    // not have been created when tablespaces were disabled.
    LOG.info(
        "Verify whether tablet replicas placed incorrectly at creation time are moved to "
            + "their appropriate placement by the load balancer");
    verifyPlacement(true /* skipTransactionTables */);
  }

  @Test
  public void negativeTest() throws Exception {
    markClusterNeedsRecreation();
    // Create tablespaces with invalid placement.
    try (Statement setupStatement = connection.createStatement()) {
      // Create a tablespace specifying a cloud that does not exist.
      Tablespace invalid_tblspc =
          new Tablespace(
              "invalid_tblspc",
              2,
              Arrays.asList(
                  new PlacementBlock(2), new PlacementBlock("cloud3", "region1", "zone1", 1)));
      invalid_tblspc.create(connection);

      // Create a tablespace wherein the individual min_num_replicas can be
      // satisfied, but the total replication factor cannot.
      Tablespace insufficient_rf_tblspc =
          new Tablespace(
              "insufficient_rf_tblspc",
              5,
              Arrays.asList(new PlacementBlock(1), new PlacementBlock(2)));
      insufficient_rf_tblspc.create(connection);

      // Create a valid tablespace.
      Tablespace valid_tblspc = new Tablespace("valid_tblspc", Arrays.asList(1, 2));
      valid_tblspc.create(connection);

      // Create a table that can be used for the index test below.
      setupStatement.execute("CREATE TABLE negativeTestTable (a int)");
    }

    final String not_enough_tservers_msg = "Not enough tablet servers in the requested placements";

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

    // Test creation of index when the replication factor cannot be satisfied.
    executeAndAssertErrorThrown(
        "CREATE INDEX invalidPlacementIdx ON negativeTestTable(a) "
            + "TABLESPACE insufficient_rf_tblspc",
        not_enough_tservers_msg);

    // Test creation of materialized view when the replication factor cannot be satisfied.
    executeAndAssertErrorThrown(
        "CREATE MATERIALIZED VIEW invalidPlacementMv TABLESPACE insufficient_rf_tblspc AS "
            + "SELECT * FROM negativeTestTable",
        not_enough_tservers_msg);

    // Test ALTER ... SET TABLESPACE

    // Create a table, index, and materialized view in the valid tablespace.
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute("CREATE TABLE validPlacementTable (a int) TABLESPACE valid_tblspc");
      setupStatement.execute(
          "CREATE INDEX validPlacementIdx ON validPlacementTable(a) TABLESPACE valid_tblspc");
      setupStatement.execute(
          "CREATE MATERIALIZED VIEW validPlacementMv TABLESPACE valid_tblspc AS "
              + "SELECT * FROM validPlacementTable");
    }

    // Test ALTER ... SET TABLESPACE to invalid tablespace.
    executeAndAssertErrorThrown(
        "ALTER TABLE validPlacementTable SET TABLESPACE invalid_tblspc", not_enough_tservers_msg);

    executeAndAssertErrorThrown(
        "ALTER INDEX validPlacementIdx SET TABLESPACE invalid_tblspc", not_enough_tservers_msg);

    executeAndAssertErrorThrown(
        "ALTER MATERIALIZED VIEW validPlacementMv SET TABLESPACE invalid_tblspc",
        not_enough_tservers_msg);

    // Test ALTER ... SET TABLESPACE to insufficient replication factor tablespace.
    executeAndAssertErrorThrown(
        "ALTER TABLE validPlacementTable SET TABLESPACE insufficient_rf_tblspc",
        not_enough_tservers_msg);

    executeAndAssertErrorThrown(
        "ALTER INDEX validPlacementIdx SET TABLESPACE insufficient_rf_tblspc",
        not_enough_tservers_msg);

    executeAndAssertErrorThrown(
        "ALTER MATERIALIZED VIEW validPlacementMv SET TABLESPACE insufficient_rf_tblspc",
        not_enough_tservers_msg);
  }

  /*
   * Colocation Tests
   */
  @Test
  public void disallowTableSpaceForIndexOnColocatedTables() throws Exception {
    markClusterNeedsRecreation();
    String customTablegroup = "test_custom_tablegroup";
    final String tablespaceClause = "TABLESPACE " + tablespaceName;
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(
          String.format("CREATE TABLEGROUP %s %s", customTablegroup, tablespaceClause));

      setupStatement.execute("CREATE TABLE t (a INT, b FLOAT) TABLEGROUP " + customTablegroup);

      // Verify that tablespace cannot be set for an index during creation.
      String errorMsg = "TABLESPACE is not supported for indexes on colocated tables.";
      executeAndAssertErrorThrown("CREATE INDEX t_idx2 ON t(b) " + tablespaceClause, errorMsg);

      // Verify that tablespace cannot be set for an index during alter.
      setupStatement.execute("CREATE INDEX t_idx1 ON t(a)");
      errorMsg = "cannot move colocated table to a different tablespace";
      executeAndAssertErrorThrown("ALTER INDEX t_idx1 SET " + tablespaceClause, errorMsg);
    }
  }

  @Test
  public void testTablesOptOutOfColocation() throws Exception {
    markClusterNeedsRecreation();
    final String dbname = "testdatabase";
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("CREATE DATABASE %s COLOCATED=TRUE", dbname));
    }

    final String colocatedTableName = "colocated_table";
    final String nonColocatedTable = "colocation_opt_out_table";
    final String nonColocatedIndex = "colocation_opt_out_idx";
    final String tablespaceClause = "TABLESPACE " + tablespaceName;
    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
        Statement stmt = connection2.createStatement()) {
      stmt.execute(
          String.format(
              "CREATE TABLE %s (h INT PRIMARY KEY, a INT, b FLOAT) WITH (colocated = false) %s",
              nonColocatedTable, tablespaceClause));
      stmt.execute(
          String.format(
              "CREATE INDEX %s ON %s (a) %s",
              nonColocatedIndex, nonColocatedTable, tablespaceClause));
      stmt.execute(
          String.format("CREATE TABLE %s (h INT PRIMARY KEY, a INT, b FLOAT)", colocatedTableName));
    }

    verifyTablePlacement(nonColocatedTable, customTablespace);
    verifyTablePlacement(nonColocatedIndex, customTablespace);

    // Test that ALTER ... SET TABLESPACE works on the non-colocated table/index.
    Tablespace ts1 = new Tablespace("tablespaceOptOutColocation", Collections.singletonList(1));
    ts1.create(connection);

    try (Connection connection2 = getConnectionBuilder().withDatabase(dbname).connect();
        Statement stmt = connection2.createStatement()) {
      stmt.execute(String.format("ALTER TABLE %s SET TABLESPACE %s", nonColocatedTable, ts1.name));
      stmt.execute(String.format("ALTER INDEX %s SET TABLESPACE %s", nonColocatedIndex, ts1.name));
    }

    // Wait for load balancer to run.
    waitForLoadBalancer();

    verifyTablePlacement(nonColocatedTable, ts1);
    verifyTablePlacement(nonColocatedIndex, ts1);
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

    CatalogEntityInfo.PlacementBlockPB placementBlock0 =
        CatalogEntityInfo.PlacementBlockPB.newBuilder()
            .setCloudInfo(cloudInfo0)
            .setMinNumReplicas(1)
            .build();

    List<CatalogEntityInfo.PlacementBlockPB> placementBlocksLive =
        new ArrayList<CatalogEntityInfo.PlacementBlockPB>();
    placementBlocksLive.add(placementBlock0);

    List<CatalogEntityInfo.PlacementBlockPB> placementBlocksReadOnly =
        new ArrayList<CatalogEntityInfo.PlacementBlockPB>();
    placementBlocksReadOnly.add(placementBlock0);

    CatalogEntityInfo.PlacementInfoPB livePlacementInfo =
        CatalogEntityInfo.PlacementInfoPB.newBuilder()
            .addAllPlacementBlocks(placementBlocksLive)
            .setNumReplicas(1)
            .setPlacementUuid(ByteString.copyFromUtf8(""))
            .build();

    CatalogEntityInfo.PlacementInfoPB readOnlyPlacementInfo =
        CatalogEntityInfo.PlacementInfoPB.newBuilder()
            .addAllPlacementBlocks(placementBlocksReadOnly)
            .setNumReplicas(1)
            .setPlacementUuid(ByteString.copyFromUtf8("readcluster"))
            .build();

    List<CatalogEntityInfo.PlacementInfoPB> readOnlyPlacements =
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

    // Alter tables with custom tablespaces to move to pg_default.
    List<String> movedTables = moveCreatedTestDataToDefaultTablespace("read_replica");

    waitForLoadBalancer();
    for (final String table : movedTables) {
      verifyPlacementForReadReplica(table);
    }
  }

  void verifyPlacementForReadReplica(final String table) throws Exception {
    LOG.info("Verifying placement for read replica for table " + table);
    final YBClient client = miniCluster.getClient();
    client.waitForReplicaCount(getTableFromName(table), 2, DEFAULT_TIMEOUT_MS);

    List<LocatedTablet> tabletLocations =
        getTableFromName(table).getTabletsLocations(DEFAULT_TIMEOUT_MS);

    // Get tablets for table.
    for (LocatedTablet tablet : tabletLocations) {
      List<LocatedTablet.Replica> replicas = tablet.getReplicas();

      long readReplicas =
          replicas.stream().filter(replica -> replica.getRole().contains("READ_REPLICA")).count();

      long liveReplicas = replicas.size() - readReplicas;

      // A live replica must be found.
      assertEquals(1, liveReplicas);

      // A read-only replica must be found.
      assertEquals(1, readReplicas);
    }
  }

  @Test
  public void testAlterTableSetTablespace() throws Exception {
    markClusterNeedsRecreation();

    String tableName = "testtable";
    String indexName = "testindex";
    String matViewName = "testmatview";
    try (Statement setupStatement = connection.createStatement()) {
      // Create table.
      setupStatement.execute(String.format("CREATE TABLE %s (a SERIAL)", tableName));

      // Create index.
      setupStatement.execute(String.format("CREATE INDEX %s ON %s(a)", indexName, tableName));

      // Create materialized view.
      setupStatement.execute(
          String.format("CREATE MATERIALIZED VIEW %s AS SELECT * FROM %s", matViewName, tableName));
    }

    // Verify placement.
    LOG.info("Verifying whether tablet replicas were placed correctly at creation time");
    verifyTablePlacement(tableName, defaultTablespace);
    verifyTablePlacement(indexName, defaultTablespace);
    verifyTablePlacement(matViewName, defaultTablespace);

    // Alter tablespace.
    try (Statement setupStatement = connection.createStatement()) {
      // Alter table set tablespace.
      setupStatement.execute(
          String.format("ALTER TABLE %s SET TABLESPACE %s", tableName, tablespaceName));
      // Alter index set tablespace.
      setupStatement.execute(
          String.format("ALTER INDEX %s SET TABLESPACE %s", indexName, tablespaceName));

      // Alter materialized view set tablespace.
      setupStatement.execute(
          String.format(
              "ALTER MATERIALIZED VIEW %s SET TABLESPACE %s", matViewName, tablespaceName));
    }

    // Verify placement.
    LOG.info("Verifying whether tablet replicas were placed correctly after alter");
    waitForLoadBalancer();
    verifyTablePlacement(tableName, customTablespace);
    verifyTablePlacement(indexName, customTablespace);
    verifyTablePlacement(matViewName, customTablespace);

    // Refresh materialized view.
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(String.format("REFRESH MATERIALIZED VIEW %s", matViewName));
    }

    // Verify placement.
    LOG.info("Verifying whether tablet replicas were placed correctly after REFRESH");
    verifyTablePlacement(matViewName, customTablespace);
  }

  /** Test ALTER TABLE SET TABLESPACE running concurrently with the load balancer. */
  @Test
  public void testAlterConcurrentWithLoadBalancer() throws Exception {
    markClusterNeedsRecreation();

    String tableName = "testtable";
    String indexName = "testindex";
    String matViewName = "testmatview";
    try (Statement setupStatement = connection.createStatement()) {
      // Create table.
      setupStatement.execute(String.format("CREATE TABLE %s (a SERIAL)", tableName));

      // Create index.
      setupStatement.execute(String.format("CREATE INDEX %s ON %s(a)", indexName, tableName));

      // Create materialized view.
      setupStatement.execute(
        String.format("CREATE MATERIALIZED VIEW %s AS SELECT * FROM %s", matViewName, tableName));
    }

    // Verify placement.
    LOG.info("Verifying whether tablet replicas were placed correctly at creation time");
    verifyTablePlacement(tableName, defaultTablespace);
    verifyTablePlacement(indexName, defaultTablespace);
    verifyTablePlacement(matViewName, defaultTablespace);

    // Alter tablespace.
    try (Statement setupStatement = connection.createStatement()) {
      // Alter table set tablespace.
      setupStatement.execute(
        String.format("ALTER TABLE %s SET TABLESPACE %s", tableName, tablespaceName));
      // Alter index set tablespace.
      setupStatement.execute(
        String.format("ALTER INDEX %s SET TABLESPACE %s", indexName, tablespaceName));

      // Alter materialized view set tablespace.
      setupStatement.execute(
        String.format(
          "ALTER MATERIALIZED VIEW %s SET TABLESPACE %s", matViewName, tablespaceName));
    }

    LOG.info("Waiting for the loadbalancer to become active...");
    assertTrue(
      miniCluster.getClient().waitForLoadBalancerActive(MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Alter tablespace concurrently with the load balancer.
    try (Statement setupStatement = connection.createStatement()) {
      // Alter table set tablespace.
      setupStatement.execute(
        String.format("ALTER TABLE %s SET TABLESPACE %s", tableName, defaultTablespace.name));
      // Alter index set tablespace.
      setupStatement.execute(
        String.format("ALTER INDEX %s SET TABLESPACE %s", indexName, defaultTablespace.name));

      // Alter materialized view set tablespace.
      setupStatement.execute(
        String.format(
          "ALTER MATERIALIZED VIEW %s SET TABLESPACE %s", matViewName, defaultTablespace.name));
    }

    LOG.info("Waiting for the load balancer to become idle...");
    assertTrue(miniCluster.getClient().waitForLoadBalancerIdle(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    // Verify placement.
    LOG.info("Verifying whether tablet replicas were placed correctly after alter");

    verifyTablePlacement(tableName, defaultTablespace);
    verifyTablePlacement(indexName, defaultTablespace);
    verifyTablePlacement(matViewName, defaultTablespace);
  }

  /**
   * Geo-partitioned table test
   *
   * <p>Creates a table with two partitions, each with its own tablespace, then moves one of the
   * partitions to a new tablespace.
   */
  @Test
  public void testTablespacePartitioning() throws Exception {
    markClusterNeedsRecreation();

    // Create original tablespace with a single placement block on cloud1.region1.zone1
    Tablespace ts1 = new Tablespace("testTablespaceZone1", Collections.singletonList(1));
    ts1.create(connection);

    // Create new tablespace with a single placment block on cloud2.region2.zone2
    Tablespace ts2 = new Tablespace("testTablespaceZone2", Collections.singletonList(2));
    ts2.create(connection);

    // Create new tablespace with a single placment block on cloud3.region3.zone3
    Tablespace ts3 = new Tablespace("testTablespaceZone3", Collections.singletonList(3));
    ts3.create(connection);

    // Create a table with two partitions, each with its own tablespace.
    final String table = "geo_partition_test_table";
    try (Statement stmt = connection.createStatement()) {
      // Create partitioned table.
      stmt.execute(String.format("CREATE TABLE %s (a int) PARTITION BY RANGE (a)", table));

      // Create partitions.
      stmt.execute(
          String.format(
              "CREATE TABLE %s_p1 PARTITION OF %s FOR VALUES FROM (0) TO (100) TABLESPACE %s",
              table, table, ts1.name));

      stmt.execute(
          String.format(
              "CREATE TABLE %s_p2 PARTITION OF %s FOR VALUES FROM (100) TO (200) TABLESPACE %s",
              table, table, ts2.name));

      // Create indexes on the partitions.
      stmt.execute(
          String.format(
              "CREATE INDEX %s_idx_p1 ON %s_p1(a) TABLESPACE %s", table, table, ts1.name));

      stmt.execute(
          String.format(
              "CREATE INDEX %s_idx_p2 ON %s_p2(a) TABLESPACE %s", table, table, ts2.name));
    }

    LOG.info("Verifying whether tablet replicas were placed correctly at creation time");
    verifyTablePlacement("geo_partition_test_table_p1", ts1);
    verifyTablePlacement("geo_partition_test_table_p2", ts2);
    verifyTablePlacement("geo_partition_test_table_idx_p1", ts1);
    verifyTablePlacement("geo_partition_test_table_idx_p2", ts2);

    // Execute the ALTER TABLE SET TABLESPACE command
    LOG.info("Moving table to new tablespace" + tablespaceName);
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(String.format("ALTER TABLE %s_p2 SET TABLESPACE %s", table, ts3.name));
      stmt.execute(String.format("ALTER INDEX %s_idx_p2 SET TABLESPACE %s", table, ts3.name));
    }

    waitForLoadBalancer();

    LOG.info("Verifying whether tablet replicas were moved to the new tablespace");
    verifyTablePlacement("geo_partition_test_table_p2", ts3);
    verifyTablePlacement("geo_partition_test_table_idx_p2", ts3);

    // Verify that the tablets for the other partition were not moved.
    verifyTablePlacement("geo_partition_test_table_p1", ts1);
    verifyTablePlacement("geo_partition_test_table_idx_p1", ts1);
  }

  @Test
  public void testAlterTableSetTablespaceWithPlacementUuid() throws Exception {
    setPlacementUuid("placement_uuid");
    try {
      testAlterTableWithPlacementUuidHelper();
    } finally {
      // Unset the placementUuid for future tests.
      setPlacementUuid("");
    }
  }

  private void testAlterTableWithPlacementUuidHelper() throws Exception {
    restartCluster();
    markClusterNeedsRecreation();

    // Create a table.
    final String testTable = "test_table";
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(String.format("CREATE TABLE %s (a SERIAL)", testTable));
    }

    LOG.info("Verifying whether tablet replicas were placed correctly at creation time");
    verifyTablePlacement(testTable, defaultTablespace);

    // Execute the ALTER TABLE SET TABLESPACE command
    setupTablespaces();
    LOG.info("Moving test_table to new tablespace " + tablespaceName);
    try (Statement setupStatement = connection.createStatement()) {
      setupStatement.execute(
          String.format("ALTER TABLE %s SET TABLESPACE %s", testTable, tablespaceName));
    }

    // Wait for loadbalancer to run.
    waitForLoadBalancer();

    LOG.info("Verifying whether tablet replicas were moved to the new tablespace");
    verifyTablePlacement(testTable, customTablespace);
  }
}
