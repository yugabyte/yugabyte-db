/*
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.kms.services.SmartKeyEARService;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CreateKMSConfigTest extends CommissionerBaseTest {

  private TaskInfo submitTask(KMSConfigTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateKMSConfig, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  // confGetter is not used in class SmartKeyEARService, so we can pass null.
  private void stubSmartKeyService() {
    when(mockEARManager.getServiceInstance(eq("SMARTKEY")))
        .thenReturn(new SmartKeyEARService(null));
  }

  private KMSConfigTaskParams createParams(String name) {
    KMSConfigTaskParams params = new KMSConfigTaskParams();
    params.configUUID = UUID.randomUUID();
    params.customerUUID = defaultCustomer.getUuid();
    params.kmsConfigName = name;
    params.kmsProvider = KeyProvider.SMARTKEY;
    params.providerConfig = Json.newObject().put("base_url", "api.amer.smartkey.io");
    return params;
  }

  @Test
  public void testCreateKMSConfigUsesPresetUUID() {
    stubSmartKeyService();
    KMSConfigTaskParams params = createParams("preset");
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());
    KmsConfig created = KmsConfig.get(params.configUUID);
    assertNotNull(created);
    assertEquals("preset", created.getName());
    assertEquals(defaultCustomer.getUuid(), created.getCustomerUUID());
  }

  @Test
  public void testCreateKMSConfigRetryIsNoOpWhenConfigExists() {
    stubSmartKeyService();
    KMSConfigTaskParams params = createParams("retried");
    assertEquals(Success, submitTask(params).getTaskState());
    assertEquals(Success, submitTask(params).getTaskState());
    assertEquals(1, KmsConfig.listKMSConfigs(defaultCustomer.getUuid()).size());
    assertNotNull(KmsConfig.get(params.configUUID));
  }
}
