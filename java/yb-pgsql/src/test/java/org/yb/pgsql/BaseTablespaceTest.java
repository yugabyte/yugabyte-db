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

import com.google.common.collect.ImmutableMap;
import com.yugabyte.util.PSQLException;

import java.sql.*;
import java.util.*;
import java.util.stream.Collectors;

import org.junit.*;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.YBTestRunner;
import org.yb.client.LocatedTablet;
import org.yb.client.TestUtils;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.MasterDdlOuterClass;
import org.yb.minicluster.MiniYBClusterBuilder;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public abstract class BaseTablespaceTest extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(BaseTablespaceTest.class);

  protected static final int MASTER_REFRESH_TABLESPACE_INFO_SECS = 2;

  protected static final int MASTER_LOAD_BALANCER_WAIT_TIME_MS = 120 * 1000;

  private static final int LOAD_BALANCER_MAX_CONCURRENT = 10;

  private static String placementUuid = "";

  // Custom tablespace to be used commonly across tests.
  static final String tablespaceName = "testTablespace";
  static final Tablespace customTablespace = new Tablespace(tablespaceName, Arrays.asList(1, 2));
  static final Tablespace defaultTablespace =
    new Tablespace("pg_default", Arrays.asList(1, 2, 3));
  private static final List<Map<String, String>> perTserverZonePlacementFlags =
    defaultTablespace.toPlacementMaps();

  // Tables that are created in default and custom tablespaces by the tests.
  // These will be used by the placement verification functions.
  ArrayList<String> tablesWithDefaultPlacement = new ArrayList<String>();
  ArrayList<String> tablesWithCustomPlacement = new ArrayList<String>();

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
    builder.addCommonTServerFlag("placement_uuid", placementUuid);
  }

  protected void setPlacementUuid(String uuid) {
    placementUuid = uuid;
  }

  /**
   * Sets placement_uuid on master.
   */
  @Override
  protected void afterStartingMiniCluster() throws Exception {
    super.afterStartingMiniCluster();
    if (placementUuid.isEmpty()) {
      return;
    }
    LOG.info("Modifying placement_info on master to set placement_uuid");
    runProcess(TestUtils.findBinary("yb-admin"),
      "--master_addresses", masterAddresses,
      "modify_placement_info",
      "cloud1.region1.zone1,cloud2.region2.zone2,cloud3.region3.zone3",
      Integer.toString(3),
      placementUuid);

    Thread.sleep(1000);
  }

  protected void createTestData (String prefixName) throws Exception {
    final String tablespaceClause = " TABLESPACE " + tablespaceName;

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
      setupStatement.execute(String.format("CREATE TABLEGROUP %s %s",
          customTablegroup, tablespaceClause));

      setupStatement.execute("CREATE TABLEGROUP " +  defaultTablegroup);

      // Create tables in default and custom tablespaces.
      setupStatement.execute(String.format("CREATE TABLE %s (a SERIAL) %s",
          customTable, tablespaceClause));

      setupStatement.execute(String.format("CREATE TABLE %s (a int CONSTRAINT %s " +
          "UNIQUE USING INDEX %s)", defaultTable, customIndex, tablespaceClause));

      // Create indexes in default and custom tablespaces.
      setupStatement.execute(String.format("CREATE INDEX %s on %s(a)",
          defaultIndexCustomTable, customTable));

      setupStatement.execute(String.format("CREATE INDEX %s on %s(a) %s",
          customIndexCustomTable, customTable, tablespaceClause));

      setupStatement.execute(String.format("CREATE INDEX %s on %s(a)",
          defaultIndex, defaultTable));

      // Create tables in tablegroups (in default and custom tablespaces)
      setupStatement.execute(String.format("CREATE TABLE %s (a SERIAL) TABLEGROUP %s",
          tableInDefaultTablegroup, defaultTablegroup));

      setupStatement.execute(String.format("CREATE TABLE %s (a SERIAL) TABLEGROUP %s",
          tableInCustomTablegroup, customTablegroup));
    }
    String transactionTableName = getTablespaceTransactionTableName(tablespaceName);
    tablesWithDefaultPlacement.clear();
    tablesWithDefaultPlacement.addAll(Arrays.asList(defaultTable, defaultIndex,
          defaultIndexCustomTable, tableInDefaultTablegroup));

    tablesWithCustomPlacement.clear();
    tablesWithCustomPlacement.addAll(Arrays.asList(customTable, customIndex,
          customIndexCustomTable, tableInCustomTablegroup, transactionTableName));
  }

  /*
   * Moves the tables and indexes created by createTestData to default tablespace.
   */
  List<String> moveCreatedTestDataToDefaultTablespace(String prefixName) throws Exception {
    final String tablespaceClause = " TABLESPACE pg_default";

    String customTable = prefixName + "_custom_table";
    String customIndex = prefixName + "_custom_index";
    String customIndexCustomTable = prefixName + "_custom_idx_on_custom_table";

    try (Statement setupStatement = connection.createStatement()) {
      // Move tables to default tablespace.
      setupStatement.execute(String.format("ALTER TABLE %s SET %s",
          customTable, tablespaceClause));

      // Move indexes to default tablespace.
      setupStatement.execute(String.format("ALTER INDEX %s SET %s",
          customIndex, tablespaceClause));

      setupStatement.execute(String.format("ALTER INDEX %s SET %s",
          customIndexCustomTable, tablespaceClause));
    }

    return Arrays.asList(customTable, customIndex, customIndexCustomTable);
  }

  protected void addTserversAndWaitForLB() throws Exception {
    int expectedTServers = miniCluster.getTabletServers().size() + 1;
    miniCluster.startTServer(perTserverZonePlacementFlags.get(1));
    miniCluster.waitForTabletServers(expectedTServers);

    waitForLoadBalancer(expectedTServers);
  }

  void waitForLoadBalancer() {
    LOG.info("Waiting for the loadbalancer to become active...");
    assertTrue(
        miniCluster.getClient().waitForLoadBalancerActive(MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    LOG.info("Waiting for the load balancer to become idle...");

    assertTrue(miniCluster.getClient().waitForLoadBalancerIdle(
      MASTER_LOAD_BALANCER_WAIT_TIME_MS));
  }

  void waitForLoadBalancer(int expectedTServers) {
    LOG.info("Waiting for the loadbalancer to become active...");
    assertTrue(
        miniCluster.getClient().waitForLoadBalancerActive(MASTER_LOAD_BALANCER_WAIT_TIME_MS));

    LOG.info("Waiting for the load balancer to become idle...");

    assertTrue(miniCluster.getClient().waitForLoadBalance(MASTER_LOAD_BALANCER_WAIT_TIME_MS,
      expectedTServers));
  }

  protected String getTablespaceTransactionTableName(String tablespace) throws Exception {
    try (Statement setupStatement = connection.createStatement()) {
      ResultSet s = setupStatement.executeQuery(
          "SELECT oid FROM pg_catalog.pg_tablespace " +
          "WHERE UPPER(spcname) = UPPER('" + tablespace + "')");
      assertTrue(s.next());
      return "transactions_" + s.getInt(1);
    }
  }

  protected YBTable getTableFromName(final String table) throws Exception {
    final YBClient client = miniCluster.getClient();
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tables =
      client.getTablesList(table).getTableInfoList();
    assertEquals("More than one table found with name " + table, 1, tables.size());
    return client.openTableByUUID(
      tables.get(0).getId().toStringUtf8());
  }

  protected void executeAndAssertErrorThrown(String statement, String err_msg) throws Exception{
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

  /*
   * Placement checker functions.
   */
  void verifyPlacement(boolean skipTransactionTables) throws Exception {
    verifyPlacement(tablesWithDefaultPlacement, defaultTablespace, skipTransactionTables);
    verifyPlacement(tablesWithCustomPlacement, customTablespace, skipTransactionTables);
  }

  void verifyDefaultPlacementForAll() throws Exception {
    verifyPlacement(tablesWithDefaultPlacement, defaultTablespace);
    verifyPlacement(tablesWithCustomPlacement, defaultTablespace);
  }

  void verifyPlacement(List<String> tables, Tablespace tablespace) throws Exception {
    verifyPlacement(tables, tablespace, false /* skipTransactionTables */);
  }

  void verifyPlacement(List<String> tables, Tablespace tablespace,
      boolean skipTransactionTables) throws Exception {
    if (skipTransactionTables) {
      tables = tables.stream()
        .filter(table -> !table.contains("transaction"))
        .collect(Collectors.toList());
    }
    for (final String table : tables) {
      verifyTablePlacement(table, tablespace);
    }
  }

  void verifyTablePlacement(final String table, Tablespace tablespace) throws Exception {
    LOG.info("Verifying placement for table " + table);

    final YBClient client = miniCluster.getClient();
    ensureReplicaCount(table, client, tablespace.numReplicas);

    List<LocatedTablet> tabletLocations = getTabletLocations(table);

    for (LocatedTablet tablet : tabletLocations) {
      verifyTabletPlacement(tablet, tablespace);
    }
  }

  private void verifyTabletPlacement(LocatedTablet tablet, Tablespace tablespace) {
    List<LocatedTablet.Replica> replicas = tablet.getReplicas();
    assertEquals(tablespace.numReplicas, replicas.size());

    int replicasInPlacement = 0;
    for (PlacementBlock placementBlock : tablespace.placementBlocks) {
      replicasInPlacement += countReplicasInPlacement(replicas, placementBlock);
    }
    assertEquals(tablespace.numReplicas, replicasInPlacement);
  }

  private int countReplicasInPlacement(
    List<LocatedTablet.Replica> replicas, PlacementBlock placementBlock) {
    return (int) replicas.stream()
      .map(LocatedTablet.Replica::getCloudInfo)
      .filter(placementBlock::isReplicaInPlacement)
      .count();
  }

  private void ensureReplicaCount(String table, YBClient client, int numReplicas) throws Exception {
    client.waitForReplicaCount(getTableFromName(table), numReplicas, DEFAULT_TIMEOUT_MS);
  }

  private List<LocatedTablet> getTabletLocations(String table) throws Exception {
    return getTableFromName(table).getTabletsLocations(DEFAULT_TIMEOUT_MS);
  }
}
