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
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.common.AssertHelper.assertYBPSuccess;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static junit.framework.TestCase.assertNull;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class CloudProviderApiControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderApiControllerTest.class);
  private static final ImmutableList<String> REGION_CODES_FROM_CLOUD_API =
      ImmutableList.of("region1", "region2");

  @Mock Config mockConfig;
  @Mock ConfigHelper mockConfigHelper;
  @Mock private play.Configuration appConfig;

  Customer customer;
  Users user;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    try {
      String kubeFile = createTempFile("test2.conf", "test5678");
      //      when(mockAccessManager.createKubernetesConfig(anyString(), anyMap(), anyBoolean()))
      //          .thenReturn(kubeFile);
    } catch (Exception e) {
      // Do nothing
    }
  }

  private Result listProviders() {
    return FakeApiHelper.doRequestWithAuthToken(
        "GET", "/api/customers/" + customer.uuid + "/providers", user.createAuthToken());
  }

  private Result createProvider(JsonNode bodyJson) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", "/api/customers/" + customer.uuid + "/providers", user.createAuthToken(), bodyJson);
  }

  private Result createKubernetesProvider(JsonNode bodyJson) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.uuid + "/providers/kubernetes",
        user.createAuthToken(),
        bodyJson);
  }

  private Result getKubernetesSuggestedConfig() {
    return FakeApiHelper.doRequestWithAuthToken(
        "GET",
        "/api/customers/" + customer.uuid + "/providers/suggested_kubernetes_config",
        user.createAuthToken());
  }

  private Result deleteProvider(UUID providerUUID) {
    return FakeApiHelper.doRequestWithAuthToken(
        "DELETE",
        "/api/customers/" + customer.uuid + "/providers/" + providerUUID,
        user.createAuthToken());
  }

  private Result editProvider(JsonNode bodyJson, UUID providerUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "PUT",
        "/api/customers/" + customer.uuid + "/providers/" + providerUUID + "/edit",
        user.createAuthToken(),
        bodyJson);
  }

  private Result patchProvider(JsonNode bodyJson, UUID providerUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "PATCH",
        "/api/customers/" + customer.uuid + "/providers/" + providerUUID,
        user.createAuthToken(),
        bodyJson);
  }

  private Result bootstrapProviderXX(JsonNode bodyJson, Provider provider) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.uuid + "/providers/" + provider.uuid + "/bootstrap",
        user.createAuthToken(),
        bodyJson);
  }

  //  @Test
  public void testListEmptyProviders() {
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertTrue(json.isArray());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  //  @Test
  public void testListProviders() {
    Provider p1 = ModelFactory.awsProvider(customer);
    p1.setConfig(
        ImmutableMap.of("MY_KEY_DATA", "SENSITIVE_DATA", "MY_SECRET_DATA", "SENSITIVE_DATA"));
    p1.save();
    Provider p2 = ModelFactory.gcpProvider(customer);
    p2.setConfig(ImmutableMap.of("FOO", "BAR"));
    p2.save();
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertAuditEntry(0, customer.uuid);
    assertEquals(2, json.size());
    assertValues(json, "uuid", ImmutableList.of(p1.uuid.toString(), p2.uuid.toString()));
    assertValues(json, "name", ImmutableList.of(p1.name, p2.name));
    json.forEach(
        (providerJson) -> {
          JsonNode config = providerJson.get("config");
          if (UUID.fromString(providerJson.get("uuid").asText()).equals(p1.uuid)) {
            assertValue(config, "MY_KEY_DATA", "SE**********TA");
            assertValue(config, "MY_SECRET_DATA", "SE**********TA");
          } else {
            assertValue(config, "FOO", "BAR");
          }
        });
  }

  //  @Test
  public void testListProvidersWithValidCustomer() {
    Provider.create(UUID.randomUUID(), Common.CloudType.aws, "Amazon");
    Provider p = ModelFactory.gcpProvider(customer);
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertEquals(1, json.size());
    assertValues(json, "uuid", ImmutableList.of(p.uuid.toString()));
    assertValues(json, "name", ImmutableList.of(p.name));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateProvider() {
    createProviderTest(
        buildProviderReq("azu", "Microsoft"),
        ImmutableList.of("region1", "region2"),
        UUID.randomUUID());
  }

  @Test
  public void testCreateGCPProviderSomeRegionInput() {
    Provider provider = buildProviderReq("gcp", "Google");
    Region region = new Region();
    region.name = "region1";
    region.provider = provider;
    region.code = "region1";
    provider.regions = ImmutableList.of(region);
    createProviderTest(provider, ImmutableList.of(), UUID.randomUUID());
  }

  @Test
  public void testCreateGCPProviderNoRegionInput() {
    createProviderTest(
        buildProviderReq("gcp", "Google"),
        ImmutableList.of("region1", "region2"),
        UUID.randomUUID());
  }

  @Test
  public void testAwsBootstrapWithDestVpcId() {
    Provider providerReq = buildProviderReq("aws", "Amazon");
    providerReq.destVpcId = "nofail";
    createProviderTest(providerReq, ImmutableList.of("region1", "region2"), UUID.randomUUID());
  }

  private Provider createProviderTest(
      Provider provider, ImmutableList<String> regionCodesFromCloudAPI, UUID actualTaskUUID) {
    JsonNode bodyJson = Json.toJson(provider);
    boolean isOnprem = CloudType.onprem.name().equals(provider.code);
    if (!isOnprem) {
      when(mockCommissioner.submit(any(TaskType.class), any(CloudBootstrap.Params.class)))
          .thenReturn(actualTaskUUID);
      when(mockCloudQueryHelper.getRegionCodes(provider)).thenReturn(regionCodesFromCloudAPI);
    }
    Result result = createProvider(bodyJson);
    assertOk(result);
    YBPTask ybpTask = Json.fromJson(Json.parse(contentAsString(result)), YBPTask.class);
    if (!isOnprem) {
      // When regions not supplied in request then we expect a call to cloud API to get region codes
      verify(mockCloudQueryHelper, times(provider.regions.isEmpty() ? 1 : 0)).getRegionCodes(any());
      assertEquals(actualTaskUUID, ybpTask.taskUUID);
    }
    Provider createdProvider = Provider.get(customer.uuid, ybpTask.resourceUUID);
    assertEquals(provider.code, createdProvider.code);
    assertEquals(provider.name, createdProvider.name);
    assertAuditEntry(1, customer.uuid);
    return createdProvider; // Note this is still partially created since our commissioner is fake.
  }

  private Provider buildProviderReq(String actualProviderCode, String actualProviderName) {
    Provider provider = new Provider();
    provider.code = actualProviderCode;
    provider.name = actualProviderName;
    return provider;
  }

  @Test
  public void testCreateMultiInstanceProvider() {
    ModelFactory.awsProvider(customer);
    createProviderTest(
        buildProviderReq("aws", "Amazon1"),
        ImmutableList.of("region1", "region2"),
        UUID.randomUUID());
  }

  @Test
  public void testCreateMultiInstanceProviderWithSameNameAndCloud() {
    ModelFactory.awsProvider(customer);
    Result result =
        assertPlatformException(
            () ->
                createProviderTest(
                    buildProviderReq("aws", "Amazon"),
                    ImmutableList.of("region1", "region2"),
                    UUID.randomUUID()));
    assertBadRequest(result, "Provider with the name Amazon already exists");
  }

  @Test
  public void testCreateMultiInstanceProviderWithSameNameButDifferentCloud() {
    ModelFactory.awsProvider(customer);
    createProviderTest(
        buildProviderReq("gcp", "Amazon1"),
        ImmutableList.of("region1", "region2"),
        UUID.randomUUID());
  }

  @Test
  public void testCreateProviderSameNameDiffCustomer() {
    Provider.create(UUID.randomUUID(), Common.CloudType.aws, "Amazon");
    createProviderTest(
        buildProviderReq("aws", "Amazon"), REGION_CODES_FROM_CLOUD_API, UUID.randomUUID());
  }

  @Test
  public void testCreateWithInvalidParams() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    Result result =
        assertPlatformException(
            () ->
                createProviderTest(
                    buildProviderReq("aws", null), REGION_CODES_FROM_CLOUD_API, UUID.randomUUID()));
    assertBadRequest(result, "\"name\":[\"error.required\"]}");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  @Parameters({"aws", "gcp", "onprem"})
  public void testCreateProviderWithConfig(String code) {
    String providerName = code + "-Provider";
    Provider providerReq = buildProviderReq(code, providerName);
    Map<String, String> reqConfig = new HashMap<>();
    if (code.equals("gcp")) {
      // Note that we do not wrap the GCP config in API requests. Caller should do extracting
      // config file details and putting it in the config map
      reqConfig.put("GCE_EMAIL", "email");
      reqConfig.put("GCE_PROJECT", "project");
      reqConfig.put("GOOGLE_APPLICATION_CREDENTIALS", "credentials");
    } else if (code.equals("aws")) {
      reqConfig.put("foo", "bar");
      reqConfig.put("foo2", "bar2");
    } else {
      reqConfig.put("home", "/bar");
    }
    providerReq.customerUUID = customer.uuid;
    providerReq.setConfig(reqConfig);
    Provider createdProvider =
        createProviderTest(providerReq, REGION_CODES_FROM_CLOUD_API, UUID.randomUUID());
    Map<String, String> config = createdProvider.getUnmaskedConfig();
    assertFalse(config.isEmpty());
    // We should technically check the actual content, but the keys are different between the
    // input payload and the saved config. (So what?! check the expected keys)
    assertEquals(reqConfig.size(), config.size());
  }

  // Following tests wont be migrated because no k8s multi-region support:
  //  public void testCreateKubernetesMultiRegionProvider() {
  //  public void testCreateKubernetesMultiRegionProviderFailure() {
  //  public void testGetK8sSuggestedConfig() {
  //  public void testGetK8sSuggestedConfigWithoutPullSecret() {
  //  public void testGetKubernetesConfigsDiscoveryFailure() {

  //  @Test
  public void testDeleteProviderWithAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    AccessKey ak = AccessKey.create(p.uuid, "access-key-code", new AccessKey.KeyInfo());
    Result result = deleteProvider(p.uuid);
    assertYBPSuccess(result, "Deleted provider: " + p.uuid);
    assertEquals(0, AccessKey.getAll(p.uuid).size());
    assertNull(Provider.get(p.uuid));
    verify(mockAccessManager, times(1))
        .deleteKeyByProvider(p, ak.getKeyCode(), ak.getKeyInfo().deleteRemote);
    assertAuditEntry(1, customer.uuid);
  }

  //  @Test
  public void testDeleteProviderWithInstanceType() {
    Provider p = ModelFactory.onpremProvider(customer);

    when(mockConfigHelper.getAWSInstancePrefixesSupported())
        .thenReturn(ImmutableList.of("m3.", "c5.", "c5d.", "c4.", "c3.", "i3."));
    ObjectNode metaData = Json.newObject();
    metaData.put("numCores", 4);
    metaData.put("memSizeGB", 300);
    InstanceType.InstanceTypeDetails instanceTypeDetails = new InstanceType.InstanceTypeDetails();
    instanceTypeDetails.volumeDetailsList = new ArrayList<>();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeSizeGB = 20;
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    instanceTypeDetails.volumeDetailsList.add(volumeDetails);
    metaData.put("longitude", -119.417932);
    metaData.put("ybImage", "yb-image-1");
    metaData.set("instanceTypeDetails", Json.toJson(instanceTypeDetails));

    InstanceType.createWithMetadata(p.uuid, "region-1", metaData);
    AccessKey ak = AccessKey.create(p.uuid, "access-key-code", new AccessKey.KeyInfo());
    Result result = deleteProvider(p.uuid);
    assertYBPSuccess(result, "Deleted provider: " + p.uuid);

    assertEquals(0, InstanceType.findByProvider(p, mockConfig, mockConfigHelper).size());
    assertNull(Provider.get(p.uuid));
  }

  //  @Test
  public void testDeleteProviderWithMultiRegionAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    AccessKey ak = AccessKey.create(p.uuid, "access-key-code", new AccessKey.KeyInfo());
    Result result = deleteProvider(p.uuid);
    assertYBPSuccess(result, "Deleted provider: " + p.uuid);
    assertEquals(0, AccessKey.getAll(p.uuid).size());
    assertNull(Provider.get(p.uuid));
    verify(mockAccessManager, times(1))
        .deleteKeyByProvider(p, ak.getKeyCode(), ak.getKeyInfo().deleteRemote);
    assertAuditEntry(1, customer.uuid);
  }

  //  @Test
  public void testDeleteProviderWithInvalidProviderUUID() {
    UUID providerUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> deleteProvider(providerUUID));
    assertBadRequest(result, "Invalid Provider UUID: " + providerUUID);
    assertAuditEntry(0, customer.uuid);
  }

  //  @Test
  public void testDeleteProviderWithUniverses() {
    Provider p = ModelFactory.awsProvider(customer);
    Universe universe = createUniverse(customer.getCustomerId());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = p.uuid.toString();
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    userIntent.regionList = new ArrayList<>();
    userIntent.regionList.add(r.uuid);
    universe =
        Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent));
    Result result = assertPlatformException(() -> deleteProvider(p.uuid));
    assertBadRequest(result, "Cannot delete Provider with Universes");
    assertAuditEntry(0, customer.uuid);
  }

  //  @Test
  public void testDeleteProviderWithoutAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    Result result = deleteProvider(p.uuid);
    assertYBPSuccess(result, "Deleted provider: " + p.uuid);
    assertNull(Provider.get(p.uuid));
    assertAuditEntry(1, customer.uuid);
  }

  //  @Test
  public void testDeleteProviderWithProvisionScript() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    String scriptFile = createTempFile("provision_instance.py", "some script");
    keyInfo.provisionInstanceScript = scriptFile;
    AccessKey.create(p.uuid, "access-key-code", keyInfo);
    Result result = deleteProvider(p.uuid);
    assertOk(result);
    assertFalse(new File(scriptFile).exists());
    assertAuditEntry(1, customer.uuid);
  }

  //  @Test
  public void testCreateAwsProviderWithInvalidAWSCredentials() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "aws-Provider");
    bodyJson.put("region", "ap-south-1");
    ObjectNode configJson = Json.newObject();
    configJson.put("AWS_ACCESS_KEY_ID", "test");
    configJson.put("AWS_SECRET_ACCESS_KEY", "secret");
    configJson.put("AWS_HOSTED_ZONE_ID", "1234");
    bodyJson.set("config", configJson);
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);
    Result result = assertPlatformException(() -> createProvider(bodyJson));
    assertBadRequest(result, "Invalid AWS Credentials.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateAwsProviderWithInvalidDevopsReply() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "aws-Provider");
    ObjectNode configJson = Json.newObject();
    configJson.put("HOSTED_ZONE_ID", "1234");
    bodyJson.set("config", configJson);
    mockDnsManagerListFailure("fail", 0);
    Result result = assertPlatformException(() -> createProvider(bodyJson));
    verify(mockDnsManager, times(1)).listDnsRecord(any(), any());
    assertInternalServerError(result, "Invalid devops API response: ");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateAwsProviderWithInvalidHostedZoneId() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "aws-Provider");
    ObjectNode configJson = Json.newObject();
    configJson.put("HOSTED_ZONE_ID", "1234");
    bodyJson.set("config", configJson);

    mockDnsManagerListFailure("fail", 1);
    Result result = assertPlatformException(() -> createProvider(bodyJson));
    verify(mockDnsManager, times(1)).listDnsRecord(any(), any());
    assertInternalServerError(result, "Invalid devops API response: ");
    assertAuditEntry(0, customer.uuid);
  }

  private void mockDnsManagerListSuccess() {
    mockDnsManagerListSuccess("test");
  }

  private void mockDnsManagerListSuccess(String mockDnsName) {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"name\": \"" + mockDnsName + "\"}";
    shellResponse.code = 0;
    when(mockDnsManager.listDnsRecord(any(), any())).thenReturn(shellResponse);
  }

  private void mockDnsManagerListFailure(String mockFailureMessage, int successCode) {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"wrong_key\": \"" + mockFailureMessage + "\"}";
    shellResponse.code = successCode;
    when(mockDnsManager.listDnsRecord(any(), any())).thenReturn(shellResponse);
  }

  @Test
  public void testCreateProviderConfigEncryption() {
    Map<String, String> testConfig = new HashMap<>();
    testConfig.put("ACCOUNT_NAME", "John Doe");
    testConfig.put("ACCOUNT_MAIL", "jdoe@yugabyte.com");
    Provider testProvider = buildProviderReq("aws", "test-provider");
    testProvider.setConfig(testConfig);
    Provider createdProvider =
        createProviderTest(testProvider, ImmutableList.of("region1", "region2"), UUID.randomUUID());
    assertTrue(createdProvider.getConfig().containsKey("encrypted"));
    Map<String, String> decryptedConfig = createdProvider.getUnmaskedConfig();
    assertEquals(testConfig, decryptedConfig);
  }

  @Test
  public void testAddRegion() {
    when(mockCommissioner.submit(any(TaskType.class), any(CloudBootstrap.Params.class)))
        .thenReturn(UUID.randomUUID());
    Provider provider = Provider.create(customer.uuid, Common.CloudType.aws, "test");
    AccessKey.create(provider.uuid, AccessKey.getDefaultKeyCode(provider), new AccessKey.KeyInfo());
    String jsonString =
        "{\"code\":\"aws\",\"name\":\"test\",\"regions\":[{\"name\":\"us-west-1\""
            + ",\"code\":\"us-west-1\",\"vnetName\":\"vpc-foo\","
            + "\"securityGroupId\":\"sg-foo\","
            + "\"zones\":[{\"code\":\"us-west-1a\",\"name\":\"us-west-1a\","
            + "\"secondarySubnet\":\"subnet-foo\",\"subnet\":\"subnet-foo\"}]}]}";
    Result result = editProvider(Json.parse(jsonString), provider.uuid);
    assertOk(result);
  }

  @Test
  public void testAddExistingRegionFail() {
    when(mockCommissioner.submit(any(TaskType.class), any(CloudBootstrap.Params.class)))
        .thenReturn(UUID.randomUUID());
    Provider provider = Provider.create(customer.uuid, Common.CloudType.aws, "test");
    Region.create(provider, "us-west-1", "us-west-1", "foo");
    String jsonString =
        "{\"code\":\"aws\",\"name\":\"test\",\"regions\":[{\"name\":\"us-west-1\""
            + ",\"code\":\"us-west-1\",\"vnetName\":\"vpc-foo\","
            + "\"securityGroupId\":\"sg-foo\","
            + "\"zones\":[{\"code\":\"us-west-1a\",\"name\":\"us-west-1a\","
            + "\"secondarySubnet\":\"subnet-foo\",\"subnet\":\"subnet-foo\"}]}]}";

    Result result =
        assertPlatformException(() -> editProvider(Json.parse(jsonString), provider.uuid));
    assertBadRequest(result, "No changes to be made for provider type: aws");
  }

  @Test
  public void testIncorrectFieldsForAddRegionFail() {
    when(mockCommissioner.submit(any(TaskType.class), any(CloudBootstrap.Params.class)))
        .thenReturn(UUID.randomUUID());
    Provider provider = Provider.create(customer.uuid, Common.CloudType.aws, "test");
    AccessKey.create(provider.uuid, AccessKey.getDefaultKeyCode(provider), new AccessKey.KeyInfo());
    String jsonString =
        "{\"code\":\"aws\",\"name\":\"test\",\"regions\":[{\"name\":\"us-west-1\""
            + ",\"code\":\"us-west-1\","
            + "\"securityGroupId\":\"sg-foo\","
            + "\"zones\":[{\"code\":\"us-west-1a\",\"name\":\"us-west-1a\","
            + "\"secondarySubnet\":\"subnet-foo\",\"subnet\":\"subnet-foo\"}]}]}";

    Result result =
        assertPlatformException(() -> editProvider(Json.parse(jsonString), provider.uuid));
    assertBadRequest(result, "Required field vnet name (VPC ID) for region: us-west-1");
  }

  @Test
  public void testAddRegionNoAccessKeyFail() {
    when(mockCommissioner.submit(any(TaskType.class), any(CloudBootstrap.Params.class)))
        .thenReturn(UUID.randomUUID());
    Provider provider = Provider.create(customer.uuid, Common.CloudType.aws, "test");
    String jsonString =
        "{\"code\":\"aws\",\"name\":\"test\",\"regions\":[{\"name\":\"us-west-1\""
            + ",\"code\":\"us-west-1\","
            + "\"securityGroupId\":\"sg-foo\","
            + "\"zones\":[{\"code\":\"us-west-1a\",\"name\":\"us-west-1a\","
            + "\"secondarySubnet\":\"subnet-foo\",\"subnet\":\"subnet-foo\"}]}]}";

    Result result =
        assertPlatformException(() -> editProvider(Json.parse(jsonString), provider.uuid));
    assertBadRequest(result, "KeyCode not found: " + AccessKey.getDefaultKeyCode(provider));
  }

  @Test
  public void testPatchProviderFailure() throws Exception {
    when(mockAccessManager.createCredentialsFile(any(), any())).thenReturn("/test-path");
    Provider provider = Provider.create(customer.uuid, Common.CloudType.gcp, "test");
    AccessKey.create(provider.uuid, AccessKey.getDefaultKeyCode(provider), new AccessKey.KeyInfo());
    String jsonString =
        "{"
            + "\"code\":\"aws\","
            + "\"name\":\"test\","
            + "\"config\": {"
            + "\"project_id\": \"test-project\","
            + "\"client_email\": \"test-email\""
            + "}}";
    Result result =
        assertPlatformException(() -> patchProvider(Json.parse(jsonString), provider.uuid));
    assertBadRequest(result, "Unknown keys found: [client_email, project_id]");
  }

  @Test
  public void testPatchProviderSuccess() throws Exception {
    when(mockAccessManager.createCredentialsFile(any(), any())).thenReturn("/test-path");
    when(mockAccessManager.readCredentialsFromFile(any()))
        .thenReturn(ImmutableMap.of("project_id", "test-project", "client_email", "test-email"));
    Provider provider = Provider.create(customer.uuid, Common.CloudType.gcp, "test");
    AccessKey.create(provider.uuid, AccessKey.getDefaultKeyCode(provider), new AccessKey.KeyInfo());
    String jsonString =
        "{"
            + "\"code\":\"aws\","
            + "\"name\":\"test\","
            + "\"config\": {"
            + "\"project_id\": \"test-project-updated\""
            + "}}";
    provider = Provider.get(customer.uuid, provider.uuid);
    Result result = patchProvider(Json.parse(jsonString), provider.uuid);
    assertOk(result);
    provider = Provider.get(customer.uuid, provider.uuid);
    Map<String, String> expectedConfig =
        ImmutableMap.of(
            "project_id", "test-project-updated",
            "client_email", "test-email",
            "GCE_PROJECT", "test-project-updated",
            "GCE_EMAIL", "test-email",
            "GOOGLE_APPLICATION_CREDENTIALS", "/test-path");
    Map<String, String> config = provider.getUnmaskedConfig();
    assertEquals(expectedConfig, config);
  }
}
