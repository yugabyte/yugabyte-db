// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValueAtPath;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static junit.framework.TestCase.assertNull;
import static junit.framework.TestCase.assertTrue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.anyMap;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.eq;
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
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudProviderDelete;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.KeyInfo;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import io.fabric8.kubernetes.api.model.Node;
import io.fabric8.kubernetes.api.model.NodeList;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.utils.Serialization;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.MockitoAnnotations;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class CloudProviderControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderControllerTest.class);

  @Mock Config mockConfig;
  @Mock RuntimeConfGetter mockConfGetter;

  Customer customer;
  Users user;

  @Before
  public void setUp() {
    MockitoAnnotations.initMocks(this);
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    try {
      String kubeFile = createTempFile("test2.conf", "test5678");
      when(mockAccessManager.createKubernetesConfig(anyString(), anyMap(), anyBoolean()))
          .thenReturn(kubeFile);
    } catch (Exception e) {
      // Do nothing
    }
    when(mockAccessManager.createKubernetesAuthDataFile(
            anyString(), anyString(), anyString(), anyBoolean()))
        .thenReturn("/tmp/some-fake-path-here/kubernetes-auth-file.txt");
    when(mockConfGetter.getConfForScope(customer, CustomerConfKeys.cloudEnabled)).thenReturn(false);
    when(mockConfGetter.getStaticConf()).thenReturn(mockConfig);
  }

  private Result listProviders() {
    return doRequestWithAuthToken(
        "GET", "/api/customers/" + customer.getUuid() + "/providers", user.createAuthToken());
  }

  private Result createProvider(JsonNode bodyJson) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.getUuid() + "/providers/ui",
        user.createAuthToken(),
        bodyJson);
  }

  private Result createKubernetesProvider(JsonNode bodyJson) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.getUuid() + "/providers/kubernetes",
        user.createAuthToken(),
        bodyJson);
  }

  private Result getKubernetesSuggestedConfig() {
    return doRequestWithAuthToken(
        "GET",
        "/api/customers/" + customer.getUuid() + "/providers/suggested_kubernetes_config",
        user.createAuthToken());
  }

  private Result deleteProvider(UUID providerUUID) {
    return doRequestWithAuthToken(
        "DELETE",
        "/api/customers/" + customer.getUuid() + "/providers/" + providerUUID,
        user.createAuthToken());
  }

  private Result editProvider(JsonNode bodyJson, UUID providerUUID) {
    return doRequestWithAuthTokenAndBody(
        "PUT",
        "/api/customers/" + customer.getUuid() + "/providers/" + providerUUID + "/edit",
        user.createAuthToken(),
        bodyJson);
  }

  private Result bootstrapProvider(JsonNode bodyJson, Provider provider) {
    return doRequestWithAuthTokenAndBody(
        "POST",
        "/api/customers/" + customer.getUuid() + "/providers/" + provider.getUuid() + "/bootstrap",
        user.createAuthToken(),
        bodyJson);
  }

  private Result getProvider(UUID providerUUID) {
    return doRequestWithAuthToken(
        "GET",
        "/api/customers/" + customer.getUuid() + "/providers/" + providerUUID,
        user.createAuthToken());
  }

  @Test
  public void testListEmptyProviders() {
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertTrue(json.isArray());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListProviders() {
    Provider p1 = ModelFactory.awsProvider(customer);
    p1.setConfigMap(
        ImmutableMap.of(
            "AWS_ACCESS_KEY_ID", "SENSITIVE_DATA", "AWS_SECRET_ACCESS_KEY", "SENSITIVE_DATA"));
    p1.save();
    Provider p2 = ModelFactory.gcpProvider(customer);
    p2.setConfigMap(ImmutableMap.of("host_project_id", "BAR"));
    p2.save();
    Result result = listProviders();
    JsonNode json = Json.parse(contentAsString(result));

    assertOk(result);
    assertAuditEntry(0, customer.getUuid());
    assertEquals(2, json.size());
    assertValues(json, "uuid", ImmutableList.of(p1.getUuid().toString(), p2.getUuid().toString()));
    assertValues(json, "name", ImmutableList.of(p1.getName(), p2.getName()));
    json.forEach(
        (providerJson) -> {
          JsonNode config = providerJson.get("config");
          if (UUID.fromString(providerJson.get("uuid").asText()).equals(p1.getUuid())) {
            assertValue(config, "AWS_ACCESS_KEY_ID", "SE**********TA");
            assertValue(config, "AWS_SECRET_ACCESS_KEY", "SE**********TA");
          } else {
            assertValue(config, "GCE_HOST_PROJECT", "BAR");
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
    assertValues(json, "uuid", ImmutableList.of(p.getUuid().toString()));
    assertValues(json, "name", ImmutableList.of(p.getName()));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  @Parameters({
    "Fake Provider, aws, 0",
    "Test Provider, aws, 1",
    "Test Provider, null, 2",
  })
  public void testProviderFindByName(String name, String code, int expected) {
    Provider.create(customer.getUuid(), Common.CloudType.aws, "Test Provider");
    Provider.create(customer.getUuid(), Common.CloudType.gcp, "Test Provider");
    Provider.create(customer.getUuid(), Common.CloudType.aws, "Another Test Provider");
    String findUrl =
        "/api/customers/" + customer.getUuid() + "/providers?name=" + URLEncoder.encode(name);
    if (!code.equals("null")) {
      findUrl += "&providerCode=" + URLEncoder.encode(code);
    }
    Result result = doRequestWithAuthToken("GET", findUrl, user.createAuthToken());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(expected, json.size());
    assertAuditEntry(0, customer.getUuid());
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
    assertValue(json, "customerUUID", customer.getUuid().toString());
    assertAuditEntry(1, customer.getUuid());
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
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateMultiInstanceProviderWithSameNameAndCloud() {
    ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    bodyJson.put("name", "Amazon");
    Result result = assertPlatformException(() -> createProvider(bodyJson));
    assertConflict(result, "Provider with the name Amazon already exists");
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
    assertAuditEntry(1, customer.getUuid());
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
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateWithInvalidParams() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "aws");
    Result result = assertPlatformException(() -> createProvider(bodyJson));
    assertBadRequest(result, "\"name\":[\"error.required\"]}");
    assertAuditEntry(0, customer.getUuid());
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
        configFileJson.put("project_id", "project");
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
      Provider provider =
          Provider.get(customer.getUuid(), UUID.fromString(json.path("uuid").asText()));
      Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
      assertFalse(config.isEmpty());
      assertEquals(configJson.size(), config.size());
    }
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testCreateProviderWithHostVpcGcp() {
    String providerName = "gcp-Provider";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "gcp");
    bodyJson.put("name", providerName);
    ObjectNode configJson = Json.newObject();
    configJson.put("project_id", "project");
    bodyJson.set("config", configJson);
    Result result = createProvider(bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", providerName);
    Provider provider =
        Provider.get(customer.getUuid(), UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
    assertTrue(config.isEmpty());
    assertAuditEntry(1, customer.getUuid());
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
    Provider provider =
        Provider.get(customer.getUuid(), UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
    assertTrue(!config.isEmpty());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateKubernetesMultiRegionProvider() {
    JsonNode k8sProviderBody = getK8sProviderCreateBody();

    Result result = createKubernetesProvider(k8sProviderBody);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "Kubernetes-Provider");
    Provider provider =
        Provider.get(customer.getUuid(), UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
    assertFalse(config.isEmpty());
    List<Region> createdRegions = Region.getByProvider(provider.getUuid());
    assertEquals(1, createdRegions.size());
    List<AvailabilityZone> createdZones =
        AvailabilityZone.getAZsForRegion(createdRegions.get(0).getUuid());
    assertEquals(1, createdZones.size());
    assertAuditEntry(1, customer.getUuid());

    verify(mockAccessManager, times(1)).createKubernetesConfig(anyString(), anyMap(), eq(false));
    verify(mockAccessManager, times(2))
        .createKubernetesAuthDataFile(anyString(), anyString(), anyString(), eq(false));
    verify(mockPrometheusConfigManager, times(1)).updateK8sScrapeConfigs();
  }

  @Test
  public void testCreateKubernetesWithNonTokenKubeConfig() {
    JsonNode k8sProviderBody = getK8sProviderCreateBody();
    ObjectNode config = (ObjectNode) k8sProviderBody.get("config");
    config.put("KUBECONFIG_CONTENT", TestUtils.readResource("test-kubeconfig-client-cert.conf"));

    Result result = createKubernetesProvider(k8sProviderBody);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "name", "Kubernetes-Provider");
    Provider provider =
        Provider.get(customer.getUuid(), UUID.fromString(json.path("uuid").asText()));
    KubernetesInfo k8sInfo = CloudInfoInterface.get(provider);
    // Provider creation shouldn't fail, and we have partial details.
    assertEquals("https://1.2.3.4", k8sInfo.getApiServerEndpoint());
    assertNotNull(k8sInfo.getKubeConfigCAFile());
    assertNull(k8sInfo.getKubeConfigTokenFile());

    verify(mockAccessManager, times(1)).createKubernetesConfig(anyString(), anyMap(), eq(false));
    verify(mockAccessManager, times(1))
        .createKubernetesAuthDataFile(anyString(), anyString(), anyString(), eq(false));
    verify(mockPrometheusConfigManager, times(1)).updateK8sScrapeConfigs();
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
    assertAuditEntry(0, customer.getUuid());
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
    Pod testPod = null;
    try {
      File jsonFile = new File("src/test/resources/testYugaware.json");
      InputStream jsonStream = new FileInputStream(jsonFile);

      testPod = Serialization.unmarshal(jsonStream, Pod.class);
      when(mockKubernetesManager.getPodObject(any(), any(), any())).thenReturn(testPod);
    } catch (Exception e) {
    }
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
      assertValueAtPath(json, "/config/KUBECONFIG_IMAGE_PULL_SECRET_NAME", pullSecretName);
      assertValueAtPath(json, "/config/KUBECONFIG_PULL_SECRET_NAME", pullSecretName);
      String registryPath = "quay.io/yugabyte/yugabyte-itest";
      assertValueAtPath(json, "/config/KUBECONFIG_IMAGE_REGISTRY", registryPath);
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
    AccessKey ak = AccessKey.create(p.getUuid(), "access-key-code", new AccessKey.KeyInfo());
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = p.getUuid();
    params.customer = customer;

    try {
      CloudProviderDelete deleteProviderTask =
          AbstractTaskBase.createTask(CloudProviderDelete.class);
      deleteProviderTask.initialize(params);
      deleteProviderTask.run();
      // Adding the timeout so as to ensure we wait for the provider deletion to be completed.
      Thread.sleep(2000);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }

    assertEquals(0, AccessKey.getAll(p.getUuid()).size());
    assertNull(Provider.get(p.getUuid()));
    verify(mockAccessManager, times(1)).deleteKeyByProvider(p, ak);
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

    InstanceType.createWithMetadata(p.getUuid(), "region-1", metaData);
    AccessKey ak = AccessKey.create(p.getUuid(), "access-key-code", new AccessKey.KeyInfo());
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = p.getUuid();
    params.customer = customer;

    try {
      CloudProviderDelete deleteProviderTask =
          AbstractTaskBase.createTask(CloudProviderDelete.class);
      deleteProviderTask.initialize(params);
      deleteProviderTask.run();
      // Adding the timeout so as to ensure we wait for the provider deletion to be completed.
      Thread.sleep(2000);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }

    assertEquals(0, InstanceType.findByProvider(p, mockConfGetter).size());
    assertNull(Provider.get(p.getUuid()));
  }

  @Test
  public void testDeleteProviderWithMultiRegionAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    AccessKey ak = AccessKey.create(p.getUuid(), "access-key-code", new AccessKey.KeyInfo());
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = p.getUuid();
    params.customer = customer;

    try {
      CloudProviderDelete deleteProviderTask =
          AbstractTaskBase.createTask(CloudProviderDelete.class);
      deleteProviderTask.initialize(params);
      deleteProviderTask.run();
      // Adding the timeout so as to ensure we wait for the provider deletion to be completed.
      Thread.sleep(2000);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }

    assertEquals(0, AccessKey.getAll(p.getUuid()).size());
    assertNull(Provider.get(p.getUuid()));
    verify(mockAccessManager, times(1))
        .deleteKeyByProvider(p, ak.getKeyCode(), ak.getKeyInfo().deleteRemote);
  }

  @Test
  public void testDeleteProviderWithInvalidProviderUUID() {
    UUID providerUUID = UUID.randomUUID();
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = providerUUID;
    params.customer = customer;

    CloudProviderDelete deleteProviderTask = AbstractTaskBase.createTask(CloudProviderDelete.class);
    deleteProviderTask.initialize(params);
    Result result = assertPlatformException(() -> deleteProviderTask.run());
    assertBadRequest(result, "Invalid Provider UUID: " + providerUUID);
  }

  @Test(expected = Exception.class)
  public void testDeleteProviderWithUniverses() {
    Provider p = ModelFactory.awsProvider(customer);
    Universe universe = createUniverse(customer.getId());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = p.getUuid().toString();
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    userIntent.regionList = new ArrayList<>();
    userIntent.regionList.add(r.getUuid());
    universe =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = p.getUuid();
    params.customer = customer;

    CloudProviderDelete deleteProviderTask = AbstractTaskBase.createTask(CloudProviderDelete.class);
    deleteProviderTask.initialize(params);
    deleteProviderTask.run();
  }

  @Test
  public void testDeleteProviderWithoutAccessKey() {
    Provider p = ModelFactory.awsProvider(customer);
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = p.getUuid();
    params.customer = customer;

    CloudProviderDelete deleteProviderTask = AbstractTaskBase.createTask(CloudProviderDelete.class);
    deleteProviderTask.initialize(params);
    deleteProviderTask.run();
  }

  @Test
  public void testDeleteProviderWithProvisionScript() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    String scriptFile = createTempFile("provision_instance.py", "some script");
    assertTrue(new File(scriptFile).exists());
    p.getDetails().provisionInstanceScript = scriptFile;
    p.save();
    AccessKey.create(p.getUuid(), "access-key-code", new KeyInfo());
    CloudProviderDelete.Params params = new CloudProviderDelete.Params();
    params.providerUUID = p.getUuid();
    params.customer = customer;

    try {
      CloudProviderDelete deleteProviderTask =
          AbstractTaskBase.createTask(CloudProviderDelete.class);
      deleteProviderTask.initialize(params);
      deleteProviderTask.run();
      // Adding the timeout so as to ensure we wait for the provider deletion to be completed.
      Thread.sleep(2000);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }

    assertFalse(new File(scriptFile).exists());
  }

  @Test
  public void testEditProviderWithInvalidProviderType() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    Result providerRes = getProvider(p.getUuid());
    ObjectNode bodyJson = (ObjectNode) Json.parse(contentAsString(providerRes));
    bodyJson.put("hostedZoneId", "1234");
    bodyJson.put("name", "aws");
    bodyJson.put("code", "aws");
    Result result = assertPlatformException(() -> editProvider(bodyJson, p.getUuid()));
    verify(mockDnsManager, times(0)).listDnsRecord(any(), any());
    assertBadRequest(result, "Changing provider type is not supported!");
    assertAuditEntry(0, customer.getUuid());
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

    Provider provider =
        Provider.get(customer.getUuid(), UUID.fromString(json.path("uuid").asText()));
    Map<String, String> config = CloudInfoInterface.fetchEnvVars(provider);
    assertNotNull(provider);
    assertEquals("1234", config.get("HOSTED_ZONE_ID"));
    assertEquals("test", config.get("HOSTED_ZONE_NAME"));
    assertAuditEntry(1, customer.getUuid());
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
    assertAuditEntry(0, customer.getUuid());
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
    assertAuditEntry(0, customer.getUuid());
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
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGcpBootstrapMultiRegionNoRegionInput() {
    Provider provider = ModelFactory.gcpProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    prepareBootstrap(bodyJson, provider, true);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testGcpBootstrapMultiRegionSomeRegionInput() {
    Provider provider = ModelFactory.gcpProvider(customer);
    ObjectNode bodyJson = Json.newObject();
    ObjectNode perRegionMetadata = Json.newObject();
    perRegionMetadata.put("region1", Json.newObject());
    bodyJson.put("perRegionMetadata", perRegionMetadata);
    prepareBootstrap(bodyJson, provider, false);
    assertAuditEntry(1, customer.getUuid());
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
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testOnPremProviderManualProvisioning() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "onprem");
    bodyJson.put("name", "onprem,-Provider");

    ObjectNode details = Json.newObject();
    details.put("skipProvisioning", true);
    details.put("sshUser", "ec2-user");
    bodyJson.set("details", details);

    ArrayNode accessKeys = Json.newArray();
    ObjectNode accessKey = Json.newObject();
    ObjectNode keyInfo = Json.newObject();
    keyInfo.put("keyPairName", "testKeyPairName");
    keyInfo.put("sshPrivateKeyContent", "keyContent");
    accessKey.put("keyInfo", keyInfo);
    accessKeys.add(accessKey);
    bodyJson.put("allAccessKeys", accessKeys);

    Result result = createProvider(bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));

    Provider provider =
        Provider.get(customer.getUuid(), UUID.fromString(json.path("uuid").asText()));

    assertEquals(true, provider.getDetails().skipProvisioning);
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

  private JsonNode getK8sProviderCreateBody() {
    ObjectMapper mapper = new ObjectMapper();

    String providerName = "Kubernetes-Provider";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("code", "kubernetes");
    bodyJson.put("name", providerName);
    ObjectNode configJson = Json.newObject();
    configJson.put("KUBECONFIG_NAME", "test");
    configJson.put("KUBECONFIG_CONTENT", TestUtils.readResource("test-kubeconfig.conf"));
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

    return bodyJson;
  }
}
