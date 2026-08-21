// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static com.yugabyte.yw.common.AWSUtil.AWS_ACCESS_KEY_ID_FIELDNAME;
import static com.yugabyte.yw.common.AWSUtil.AWS_SECRET_ACCESS_KEY_FIELDNAME;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.CustomerConfigMapper;
import api.v2.models.CustomerConfigInfo;
import api.v2.models.CustomerConfigSpec;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CustomerConfigMapperTest {

  @Test
  public void toCustomerConfig_mapsSpecAndInfo() {
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

    assertNotNull(out.getSpec());
    assertNotNull(out.getInfo());
    CustomerConfigSpec spec = out.getSpec();
    CustomerConfigInfo info = out.getInfo();

    assertEquals("cfg-name", spec.getConfigName());
    assertEquals(customerUuid, info.getCustomerUuid());
    assertEquals(CustomerConfigSpec.TypeEnum.ALERTS, spec.getType());
    assertEquals(CustomerConfig.ALERTS_PREFERENCES, spec.getName());
    assertNotNull(spec.getData());
    assertEquals("a@b.c", spec.getData().get("alertingEmail"));

    assertEquals(configUuid, info.getUuid());
    assertEquals(CustomerConfigInfo.StateEnum.ACTIVE, info.getState());
    assertFalse(info.getIsKubernetesOperatorControlled());
  }

  @Test
  public void toCustomerConfig_masksStorageCredentials() {
    CustomerConfig cfg =
        new CustomerConfig()
            .setConfigUUID(UUID.randomUUID())
            .setConfigName("aws-backup-config")
            .setCustomerUUID(UUID.randomUUID())
            .setType(CustomerConfig.ConfigType.STORAGE)
            .setName("S3")
            .setState(CustomerConfig.ConfigState.Active)
            .setData(
                Json.newObject()
                    .put(AWS_ACCESS_KEY_ID_FIELDNAME, "AKIAWTVAQKRJKD7UOSSH")
                    .put(
                        AWS_SECRET_ACCESS_KEY_FIELDNAME,
                        "hl5ZlGyRkKu8amsdkfcbd1eQqLmpqiE0Bzknk0JmX")
                    .put("BACKUP_LOCATION", "s3://backups"));

    ObjectNode expectedMasked = CommonUtils.maskConfig(cfg.getData());

    api.v2.models.CustomerConfig out = CustomerConfigMapper.INSTANCE.toCustomerConfig(cfg);

    assertEquals(
        expectedMasked.get(AWS_ACCESS_KEY_ID_FIELDNAME).asText(),
        out.getSpec().getData().get(AWS_ACCESS_KEY_ID_FIELDNAME));
    assertEquals(
        expectedMasked.get(AWS_SECRET_ACCESS_KEY_FIELDNAME).asText(),
        out.getSpec().getData().get(AWS_SECRET_ACCESS_KEY_FIELDNAME));
    assertNotEquals(
        "AKIAWTVAQKRJKD7UOSSH", out.getSpec().getData().get(AWS_ACCESS_KEY_ID_FIELDNAME));
    assertEquals("s3://backups", out.getSpec().getData().get("BACKUP_LOCATION"));
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

    assertEquals(CustomerConfigSpec.TypeEnum.STORAGE, out.getSpec().getType());
    assertNull(out.getSpec().getData());
  }
}
