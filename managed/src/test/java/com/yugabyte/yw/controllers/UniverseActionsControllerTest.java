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

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class UniverseActionsControllerTest extends UniverseControllerTestBase {
  @Test
  public void testUniverseBackupFlagSuccess() {
    Universe u = createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/update_backup_state?markActive=true";
    Result result = doRequestWithAuthToken("PUT", url, authToken);
    assertOk(result);
    assertThat(
        Universe.getOrBadRequest(u.getUniverseUUID()).getConfig().get(Universe.TAKE_BACKUPS),
        allOf(notNullValue(), equalTo("true")));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseBackupFlagFailure() {
    Universe u = createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/update_backup_state";
    Result result = doRequestWithAuthToken("PUT", url, authToken);
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testResetVersionUniverse() {
    Universe u = createUniverse("TestUniverse", customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/setup_universe_2dc";
    assertNotEquals(Universe.getOrBadRequest(u.getUniverseUUID()).getVersion(), -1);
    Result result = doRequestWithAuthToken("PUT", url, authToken);
    assertOk(result);
    assertEquals(Universe.getOrBadRequest(u.getUniverseUUID()).getVersion(), -1);
  }

  @Test
  public void testUniverseSetKey() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.SetUniverseKey);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    // Create the universe with encryption enabled through SMARTKEY KMS provider
    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

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
            .put("provider", p.getUuid().toString())
            .put("ycqlPassword", "@123Byte")
            .put("enableYSQL", "false")
            .put("accessKeyCode", accessKeyCode)
            .put("ybSoftwareVersion", "0.0.0.1-b1");

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    createBodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    createBodyJson.set("nodeDetailsSet", Json.newArray());
    createBodyJson.put("nodePrefix", "demo-node");

    String createUrl = "/api/customers/" + customer.getUuid() + "/universes";

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

    fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    // Rotate the universe key
    EncryptionAtRestKeyParams taskParams = new EncryptionAtRestKeyParams();
    ObjectNode bodyJson = (ObjectNode) Json.toJson(taskParams);
    bodyJson.put("configUUID", kmsConfig.getConfigUUID().toString());
    bodyJson.put("algorithm", "AES");
    bodyJson.put("key_size", "256");
    bodyJson.put("key_op", "ENABLE");
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + testUniUUID + "/set_key";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.SetUniverseKey), argCaptor.capture());
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testUniversePauseValidUUID() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.PauseUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

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
    Universe.saveDetails(u.getUniverseUUID(), updater);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/pause";
    Result result = doRequestWithAuthToken("POST", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Pause)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseResumeValidUUID() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.ResumeUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

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
    Universe.saveDetails(u.getUniverseUUID(), updater);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/resume";
    Result result = doRequestWithAuthToken("POST", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Resume)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUnlockUniverseSuccess() {
    Universe u = createUniverse(customer.getId());

    // Set universe to be in updating state.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.updateInProgress = true;
          universeDetails.updateSucceeded = false;
          universe.setUniverseDetails(universeDetails);
        };

    // Save the updates to the universe.
    u = Universe.saveDetails(u.getUniverseUUID(), updater);

    // Validate that universe in update state.
    assertEquals(true, u.getUniverseDetails().updateInProgress);
    assertEquals(false, u.getUniverseDetails().updateSucceeded);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/unlock";
    Result result = doRequestWithAuthToken("POST", url, authToken);
    assertOk(result);

    // Validate universe is unlocked.
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    assertEquals(false, u.getUniverseDetails().updateInProgress);
    assertEquals(true, u.getUniverseDetails().updateSucceeded);
  }
}
