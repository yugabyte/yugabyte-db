package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.params.KMSConfigTaskParams;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.services.SmartKeyEARService;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.nio.charset.StandardCharsets;
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

  @Test
  public void testUpdateKMSConfigValidatesOwnKeyAfterRotation() {
    ObjectNode authConfig = Json.newObject().put("base_url", "api.amer.smartkey.io");
    UUID oldConfigUUID =
        ModelFactory.createKMSConfig(defaultCustomer.getUuid(), "SMARTKEY", authConfig, "old")
            .getConfigUUID();
    UUID newConfigUUID =
        ModelFactory.createKMSConfig(defaultCustomer.getUuid(), "SMARTKEY", authConfig, "new")
            .getConfigUUID();
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    UUID universeUUID = universe.getUniverseUUID();

    // The universe used the old config, then rotated its master key to the new config.
    byte[] oldKeyRef = "old-key-ref".getBytes(StandardCharsets.UTF_8);
    ObjectNode oldContext = Json.newObject().put("id", "old-key-id");
    EncryptionAtRestUtil.addKeyRef(universeUUID, oldConfigUUID, oldKeyRef, oldContext);
    EncryptionAtRestUtil.activateKeyRef(universeUUID, oldConfigUUID, oldKeyRef);
    byte[] newKeyRef = "new-key-ref".getBytes(StandardCharsets.UTF_8);
    EncryptionAtRestUtil.addKeyRef(universeUUID, newConfigUUID, newKeyRef, Json.newObject());
    EncryptionAtRestUtil.activateKeyRef(universeUUID, newConfigUUID, newKeyRef);

    EncryptionAtRestService mockService = mock(EncryptionAtRestService.class);
    when(mockEARManager.getServiceInstance(eq("SMARTKEY"))).thenReturn(mockService);
    when(mockService.validateConfigForUpdate(
            eq(universeUUID), eq(oldConfigUUID), eq(oldKeyRef), eq(oldContext), any(), any()))
        .thenReturn(new byte[] {1});
    when(mockService.updateAuthConfig(eq(oldConfigUUID), any()))
        .thenReturn(KmsConfig.get(oldConfigUUID));

    KMSConfigTaskParams params = new KMSConfigTaskParams();
    params.configUUID = oldConfigUUID;
    params.customerUUID = defaultCustomer.getUuid();
    params.kmsConfigName = "old";
    params.kmsProvider = KeyProvider.SMARTKEY;
    params.providerConfig = authConfig;
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockService, never())
        .validateConfigForUpdate(any(), any(), eq(newKeyRef), any(), any(), any());
  }
}
