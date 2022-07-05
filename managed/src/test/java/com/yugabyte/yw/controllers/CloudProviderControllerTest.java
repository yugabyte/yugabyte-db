// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValueAtPath;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.common.AssertHelper.assertYBPSuccess;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static junit.framework.TestCase.assertNull;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyMap;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.NullNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
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
import java.io.IOException;
import java.net.URLEncoder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.NodeList;
import io.fabric8.kubernetes.api.model.Secret;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class CloudProviderControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderControllerTest.class);

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
      when(mockAccessManager.createKubernetesConfig(anyString(), anyMap(), anyBoolean()))
          .thenReturn(kubeFile);
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
        "POST",
        "/api/customers/" + customer.uuid + "/providers/ui",
        user.createAuthToken(),
        bodyJson);
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

  private Result bootstrapProvider(JsonNode bodyJson, Provider provider) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.uuid + "/providers/" + provider.uuid + "/bootstrap",
        user.createAuthToken(),
        bodyJson);
  }

  @Test
  public void testListEmptyProviders() {
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertTrue(json.isArray());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
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

  @Test
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
  @Parameters({
    "Fake Provider, aws, 0",
    "Test Provider, aws, 1",
    "Test Provider, null, 2",
  })
  public void testProviderFindByName(String name, String code, int expected) {
    Provider.create(customer.uuid, Common.CloudType.aws, "Test Provider");
    Provider.create(customer.uuid, Common.CloudType.gcp, "Test Provider");
    Provider.create(customer.uuid, Common.CloudType.aws, "Another Test Provider");
    String findUrl =
        "/api/customers/" + customer.uuid + "/providers?name=" + URLEncoder.encode(name);
    if (!code.equals("null")) {
      findUrl += "&providerCode=" + URLEncoder.encode(code);
    }
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", findUrl, user.createAuthToken());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(expected, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateProvider() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "azu");
    bodyJson.put("name", "Microsoft");
    bodyJson.put("config", NullNode.getInstance());
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "Microsoft");
    assertValue(json, "customerUUID", customer.uuid.toString());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateMultiInstanceProvider() {
    ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "Amazon1");
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "name", "Amazon1");
    assertOk(result);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateMultiInstanceProviderWithSameNameAndCloud() {
    ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "Amazon");
    Result result = assertPlatformException(() -> createProvider(bodyJson));
    assertBadRequest(result, "Provider with the name Amazon already exists");
  }

  @Test
  public void testCreateMultiInstanceProviderWithSameNameButDifferentCloud() {
    ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "gcp");
    bodyJson.put("name", "Amazon");
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "name", "Amazon");
    assertOk(result);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateProviderSameNameDiffCustomer() {
    Provider.create(UUID.randomUUID(), Common.CloudType.aws, "Amazon");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "Amazon");
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "Amazon");
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateWithInvalidParams() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    Result result = assertPlatformException(() -> createProvider(bodyJson));
    assertBadRequest(result, "\"name\":[\"error.required\"]}");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateProviderWithConfig() {
    List<String> providerCodes = ImmutableList.of("aws", "gcp");
    for (String code : providerCodes) {
      String providerName = code + "-Provider";
      ObjectNode bodyJson = Json.newObject();
      bodyJson.put("code", code);
      bodyJson.put("name", providerName);
      ObjectNode configJson = Json.newObject();
      ObjectNode configFileJson = Json.newObject();
      if (code.equals("gcp")) {
        // Technically this is not the input format of the file, but we're using this to match the
        // number of elements...
        configFileJson.put("GCE_EMAIL", "email");
        configFileJson.put("GCE_PROJECT", "project");
        configFileJson.put("GOOGLE_APPLICATION_CREDENTIALS", "credentials");
        configJson.put("config_file_contents", configFileJson);
      } else if (code.equals("aws")) {
        configJson.put("AWS_ACCESS_KEY_ID", "key");
        configJson.put("AWS_SECRET_ACCESS_KEY", "secret");
      }
      bodyJson.set("config", configJson);
      Result result = createProvider(bodyJson);
      JsonNode json = Json.parse(contentAsString(result));
      assertOk(result);
      assertValue(json, "name", providerName);
      Provider provider = Provider.get(customer.uuid, UUID.fromString(json.path("uuid").asText()));
      Map<String, String> config = provider.getUnmaskedConfig();
      assertFalse(config.isEmpty());
      // We should technically check the actual content, but the keys are different between the
      // input payload and the saved config.
      // (TODO: Then check the expected keys :) )
      if (code.equals("gcp")) {
        assertEquals(configFileJson.size(), config.size());
      } else {
        assertEquals(configJson.size(), config.size());
      }
    }
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testCreateProviderWithHostVpcGcp() {
    String providerName = "gcp-Provider";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "gcp");
    bodyJson.put("name", providerName);
    ObjectNode configJson = Json.newObject();
    configJson.put("use_host_vpc", true);
    configJson.put("project_id", "project");
    bodyJson.set("config", configJson);
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", providerName);
    Provider provider = Provider.get(customer.uuid, UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = provider.getUnmaskedConfig();
    assertTrue(config.isEmpty());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateProviderWithHostCredentialsGcp() {
    String providerName = "gcp-Provider";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "gcp");
    bodyJson.put("name", providerName);
    ObjectNode configJson = Json.newObject();
    ObjectNode configFileJson = Json.newObject();
    configFileJson.put("client_email", "email");
    configFileJson.put("project_id", "project");
    configFileJson.put("GOOGLE_APPLICATION_CREDENTIALS", "credentials");
    configJson.put("config_file_contents", configFileJson);
    configJson.put("use_host_credentials", true);
    bodyJson.set("config", configJson);
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", providerName);
    Provider provider = Provider.get(customer.uuid, UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = provider.getUnmaskedConfig();
    assertTrue(config.isEmpty());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateKubernetesMultiRegionProvider() {
    ObjectMapper mapper = new ObjectMapper();

    String providerName = "Kubernetes-Provider";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "kubernetes");
    bodyJson.put("name", providerName);
    ObjectNode configJson = Json.newObject();
    configJson.put("KUBECONFIG_NAME", "test");
    configJson.put("KUBECONFIG_CONTENT", "test");
    bodyJson.set("config", configJson);

    ArrayNode regions = mapper.createArrayNode();
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "US-West");
    regionJson.put("name", "US West");
    ArrayNode azs = mapper.createArrayNode();
    ObjectNode azJson = Json.newObject();
    azJson.put("code", "us-west1-a");
    azJson.put("name", "us-west1-a");
    azs.add(azJson);
    regionJson.putArray("zoneList").addAll(azs);
    regions.add(regionJson);

    bodyJson.putArray("regionList").addAll(regions);

    Result result = createKubernetesProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", providerName);
    Provider provider = Provider.get(customer.uuid, UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = provider.getUnmaskedConfig();
    assertFalse(config.isEmpty());
    List<Region> createdRegions = Region.getByProvider(provider.uuid);
    assertEquals(1, createdRegions.size());
    List<AvailabilityZone> createdZones =
        AvailabilityZone.getAZsForRegion(createdRegions.get(0).uuid);
    assertEquals(1, createdZones.size());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateKubernetesMultiRegionProviderFailure() {
    ObjectMapper mapper = new ObjectMapper();

    String providerName = "Kubernetes-Provider";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "kubernetes");
    bodyJson.put("name", providerName);
    ObjectNode configJson = Json.newObject();
    configJson.put("KUBECONFIG_NAME", "test");
    configJson.put("KUBECONFIG_CONTENT", "test");
    bodyJson.set("config", configJson);

    ArrayNode regions = mapper.createArrayNode();
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "US-West");
    regionJson.put("name", "US West");
    ArrayNode azs = mapper.createArrayNode();
    ObjectNode azJson = Json.newObject();
    azJson.put("code", "us-west1-a");
    azJson.put("name", "us-west1-a");
    azJson.put("config", configJson);
    azs.add(azJson);
    regionJson.putArray("zoneList").addAll(azs);
    regions.add(regionJson);

    bodyJson.putArray("regionList").addAll(regions);

    Result result = assertPlatformException(() -> createKubernetesProvider(bodyJson));
    JsonNode json = Json.parse(contentAsString(result));
    assertBadRequest(result, "Kubeconfig can't be at two levels");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGetK8sSuggestedConfig() {
    testGetK8sSuggestedConfigBase(false);
  }

  @Test
  public void testGetK8sSuggestedConfigWithoutPullSecret() {
    testGetK8sSuggestedConfigBase(true);
  }

  private void testGetK8sSuggestedConfigBase(boolean noPullSecret) {
    String pullSecretName = "pull-sec";
    String storageClassName = "ssd-class";
    // Was not able to get this working after trying various
    // approaches, so added the values to application.test.conf
    // directly
    // when(mockAppConfig.getString("yb.kubernetes.storageClass")).thenReturn(storageClassName);
    // when(mockAppConfig.getString("yb.kubernetes.pullSecretName")).thenReturn(pullSecretName);

    String nodeInfos =
        "{\"items\": ["
            + "{\"metadata\": {\"labels\": "
            + "{\"failure-domain.beta.kubernetes.io/region\": \"deprecated\", "
            + "\"failure-domain.beta.kubernetes.io/zone\": \"deprecated\", "
            + "\"topology.kubernetes.io/region\": \"region-1\", \"topology.kubernetes.io/zone\": \"r1-az1\"}, "
            + "\"name\": \"node-1\"}}, "
            + "{\"metadata\": {\"labels\": "
            + "{\"failure-domain.beta.kubernetes.io/region\": \"region-2\", "
            + "\"failure-domain.beta.kubernetes.io/zone\": \"r2-az1\"}, "
            + "\"name\": \"node-2\"}}, "
            + "{\"metadata\": {\"labels\": "
            + "{\"topology.kubernetes.io/region\": \"region-3\", \"topology.kubernetes.io/zone\": \"r3-az1\"}, "
            + "\"name\": \"node-3\"}}"
            + "]}";
    List<Node> nodes = TestUtils.deserialize(nodeInfos, NodeList.class).getItems();
    when(mockKubernetesManager.getNodeInfos(any())).thenReturn(nodes);

    String secretContent =
        "{\"metadata\": {"
            + "\"annotations\": {\"kubectl.kubernetes.io/last-applied-configuration\": \"removed\"}, "
            + "\"creationTimestamp\": \"2021-03-05\", \"name\": \""
            + pullSecretName
            + "\", "
            + "\"namespace\": \"testns\", "
            + "\"resourceVersion\": \"118225713\", \"selfLink\": \"/api/v1/to-be-removed\", "
            + "\"uid\": \"15fab1c4-3828-4783-b3b1-413c4e131bc7\"}, "
            + "\"data\": {\".dockerconfigjson\": \"sec-key\"}}";
    if (noPullSecret) {
      String msg = "Error from server (NotFound): secrets \"" + pullSecretName + "\" not found";
      when(mockKubernetesManager.getSecret(null, pullSecretName, null))
          .thenThrow(new RuntimeException(msg));
    } else {
      Secret secret = TestUtils.deserialize(secretContent, Secret.class);
      when(mockKubernetesManager.getSecret(null, pullSecretName, null)).thenReturn(secret);
    }

    Result result = getKubernetesSuggestedConfig();
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    if (noPullSecret) {
      assertTrue(Json.fromJson(json.path("config"), Map.class).isEmpty());
    } else {
      String parsedSecretString =
          "{\"metadata\":{"
              + "\"annotations\":{},"
              + "\"name\":\""
              + pullSecretName
              + "\"},"
              + "\"data\":{\".dockerconfigjson\":\"sec-key\"}}";
      Secret parsedSecret = TestUtils.deserialize(parsedSecretString, Secret.class);

      assertValueAtPath(json, "/config/KUBECONFIG_IMAGE_PULL_SECRET_NAME", pullSecretName);
      assertValueAtPath(json, "/config/KUBECONFIG_PULL_SECRET_NAME", pullSecretName);
      assertValueAtPath(json, "/config/KUBECONFIG_PULL_SECRET_CONTENT", parsedSecret.toString());
    }

    assertValues(
        json,
        "code",
        ImmutableList.of(
            "kubernetes", "region-3", "r3-az1", "region-1", "r1-az1", "region-2", "r2-az1"));
    assertValues(json, "STORAGE_CLASS", ImmutableList.of(storageClassName));
  }

  @Test
  public void testGetKubernetesConfigsDiscoveryFailure() {
    String nodeInfos =
        "{\"items\": ["
            + "{\"metadata\": {\"name\": \"node-1\"}}, "
            + "{\"metadata\": {\"name\": \"node-2\"}}, "
            + "{\"metadata\": {\"name\": \"node-3\"}}"
            + "]}";
    List<Node> nodes = TestUtils.deserialize(nodeInfos, NodeList.class).getItems();
    when(mockKubernetesManager.getNodeInfos(any())).thenReturn(nodes);

    Result result = assertPlatformException(() -> getKubernetesSuggestedConfig());
    assertInternalServerError(result, "No region and zone information found.");
  }

  @Test
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

  @Test
  public void testDeleteProviderWithInstanceType() {
    Provider p = ModelFactory.onpremProvider(customer);

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

  @Test
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

  @Test
  public void testDeleteProviderWithInvalidProviderUUID() {
    UUID providerUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> deleteProvider(providerUUID));
    assertBadRequest(result, "Invalid Provider UUID: " + providerUUID);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
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

  @Test
  public void testDeleteProviderWithoutAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    Result result = deleteProvider(p.uuid);
    assertYBPSuccess(result, "Deleted provider: " + p.uuid);
    assertNull(Provider.get(p.uuid));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
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

  @Test
  public void testEditProviderKubernetes() {
    Map<String, String> config = new HashMap<>();
    config.put("KUBECONFIG_PROVIDER", "gke");
    config.put("KUBECONFIG_SERVICE_ACCOUNT", "yugabyte-helm");
    config.put("KUBECONFIG_STORAGE_CLASSES", "");
    config.put("KUBECONFIG", "test.conf");
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.kubernetes, config);

    ObjectNode bodyJson = Json.newObject();
    config.put("KUBECONFIG_STORAGE_CLASSES", "slow");
    bodyJson.set("config", Json.toJson(config));
    bodyJson.put("name", "kubernetes");
    bodyJson.put("code", "kubernetes");

    Result result = editProvider(bodyJson, p.uuid);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(p.uuid, UUID.fromString(json.get("resourceUUID").asText()));
    p.refresh();
    assertEquals("slow", p.getUnmaskedConfig().get("KUBECONFIG_STORAGE_CLASSES"));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testEditProviderKubernetesConfigEdit() {
    Map<String, String> config = new HashMap<>();
    config.put("KUBECONFIG_PROVIDER", "gke");
    config.put("KUBECONFIG_SERVICE_ACCOUNT", "yugabyte-helm");
    config.put("KUBECONFIG_STORAGE_CLASSES", "");
    config.put("KUBECONFIG", "test.conf");
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.kubernetes, config);

    ObjectNode bodyJson = Json.newObject();
    config.put("KUBECONFIG_NAME", "test2.conf");
    config.put("KUBECONFIG_CONTENT", "test5678");
    bodyJson.set("config", Json.toJson(config));
    bodyJson.put("name", "kubernetes");
    bodyJson.put("code", "kubernetes");

    Result result = editProvider(bodyJson, p.uuid);
    assertOk(result);
    assertAuditEntry(1, customer.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(p.uuid, UUID.fromString(json.get("resourceUUID").asText()));
    p.refresh();
    assertTrue(p.getUnmaskedConfig().get("KUBECONFIG").contains("test2.conf"));
    Path path = Paths.get(p.getUnmaskedConfig().get("KUBECONFIG"));
    try {
      List<String> contents = Files.readAllLines(path);
      assertEquals(contents.get(0), "test5678");
    } catch (IOException e) {
      // Do nothing
    }
  }

  @Test
  public void testEditProviderWithAWSProviderType() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.aws);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("hostedZoneId", "1234");
    bodyJson.put("name", "aws");
    bodyJson.put("code", "aws");
    mockDnsManagerListSuccess();
    Result result = editProvider(bodyJson, p.uuid);
    verify(mockDnsManager, times(1)).listDnsRecord(any(), any());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(p.uuid, UUID.fromString(json.get("resourceUUID").asText()));
    p.refresh();
    assertEquals("1234", p.getUnmaskedConfig().get("HOSTED_ZONE_ID"));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testEditProviderWithInvalidProviderType() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("hostedZoneId", "1234");
    bodyJson.put("name", "aws");
    bodyJson.put("code", "aws");
    Result result = assertPlatformException(() -> editProvider(bodyJson, p.uuid));
    verify(mockDnsManager, times(0)).listDnsRecord(any(), any());
    assertBadRequest(result, "No changes to be made for provider type: onprem");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testEditProviderWithEmptyHostedZoneId() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.aws);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("hostedZoneId", "");
    bodyJson.put("name", "aws");
    bodyJson.put("code", "aws");
    Result result = assertPlatformException(() -> editProvider(bodyJson, p.uuid));
    verify(mockDnsManager, times(0)).listDnsRecord(any(), any());
    assertBadRequest(result, "No changes to be made for provider type: aws");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateAwsProviderWithValidHostedZoneId() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "aws-Provider");
    ObjectNode configJson = Json.newObject();
    configJson.put("HOSTED_ZONE_ID", "1234");
    bodyJson.set("config", configJson);

    mockDnsManagerListSuccess("test");
    Result result = createProvider(bodyJson);
    verify(mockDnsManager, times(1)).listDnsRecord(any(), any());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    Provider provider = Provider.get(customer.uuid, UUID.fromString(json.path("uuid").asText()));
    assertNotNull(provider);
    assertEquals("1234", provider.getHostedZoneId());
    assertEquals("test", provider.getHostedZoneName());
    assertEquals("1234", provider.getUnmaskedConfig().get("HOSTED_ZONE_ID"));
    assertEquals("test", provider.getUnmaskedConfig().get("HOSTED_ZONE_NAME"));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateAwsProviderWithInValidAWSCredentials() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "aws-Provider");
    bodyJson.put("region", "ap-south-1");
    ObjectNode configJson = Json.newObject();
    configJson.put("AWS_ACCESS_KEY_ID", "test");
    configJson.put("AWS_SECRET_ACCESS_KEY", "secret");
    configJson.put("HOSTED_ZONE_ID", "1234");
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

  @Test
  public void testGcpBootstrapMultiRegionNoRegionInput() {
    Provider provider = ModelFactory.gcpProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    prepareBootstrap(bodyJson, provider, true);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testGcpBootstrapMultiRegionSomeRegionInput() {
    Provider provider = ModelFactory.gcpProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    ObjectNode perRegionMetadata = Json.newObject();
    perRegionMetadata.put("region1", Json.newObject());
    bodyJson.put("perRegionMetadata", perRegionMetadata);
    prepareBootstrap(bodyJson, provider, false);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testAwsBootstrapWithDestVpcId() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Mockito.any(TaskType.class), Mockito.any(CloudBootstrap.Params.class)))
        .thenReturn(fakeTaskUUID);
    Provider provider = ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("destVpcId", "nofail");
    Result result = bootstrapProvider(bodyJson, provider);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertNotNull(json);
    assertNotNull(json.get("taskUUID"));
    assertAuditEntry(1, customer.uuid);
  }

  private void prepareBootstrap(
      ObjectNode bodyJson, Provider provider, boolean expectCallToGetRegions) {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(CloudBootstrap.Params.class)))
        .thenReturn(fakeTaskUUID);
    when(mockCloudQueryHelper.getRegionCodes(provider))
        .thenReturn(ImmutableList.of("region1", "region2"));
    Result result = bootstrapProvider(bodyJson, provider);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertNotNull(json);
    assertNotNull(json.get("taskUUID"));
    // TODO(bogdan): figure out a better way to inspect what tasks and with what params get started.
    verify(mockCloudQueryHelper, times(expectCallToGetRegions ? 1 : 0)).getRegionCodes(provider);
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
}
