/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Result;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

@RunWith(JUnitParamsRunner.class)
public class UniverseActionsControllerTest extends UniverseControllerTestBase {
  @Test
  public void testUniverseBackupFlagSuccess() {
    Universe u = createUniverse(customer.getCustomerId());
    String url =
        "/api/customers/"
            + customer.uuid
            + "/universes/"
            + u.universeUUID
            + "/update_backup_state?markActive=true";
    Result result = doRequestWithAuthToken("PUT", url, authToken);
    assertOk(result);
    assertThat(
        Universe.getOrBadRequest(u.universeUUID).getConfig().get(Universe.TAKE_BACKUPS),
        allOf(notNullValue(), equalTo("true")));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseBackupFlagFailure() {
    Universe u = createUniverse(customer.getCustomerId());
    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/update_backup_state";
    Result result = doRequestWithAuthToken("PUT", url, authToken);
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testResetVersionUniverse() {
    Universe u = createUniverse("TestUniverse", customer.getCustomerId());
    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/setup_universe_2dc";
    assertNotEquals(Universe.getOrBadRequest(u.universeUUID).version, -1);
    Result result = doRequestWithAuthToken("PUT", url, authToken);
    assertOk(result);
    assertEquals(Universe.getOrBadRequest(u.universeUUID).version, -1);
  }

  @Test
  public void testUniverseSetKey() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    // Create the universe with encryption enabled through SMARTKEY KMS provider
    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams createTaskParams = new UniverseDefinitionTaskParams();
    ObjectNode createBodyJson = (ObjectNode) Json.toJson(createTaskParams);

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "encryptionAtRestUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("enableNodeToNodeEncrypt", true)
            .put("enableClientToNodeEncrypt", true)
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
            .put("ycqlPassword", "@123Byte")
            .put("enableYSQL", "false")
            .put("accessKeyCode", accessKeyCode);

    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    ObjectNode cloudInfo = Json.newObject();
    cloudInfo.put("region", "region1");
    ObjectNode nodeDetails = Json.newObject();
    nodeDetails.put("nodeName", "testing-1");
    nodeDetails.set("cloudInfo", cloudInfo);
    ArrayNode nodeDetailsSet = Json.newArray().add(nodeDetails);
    createBodyJson.set("clusters", clustersJsonArray);
    createBodyJson.set("nodeDetailsSet", nodeDetailsSet);
    createBodyJson.put("nodePrefix", "demo-node");

    String createUrl = "/api/customers/" + customer.uuid + "/universes";

    final ArrayNode keyOps = Json.newArray().add("EXPORT").add("APPMANAGEABLE");
    ObjectNode createPayload =
        Json.newObject().put("name", "some name").put("obj_type", "AES").put("key_size", "256");
    createPayload.set("key_ops", keyOps);

    Result createResult =
        doRequestWithAuthTokenAndBody("POST", createUrl, authToken, createBodyJson);
    assertOk(createResult);
    JsonNode json = Json.parse(contentAsString(createResult));
    assertNotNull(json.get("universeUUID"));
    String testUniUUID = json.get("universeUUID").asText();

    fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    // Rotate the universe key
    EncryptionAtRestKeyParams taskParams = new EncryptionAtRestKeyParams();
    ObjectNode bodyJson = (ObjectNode) Json.toJson(taskParams);
    bodyJson.put("configUUID", kmsConfig.configUUID.toString());
    bodyJson.put("algorithm", "AES");
    bodyJson.put("key_size", "256");
    bodyJson.put("key_op", "ENABLE");
    String url = "/api/customers/" + customer.uuid + "/universes/" + testUniUUID + "/set_key";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.SetUniverseKey), argCaptor.capture());
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testUniversePauseValidUUID() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/pause";
    Result result = doRequestWithAuthToken("POST", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Pause)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseResumeValidUUID() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/resume";
    Result result = doRequestWithAuthToken("POST", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Resume)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseToggleTlsWithEmptyParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = prepareUniverseForToggleTls(false, false, null);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject()));
    assertBadRequest(result, "This field is required");

    ArgumentCaptor<UpgradeParams> argCaptor = ArgumentCaptor.forClass(UpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.UpgradeUniverse), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseToggleTlsWithInvalidUpgradeOption() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = prepareUniverseForToggleTls(false, false, null);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    ObjectNode bodyJson = prepareRequestBodyForToggleTls(true, true, null);
    bodyJson.put("upgradeOption", "ROLLING");
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Invalid upgrade option");

    ArgumentCaptor<UpgradeParams> argCaptor = ArgumentCaptor.forClass(UpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.UpgradeUniverse), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseToggleTlsWithNoChangeInParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = prepareUniverseForToggleTls(false, false, null);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    ObjectNode bodyJson = prepareRequestBodyForToggleTls(false, false, null);
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "No changes in Tls parameters");

    ArgumentCaptor<UpgradeParams> argCaptor = ArgumentCaptor.forClass(UpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.UpgradeUniverse), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseToggleTlsWithInvalidRootCa() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = prepareUniverseForToggleTls(false, false, null);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    ObjectNode bodyJson = prepareRequestBodyForToggleTls(true, false, UUID.randomUUID());
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "No valid rootCA");

    ArgumentCaptor<UpgradeParams> argCaptor = ArgumentCaptor.forClass(UpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.UpgradeUniverse), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseToggleTlsWithRootCaUpdate() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID certUUID1 = CertificateHelper.createRootCA("test cert 1", customer.uuid, "/tmp/certs");
    UUID certUUID2 = CertificateHelper.createRootCA("test cert 2", customer.uuid, "/tmp/certs");
    UUID universeUUID = prepareUniverseForToggleTls(true, true, certUUID1);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    ObjectNode bodyJson = prepareRequestBodyForToggleTls(false, true, certUUID2);
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot update root certificate");

    ArgumentCaptor<UpgradeParams> argCaptor = ArgumentCaptor.forClass(UpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.UpgradeUniverse), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseToggleTlsWithNodesInTransit() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = prepareUniverseForToggleTls(false, false, null);
    setInTransitNode(universeUUID);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    ObjectNode bodyJson = prepareRequestBodyForToggleTls(true, true, null);
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "as it has nodes in one of");

    ArgumentCaptor<UpgradeParams> argCaptor = ArgumentCaptor.forClass(UpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.UpgradeUniverse), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseToggleTlsWithValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = prepareUniverseForToggleTls(false, false, null);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    ObjectNode bodyJson = prepareRequestBodyForToggleTls(true, true, null);
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    ArgumentCaptor<UpgradeParams> argCaptor = ArgumentCaptor.forClass(UpgradeParams.class);
    verify(mockCommissioner, times(1)).submit(eq(TaskType.UpgradeUniverse), argCaptor.capture());

    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    assertNotNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(1, customer.uuid);
  }

  private UUID prepareUniverseForToggleTls(
      boolean enableNodeToNodeEncrypt, boolean enableClientToNodeEncrypt, UUID rootCA) {
    UUID universeUUID = createUniverse(customer.getCustomerId()).universeUUID;
    Universe.saveDetails(universeUUID, ApiUtils.mockUniverseUpdater());
    // Update current TLS params
    Universe.saveDetails(
        universeUUID,
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          PlacementInfo placementInfo = universeDetails.getPrimaryCluster().placementInfo;
          UniverseDefinitionTaskParams.UserIntent userIntent =
              universeDetails.getPrimaryCluster().userIntent;
          userIntent.providerType = Common.CloudType.aws;
          userIntent.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
          userIntent.enableClientToNodeEncrypt = enableClientToNodeEncrypt;
          universeDetails.rootCA = rootCA;
          universeDetails.upsertPrimaryCluster(userIntent, placementInfo);
          universe.setUniverseDetails(universeDetails);
        });
    return universeUUID;
  }

  private ObjectNode prepareRequestBodyForToggleTls(
      boolean enableNodeToNodeEncrypt, boolean enableClientToNodeEncrypt, UUID rootCA) {
    return Json.newObject()
        .put("enableNodeToNodeEncrypt", enableNodeToNodeEncrypt)
        .put("enableClientToNodeEncrypt", enableClientToNodeEncrypt)
        .put("rootCA", rootCA != null ? rootCA.toString() : "");
  }
}
