// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.ArrayList;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class DrConfigTest extends FakeDBApplication {
  private Universe sourceUniverse;
  private Universe targetUniverse;
  private CustomerConfig config;
  private BootstrapBackupParams backupRequestParams;

  private CustomerConfig createData(Customer customer) {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"Test\", \"configName\": \"Test\", \"type\": "
                + "\"STORAGE\", \"data\": {\"foo\": \"bar\"},"
                + "\"configUUID\": \"5e8e4887-343b-47dd-a126-71c822904c06\"}");
    return CustomerConfig.createWithFormData(customer.getUuid(), formData);
  }

  @Before
  public void setUp() {
    Customer defaultCustomer = ModelFactory.testCustomer();
    config = createData(defaultCustomer);
    sourceUniverse = createUniverse("source Universe");
    targetUniverse = createUniverse("target Universe");

    backupRequestParams = new BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = config.getConfigUUID();
  }

  @Test
  public void testCreateDbScopedDrConfig() {
    Set<String> sourceDbIds = Set.of("db1", "db2");
    DrConfig drConfig =
        DrConfig.create(
            "replication1",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbIds,
            false);
    DrConfig found = DrConfig.getOrBadRequest(drConfig.getUuid());
    XClusterConfig activeXClusterConfig = found.getActiveXClusterConfig();

    assertEquals(drConfig.getUuid(), found.getUuid());
    assertEquals(sourceDbIds, activeXClusterConfig.getDbIds());
    assertEquals(0, activeXClusterConfig.getTableIds().size());
    assertEquals(ConfigType.Db, activeXClusterConfig.getType());
    assertEquals(config.getConfigUUID(), found.getStorageConfigUuid());
    assertTrue(
        activeXClusterConfig
            .getReplicationGroupName()
            .startsWith(sourceUniverse.getUniverseUUID().toString()));
  }

  @Test
  public void testCreateTxnDrConfig() {
    Set<String> sourceTableIds = Set.of("table1", "table2", "table3");
    DrConfig drConfig =
        DrConfig.create(
            "replication2",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            sourceTableIds,
            backupRequestParams,
            new PitrParams());
    DrConfig found = DrConfig.getOrBadRequest(drConfig.getUuid());
    XClusterConfig activeXClusterConfig = found.getActiveXClusterConfig();

    assertEquals(drConfig.getUuid(), found.getUuid());
    assertEquals(sourceTableIds, activeXClusterConfig.getTableIds());
    assertEquals(0, activeXClusterConfig.getDbIds().size());
    assertEquals(ConfigType.Txn, activeXClusterConfig.getType());
    assertEquals(config.getConfigUUID(), found.getStorageConfigUuid());
  }

  @Test
  public void testUniqueReplicationGroupName() {
    Set<String> sourceDbId1 = Set.of("db1", "db2");
    DrConfig drConfig1 =
        DrConfig.create(
            "replication1",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbId1,
            false);
    DrConfig found1 = DrConfig.getOrBadRequest(drConfig1.getUuid());
    XClusterConfig activeXClusterConfig1 = found1.getActiveXClusterConfig();
    assertEquals(drConfig1.getUuid(), found1.getUuid());

    Set<String> sourceDbId2 = Set.of("db2");
    DrConfig drConfig2 =
        DrConfig.create(
            "replication",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbId2,
            false);
    DrConfig found2 = DrConfig.getOrBadRequest(drConfig2.getUuid());
    XClusterConfig activeXClusterConfig2 = found2.getActiveXClusterConfig();
    assertEquals(drConfig2.getUuid(), found2.getUuid());
    assertNotEquals(
        activeXClusterConfig1.getReplicationGroupName(),
        activeXClusterConfig2.getReplicationGroupName());
  }

  @Test
  public void testDrConfigNoXClusterConfigAttached() {
    Set<String> sourceDbId1 = Set.of("db1", "db2");
    DrConfig drConfig =
        DrConfig.create(
            "replication1",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbId1,
            false);
    drConfig.setXClusterConfigs(new ArrayList<>());
    assertFalse(drConfig.hasActiveXClusterConfig());
    assertThrows(IllegalStateException.class, drConfig::getActiveXClusterConfig);
  }

  @Test
  public void testDrConfigHasExactlyOneXClusterConfig() {
    Set<String> sourceDbId1 = Set.of("db1", "db2");
    DrConfig drConfig =
        DrConfig.create(
            "replication1",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbId1,
            false);
    assertTrue(drConfig.hasActiveXClusterConfig());
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    assertNotNull(xClusterConfig);

    xClusterConfig.setSecondary(true);
    xClusterConfig.update();
    assertFalse(drConfig.hasActiveXClusterConfig());
    assertThrows(IllegalStateException.class, drConfig::getActiveXClusterConfig);
  }

  @Test
  public void testDrConfigHasNoActiveXClusterConfig() {
    Set<String> sourceDbId1 = Set.of("db1");
    DrConfig drConfig =
        DrConfig.create(
            "replication1",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbId1,
            false);

    drConfig.addXClusterConfig(
        targetUniverse.getUniverseUUID(), sourceUniverse.getUniverseUUID(), ConfigType.Db, false);
    drConfig.update();
    for (XClusterConfig xClusterConfig : drConfig.getXClusterConfigs()) {
      xClusterConfig.setSecondary(true);
      xClusterConfig.update();
    }

    assertFalse(drConfig.hasActiveXClusterConfig());
    assertThrows(IllegalStateException.class, drConfig::getActiveXClusterConfig);
  }
}
