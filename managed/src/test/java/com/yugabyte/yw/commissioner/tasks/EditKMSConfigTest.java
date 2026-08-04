package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.ModelFactory;
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
public class EditKMSConfigTest extends CommissionerBaseTest {

  private TaskInfo submitTask(KMSConfigTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditKMSConfig, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testUpdateKMSConfig() {
    ObjectNode kmsConfigReq = Json.newObject().put("base_url", "api.amer.smartkey.io");
    ModelFactory.createKMSConfig(defaultCustomer.getUuid(), "SMARTKEY", kmsConfigReq, "test");
    UUID configUUID = KmsConfig.listKMSConfigs(defaultCustomer.getUuid()).get(0).getConfigUUID();
    // confGetter is not used in class SmartKeyEARService, so we can pass null.
    when(mockEARManager.getServiceInstance(eq("SMARTKEY")))
        .thenReturn(new SmartKeyEARService(null));
    KMSConfigTaskParams params = new KMSConfigTaskParams();
    params.configUUID = configUUID;
    params.customerUUID = defaultCustomer.getUuid();
    params.kmsConfigName = "test";
    params.kmsProvider = KeyProvider.SMARTKEY;
    kmsConfigReq.put("base_url", "api.eu.smartkey.io");
    params.providerConfig = kmsConfigReq;
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());
  }
}
