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
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertYWSE;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static junit.framework.TestCase.assertFalse;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Matchers;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public abstract class UniverseCreateControllerTestBase extends UniverseControllerTestBase {
  protected abstract String universeCreateUrl();

  protected abstract JsonNode getUniverseJson(Result universeCreateResponse);

  private void checkTaskUUID(UUID fakeTaskUUID, Result universeCreateResponse) {
    String taskUUID = Json.parse(contentAsString(universeCreateResponse)).get("taskUUID").asText();
    assertEquals(taskUUID, fakeTaskUUID.toString());
  }

  /** Migrated to {@link UniverseClustersControllerTest} */
  @Test
  public void testUniverseCreateWithInvalidParams() {
    String url = universeCreateUrl();
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject()));
    assertBadRequest(result, "clusters: This field is required");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithInvalidUniverseName() {
    String url = universeCreateUrl();
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "Foo_Bar")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Invalid universe name format, valid characters [a-zA-Z0-9-].");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithSingleAvailabilityZones() {
    String url = universeCreateUrl();
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    JsonNode json = getUniverseJson(result);
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("universeDetails"));
    assertNotNull(json.get("universeConfig"));
    // setTxnTableWaitCountFlag will be false as enableYSQL is false in this case
    assertFalse(json.get("universeDetails").get("setTxnTableWaitCountFlag").asBoolean());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("SingleUserUniverse")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithYsqlEnabled() {
    String url = universeCreateUrl();
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
            .put("accessKeyCode", accessKeyCode)
            .put("enableYSQL", "true");
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    JsonNode json = getUniverseJson(result);
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("universeDetails"));
    assertNotNull(json.get("universeConfig"));
    // setTxnTableWaitCountFlag should be true as enableYSQL is true in this case
    assertTrue(json.get("universeDetails").get("setTxnTableWaitCountFlag").asBoolean());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("SingleUserUniverse")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  @Parameters({
    "true, false, false, false, false",
    "true, true, false, true, false",
    "true, false, true, true, true",
    "true, true, true, true, true",
    "false, false, true, false, true",
    "false, true, true, true, true",
  })
  public void testUniverseCreateForSelfSignedTLS(
      boolean rootAndClientRootCASame,
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt,
      boolean rootCAExists,
      boolean clientRootCAExists) {
    String url = universeCreateUrl();
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("enableNodeToNodeEncrypt", enableNodeToNodeEncrypt)
            .put("enableClientToNodeEncrypt", enableClientToNodeEncrypt)
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
            .put("accessKeyCode", accessKeyCode);

    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    ObjectNode bodyJson = Json.newObject().put("nodePrefix", "demo-node");
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());
    bodyJson.put("rootAndClientRootCASame", rootAndClientRootCASame);

    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    ArgumentCaptor<UniverseTaskParams> taskParams =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    assertOk(result);
    checkTaskUUID(fakeTaskUUID, result);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), taskParams.capture());
    UniverseDefinitionTaskParams taskParam = (UniverseDefinitionTaskParams) taskParams.getValue();
    if (rootCAExists) assertNotNull(taskParam.rootCA);
    else assertNull(taskParam.rootCA);
    if (clientRootCAExists) assertNotNull(taskParam.clientRootCA);
    else assertNull(taskParam.clientRootCA);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testK8sUniverseCreateOneClusterPerNamespacedProviderFailure() {
    String url = universeCreateUrl();
    Provider p = ModelFactory.kubernetesProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    az1.updateConfig(ImmutableMap.of("KUBENAMESPACE", "test-ns1"));
    InstanceType i =
        InstanceType.upsert(p.uuid, "small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ModelFactory.createUniverse(
        "K8sUniverse1", customer.getCustomerId(), Common.CloudType.kubernetes);

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "K8sUniverse2")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.kubernetes));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(
        result,
        "Only one universe can be created with providers having "
            + "KUBENAMESPACE set in the AZ config.");
  }

  @Test
  public void testUniverseCreateWithDisabledYedis() {
    String url = universeCreateUrl();
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("enableYEDIS", "false")
            .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.put("accessKeyCode", accessKeyCode);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);

    JsonNode json = getUniverseJson(result);
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("universeConfig"));

    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    JsonNode primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    JsonNode userIntentJsonNode = primaryClusterJson.get("userIntent");
    assertNotNull(userIntentJsonNode);

    assertEquals("false", userIntentJsonNode.get("enableYEDIS").toString());
  }

  @Test
  @Parameters(method = "parametersToDeviceInfoValidation")
  public void testUniverseCreateDeviceInfoValidation(
      Common.CloudType cloudType,
      String instanceType,
      PublicCloudConstants.StorageType storageType,
      Integer numVolumes,
      Integer volumeSize,
      Integer diskIops,
      Integer throughput,
      String mountPoints,
      String errorMessage) {
    String url = universeCreateUrl();
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p;
    switch (cloudType) {
      case aws:
        p = ModelFactory.awsProvider(customer);
        break;
      case gcp:
        p = ModelFactory.gcpProvider(customer);
        break;
      case azu:
        p = ModelFactory.azuProvider(customer);
        break;
      case kubernetes:
        p = ModelFactory.kubernetesProvider(customer);
        break;
      case onprem:
        p = ModelFactory.onpremProvider(customer);
        break;
      case other:
        p = ModelFactory.newProvider(customer, Common.CloudType.other);
        break;
      default:
        throw new UnsupportedOperationException();
    }
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.uuid, instanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    ObjectNode deviceInfo =
        createDeviceInfo(storageType, numVolumes, volumeSize, diskIops, throughput, mountPoints);
    if (deviceInfo.fields().hasNext()) {
      userIntentJson.set("deviceInfo", deviceInfo);
    }
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    if (errorMessage == null) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      assertOk(result);
    } else {
      Result result =
          assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      assertBadRequest(result, errorMessage);
    }
  }

  @Test
  public void testCreateUniverseEncryptionAtRestNoKMSConfig() {
    String url = universeCreateUrl();
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());

    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    ObjectNode bodyJson = (ObjectNode) Json.toJson(taskParams);

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "encryptionAtRestUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("enableNodeToNodeEncrypt", true)
            .put("enableClientToNodeEncrypt", true)
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
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
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsSet);
    bodyJson.put("nodePrefix", "demo-node");

    // TODO: (Daniel) - Add encryptionAtRestConfig to the payload to actually
    //  test what this unit test says it is testing for

    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    JsonNode json = getUniverseJson(result);
    assertOk(result);

    // Check that the encryption key file was not created in file system
    File key =
        new File(
            "/tmp/certs/"
                + customer.uuid.toString()
                + "/universe."
                + json.get("universeUUID").asText()
                + "-1.key");
    assertFalse(key.exists());
    checkTaskUUID(fakeTaskUUID, result);

    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), argCaptor.capture());

    // The KMS provider service should not begin to make any requests since there is no KMS config
    verify(mockApiHelper, times(0)).postRequest(any(String.class), any(JsonNode.class), anyMap());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateUniverseEncryptionAtRestWithKMSConfigExists() {
    String url = universeCreateUrl();
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.uuid, accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    ObjectNode bodyJson = (ObjectNode) Json.toJson(taskParams);

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "encryptionAtRestUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("enableNodeToNodeEncrypt", true)
            .put("enableClientToNodeEncrypt", true)
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
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
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsSet);
    bodyJson.put("nodePrefix", "demo-node");
    bodyJson.put(
        "encryptionAtRestConfig",
        Json.newObject()
            .put("configUUID", kmsConfig.configUUID.toString())
            .put("key_op", "ENABLE"));
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);

    checkTaskUUID(fakeTaskUUID, result);

    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), argCaptor.capture());
    assertAuditEntry(1, customer.uuid);
  }

  @SuppressWarnings("unused")
  private Object[] parametersToDeviceInfoValidation() {
    return new Object[][] {
      // Success cases
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP2,
        1,
        100,
        null,
        null,
        null,
        null
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.IO1,
        1,
        100,
        1000,
        null,
        null,
        null
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP3,
        1,
        100,
        1000,
        125,
        null,
        null
      },
      {Common.CloudType.aws, "i3.2xlarge", null, 1, 100, 1000, 125, null, null},
      {Common.CloudType.aws, "c5d.2xlarge", null, 1, 100, 1000, 125, null, null},
      {
        Common.CloudType.gcp,
        "c3.xlarge",
        PublicCloudConstants.StorageType.Persistent,
        1,
        100,
        null,
        null,
        null,
        null
      },
      {
        Common.CloudType.gcp,
        "c3.xlarge",
        PublicCloudConstants.StorageType.Scratch,
        1,
        100,
        null,
        null,
        null,
        null
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.StandardSSD_LRS,
        1,
        100,
        null,
        null,
        null,
        null
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.Premium_LRS,
        1,
        100,
        null,
        null,
        null,
        null
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.UltraSSD_LRS,
        1,
        100,
        null,
        null,
        null,
        null
      },
      {Common.CloudType.kubernetes, "c3.xlarge", null, 1, 100, null, null, null, null},
      {Common.CloudType.onprem, "c3.xlarge", null, 1, 100, null, null, "/var", null},
      {Common.CloudType.other, "c3.xlarge", null, null, null, null, null, null, null},

      //  Failure cases
      {
        Common.CloudType.aws,
        "c3.xlarge",
        null,
        null,
        null,
        null,
        null,
        null,
        "deviceInfo can't be empty for universe on aws provider"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        null,
        1,
        100,
        null,
        null,
        null,
        "storageType can't be empty for universe on aws provider"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP2,
        null,
        100,
        null,
        null,
        null,
        "Number of volumes field is mandatory"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP2,
        1,
        null,
        null,
        null,
        null,
        "Volume size field is mandatory"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.Persistent,
        1,
        100,
        null,
        null,
        null,
        "Cloud type aws is not compatible with storage type Persistent"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.IO1,
        1,
        100,
        null,
        null,
        null,
        "Disk IOPS is mandatory for IO1 storage"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP3,
        1,
        100,
        null,
        125,
        null,
        "Disk IOPS is mandatory for GP3 storage"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP3,
        1,
        100,
        1000,
        null,
        null,
        "Disk throughput is mandatory for GP3 storage"
      },
      {
        Common.CloudType.aws,
        "i3.2xlarge",
        PublicCloudConstants.StorageType.GP2,
        1,
        100,
        null,
        null,
        null,
        "AWS instance with ephemeral storage can't have storageType set"
      },
      {
        Common.CloudType.aws,
        "c5d.2xlarge",
        PublicCloudConstants.StorageType.GP2,
        1,
        100,
        null,
        null,
        null,
        "AWS instance with ephemeral storage can't have storageType set"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP2,
        1,
        -100,
        null,
        null,
        null,
        "Volume size should be positive"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP2,
        -1,
        100,
        null,
        null,
        null,
        "Number of volumes should be positive"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP3,
        1,
        100,
        -1,
        125,
        null,
        "Disk IOPS should be positive"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP3,
        1,
        100,
        1000,
        -1,
        null,
        "Disk throughput should be positive"
      },
      {
        Common.CloudType.gcp,
        "c3.xlarge",
        PublicCloudConstants.StorageType.Persistent,
        null,
        100,
        null,
        null,
        null,
        "Number of volumes field is mandatory"
      },
      {
        Common.CloudType.gcp,
        "c3.xlarge",
        PublicCloudConstants.StorageType.Scratch,
        1,
        null,
        null,
        null,
        null,
        "Volume size field is mandatory"
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.StandardSSD_LRS,
        null,
        100,
        null,
        null,
        null,
        "Number of volumes field is mandatory"
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.Premium_LRS,
        1,
        null,
        null,
        null,
        null,
        "Volume size field is mandatory"
      },
      {
        Common.CloudType.kubernetes,
        "c3.xlarge",
        null,
        null,
        100,
        null,
        null,
        null,
        "Number of volumes field is mandatory"
      },
      {
        Common.CloudType.kubernetes,
        "c3.xlarge",
        null,
        1,
        null,
        null,
        null,
        null,
        "Volume size field is mandatory"
      },
      {
        Common.CloudType.onprem,
        "c3.xlarge",
        null,
        null,
        100,
        null,
        null,
        "/var",
        "Number of volumes field is mandatory"
      },
      {
        Common.CloudType.onprem,
        "c3.xlarge",
        null,
        1,
        null,
        null,
        null,
        "/var",
        "Volume size field is mandatory"
      },
      {
        Common.CloudType.onprem,
        "c3.xlarge",
        null,
        1,
        100,
        null,
        null,
        null,
        "Mount points are mandatory for onprem cluster"
      },
    };
  }
}
