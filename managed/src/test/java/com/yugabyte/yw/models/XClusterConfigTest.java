// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.TableType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig.Status;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;

public class XClusterConfigTest extends FakeDBApplication {
  private Universe sourceUniverse;
  private Universe targetUniverse;
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    sourceUniverse = createUniverse("source Universe");
    targetUniverse = createUniverse("target Universe");
  }

  @Test
  public void testCreateDbScopedXCluster() {
    Set<String> sourceDbIds = new HashSet<String>(Arrays.asList("db1", "db2"));
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            "xcluster config",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            XClusterConfigStatusType.Initialized,
            false /* imported */);
    xClusterConfig.addNamespaces(sourceDbIds);
    xClusterConfig.setTableType(TableType.YSQL);
    // Dr is only based on transactional or db scoped replication.
    xClusterConfig.setType(ConfigType.Db);
    xClusterConfig.update();

    XClusterConfig found = XClusterConfig.getOrBadRequest(xClusterConfig.getUuid());
    assertEquals(xClusterConfig.getUuid(), found.getUuid());
    assertEquals(sourceDbIds.size(), found.getDbIds().size());
    assertEquals(TableType.YSQL, found.getTableType());
    assertEquals(ConfigType.Db, found.getType());
  }

  @Test
  public void testAddRemoveNamespaces() {
    Set<String> dbIdsToAdd = new HashSet<String>(Arrays.asList("db1", "db2", "db3"));
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            "xcluster config",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            XClusterConfigStatusType.Initialized,
            false /* imported */);
    xClusterConfig.addNamespaces(dbIdsToAdd);
    Set<String> dbIdsToRemove = new HashSet<String>(Arrays.asList("db1", "db2"));

    XClusterConfig addConfig = XClusterConfig.getOrBadRequest(xClusterConfig.getUuid());
    assertEquals(3, addConfig.getNamespaces().size());
    addConfig.removeNamespaces(dbIdsToRemove);
    XClusterConfig removeConfig = XClusterConfig.getOrBadRequest(xClusterConfig.getUuid());
    assertEquals(1, removeConfig.getNamespaces().size());
  }

  @Test
  public void testXClusterTablesSortedByCode() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            "xcluster config",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            XClusterConfigStatusType.Initialized,
            false /* imported */);

    XClusterTableConfig running = new XClusterTableConfig();
    running.setStatus(Status.Running);
    XClusterTableConfig extraTableOnTarget = new XClusterTableConfig();
    extraTableOnTarget.setStatus(Status.ExtraTableOnTarget);
    XClusterTableConfig droppedFromSource = new XClusterTableConfig();
    droppedFromSource.setStatus(Status.DroppedFromSource);
    XClusterTableConfig unableToFetch = new XClusterTableConfig();
    unableToFetch.setStatus((Status.UnableToFetch));
    Set<XClusterTableConfig> tableConfigs =
        new HashSet<>(Arrays.asList(running, extraTableOnTarget, droppedFromSource, unableToFetch));
    xClusterConfig.setTables(tableConfigs);

    LinkedHashSet<XClusterTableConfig> sortedTablesSet =
        (LinkedHashSet) xClusterConfig.getTableDetails();
    ArrayList<XClusterTableConfig> sortedTablesList = new ArrayList<>(sortedTablesSet);
    assertEquals(Status.ExtraTableOnTarget, sortedTablesList.get(0).getStatus());
    assertEquals(Status.DroppedFromSource, sortedTablesList.get(1).getStatus());
    assertEquals(Status.UnableToFetch, sortedTablesList.get(2).getStatus());
    assertEquals(Status.Running, sortedTablesList.get(3).getStatus());
  }
}
