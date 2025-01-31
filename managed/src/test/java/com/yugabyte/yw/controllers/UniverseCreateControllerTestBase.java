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

import static com.yugabyte.yw.commissioner.Common.CloudType.kubernetes;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.PlacementInfoUtil.updateUniverseDefinition;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static junit.framework.TestCase.assertFalse;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
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
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.ArgumentMatchers;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public abstract class UniverseCreateControllerTestBase extends UniverseControllerTestBase {

  protected static final String FORBIDDEN_IP_1 = "1.2.3.4";
  protected static final String FORBIDDEN_IP_2 = "2.3.4.5";

  private String TMP_CHART_PATH = "/tmp/yugaware_tests/" + getClass().getSimpleName() + "/charts";

  @Override
  protected GuiceApplicationBuilder appOverrides(GuiceApplicationBuilder applicationBuilder) {
    return applicationBuilder.configure(
        "yb.security.forbidden_ips", FORBIDDEN_IP_1 + ", " + FORBIDDEN_IP_2);
  }

  @Before
  public void setUpTest() {
    new File(TMP_CHART_PATH).mkdirs();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_CHART_PATH));
    super.tearDown();
  }

  public abstract Result sendCreateRequest(ObjectNode bodyJson);

  public abstract Result sendCreateRequestWithNodeDetailsSet(ObjectNode bodyJson);

  public abstract Result sendPrimaryCreateConfigureRequest(ObjectNode topJson);

  public abstract Result sendPrimaryEditConfigureRequest(ObjectNode topJson);

  public abstract Result sendAsyncCreateConfigureRequest(ObjectNode topJson);

  protected abstract JsonNode getUniverseJson(Result universeCreateResponse);

  protected abstract JsonNode getUniverseDetailsJson(Result universeConfigureResponse);

  private void checkTaskUUID(UUID fakeTaskUUID, Result universeCreateResponse) {
    String taskUUID = Json.parse(contentAsString(universeCreateResponse)).get("taskUUID").asText();
    assertEquals(taskUUID, fakeTaskUUID.toString());
  }

  /** Migrated to {@link UniverseClustersControllerTest} */
  @Test
  public void testUniverseCreateWithInvalidParams() {
    Result result = assertPlatformException(() -> sendCreateRequest(Json.newObject()));
    assertBadRequest(result, "clusters: This field is required");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseCreateWithInvalidUniverseName() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "Foo_Bar")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    Result result = assertPlatformException(() -> sendCreateRequest(bodyJson));
    assertBadRequest(
        result,
        "Invalid universe name format, regex used for validation is "
            + "^[a-zA-Z0-9]([-a-zA-Z0-9]*[a-zA-Z0-9])?$.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseCreateWithRuntimeFlagsSet() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("enableYSQL", "false")
            .put("accessKeyCode", accessKeyCode)
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());
    ObjectNode runtimeFlags = Json.newObject();
    runtimeFlags.put("yb.security.type", "securityType");
    bodyJson.set("runtimeFlags", runtimeFlags);

    Result result = sendCreateRequest(bodyJson);
    assertOk(result);
    JsonNode json = getUniverseJson(result);
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("universeDetails"));
    assertNotNull(json.get("universeConfig"));
    // setTxnTableWaitCountFlag will be false as enableYSQL is false in this case
    assertFalse(json.get("universeDetails").get("setTxnTableWaitCountFlag").asBoolean());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("SingleUserUniverse")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));

    SettableRuntimeConfigFactory factory =
        app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    assertNotNull(
        factory
            .forUniverse(Universe.getUniverseByName("SingleUserUniverse"))
            .getString("yb.security.type"));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseCreateWithSingleAvailabilityZones() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("enableYSQL", "false")
            .put("accessKeyCode", accessKeyCode)
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = sendCreateRequest(bodyJson);
    assertOk(result);
    JsonNode json = getUniverseJson(result);
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("universeDetails"));
    assertNotNull(json.get("universeConfig"));
    // setTxnTableWaitCountFlag will be false as enableYSQL is false in this case
    assertFalse(json.get("universeDetails").get("setTxnTableWaitCountFlag").asBoolean());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("SingleUserUniverse")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseCreateWithYsqlEnabled() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("enableYCQL", "false")
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = sendCreateRequest(bodyJson);
    assertOk(result);
    JsonNode json = getUniverseJson(result);
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("universeDetails"));
    assertNotNull(json.get("universeConfig"));
    // setTxnTableWaitCountFlag should be true as enableYSQL is true in this case
    assertTrue(json.get("universeDetails").get("setTxnTableWaitCountFlag").asBoolean());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("SingleUserUniverse")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseCreateWithoutYsqlPasswordAndYsqlEnabled() {
    Result result = assertPlatformException(this::createUniverseWithoutYsqlPasswordAndYsqlEnabled);
    assertBadRequest(result, "Password shouldn't be empty.");
  }

  @Test
  public void testUniverseCreateWithoutYsqlPasswordAndYsqlEnabledCloud() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    Result result = createUniverseWithoutYsqlPasswordAndYsqlEnabled();
    assertOk(result);
  }

  @Test
  public void testUniverseCreateWithoutYcqlPasswordAndYcqlEnabled() {
    Result result = assertPlatformException(this::createUniverseWithoutYcqlPasswordAndYcqlEnabled);
    assertBadRequest(result, "Password shouldn't be empty.");
  }

  @Test
  public void testUniverseCreateWithoutYcqlPasswordAndYcqlEnabledCloud() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    Result result = createUniverseWithoutYcqlPasswordAndYcqlEnabled();
    assertOk(result);
  }

  @Test
  public void testUniverseCreateWithContradictoryGflags() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("enableYSQL", "true")
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    userIntentJson
        .putArray("masterGFlags")
        .add(Json.newObject().put("name", "enable_ysql").put("value", "false"));

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.getUuid() + "/universes";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(
        result,
        "G-Flag value 'false' for 'enable_ysql' is not compatible with intent value 'true'");

    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    Result cloudResult = sendCreateRequest(bodyJson);
    assertOk(cloudResult);
  }

  @Test
  public void testUniverseCreateWithBothEndPointsDisabled() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("enableYSQL", "false")
            .put("enableYCQL", "false")
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.getUuid() + "/universes";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Enable atleast one endpoint among YSQL and YCQL");
  }

  @Test
  @Parameters({
    "true, true, true",
    // "true, true, false",// invalid: clientTLS false and bothCASame true
    "true, false, true",
    // "true, false, false",// invalid: clientTLS false and bothCASame true
    "false, true, true",
    "false, true, false",
    "false, false, true",
    "false, false, false"
  })
  public void testUniverseCreateForSelfSignedTLS(
      boolean rootAndClientRootCASame,
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt) {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("enableNodeToNodeEncrypt", enableNodeToNodeEncrypt)
            .put("enableClientToNodeEncrypt", enableClientToNodeEncrypt)
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("ybSoftwareVersion", "0.0.0.1-b1");

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    ObjectNode bodyJson = Json.newObject().put("nodePrefix", "demo-node");
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());
    bodyJson.put("rootAndClientRootCASame", rootAndClientRootCASame);

    Result result = sendCreateRequest(bodyJson);
    assertOk(result);
    checkTaskUUID(fakeTaskUUID, result);

    ArgumentCaptor<UniverseTaskParams> taskParams =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), taskParams.capture());
    UniverseDefinitionTaskParams taskParam = (UniverseDefinitionTaskParams) taskParams.getValue();
    UserIntent userIntent = taskParam.getPrimaryCluster().userIntent;
    assertEquals(enableNodeToNodeEncrypt, userIntent.enableNodeToNodeEncrypt);
    assertEquals(enableClientToNodeEncrypt, userIntent.enableClientToNodeEncrypt);
    assertEquals(rootAndClientRootCASame, taskParam.rootAndClientRootCASame);
    if (userIntent.enableNodeToNodeEncrypt
        || (taskParam.rootAndClientRootCASame && userIntent.enableClientToNodeEncrypt)) {
      assertNotNull(taskParam.rootCA);
    } else {
      assertNull(taskParam.rootCA);
    }
    if (userIntent.enableClientToNodeEncrypt) {
      assertNotNull(taskParam.getClientRootCA());
    } else {
      assertNull(taskParam.getClientRootCA());
    }

    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testK8sUniverseCreateOneClusterPerNamespacedProviderFailure() {
    Provider p = ModelFactory.kubernetesProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    az1.updateConfig(ImmutableMap.of("KUBENAMESPACE", "test-ns1"));
    az1.save();
    InstanceType i =
        InstanceType.upsert(p.getUuid(), "small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ModelFactory.createUniverse("K8sUniverse1", customer.getId(), Common.CloudType.kubernetes);

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "K8sUniverse2")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.kubernetes));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = assertPlatformException(() -> sendCreateRequest(bodyJson));
    assertBadRequest(
        result,
        "Only one universe can be created with providers having "
            + "KUBENAMESPACE set in the AZ config.");
  }

  @Test
  // @formatter:off
  @Parameters({
    "2.15.4.0-b12, true",
    "2.15.3.0-b1, false",
    "2.16.1.0-b11, true",
  })
  // @formatter:on
  public void testK8sUniverseCreateNewHelmNaming(String ybVersion, boolean newNamingStyle) {
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    Provider p = ModelFactory.kubernetesProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    Map<String, String> config = new HashMap<>();
    config.put("KUBECONFIG", "xyz");
    CloudInfoInterface.setCloudProviderInfoFromConfig(p, config);
    p.save();
    InstanceType i =
        InstanceType.upsert(p.getUuid(), "small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "K8sUniverseNewStyle")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("ybSoftwareVersion", ybVersion);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());

    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.kubernetes));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = sendCreateRequest(bodyJson);
    assertOk(result);
    assertEquals(newNamingStyle, expectedTaskParams.getValue().useNewHelmNamingStyle);
  }

  @Test
  public void testUniverseCreateWithDisabledYedis() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("enableYEDIS", "false")
            .put("provider", p.getUuid().toString())
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.put("accessKeyCode", accessKeyCode);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    Result result = sendCreateRequest(bodyJson);
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
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create("1.0.0.0")
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-1.0.0.0-helm.tar.gz")
            .withFilePath("/opt/yugabyte/releases/1.0.0.0/yb-1.0.0.0-x86_64-linux.tar.gz");
    when(mockReleaseManager.getReleaseByVersion("1.0.0.0"))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(TMP_CHART_PATH, "ucctb_yugabyte-1.0.0.0-helm.tar.gz", "Sample helm chart data");
    createTempFile(
        "/opt/yugabyte/releases/1.0.0.0", "yb-1.0.0.0-x86_64-linux.tar.gz", "Sample package data");

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
        // case other:
        //   p = ModelFactory.newProvider(customer, Common.CloudType.other);
        //   break;
      default:
        throw new UnsupportedOperationException();
    }
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    if (cloudType == kubernetes) {
      Map<String, String> config = new HashMap<>();
      config.put("KUBECONFIG", "xyz");
      CloudInfoInterface.setCloudProviderInfoFromConfig(p, config);
      p.save();
    }
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), instanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("ybSoftwareVersion", "1.0.0.0");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    ObjectNode deviceInfo =
        createDeviceInfo(storageType, numVolumes, volumeSize, diskIops, throughput, mountPoints);
    if (deviceInfo.fields().hasNext()) {
      userIntentJson.set("deviceInfo", deviceInfo);
    }
    UniverseDefinitionTaskParams.Cluster cluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY,
            Json.fromJson(userIntentJson, UserIntent.class));
    cluster.placementInfo =
        ModelFactory.constructPlacementInfoObject(Collections.singletonMap(az1.getUuid(), 1));
    NodeDetails node = new NodeDetails();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.instance_type = i.getInstanceTypeCode();
    node.cloudInfo.az = az1.getName();
    node.azUuid = az1.getUuid();
    node.nodeName = "namememr";
    node.placementUuid = cluster.uuid;
    bodyJson.set("clusters", Json.newArray().add(Json.toJson(cluster)));
    bodyJson.set("nodeDetailsSet", Json.newArray().add(Json.toJson(node)));
    bodyJson.put("nodePrefix", "demo-node");

    if (errorMessage == null) {
      Result result = sendCreateRequest(bodyJson);
      assertOk(result);
    } else {
      Result result = assertPlatformException(() -> sendCreateRequest(bodyJson));
      assertBadRequest(result, errorMessage);
    }
  }

  @Test
  public void testCreateUniverseEncryptionAtRestNoKMSConfig() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());

    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    releaseMetadata.filePath = "/yb/release.tar.gz";
    ReleaseContainer release =
        new ReleaseContainer(
            releaseMetadata, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils);
    when(mockReleaseManager.getReleaseByVersion("0.0.1")).thenReturn(release);

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
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("ybSoftwareVersion", "0.0.0.1-b1");

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());
    bodyJson.put("nodePrefix", "demo-node");

    // TODO: (Daniel) - Add encryptionAtRestConfig to the payload to actually
    //  test what this unit test says it is testing for

    Result result = sendCreateRequest(bodyJson);
    JsonNode json = getUniverseJson(result);
    assertOk(result);

    // Check that the encryption key file was not created in file system
    File key =
        new File(
            "/tmp/certs/"
                + customer.getUuid().toString()
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
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateUniverseEncryptionAtRestWithKMSConfigExists() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
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
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("ybSoftwareVersion", "0.0.0.1-b1");

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());
    bodyJson.put("nodePrefix", "demo-node");
    bodyJson.set(
        "encryptionAtRestConfig",
        Json.newObject()
            .put("configUUID", kmsConfig.getConfigUUID().toString())
            .put("key_op", "ENABLE"));
    Result result = sendCreateRequest(bodyJson);
    assertOk(result);

    checkTaskUUID(fakeTaskUUID, result);

    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), argCaptor.capture());
    assertAuditEntry(1, customer.getUuid());
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
        3000,
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
      {Common.CloudType.azu, "c3.xlarge", StorageType.Premium_LRS, 1, 100, null, null, null, null},
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.PremiumV2_LRS,
        1,
        100,
        null,
        null,
        null,
        "Disk IOPS is mandatory for PremiumV2_LRS storage"
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.UltraSSD_LRS,
        1,
        100,
        3000,
        125,
        null,
        null
      },
      {Common.CloudType.kubernetes, "c3.xlarge", null, 1, 100, null, null, null, null},
      {Common.CloudType.onprem, "c3.xlarge", null, 1, 100, null, null, "/var", null},
      // {Common.CloudType.other, "c3.xlarge", null, null, null, null, null, null, null},

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
        3000,
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
        "Disk IOPS for storage type GP3 should be in range [3000, 16000]"
      },
      {
        Common.CloudType.aws,
        "c3.xlarge",
        PublicCloudConstants.StorageType.GP3,
        1,
        100,
        3000,
        -1,
        null,
        "Disk throughput for storage type GP3 should be in range [125, 1000]"
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
        PublicCloudConstants.StorageType.UltraSSD_LRS,
        1,
        100,
        null,
        125,
        null,
        "Disk IOPS is mandatory for UltraSSD_LRS storage"
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.UltraSSD_LRS,
        1,
        100,
        3000,
        null,
        null,
        "Disk throughput is mandatory for UltraSSD_LRS storage"
      },
      {
        Common.CloudType.azu,
        "c3.xlarge",
        PublicCloudConstants.StorageType.PremiumV2_LRS,
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

  @Test
  public void testUniverseCreateWithoutAvailabilityZone_fail() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", "a-instance")
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));

    Result result = assertPlatformException(() -> sendPrimaryCreateConfigureRequest(bodyJson));
    assertInternalServerError(
        result,
        "Couldn't find available nodes with type a-instance for given regions: ["
            + r.getUuid()
            + "]");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomConfigureCreateWithMultiAZMultiRegion() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(getTestUserIntent(r, p, i, 5), null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams.getPrimaryCluster();
    // Needed for the universe_resources call.
    DeviceInfo di = new DeviceInfo();
    di.storageType = StorageType.GP2;
    di.volumeSize = 100;
    di.numVolumes = 2;
    primaryCluster.userIntent.deviceInfo = di;

    List<PlacementAZ> azList =
        primaryCluster.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 3);

    PlacementAZ paz = azList.get(0);
    paz.numNodesInAZ += 2;
    primaryCluster.userIntent.numNodes += 2;

    primaryCluster.userIntent.universeName = "test";
    primaryCluster.userIntent.enableYCQL = false;
    primaryCluster.userIntent.ysqlPassword = "@123Byte";
    primaryCluster.userIntent.enableYCQLAuth = false;
    primaryCluster.userIntent.ybSoftwareVersion = "0.0.0.1-b1";

    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    primaryCluster.userIntent.accessKeyCode = accessKeyCode;

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);

    Result result = sendPrimaryCreateConfigureRequest(topJson);
    assertOk(result);

    JsonNode json = getUniverseDetailsJson(result);
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(7, nodeDetailJson.size());
    // Now test the resource endpoint also works.
    // TODO: put this in its own test once we refactor the provider+region+az creation and payload
    // generation...
    result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/universe_resources",
            authToken,
            topJson);
    assertOk(result);
  }

  @Test
  public void testOnPremConfigureCreateInvalidAZNodeComboNonEmptyNodeDetailsSet_fail() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    List<AvailabilityZone> azList = new ArrayList<>();
    azList.add(az1);
    azList.add(az2);

    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = setupOnPremTestData(6, p, r, azList);

    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, p, i, 5);
    userIntent.providerType = Common.CloudType.onprem;
    userIntent.instanceType = "type.small";
    taskParams.upsertPrimaryCluster(userIntent, null);
    taskParams.nodeDetailsSet = new HashSet<>();
    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams.getPrimaryCluster();

    updateUniverseDefinition(taskParams, customer.getId(), primaryCluster.uuid, CREATE);

    taskParams.getPrimaryCluster().userIntent.numNodes += 5;
    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    Result result = assertPlatformException(() -> sendPrimaryCreateConfigureRequest(topJson));
    assertBadRequest(result, "Couldn't find 4 node(s) of type type.small");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseCreateWithIncorrectNodes() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("enableYSQL", "true")
            .put("ybSoftwareVersion", "0.0.0.1-b1");

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));

    UUID randomUUID = UUID.randomUUID();
    ObjectNode nodeDetails = Json.newObject();
    nodeDetails.put("nodeName", "testing-1");
    nodeDetails.set("cloudInfo", Json.newObject().put("region", "region1"));
    nodeDetails.put("placementUuid", randomUUID.toString()); // Random cluster.
    ArrayNode nodeDetailsSet = Json.newArray().add(nodeDetails);
    bodyJson.set("nodeDetailsSet", nodeDetailsSet);

    String url = "/api/customers/" + customer.getUuid() + "/universes";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Unknown cluster " + randomUUID.toString() + " for node with idx -1");
  }

  protected UniverseDefinitionTaskParams setupOnPremTestData(
      int numNodesToBeConfigured, Provider p, Region r, List<AvailabilityZone> azList) {
    int numAZsToBeConfigured = azList.size();
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    for (int k = 0; k < numNodesToBeConfigured; ++k) {
      NodeInstanceFormData.NodeInstanceData details = new NodeInstanceFormData.NodeInstanceData();
      details.ip = "10.255.67." + k;
      details.region = r.getCode();

      if (numAZsToBeConfigured == 2) {
        if (k % 2 == 0) {
          details.zone = azList.get(0).getCode();
        } else {
          details.zone = azList.get(1).getCode();
        }
      } else {
        details.zone = azList.get(0).getCode();
      }
      details.instanceType = "type.small";
      details.nodeName = "test_name" + k;

      if (numAZsToBeConfigured == 2) {
        if (k % 2 == 0) {
          NodeInstance.create(azList.get(0).getUuid(), details);
        } else {
          NodeInstance.create(azList.get(0).getUuid(), details);
        }
      } else {
        NodeInstance.create(azList.get(0).getUuid(), details);
      }
    }

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, p, i, 3);
    userIntent.providerType = Common.CloudType.onprem;
    userIntent.instanceType = "type.small";
    taskParams.nodeDetailsSet = new HashSet<>();

    taskParams.upsertPrimaryCluster(userIntent, null);

    return taskParams;
  }

  protected Result createUniverseWithoutYcqlPasswordAndYcqlEnabled() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("enableYCQLAuth", "true")
            .put("ycqlPassword", "")
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.getUuid() + "/universes";
    return doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
  }

  protected Result createUniverseWithoutYsqlPasswordAndYsqlEnabled() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .put("enableYSQLAuth", "true")
            .put("ysqlPassword", "")
            .put("ybSoftwareVersion", "0.0.0.1-b1");
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(Common.CloudType.aws));
    bodyJson.set("clusters", clustersArray(userIntentJson, Json.newObject()));
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.getUuid() + "/universes";
    return doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
  }

  // Kubernetes service overrides test

  // SEO: ServiceEndpointOverrides
  @Test
  public void testCreateK8sUniverseMatchingSEOSingleNSMultiAZSuccess() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 3 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();
    azConfig.put("OVERRIDES", serviceEndpoint);
    List<AvailabilityZone> zones = pair.getSecond();
    // Same overrides in all 3
    for (AvailabilityZone az : zones) {
      CloudInfoInterface.setCloudProviderInfoFromConfig(az, azConfig);
      az.save();
    }
    Result result = sendCreateRequestWithNodeDetailsSet(pair.getFirst());
    assertOk(result);
  }

  @Test
  public void testCreateK8sUniverseEmptyArraySEOSingleNSMultiAZSuccess() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 3 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));
    Map<String, String> azConfig = new HashMap<>();
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();
    Result result = sendCreateRequestWithNodeDetailsSet(pair.getFirst());
    assertOk(result);
  }

  @Test
  public void testCreateK8sUniverseNoSEOSingleNSMultiAZSuccess() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // No AZ Overrides
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));
    Result result = sendCreateRequestWithNodeDetailsSet(pair.getFirst());
    assertOk(result);
  }

  @Test
  public void testCreateK8sUniverseMatchingSEOMultiNSMultiAZSuccess() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // Same Overrides in AZ for "ns-1"
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();

    // Different override in AZ for "ns-2"
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    azConfig.put("KUBENAMESPACE", "ns-2");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();
    // Should succeed since overrides are same in given namespace.
    Result result = sendCreateRequestWithNodeDetailsSet(pair.getFirst());
    assertOk(result);
  }

  @Test
  public void testCreateK8sUniverseWithRRMatchingSEOSameNSMultiAZSuccess() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 4 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, true /* createRR */, customer, createValidDeviceInfo(kubernetes));

    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();
    azConfig.put("OVERRIDES", serviceEndpoint);
    List<AvailabilityZone> zones = pair.getSecond();
    for (AvailabilityZone az : zones) {
      CloudInfoInterface.setCloudProviderInfoFromConfig(az, azConfig);
      az.save();
    }
    Result result = sendCreateRequestWithNodeDetailsSet(pair.getFirst());
    assertOk(result);
  }

  @Test
  public void testCreateK8sUniverseWithRRMatchingSEOMultiNSMultiAZSuccess() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, true /* createRR */, customer, createValidDeviceInfo(kubernetes));

    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // AZ overrides same in "ns-1"
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    // Different NS "ns-2"
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    azConfig.put("KUBENAMESPACE", "ns-2");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(3), azConfig);
    zones.get(3).save();
    // Should succeed since overrides are same in given namespace.
    Result result = sendCreateRequestWithNodeDetailsSet(pair.getFirst());
    assertOk(result);
  }

  @Test
  public void testCreateK8sUniverseMCSMatchingSEOSameNSMultiAZSuccess() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 4 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));

    // Kubeconfigs
    String clusterConfig1 = TestUtils.readResource("kubernetes/cluster-1-kubeconfig.conf");
    String clusterConfig2 = TestUtils.readResource("kubernetes/cluster-2-kubeconfig.conf");
    createTempFile("/tmp/yugaware_tests", "cluster-1.conf", clusterConfig1);
    createTempFile("/tmp/yugaware_tests", "cluster-2.conf", clusterConfig2);
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");

    // Namespace is default in both clusters
    // Zone 1 and Zone 2 belong to same cluster: same overrides
    Map<String, String> azConfig = new HashMap<>();
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBECONFIG", "/tmp/yugaware_tests/cluster-1.conf");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();

    // Zone 3 belongs to different cluster: different overrides
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    azConfig.put("KUBECONFIG", "/tmp/yugaware_tests/cluster-2.conf");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    // Succeed since different overrides are in different clusters
    Result result = sendCreateRequestWithNodeDetailsSet(pair.getFirst());
    assertOk(result);
  }

  @Test
  public void testCreateK8sUniverseMismatchSEOMultiNSMultiAZFail() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 3 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    // Conflicting overrides in same Namespace "ns-1"
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();

    azConfig.put("KUBENAMESPACE", "ns-2");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();
    RuntimeException ex =
        assertThrows(
            RuntimeException.class, () -> sendCreateRequestWithNodeDetailsSet(pair.getFirst()));
    assertTrue(
        ex.getMessage().contains("::ns-1 has conflicting namespace scope service overrides"));
  }

  @Test
  public void testCreateK8sUniverseMCSMismatchSEOSingleNSMultiAZFail() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 4 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));

    // Kubeconfigs
    String clusterConfig1 = TestUtils.readResource("kubernetes/cluster-1-kubeconfig.conf");
    String clusterConfig2 = TestUtils.readResource("kubernetes/cluster-2-kubeconfig.conf");
    createTempFile("/tmp/yugaware_tests", "cluster-1.conf", clusterConfig1);
    createTempFile("/tmp/yugaware_tests", "cluster-2.conf", clusterConfig2);
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");

    // Zone 1 and Zone 2 belong to same cluster, same NS: conflicting overrides
    Map<String, String> azConfig = new HashMap<>();
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBECONFIG", "/tmp/yugaware_tests/cluster-1.conf");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();

    // Zone 3 belongs to different cluster
    azConfig.put("KUBECONFIG", "/tmp/yugaware_tests/cluster-2.conf");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    String nodePrefix = Util.getNodePrefix(customer.getId(), "K8sUniverseNewStyle");
    String namespace =
        KubernetesUtil.getKubernetesNamespace(true, nodePrefix, "az-2", azConfig, true, false);
    RuntimeException ex =
        assertThrows(
            RuntimeException.class, () -> sendCreateRequestWithNodeDetailsSet(pair.getFirst()));
    assertTrue(
        ex.getMessage()
            .contains(
                String.format(
                    "cluster-1::%s has conflicting namespace scope service overrides", namespace)));
  }

  @Test
  public void testCreateK8sUniverseWithRRMismatchSEOSingleNSMultiAZFail() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 4 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, true /* createRR */, customer, createValidDeviceInfo(kubernetes));

    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // All AZs in one NS "ns-1"
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    // Conflicting override in az-4
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(3), azConfig);
    zones.get(3).save();
    RuntimeException ex =
        assertThrows(
            RuntimeException.class, () -> sendCreateRequestWithNodeDetailsSet(pair.getFirst()));
    assertTrue(ex.getMessage().contains("ns-1 has conflicting namespace scope service overrides"));
  }

  @Test
  public void testCreateK8sUniverseMismatchSEOSingleNSMultiAZFail() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 3 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // Same override in az-1, az-2
    azConfig.put("OVERRIDES", serviceEndpoint);
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();

    // Conflicting override
    azConfig.put("OVERRIDES", "serviceEndpoints: []");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    String nodePrefix = Util.getNodePrefix(customer.getId(), "K8sUniverseNewStyle");
    String namespace =
        KubernetesUtil.getKubernetesNamespace(true, nodePrefix, "az-3", azConfig, true, false);
    RuntimeException ex =
        assertThrows(
            RuntimeException.class, () -> sendCreateRequestWithNodeDetailsSet(pair.getFirst()));
    assertTrue(
        ex.getMessage()
            .contains(
                String.format(
                    "::%s has conflicting namespace scope service overrides", namespace)));
  }

  @Test
  public void testCreateK8sUniverseConflictingSameNameSEOFail() {
    String ybVersion = "2024.2.0.0-b2";
    when(mockRuntimeConfig.getBoolean("yb.use_new_helm_naming")).thenReturn(true);
    when(mockRuntimeConfig.getString("yb.universe.default_service_scope_for_k8s"))
        .thenReturn("Namespaced");
    ArgumentCaptor<UniverseDefinitionTaskParams> expectedTaskParams =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), expectedTaskParams.capture()))
        .thenReturn(fakeTaskUUID);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(ybVersion)
            .withChartPath(TMP_CHART_PATH + "/ucctb_yugabyte-" + ybVersion + "-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(ybVersion))
        .thenReturn(
            new ReleaseContainer(rm, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    createTempFile(
        TMP_CHART_PATH, "ucctb_yugabyte-" + ybVersion + "-helm.tar.gz", "Sample helm chart data");

    // AZ overrides for serviceEndpoints are same in all 3 AZs
    Pair<ObjectNode, List<AvailabilityZone>> pair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, false /* createRR */, customer, createValidDeviceInfo(kubernetes));
    String serviceEndpoint =
        TestUtils.readResource("kubernetes/repeated_service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();
    azConfig.put("OVERRIDES", serviceEndpoint);
    List<AvailabilityZone> zones = pair.getSecond();
    // Same overrides in all 3
    for (AvailabilityZone az : zones) {
      CloudInfoInterface.setCloudProviderInfoFromConfig(az, azConfig);
      az.save();
    }

    // Fail since overrides have repeated service endpoint name
    RuntimeException ex =
        assertThrows(
            RuntimeException.class, () -> sendCreateRequestWithNodeDetailsSet(pair.getFirst()));
    assertTrue(
        ex.getMessage()
            .contains("Overrides contain same service name 'yb-tserver-service' twice!"));
  }
}
