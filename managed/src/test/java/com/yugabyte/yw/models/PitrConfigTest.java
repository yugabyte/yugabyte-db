// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

public class PitrConfigTest extends FakeDBApplication {

  private CustomerConfig config;

  @Before
  public void setUp() {
    Customer defaultCustomer = ModelFactory.testCustomer();
    config = createData(defaultCustomer);
  }

  private CustomerConfig createData(Customer customer) {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"Test\", \"configName\": \"Test\", \"type\": "
                + "\"STORAGE\", \"data\": {\"foo\": \"bar\"},"
                + "\"configUUID\": \"5e8e4887-343b-47dd-a126-71c822904c06\"}");
    return CustomerConfig.createWithFormData(customer.getUuid(), formData);
  }

  @Test
  public void testPitrConfigXClusterCount() {

    Universe sourceUniverse = createUniverse("source Universe");
    Universe targetUniverse = createUniverse("target Universe");

    BootstrapBackupParams backupRequestParams;
    backupRequestParams = new BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = config.getConfigUUID();

    Set<String> sourceDbIds = Set.of("db1", "db2");
    DrConfig drConfig1 =
        DrConfig.create(
            "replication1",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbIds,
            false);
    XClusterConfig xClusterConfig1 = drConfig1.getActiveXClusterConfig();

    CreatePitrConfigParams createPitrConfigParams = new CreatePitrConfigParams();
    createPitrConfigParams.setUniverseUUID(sourceUniverse.getUniverseUUID());
    createPitrConfigParams.customerUUID = Customer.get(sourceUniverse.getCustomerId()).getUuid();
    createPitrConfigParams.name = null;
    createPitrConfigParams.keyspaceName = "mockNamespace";
    createPitrConfigParams.tableType = TableType.PGSQL_TABLE_TYPE;
    createPitrConfigParams.retentionPeriodInSeconds = 1;
    createPitrConfigParams.xClusterConfig = xClusterConfig1;
    createPitrConfigParams.intervalInSeconds = 1;
    createPitrConfigParams.createdForDr = true;

    PitrConfig pitrConfig = PitrConfig.create(UUID.randomUUID(), createPitrConfigParams);
    xClusterConfig1.addPitrConfig(pitrConfig);
    pitrConfig.refresh();

    assertEquals(1, pitrConfig.getXClusterConfigs().size());
    assertEquals(xClusterConfig1.getUuid(), pitrConfig.getXClusterConfigs().get(0).getUuid());

    Universe secondTargetUniverse = createUniverse("second target Universe");
    // A -> B and A -> C.
    DrConfig drConfig2 =
        DrConfig.create(
            "replication2",
            sourceUniverse.getUniverseUUID(),
            secondTargetUniverse.getUniverseUUID(),
            backupRequestParams,
            new PitrParams(),
            sourceDbIds,
            false);
    XClusterConfig xClusterConfig2 = drConfig2.getActiveXClusterConfig();
    xClusterConfig2.addPitrConfig(pitrConfig);

    // Pitr Config is associated to 2 xcluster configs.
    pitrConfig.refresh();
    assertEquals(2, pitrConfig.getXClusterConfigs().size());
    assertEquals(xClusterConfig2.getUuid(), pitrConfig.getXClusterConfigs().get(1).getUuid());

    // xcluster_pitr should be cascade deleted when xcluster config is deleted.
    xClusterConfig2.delete();
    pitrConfig.refresh();
    assertEquals(1, pitrConfig.getXClusterConfigs().size());
    assertEquals(xClusterConfig1.getUuid(), pitrConfig.getXClusterConfigs().get(0).getUuid());
  }
}
