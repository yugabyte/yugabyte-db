// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UniverseYbDbAdminHandler;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Matchers;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import org.yb.client.YBClient;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

import java.io.File;
import java.io.IOException;
import java.util.*;

import static com.yugabyte.yw.common.ApiUtils.getDefaultUserIntent;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.*;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.PlacementInfoUtil.*;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static junit.framework.TestCase.assertFalse;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Matchers.eq;
import static org.mockito.Mockito.*;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.test.Helpers.contentAsString;

@RunWith(JUnitParamsRunner.class)
public class UniverseControllerTest extends WithApplication {
  private static Commissioner mockCommissioner;
  private static MetricQueryHelper mockMetricQueryHelper;

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock private play.Configuration mockAppConfig;

  private HealthChecker healthChecker;
  private Customer customer;
  private Users user;
  private KmsConfig kmsConfig;
  private String authToken;
  private YBClientService mockService;
  private YBClient mockClient;
  private ApiHelper mockApiHelper;
  private CallHome mockCallHome;
  private CustomerConfig s3StorageConfig;
  private EncryptionAtRestManager mockEARManager;
  private YsqlQueryExecutor mockYsqlQueryExecutor;
  private YcqlQueryExecutor mockYcqlQueryExecutor;
  private ShellProcessHandler mockShellProcessHandler;
  protected CallbackController mockCallbackController;
  protected PlayCacheSessionStore mockSessionStore;
  private AlertConfigurationWriter mockAlertConfigurationWriter;
  private Config mockRuntimeConfig;

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockMetricQueryHelper = mock(MetricQueryHelper.class);
    mockClient = mock(YBClient.class);
    mockService = mock(YBClientService.class);
    mockApiHelper = mock(ApiHelper.class);
    mockCallHome = mock(CallHome.class);
    mockEARManager = mock(EncryptionAtRestManager.class);
    mockYsqlQueryExecutor = mock(YsqlQueryExecutor.class);
    mockYcqlQueryExecutor = mock(YcqlQueryExecutor.class);
    mockShellProcessHandler = mock(ShellProcessHandler.class);
    mockCallbackController = mock(CallbackController.class);
    mockSessionStore = mock(PlayCacheSessionStore.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    mockRuntimeConfig = mock(Config.class);
    healthChecker = mock(HealthChecker.class);

    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(false);
    when(mockRuntimeConfig.getBoolean("yb.security.use_oauth")).thenReturn(false);

    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(YBClientService.class).toInstance(mockService))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(CallHome.class).toInstance(mockCallHome))
        .overrides(bind(EncryptionAtRestManager.class).toInstance(mockEARManager))
        .overrides(bind(YsqlQueryExecutor.class).toInstance(mockYsqlQueryExecutor))
        .overrides(bind(YcqlQueryExecutor.class).toInstance(mockYcqlQueryExecutor))
        .overrides(bind(ShellProcessHandler.class).toInstance(mockShellProcessHandler))
        .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
        .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
        .overrides(bind(play.Configuration.class).toInstance(mockAppConfig))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockRuntimeConfig)))
        .overrides(bind(HealthChecker.class).toInstance(healthChecker))
        .build();
  }

  private PlacementInfo constructPlacementInfoObject(Map<UUID, Integer> azToNumNodesMap) {

    Map<UUID, PlacementInfo.PlacementCloud> placementCloudMap = new HashMap<>();
    Map<UUID, PlacementInfo.PlacementRegion> placementRegionMap = new HashMap<>();
    for (UUID azUUID : azToNumNodesMap.keySet()) {
      AvailabilityZone currentAz = AvailabilityZone.get(azUUID);

      // Get existing PlacementInfo Cloud or set up a new one.
      Provider currentProvider = currentAz.getProvider();
      PlacementInfo.PlacementCloud cloudItem =
          placementCloudMap.getOrDefault(currentProvider.uuid, null);
      if (cloudItem == null) {
        cloudItem = new PlacementInfo.PlacementCloud();
        cloudItem.uuid = currentProvider.uuid;
        cloudItem.code = currentProvider.code;
        cloudItem.regionList = new ArrayList<>();
        placementCloudMap.put(currentProvider.uuid, cloudItem);
      }

      // Get existing PlacementInfo Region or set up a new one.
      Region currentRegion = currentAz.region;
      PlacementInfo.PlacementRegion regionItem =
          placementRegionMap.getOrDefault(currentRegion.uuid, null);
      if (regionItem == null) {
        regionItem = new PlacementInfo.PlacementRegion();
        regionItem.uuid = currentRegion.uuid;
        regionItem.name = currentRegion.name;
        regionItem.code = currentRegion.code;
        regionItem.azList = new ArrayList<>();
        cloudItem.regionList.add(regionItem);
        placementRegionMap.put(currentRegion.uuid, regionItem);
      }

      // Get existing PlacementInfo AZ or set up a new one.
      PlacementInfo.PlacementAZ azItem = new PlacementInfo.PlacementAZ();
      azItem.name = currentAz.name;
      azItem.subnet = currentAz.subnet;
      azItem.replicationFactor = 1;
      azItem.uuid = currentAz.uuid;
      azItem.numNodesInAZ = azToNumNodesMap.get(azUUID);
      regionItem.azList.add(azItem);
    }
    PlacementInfo placementInfo = new PlacementInfo();
    placementInfo.cloudList = ImmutableList.copyOf(placementCloudMap.values());
    return placementInfo;
  }

  private static boolean areConfigObjectsEqual(
      ArrayNode nodeDetailSet, Map<UUID, Integer> azToNodeMap) {
    for (JsonNode nodeDetail : nodeDetailSet) {
      UUID azUUID = UUID.fromString(nodeDetail.get("azUuid").asText());
      azToNodeMap.put(azUUID, azToNodeMap.getOrDefault(azUUID, 0) - 1);
    }
    return !azToNodeMap.values().removeIf(nodeDifference -> nodeDifference != 0);
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(customer);
    user = ModelFactory.testUser(customer);
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put("name", "some config name")
            .put("base_url", "some_base_url")
            .put("api_key", "some_api_token");
    kmsConfig = ModelFactory.createKMSConfig(customer.uuid, "SMARTKEY", kmsConfigReq);
    authToken = user.createAuthToken();

    when(mockAppConfig.getString("yb.storage.path")).thenReturn("/tmp");
    when(mockRuntimeConfig.getString("yb.storage.path")).thenReturn("/tmp");
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/certs"));
  }

  @Test
  public void testEmptyUniverseListWithValidUUID() {
    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/universes", authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseListWithValidUUID() {
    Universe u = createUniverse(customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();

    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/universes", authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertNotNull(json);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    assertValue(json.get(0), "universeUUID", u.universeUUID.toString());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseBackupFlagSuccess() {
    Universe u = createUniverse(customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
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
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/update_backup_state";
    Result result = doRequestWithAuthToken("PUT", url, authToken);
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseGetWithValidUniverseUUID() {
    UserIntent ui = getDefaultUserIntent(customer);
    UUID uUUID = createUniverse(customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater(ui));

    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID;
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    JsonNode primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    JsonNode userIntentJson = primaryClusterJson.get("userIntent");
    assertNotNull(userIntentJson);
    assertThat(userIntentJson.get("replicationFactor").asInt(), allOf(notNullValue(), equalTo(3)));

    JsonNode nodeDetailsMap = universeDetails.get("nodeDetailsSet");
    assertNotNull(nodeDetailsMap);
    assertNotNull(json.get("resources"));
    for (Iterator<JsonNode> it = nodeDetailsMap.elements(); it.hasNext(); ) {
      JsonNode node = it.next();
      int nodeIdx = node.get("nodeIdx").asInt();
      assertValue(node, "nodeName", "host-n" + nodeIdx);
    }
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGetMasterLeaderWithValidParams() {
    Universe universe = createUniverse(customer.getCustomerId());
    String url =
        "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID + "/leader";
    String host = "1.2.3.4";
    HostAndPort hostAndPort = HostAndPort.fromParts(host, 9000);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(hostAndPort);
    when(mockService.getClient(any(), any())).thenReturn(mockClient);
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "privateIP", host);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithInvalidParams() {
    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject()));
    assertBadRequest(result, "clusters: This field is required");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithInvalidUniverseName() {
    String url = "/api/customers/" + customer.uuid + "/universes";
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
  public void testUniverseCreateWithoutAvailabilityZone() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "SingleUserUniverse")
            .put("instanceType", "a-instance")
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.put("currentClusterType", "PRIMARY");
    bodyJson.put("clusterOperation", "CREATE");

    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertInternalServerError(result, "No AZ found across regions: [" + r.uuid + "]");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithSingleAvailabilityZones() {
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
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.aws));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
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
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.aws));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
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
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.aws));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    ObjectNode bodyJson = Json.newObject().put("nodePrefix", "demo-node");
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());
    bodyJson.put("rootAndClientRootCASame", rootAndClientRootCASame);

    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    ArgumentCaptor<UniverseTaskParams> taskParams =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
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
    Provider p = ModelFactory.kubernetesProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    az1.updateConfig(ImmutableMap.of("KUBENAMESPACE", "test-ns1"));
    InstanceType i =
        InstanceType.upsert(p.uuid, "small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ModelFactory.createUniverse("K8sUniverse1", customer.getCustomerId(), CloudType.kubernetes);

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
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.kubernetes));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(
        result,
        "Only one universe can be created with providers having "
            + "KUBENAMESPACE set in the AZ config.");
  }

  @Test
  public void testUniverseUpdateWithInvalidParams() {
    Universe u = createUniverse(customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("PUT", url, authToken, Json.newObject()));
    assertBadRequest(result, "clusters: This field is required");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseUpdateWithValidParams() {
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
    Universe u = createUniverse(customer.getCustomerId());
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.name)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    assertNotNull(json.get("universeDetails"));

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Update)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithInvalidTServerJson() {
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
    Universe u = createUniverse(customer.getCustomerId());
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("masterGFlags", "abcd")
            .put("universeName", u.name)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.uuid.toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    assertNotNull(json.get("universeDetails"));
    assertTrue(json.get("universeConfig").asText().isEmpty());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Update)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseExpand() {
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
    Universe u = createUniverse(customer.getCustomerId());
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.name)
            .put("numNodes", 5)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.uuid.toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    JsonNode primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    assertNotNull(primaryClusterJson.get("userIntent"));
    assertAuditEntry(1, customer.uuid);

    fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    // Try universe expand only, and re-check.
    userIntentJson.put("numNodes", 9);
    result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    assertNotNull(primaryClusterJson.get("userIntent"));
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testUniverseDestroyValidUUID() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UserIntent userIntent = new UserIntent();
          userIntent.providerType = CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Delete)));

    assertTrue(customer.getUniverseUUIDs().isEmpty());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseDestroyValidUUIDIsForceDelete() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    UUID randUUID = UUID.randomUUID();
    CustomerTask.create(
        customer,
        u.universeUUID,
        randUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        "test");

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UserIntent userIntent = new UserIntent();
          userIntent.providerType = CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);

    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "?isForceDelete=true";
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Delete)));
    assertNotNull(CustomerTask.findByTaskUUID(randUUID).getCompletionTime());

    assertTrue(customer.getUniverseUUIDs().isEmpty());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  // @formatter:off
  @Parameters({
    "true, true",
    "false, true",
    "true, false",
    "false, false",
    "null, true",
  })
  // @formatter:on
  public void testUniverseDestroyValidUUIDIsForceDeleteAndDeleteBackup(
      Boolean isDeleteBackups, Boolean isForceDelete) {
    UUID fakeTaskUUID = UUID.randomUUID();
    String url;
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UserIntent userIntent = new UserIntent();
          userIntent.providerType = CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);

    Backup b = ModelFactory.createBackup(customer.uuid, u.universeUUID, s3StorageConfig.configUUID);
    b.transitionState(Backup.BackupState.Completed);
    if (isDeleteBackups == null) {
      url =
          "/api/customers/"
              + customer.uuid
              + "/universes/"
              + u.universeUUID
              + "?isForceDelete="
              + isForceDelete;
    } else {
      url =
          "/api/customers/"
              + customer.uuid
              + "/universes/"
              + u.universeUUID
              + "?isForceDelete="
              + isForceDelete
              + "&isDeleteBackups="
              + isDeleteBackups;
    }
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertAuditEntry(1, customer.uuid);
  }

  private ObjectNode setupVMImageUpgradeParams(UUID uUUID, Provider p, String instanceType) {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    ObjectNode bodyJson = getValidPayload(uUUID, "Rolling");
    bodyJson.put("taskType", UpgradeUniverse.UpgradeTaskType.VMImage.toString());
    InstanceType i =
        InstanceType.upsert(p.uuid, instanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());
    ObjectNode userIntentJson =
        Json.newObject()
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("providerType", p.code)
            .put("provider", p.uuid.toString());
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    return bodyJson;
  }

  @Test
  public void testVMImageUpgradeWithUnsupportedProvider() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    UUID fakeTaskUUID = UUID.randomUUID();
    UUID uUUID = createUniverse(customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    Provider p = ModelFactory.onpremProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(uUUID, p, "type.small");

    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "VM image upgrade is only supported for AWS / GCP");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testVMImageUpgradeWithEphemerals() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    UUID fakeTaskUUID = UUID.randomUUID();
    UUID uUUID = createUniverse(customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    Provider p = ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(uUUID, p, "i3.xlarge");

    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Cannot upgrade a universe with ephemeral storage");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testVMImageUpgradeWithNoImage() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    UUID fakeTaskUUID = UUID.randomUUID();
    UUID uUUID = createUniverse(customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    Provider p = ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(uUUID, p, "c5.xlarge");

    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "machineImages param is required for taskType: VMImage");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testVMImageUpgradeValidParams() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Universe u = createUniverse(customer.getCustomerId());
    Provider p = ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(u.universeUUID, p, "c5.xlarge");
    ObjectNode images = Json.newObject();
    UUID r = UUID.randomUUID();
    images.put(r.toString(), "image-" + r.toString());
    bodyJson.set("machineImages", images);
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeVMImage)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseUpgradeWithEmptyParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    UUID uUUID = createUniverse(customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject()));
    assertBadRequest(result, "clusters: This field is required");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  private ObjectNode getValidPayload(UUID univUUID, String upgradeOption) {
    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", univUUID.toString())
            .put("taskType", "Software")
            .put("upgradeOption", upgradeOption)
            .put("ybSoftwareVersion", "0.0.1");
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "Single UserUniverse")
            .put("ybSoftwareVersion", "0.0.1");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    return bodyJson;
  }

  // Change the node state to removed, for one of the nodes in the given universe uuid.
  private void setInTransitNode(UUID universeUUID) {
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          NodeDetails node = universeDetails.nodeDetailsSet.iterator().next();
          node.state = NodeState.Removed;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(universeUUID, updater);
  }

  private void testUniverseUpgradeWithNodesInTransitHelper(String upgradeOption) {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    UUID uUUID = createUniverse(customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    setInTransitNode(uUUID);

    ObjectNode bodyJson = getValidPayload(uUUID, upgradeOption);
    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID + "/upgrade";
    if (upgradeOption.equals("Rolling")) {
      Result result =
          assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      assertBadRequest(result, "as it has nodes in one of");
      assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
      assertAuditEntry(0, customer.uuid);
    } else {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      assertOk(result);
      JsonNode json = Json.parse(contentAsString(result));
      assertValue(json, "taskUUID", fakeTaskUUID.toString());
      assertAuditEntry(1, customer.uuid);
    }
  }

  @Test
  public void testUniverseUpgradeWithNodesInTransit() {
    testUniverseUpgradeWithNodesInTransitHelper("Rolling");
  }

  @Test
  public void testUniverseUpgradeWithNodesInTransitNonRolling() {
    testUniverseUpgradeWithNodesInTransitHelper("Non-Rolling");
  }

  @Test
  public void testUniverseUpgradeWithNodesInTransitNonRestart() {
    testUniverseUpgradeWithNodesInTransitHelper("Non-Restart");
  }

  @Test
  public void testUniverseExpandWithTransitNodes() {
    UUID fakeTaskUUID = UUID.randomUUID();

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = createUniverse(customer.getCustomerId());
    Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    setInTransitNode(u.universeUUID);

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.name)
            .put("numNodes", 5)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, "as it has nodes in one of");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseSoftwareUpgradeValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson = getValidPayload(u.universeUUID, "Rolling");
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeSoftware)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseRollingRestartValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.universeUUID.toString())
            .put("taskType", "Restart")
            .put("upgradeOption", "Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Restart)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseRollingRestartNonRolling() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.universeUUID.toString())
            .put("taskType", "Restart")
            .put("upgradeOption", "Non-Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Rolling restart has to be a ROLLING UPGRADE.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseSoftwareUpgradeWithInvalidParams() {
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject().put("universeUUID", u.universeUUID.toString()).put("taskType", "Software");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "ybSoftwareVersion param is required for taskType: Software");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseGFlagsUpgradeValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject().put("universeUUID", u.universeUUID.toString()).put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \"master-flag\", \"value\": \"123\"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \"tserver-flag\", \"value\": \"456\"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeGflags)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseGFlagsUpgradeWithTrimParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject().put("universeUUID", u.universeUUID.toString()).put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \" master-flag \", \"value\": \" 123 \"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \" tserver-flag \", \"value\": \" 456 \"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeGflags)));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseTrimFlags() {
    Map<String, String> data = new HashMap<>();
    data.put(" Test ", " One ");
    data.put(" Test 2 ", " Two ");

    Map<String, String> result = UniverseCRUDHandler.trimFlags(data);
    assertEquals(result.size(), 2);
    assertEquals(result.get("Test"), "One");
    assertEquals(result.get("Test 2"), "Two");
  }

  @Test
  public void testUniverseGFlagsUpgradeWithInvalidParams() {
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject().put("universeUUID", u.universeUUID.toString()).put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Test Universe");
    userIntentJson.set("masterGFlags", Json.parse("[\"gflag1\", \"123\"]"));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseGFlagsUpgradeWithSameGFlags() {
    Universe u = createUniverse(customer.getCustomerId());

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = ImmutableMap.of("master-flag", "123");
          userIntent.tserverGFlags = ImmutableMap.of("tserver-flag", "456");
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(u.universeUUID, updater);

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.universeUUID.toString())
            .put("taskType", "GFlags")
            .put("upgradeOption", "Non-Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", u.name);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \"master-flag\", \"value\": \"123\"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \"tserver-flag\", \"value\": \"456\"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMissingGflags() {
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJsonMissingGFlags =
        Json.newObject().put("universeUUID", u.universeUUID.toString()).put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJsonMissingGFlags.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result =
        assertYWSE(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJsonMissingGFlags));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMalformedTServerFlags() {
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject().put("universeUUID", u.universeUUID.toString()).put("taskType", "GFlags");
    ObjectNode userIntentJson =
        Json.newObject().put("universeName", "Single UserUniverse").put("tserverGFlags", "abcd");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMalformedMasterGFlags() {
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject().put("universeUUID", u.universeUUID.toString()).put("taskType", "GFlags");
    ObjectNode userIntentJson =
        Json.newObject().put("universeName", "Single UserUniverse").put("masterGFlags", "abcd");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseNonRollingGFlagsUpgrade() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.universeUUID.toString())
            .put("taskType", "GFlags")
            .put("upgradeOption", "Non-Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \"master-flag\", \"value\": \"123\"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \"tserver-flag\", \"value\": \"456\"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    ArgumentCaptor<UniverseTaskParams> taskParams =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), taskParams.capture());
    UpgradeParams taskParam = (UpgradeParams) taskParams.getValue();
    assertEquals(taskParam.upgradeOption, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    assertEquals(taskParam.masterGFlags, ImmutableMap.of("master-flag", "123"));
    assertEquals(taskParam.tserverGFlags, ImmutableMap.of("tserver-flag", "456"));
    UserIntent primaryClusterIntent = taskParam.getPrimaryCluster().userIntent;
    assertEquals(primaryClusterIntent.masterGFlags, taskParam.masterGFlags);
    assertEquals(primaryClusterIntent.tserverGFlags, taskParam.tserverGFlags);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseNonRollingSoftwareUpgrade() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.universeUUID.toString())
            .put("taskType", "Software")
            .put("upgradeOption", "Non-Rolling")
            .put("ybSoftwareVersion", "new-version");
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "Single UserUniverse")
            .put("ybSoftwareVersion", "new-version");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    ArgumentCaptor<UniverseTaskParams> taskParams =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), taskParams.capture());
    UpgradeParams taskParam = (UpgradeParams) taskParams.getValue();
    assertEquals(taskParam.upgradeOption, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    assertEquals("new-version", taskParam.ybSoftwareVersion);
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseStatusSuccess() {
    JsonNode fakeReturn = Json.newObject().set(UNIVERSE_ALIVE_METRIC, Json.newObject());
    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(fakeReturn);
    Universe u = createUniverse("TestUniverse", customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/status";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testUniverseStatusError() {
    ObjectNode fakeReturn = Json.newObject().put("error", "foobar");
    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(fakeReturn);

    Universe u = createUniverse(customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/status";
    Result result = assertYWSE(() -> doRequestWithAuthToken("GET", url, authToken));
    // TODO(API) - Should this be an http error and that too bad request?
    assertBadRequest(result, "foobar");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testFindWithUniverseNameExists() {
    Universe u = createUniverse("TestUniverse", customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/find?name=" + u.name;
    Result result = doRequestWithAuthToken("GET", url, authToken);

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 1);
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
  public void testFindWithUniverseDoesNotExist() {
    createUniverse(customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/find?name=FakeUniverse";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomConfigureCreateWithMultiAZMultiRegion() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(getTestUserIntent(r, p, i, 5), null);
    taskParams.clusterOperation = CREATE;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getCustomerId(), taskParams.getPrimaryCluster().uuid);
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    // Needed for the universe_resources call.
    DeviceInfo di = new DeviceInfo();
    di.volumeSize = 100;
    di.numVolumes = 2;
    primaryCluster.userIntent.deviceInfo = di;

    List<PlacementInfo.PlacementAZ> azList =
        primaryCluster.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 3);

    PlacementInfo.PlacementAZ paz = azList.get(0);
    paz.numNodesInAZ += 2;
    primaryCluster.userIntent.numNodes += 2;
    final String url = "/api/customers/" + customer.uuid + "/universe_configure";

    ObjectNode noCurClusterTypeJson = (ObjectNode) Json.toJson(taskParams);
    noCurClusterTypeJson.put("clusterOperation", "CREATE");
    noCurClusterTypeJson.put("currentClusterType", "");

    Result result =
        assertYWSE(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, noCurClusterTypeJson));
    assertBadRequest(result, "currentClusterType");

    ObjectNode noCurClusterOp = (ObjectNode) Json.toJson(taskParams);
    noCurClusterOp.remove("clusterOperation");

    result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, noCurClusterOp));
    assertBadRequest(result, "clusterOperation");

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    topJson.put("currentClusterType", "PRIMARY");
    topJson.put("clusterOperation", "CREATE");

    result = doRequestWithAuthTokenAndBody("POST", url, authToken, topJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(7, nodeDetailJson.size());
    // Now test the resource endpoint also works.
    // TODO: put this in its own test once we refactor the provider+region+az creation and payload
    // generation...
    result =
        doRequestWithAuthTokenAndBody(
            "POST", "/api/customers/" + customer.uuid + "/universe_resources", authToken, topJson);
    assertOk(result);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testConfigureCreateWithReadOnlyClusters() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");

    Region rReadOnly = Region.create(p, "region-readOnly-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(rReadOnly, "az-readOnly-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(rReadOnly, "az-readOnly-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univWithReadOnlyCreate";
    UUID readOnlyUuid0 = UUID.randomUUID();
    UUID readOnlyUuid1 = UUID.randomUUID();
    taskParams.upsertPrimaryCluster(getTestUserIntent(r, p, i, 5), null);
    taskParams.upsertCluster(getTestUserIntent(rReadOnly, p, i, 5), null, readOnlyUuid0);
    taskParams.upsertCluster(getTestUserIntent(rReadOnly, p, i, 5), null, readOnlyUuid1);

    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getCustomerId(), taskParams.getPrimaryCluster().uuid, CREATE);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getCustomerId(), readOnlyUuid0, CREATE);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getCustomerId(), readOnlyUuid1, CREATE);

    Cluster primaryCluster = taskParams.getPrimaryCluster();
    List<PlacementInfo.PlacementAZ> azList =
        primaryCluster.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 2);

    Cluster readOnlyCluster0 = taskParams.getClusterByUuid(readOnlyUuid0);
    azList = readOnlyCluster0.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 2);

    Cluster readOnlyCluster1 = taskParams.getClusterByUuid(readOnlyUuid1);
    azList = readOnlyCluster1.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 2);

    Map<UUID, Integer> azUUIDToNumNodeMap = getAzUuidToNumNodes(primaryCluster.placementInfo);
    Map<UUID, Integer> azUUIDToNumNodeMapReadOnly0 =
        getAzUuidToNumNodes(readOnlyCluster0.placementInfo);
    Map<UUID, Integer> azUUIDToNumNodeMapReadOnly1 =
        getAzUuidToNumNodes(readOnlyCluster1.placementInfo);
    for (Map.Entry<UUID, Integer> entry : azUUIDToNumNodeMapReadOnly0.entrySet()) {
      UUID uuid = entry.getKey();
      int numNodes = entry.getValue();
      if (azUUIDToNumNodeMap.containsKey(uuid)) {
        int prevNumNodes = azUUIDToNumNodeMap.get(uuid);
        azUUIDToNumNodeMap.put(uuid, prevNumNodes + numNodes);
      } else {
        azUUIDToNumNodeMap.put(uuid, numNodes);
      }
    }
    for (Map.Entry<UUID, Integer> entry : azUUIDToNumNodeMapReadOnly1.entrySet()) {
      UUID uuid = entry.getKey();
      int numNodes = entry.getValue();
      if (azUUIDToNumNodeMap.containsKey(uuid)) {
        int prevNumNodes = azUUIDToNumNodeMap.get(uuid);
        azUUIDToNumNodeMap.put(uuid, prevNumNodes + numNodes);
      } else {
        azUUIDToNumNodeMap.put(uuid, numNodes);
      }
    }
    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    topJson.put("currentClusterType", "ASYNC");
    topJson.put("clusterOperation", "CREATE");
    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, topJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(15, nodeDetailJson.size());
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUUIDToNumNodeMap));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomConfigureEditWithPureExpand() {
    Provider p = ModelFactory.awsProvider(customer);
    Universe u = createUniverse(customer.getCustomerId());

    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(p.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    utd.universeUUID = u.universeUUID;
    UserIntent ui = getTestUserIntent(r, p, i, 5);
    ui.universeName = u.name;
    ui.ybSoftwareVersion = "1.0";
    ui.preferredRegion = ui.regionList.get(0);
    utd.upsertPrimaryCluster(ui, null);
    PlacementInfoUtil.updateUniverseDefinition(
        utd,
        customer.getCustomerId(),
        utd.getPrimaryCluster().uuid,
        UniverseConfigureTaskParams.ClusterOperationType.CREATE);
    Universe.UniverseUpdater updater = universe -> universe.setUniverseDetails(utd);
    Universe.saveDetails(u.universeUUID, updater);
    u = Universe.getOrBadRequest(u.universeUUID);
    int totalNumNodesAfterExpand = 0;
    Map<UUID, Integer> azUuidToNumNodes =
        getAzUuidToNumNodes(u.getUniverseDetails().nodeDetailsSet);
    for (Map.Entry<UUID, Integer> entry : azUuidToNumNodes.entrySet()) {
      totalNumNodesAfterExpand += entry.getValue() + 1;
      azUuidToNumNodes.put(entry.getKey(), entry.getValue() + 1);
    }
    UniverseDefinitionTaskParams editTestUTD = u.getUniverseDetails();
    Cluster primaryCluster = editTestUTD.getPrimaryCluster();
    primaryCluster.userIntent.numNodes = totalNumNodesAfterExpand;
    primaryCluster.placementInfo = constructPlacementInfoObject(azUuidToNumNodes);

    ObjectNode editJson = (ObjectNode) Json.toJson(editTestUTD);
    editJson.put("currentClusterType", "PRIMARY");
    editJson.put("clusterOperation", "EDIT");
    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, editJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(nodeDetailJson.size(), totalNumNodesAfterExpand);
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUuidToNumNodes));
    assertAuditEntry(0, customer.uuid);
  }

  public UniverseDefinitionTaskParams setupOnPremTestData(
      int numNodesToBeConfigured, Provider p, Region r, List<AvailabilityZone> azList) {
    int numAZsToBeConfigured = azList.size();
    InstanceType i =
        InstanceType.upsert(p.uuid, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    for (int k = 0; k < numNodesToBeConfigured; ++k) {
      NodeInstanceFormData.NodeInstanceData details = new NodeInstanceFormData.NodeInstanceData();
      details.ip = "10.255.67." + k;
      details.region = r.code;

      if (numAZsToBeConfigured == 2) {
        if (k % 2 == 0) {
          details.zone = azList.get(0).code;
        } else {
          details.zone = azList.get(1).code;
        }
      } else {
        details.zone = azList.get(0).code;
      }
      details.instanceType = "type.small";
      details.nodeName = "test_name" + k;

      if (numAZsToBeConfigured == 2) {
        if (k % 2 == 0) {
          NodeInstance.create(azList.get(0).uuid, details);
        } else {
          NodeInstance.create(azList.get(0).uuid, details);
        }
      } else {
        NodeInstance.create(azList.get(0).uuid, details);
      }
    }

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getTestUserIntent(r, p, i, 3);
    userIntent.providerType = CloudType.onprem;
    userIntent.instanceType = "type.small";
    taskParams.nodeDetailsSet = new HashSet<>();

    taskParams.upsertPrimaryCluster(userIntent, null);

    return taskParams;
  }

  @Test
  public void testOnPremConfigureCreateWithValidAZInstanceTypeComboNotEnoughNodes() {
    Provider p = ModelFactory.newProvider(customer, CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(p.uuid, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getTestUserIntent(r, p, i, 5);
    userIntent.providerType = CloudType.onprem;
    taskParams.upsertPrimaryCluster(userIntent, null);

    taskParams.nodeDetailsSet = new HashSet<>();

    for (int k = 0; k < 4; ++k) {
      NodeInstanceFormData.NodeInstanceData details = new NodeInstanceFormData.NodeInstanceData();
      details.ip = "10.255.67." + i;
      details.region = r.code;
      details.zone = az1.code;
      details.instanceType = "test_instance_type";
      details.nodeName = "test_name";
      NodeInstance.create(az1.uuid, details);
    }

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    topJson.put("currentClusterType", "PRIMARY");
    topJson.put("clusterOperation", "CREATE");
    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, topJson));

    assertBadRequest(result, "Invalid Node/AZ combination for given instance type type.small");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testOnPremConfigureCreateInvalidAZNodeComboNonEmptyNodeDetailsSet() {
    Provider p = ModelFactory.newProvider(customer, CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    List<AvailabilityZone> azList = new ArrayList<>();
    azList.add(az1);
    azList.add(az2);

    InstanceType i =
        InstanceType.upsert(p.uuid, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = setupOnPremTestData(6, p, r, azList);

    UserIntent userIntent = getTestUserIntent(r, p, i, 5);
    userIntent.providerType = CloudType.onprem;
    userIntent.instanceType = "type.small";
    taskParams.upsertPrimaryCluster(userIntent, null);
    taskParams.nodeDetailsSet = new HashSet<>();
    Cluster primaryCluster = taskParams.getPrimaryCluster();

    updateUniverseDefinition(taskParams, customer.getCustomerId(), primaryCluster.uuid, CREATE);

    // Set placement info with number of nodes valid but
    for (int k = 0; k < 5; k++) {
      NodeDetails nd = new NodeDetails();
      nd.state = NodeDetails.NodeState.ToBeAdded;
      nd.azUuid = az1.uuid;
      nd.placementUuid = primaryCluster.uuid;
      taskParams.nodeDetailsSet.add(nd);
    }

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    topJson.put("currentClusterType", "PRIMARY");
    topJson.put("clusterOperation", "CREATE");

    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, topJson));
    assertBadRequest(result, "Invalid Node/AZ combination for given instance type type.small");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testOnPremConfigureValidAZNodeComboNonEmptyNodeDetailsSet() {
    Provider p = ModelFactory.newProvider(customer, CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");

    List<AvailabilityZone> azList = new ArrayList<>();
    azList.add(az1);

    UniverseDefinitionTaskParams taskParams = setupOnPremTestData(6, p, r, azList);

    Cluster primaryCluster = taskParams.getPrimaryCluster();

    updateUniverseDefinition(taskParams, customer.getCustomerId(), primaryCluster.uuid, CREATE);

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    topJson.put("clusterOperation", "CREATE");
    topJson.put("currentClusterType", "PRIMARY");
    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, topJson);
    assertOk(result);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testConfigureEditOnPremInvalidNodeAZCombo() {
    Provider p = ModelFactory.newProvider(customer, CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");

    List<AvailabilityZone> azList = new ArrayList<>();
    azList.add(az1);
    azList.add(az2);

    UniverseDefinitionTaskParams taskParams = setupOnPremTestData(6, p, r, azList);

    InstanceType i =
        InstanceType.upsert(p.uuid, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    taskParams.nodePrefix = "test_uni";
    UserIntent userIntent = getTestUserIntent(r, p, i, 5);
    userIntent.providerType = CloudType.onprem;
    userIntent.instanceType = "type.small";
    taskParams.upsertPrimaryCluster(userIntent, null);
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    updateUniverseDefinition(taskParams, customer.getCustomerId(), primaryCluster.uuid, CREATE);

    // Set the nodes state to inUse
    int k = 0;
    for (NodeInstance ni : NodeInstance.listByProvider(p.uuid)) {
      if (k < 5) {
        k++;
        ni.inUse = true;
        ni.save();
      } else {
        break;
      }
    }

    // Simulate a running universe by setting existing nodes to Live state.
    for (NodeDetails nd : taskParams.nodeDetailsSet) {
      nd.state = NodeDetails.NodeState.Live;
    }

    // Set placement info with addition of nodes that is more than what has been configured
    for (int m = 0; m < 7; m++) {
      NodeDetails nd = new NodeDetails();
      nd.state = NodeDetails.NodeState.ToBeAdded;
      nd.azUuid = az1.uuid;
      nd.placementUuid = primaryCluster.uuid;
      taskParams.nodeDetailsSet.add(nd);
    }

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    topJson.put("clusterOperation", "EDIT");
    topJson.put("currentClusterType", "PRIMARY");
    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, topJson));
    assertBadRequest(result, "Invalid Node/AZ combination for given instance type type.small");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateUniverseEncryptionAtRestNoKMSConfig() {
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
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.aws));
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

    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
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
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), argCaptor.capture());

    // The KMS provider service should not begin to make any requests since there is no KMS config
    verify(mockApiHelper, times(0)).postRequest(any(String.class), any(JsonNode.class), anyMap());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateUniverseEncryptionAtRestWithKMSConfigExists() {
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
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.aws));
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
    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);

    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), argCaptor.capture());
    assertAuditEntry(1, customer.uuid);
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
            .put("accessKeyCode", accessKeyCode);

    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    userIntentJson.set("regionList", regionList);
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.aws));
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
  public void testRunQueryWithInvalidUniverse() throws Exception {
    // Setting platform type as correct.
    when(mockAppConfig.getString("yb.mode", "PLATFORM")).thenReturn("OSS");
    // Setting insecure mode.
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));

    Customer c2 = ModelFactory.testCustomer("tc2", "Test Customer 2");
    Universe u = createUniverse(c2.getCustomerId());
    ObjectNode bodyJson = Json.newObject();
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/run_query";
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", url)
            .header("X-AUTH-TOKEN", authToken)
            .bodyJson(bodyJson)
            .header("Origin", "https://" + UniverseYbDbAdminHandler.LEARN_DOMAIN_NAME);
    Result result = routeWithYWErrHandler(request, app);
    assertBadRequest(
        result,
        String.format(
            "Universe UUID: %s doesn't belong to Customer UUID: %s",
            u.universeUUID, customer.uuid));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  // @formatter:off
  @Parameters({
    // cloud customer, normal username
    "true,  foo, foo, baz, baz, true, true,",
    // not cloud customer
    "false, foo, foo, baz, baz, false, false, Invalid Customer type",
    // cloud customer, double quotes in username
    "true,  foo, foo, ba\"z, baz, false, false, Invalid username",
    // cloud customer, username surrounded by double quotes
    "true,  foo, foo, \"baz\", baz, true, true,",
    // cloud customer, username surrounded by double quotes + double quotes inside
    "true,  foo, foo, \"ba\"z\", baz, false, false, Invalid username",
    // cloud customer, backslash in username
    "true,  foo, foo, ba\\z, baz, true, true,",
    // cloud customer, only YSQL user
    "true, foo,, baz, baz, true, false,",
    // cloud customer, only YCQL user
    "true,, foo, baz, baz, false, true,",
    // cloud customer, neither YSQL nor YCQL user
    "true,,, baz, baz, false, false, Need to provide YSQL and/or YCQL username.",
  })
  // @formatter:on
  public void testCreateUserInDB(
      boolean isCloudCustomer,
      String ysqlAdminUsername,
      String ycqlAdminUsername,
      String username,
      String password,
      boolean ysqlProcessed,
      boolean ycqlProcessed,
      String responseError) {
    Universe u = createUniverse(customer.getCustomerId());
    if (isCloudCustomer) {
      customer.code = "cloud";
    }
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    ObjectNode bodyJson =
        Json.newObject()
            .put("ycqlAdminUsername", ycqlAdminUsername)
            .put("ysqlAdminUsername", ysqlAdminUsername)
            .put("ycqlAdminPassword", "bar")
            .put("ysqlAdminPassword", "bar")
            .put("dbName", "test")
            .put("username", username)
            .put("password", password);
    String url =
        "/api/customers/"
            + customer.uuid
            + "/universes/"
            + u.universeUUID
            + "/create_db_credentials";
    if (ycqlProcessed || ysqlProcessed) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      Mockito.verify(mockYcqlQueryExecutor, times(ycqlProcessed ? 1 : 0)).createUser(any(), any());
      Mockito.verify(mockYsqlQueryExecutor, times(ysqlProcessed ? 1 : 0)).createUser(any(), any());
      assertOk(result);
    } else {
      Result result =
          assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
      assertErrorResponse(result, responseError);
    }
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  // @formatter:off
  @Parameters({
    // cloud customer, normal username
    "true,  baz, baz, baz, baz, true, true,",
    // not cloud customer
    "false, baz, baz, baz, baz, false, false, Invalid Customer type",
    // cloud customer, double quotes in username
    "true,  ba\"z, baz, baz, baz, false, false, Invalid username",
    // cloud customer, usernames surrounded by double quotes
    "true,  \"baz\", baz, \"baz\", baz, true, true,",
    // cloud customer, double quotes in username which surrounded by double quotes
    "true,  \"ba\"z\", baz, baz, baz, false, false, Invalid username",
    // cloud customer, backslash in username
    "true,  ba\\z, baz, baz, baz, true, true,",
    // cloud customer, only YSQL user
    "true,  baz, baz,,, true, false,",
    // cloud customer, only YSQL user, YCQL user is set as ""
    "true,  baz, baz, \"\", baz, true, false,",
    // cloud customer, only YCQL user
    "true,,, baz, baz, false, true,",
    // cloud customer, only YCQL user, YSQL user is set as ""
    "true, \"\", baz, baz, baz, false, true,",
    // cloud customer, neither YSQL nor YCQL user
    "true,,,,, false, false, Need to provide YSQL and/or YCQL username.",
  })
  // @formatter:on
  public void testSetDatabaseCredentials(
      boolean isCloudCustomer,
      String ysqlAdminUsername,
      String ysqlPassword,
      String ycqlAdminUsername,
      String ycqlPassword,
      boolean ysqlProcessed,
      boolean ycqlProcessed,
      String responseError) {
    Universe u = createUniverse(customer.getCustomerId());
    if (isCloudCustomer) {
      customer.code = "cloud";
    }
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    ObjectNode bodyJson =
        Json.newObject()
            .put("ycqlAdminUsername", ycqlAdminUsername)
            .put("ysqlAdminUsername", ysqlAdminUsername)
            .put("ycqlCurrAdminPassword", "foo")
            .put("ysqlCurrAdminPassword", "foo")
            .put("ycqlAdminPassword", ycqlPassword)
            .put("ysqlAdminPassword", ysqlPassword)
            .put("dbName", "test");
    String url =
        "/api/customers/"
            + customer.uuid
            + "/universes/"
            + u.universeUUID
            + "/update_db_credentials";
    if (ycqlProcessed || ysqlProcessed) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      Mockito.verify(mockYcqlQueryExecutor, times(ycqlProcessed ? 1 : 0))
          .updateAdminPassword(any(), any());
      Mockito.verify(mockYsqlQueryExecutor, times(ysqlProcessed ? 1 : 0))
          .updateAdminPassword(any(), any());
      assertOk(result);
    } else {
      Result result =
          assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      Mockito.verifyNoMoreInteractions(mockYcqlQueryExecutor, mockYsqlQueryExecutor);
      assertErrorResponse(result, responseError);
    }
    assertAuditEntry(0, customer.uuid);
  }

  private void setupDiskUpdateTest(
      int diskSize, String instanceType, PublicCloudConstants.StorageType storageType, Universe u) {

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UserIntent userIntent = new UserIntent();
          userIntent.instanceType = instanceType;
          userIntent.providerType = CloudType.aws;
          DeviceInfo di = new DeviceInfo();
          di.volumeSize = diskSize;
          di.numVolumes = 2;
          di.storageType = storageType;
          userIntent.deviceInfo = di;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);
  }

  @Test
  public void testExpandDiskSizeFailureInvalidSize() {
    Universe u = createUniverse(customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    setupDiskUpdateTest(100, "c4.xlarge", StorageType.GP2, u);
    u = Universe.getOrBadRequest(u.universeUUID);

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 50);

    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/disk_update";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Size can only be increased.");
  }

  @Test
  public void testExpandDiskSizeFailureInvalidStorage() {
    Universe u = createUniverse(customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    setupDiskUpdateTest(100, "c4.xlarge", StorageType.Scratch, u);
    u = Universe.getOrBadRequest(u.universeUUID);

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 150);

    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/disk_update";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Scratch type disk cannot be modified.");
  }

  @Test
  public void testExpandDiskSizeFailureInvalidInstance() {
    Universe u = createUniverse(customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    setupDiskUpdateTest(100, "i3.xlarge", StorageType.GP2, u);
    u = Universe.getOrBadRequest(u.universeUUID);

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 150);

    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/disk_update";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot modify instance volumes.");
  }

  @Test
  public void testExpandDiskSizeSuccess() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();
    setupDiskUpdateTest(100, "c4.xlarge", StorageType.GP2, u);
    u = Universe.getOrBadRequest(u.universeUUID);

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 150);

    String url =
        "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/disk_update";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.UpdateDiskSize), argCaptor.capture());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testUniverseCreateWithDisabledYedis() {
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
    userIntentJson.set("deviceInfo", createValidDeviceInfo(CloudType.aws));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);

    JsonNode json = Json.parse(contentAsString(result));
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
  // @formatter:off
  @Parameters({
    // not insecure, wrong origin, wrong ybmode => failure
    "false,,, false",
    // insecure, wrong origin, wrong ybmode => failure
    "true,,, false",
    // insecure, correct origin, wrong ybmode => failure
    "true, https://learn.yugabyte.com,, false",
    // insecure, correct origin, wrong ybmode => failure
    "true, https://learn.yugabyte.com, PLATFORM, false",
    // insecure, correct origin, correct ybmode => success
    "true, https://learn.yugabyte.com, OSS, true",
  })
  // @formatter:on
  public void testRunQuery_ValidPlatform(
      boolean insecure, String origin, String ybmode, boolean isGoodResult) throws Exception {
    Universe u = createUniverse(customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();

    if (insecure) {
      ConfigHelper configHelper = new ConfigHelper();
      configHelper.loadConfigToDB(
          ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));
    }
    when(mockAppConfig.getString("yb.mode", "PLATFORM")).thenReturn(ybmode == null ? "" : ybmode);

    ObjectNode bodyJson =
        Json.newObject().put("query", "select * from product limit 1").put("db_name", "demo");
    when(mockYsqlQueryExecutor.executeQuery(any(), any()))
        .thenReturn(Json.newObject().put("foo", "bar"));

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/run_query";
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", url).header("X-AUTH-TOKEN", authToken).bodyJson(bodyJson);
    if (!StringUtils.isEmpty(origin)) {
      request = request.header("Origin", origin);
    }
    Result result = routeWithYWErrHandler(request, app);

    JsonNode json = Json.parse(contentAsString(result));
    if (isGoodResult) {
      assertOk(result);
      assertEquals("bar", json.get("foo").asText());
      assertAuditEntry(1, customer.uuid);
    } else {
      assertBadRequest(result, UniverseYbDbAdminHandler.RUN_QUERY_ISNT_ALLOWED);
      assertAuditEntry(0, customer.uuid);
    }
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
          UserIntent userIntent = new UserIntent();
          userIntent.providerType = CloudType.aws;
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
          UserIntent userIntent = new UserIntent();
          userIntent.providerType = CloudType.aws;
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
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.providerType = CloudType.aws;
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

  @Test
  public void testUniverseToggleTlsWithEmptyParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = prepareUniverseForToggleTls(false, false, null);

    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/toggle_tls";
    Result result =
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject()));
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
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
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
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
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
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
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
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
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
        assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
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

  @Test
  @Parameters({
    "list universes, true, , GET",
    "get universe, false, , GET",
    "get universe leader, false, /leader, GET"
  })
  public void invalidCustomerUUID(
      String testDescription, boolean isList, String urlSuffix, String httpMethod) {
    UUID invalidCustomerUUID = UUID.randomUUID();
    String universesPath = isList ? "/universes" : "/universes/" + UUID.randomUUID();
    String url = "/api/customers/" + invalidCustomerUUID + universesPath + urlSuffix;
    Result result = doRequestWithAuthToken(httpMethod, url, authToken);
    assertEquals(url, FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.uuid);
  }

  // TODO(vineeth) Decide: Should these result in FORBIDDEN after RBAC?
  @Test
  @Parameters({
    "get universe, , GET",
    "delete universe, , DELETE",
    "get universe status, /status, GET",
    "pause universe, /pause, POST",
    "resume universe, /resume, POST",
    "get universe leader, /leader, GET",
    "setup 2dc universe, /setup_universe_2dc, PUT"
  })
  public void invalidUniverseUUID(String testDescription, String urlSuffix, String httpMethod) {
    UUID randomUUID = UUID.randomUUID();
    String url = "/api/customers/" + customer.uuid + "/universes/" + randomUUID + urlSuffix;
    Result result = assertYWSE(() -> doRequestWithAuthToken(httpMethod, url, authToken));
    assertBadRequest(result, "Cannot find universe " + randomUUID);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  @Parameters(method = "parametersToDeviceInfoValidation")
  public void testUniverseCreateDeviceInfoValidation(
      CloudType cloudType,
      String instanceType,
      StorageType storageType,
      Integer numVolumes,
      Integer volumeSize,
      Integer diskIops,
      Integer throughput,
      String mountPoints,
      String errorMessage) {
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
        p = ModelFactory.newProvider(customer, CloudType.other);
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

    String url = "/api/customers/" + customer.uuid + "/universes";
    if (errorMessage == null) {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      assertOk(result);
    } else {
      Result result =
          assertYWSE(() -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      assertBadRequest(result, errorMessage);
    }
  }

  @SuppressWarnings("unused")
  private Object[] parametersToDeviceInfoValidation() {
    return new Object[][] {
      // Success cases
      {CloudType.aws, "c3.xlarge", StorageType.GP2, 1, 100, null, null, null, null},
      {CloudType.aws, "c3.xlarge", StorageType.IO1, 1, 100, 1000, null, null, null},
      {CloudType.aws, "c3.xlarge", StorageType.GP3, 1, 100, 1000, 125, null, null},
      {CloudType.aws, "i3.2xlarge", null, 1, 100, 1000, 125, null, null},
      {CloudType.aws, "c5d.2xlarge", null, 1, 100, 1000, 125, null, null},
      {CloudType.gcp, "c3.xlarge", StorageType.Persistent, 1, 100, null, null, null, null},
      {CloudType.gcp, "c3.xlarge", StorageType.Scratch, 1, 100, null, null, null, null},
      {CloudType.azu, "c3.xlarge", StorageType.StandardSSD_LRS, 1, 100, null, null, null, null},
      {CloudType.azu, "c3.xlarge", StorageType.Premium_LRS, 1, 100, null, null, null, null},
      {CloudType.azu, "c3.xlarge", StorageType.UltraSSD_LRS, 1, 100, null, null, null, null},
      {CloudType.kubernetes, "c3.xlarge", null, 1, 100, null, null, null, null},
      {CloudType.onprem, "c3.xlarge", null, 1, 100, null, null, "/var", null},
      {CloudType.other, "c3.xlarge", null, null, null, null, null, null, null},

      //  Failure cases
      {
        CloudType.aws,
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
        CloudType.aws,
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
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP2,
        null,
        100,
        null,
        null,
        null,
        "Number of volumes field is mandatory"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP2,
        1,
        null,
        null,
        null,
        null,
        "Volume size field is mandatory"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.Persistent,
        1,
        100,
        null,
        null,
        null,
        "Cloud type aws is not compatible with storage type Persistent"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.IO1,
        1,
        100,
        null,
        null,
        null,
        "Disk IOPS is mandatory for IO1 storage"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP3,
        1,
        100,
        null,
        125,
        null,
        "Disk IOPS is mandatory for GP3 storage"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP3,
        1,
        100,
        1000,
        null,
        null,
        "Disk throughput is mandatory for GP3 storage"
      },
      {
        CloudType.aws,
        "i3.2xlarge",
        StorageType.GP2,
        1,
        100,
        null,
        null,
        null,
        "AWS instance with ephemeral storage can't have storageType set"
      },
      {
        CloudType.aws,
        "c5d.2xlarge",
        StorageType.GP2,
        1,
        100,
        null,
        null,
        null,
        "AWS instance with ephemeral storage can't have storageType set"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP2,
        1,
        -100,
        null,
        null,
        null,
        "Volume size should be positive"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP2,
        -1,
        100,
        null,
        null,
        null,
        "Number of volumes should be positive"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP3,
        1,
        100,
        -1,
        125,
        null,
        "Disk IOPS should be positive"
      },
      {
        CloudType.aws,
        "c3.xlarge",
        StorageType.GP3,
        1,
        100,
        1000,
        -1,
        null,
        "Disk throughput should be positive"
      },
      {
        CloudType.gcp,
        "c3.xlarge",
        StorageType.Persistent,
        null,
        100,
        null,
        null,
        null,
        "Number of volumes field is mandatory"
      },
      {
        CloudType.gcp,
        "c3.xlarge",
        StorageType.Scratch,
        1,
        null,
        null,
        null,
        null,
        "Volume size field is mandatory"
      },
      {
        CloudType.azu,
        "c3.xlarge",
        StorageType.StandardSSD_LRS,
        null,
        100,
        null,
        null,
        null,
        "Number of volumes field is mandatory"
      },
      {
        CloudType.azu,
        "c3.xlarge",
        StorageType.Premium_LRS,
        1,
        null,
        null,
        null,
        null,
        "Volume size field is mandatory"
      },
      {
        CloudType.kubernetes,
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
        CloudType.kubernetes,
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
        CloudType.onprem,
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
        CloudType.onprem,
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
        CloudType.onprem,
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

  private ObjectNode createValidDeviceInfo(CloudType cloudType) {
    switch (cloudType) {
      case aws:
        return createDeviceInfo(StorageType.GP2, 1, 100, null, null, null);
      case gcp:
        return createDeviceInfo(StorageType.Persistent, 1, 100, null, null, null);
      case azu:
        return createDeviceInfo(StorageType.Premium_LRS, 1, 100, null, null, null);
      case kubernetes:
        return createDeviceInfo(null, 1, 100, null, null, null);
      default:
        throw new UnsupportedOperationException();
    }
  }

  private ObjectNode createDeviceInfo(
      StorageType storageType,
      Integer numVolumes,
      Integer volumeSize,
      Integer diskIops,
      Integer throughput,
      String mountPoints) {
    ObjectNode deviceInfo = Json.newObject();
    if (storageType != null) {
      deviceInfo.put("storageType", storageType.name());
    }
    if (volumeSize != null) {
      deviceInfo.put("volumeSize", volumeSize);
    }
    if (numVolumes != null) {
      deviceInfo.put("numVolumes", numVolumes);
    }
    if (diskIops != null) {
      deviceInfo.put("diskIops", diskIops);
    }
    if (throughput != null) {
      deviceInfo.put("throughput", throughput);
    }
    if (mountPoints != null) {
      deviceInfo.put("mountPoints", mountPoints);
    }
    return deviceInfo;
  }
}
