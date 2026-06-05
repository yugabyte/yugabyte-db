// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.CustomerConfigMapper;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CustomerConfigMapperTest {

  @Test
  public void toCustomerConfig_mapsCoreFieldsAndData() {
    UUID configUuid = UUID.randomUUID();
    UUID customerUuid = UUID.randomUUID();
    CustomerConfig cfg =
        new CustomerConfig()
            .setConfigUUID(configUuid)
            .setConfigName("cfg-name")
            .setCustomerUUID(customerUuid)
            .setType(CustomerConfig.ConfigType.ALERTS)
            .setName(CustomerConfig.ALERTS_PREFERENCES)
            .setState(CustomerConfig.ConfigState.Active)
            .setData(Json.newObject().put("alertingEmail", "a@b.c"));

    api.v2.models.CustomerConfig out = CustomerConfigMapper.INSTANCE.toCustomerConfig(cfg);

    assertEquals(configUuid, out.getConfigUuid());
    assertEquals("cfg-name", out.getConfigName());
    assertEquals(customerUuid, out.getCustomerUuid());
    assertEquals(api.v2.models.CustomerConfig.TypeEnum.ALERTS, out.getType());
    assertEquals(CustomerConfig.ALERTS_PREFERENCES, out.getName());
    assertEquals(api.v2.models.CustomerConfig.StateEnum.ACTIVE, out.getState());
    assertFalse(out.getIsKubernetesOperatorControlled());
    assertNotNull(out.getData());
    assertEquals("a@b.c", out.getData().get("alertingEmail"));
  }

  @Test
  public void toCustomerConfig_nullData() {
    CustomerConfig cfg =
        new CustomerConfig()
            .setConfigUUID(UUID.randomUUID())
            .setConfigName("n")
            .setCustomerUUID(UUID.randomUUID())
            .setType(CustomerConfig.ConfigType.STORAGE)
            .setName("s")
            .setState(CustomerConfig.ConfigState.Active)
            .setData(null);

    api.v2.models.CustomerConfig out = CustomerConfigMapper.INSTANCE.toCustomerConfig(cfg);

    assertEquals(api.v2.models.CustomerConfig.TypeEnum.STORAGE, out.getType());
    assertNull(out.getData());
  }
}
