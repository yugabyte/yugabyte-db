// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class DrConfigTest extends FakeDBApplication {
  private Universe sourceUniverse;
  private Universe targetUniverse;
  private Customer defaultCustomer;
  private CustomerConfig config;
  private BootstarpBackupParams backupRequestParams;

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
    defaultCustomer = ModelFactory.testCustomer();
    config = createData(defaultCustomer);
    sourceUniverse = createUniverse("source Universe");
    targetUniverse = createUniverse("target Universe");

    backupRequestParams = new BootstarpBackupParams();
    backupRequestParams.storageConfigUUID = config.getConfigUUID();
  }

  @Test
  public void testCreateDbScopedDrConfig() {
    Set<String> sourceDbIds = new HashSet<String>(Arrays.asList("db1", "db2"));
    DrConfig drConfig =
        DrConfig.create(
            "replication1",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            sourceDbIds);
    DrConfig found = DrConfig.getOrBadRequest(drConfig.getUuid());
    XClusterConfig activeXClusterConfig = found.getActiveXClusterConfig();

    assertEquals(drConfig.getUuid(), found.getUuid());
    assertEquals(sourceDbIds, activeXClusterConfig.getDbIds());
    assertEquals(0, activeXClusterConfig.getTableIds().size());
    assertEquals(ConfigType.Db, activeXClusterConfig.getType());
    assertEquals(config.getConfigUUID(), found.getStorageConfigUuid());
  }

  @Test
  public void testCreateTxnDrConfig() {
    Set<String> sourceTableIds = new HashSet<String>(Arrays.asList("table1", "table2", "table3"));
    DrConfig drConfig =
        DrConfig.create(
            "replication2",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            sourceTableIds,
            backupRequestParams);
    DrConfig found = DrConfig.getOrBadRequest(drConfig.getUuid());
    XClusterConfig activeXClusterConfig = found.getActiveXClusterConfig();

    assertEquals(drConfig.getUuid(), found.getUuid());
    assertEquals(sourceTableIds, activeXClusterConfig.getTableIds());
    assertEquals(0, activeXClusterConfig.getDbIds().size());
    assertEquals(ConfigType.Txn, activeXClusterConfig.getType());
    assertEquals(config.getConfigUUID(), found.getStorageConfigUuid());
  }
}
