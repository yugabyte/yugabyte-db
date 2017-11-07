// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ApiUtils.getDefaultUserIntent;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static com.yugabyte.yw.common.PlacementInfoUtil.UNIVERSE_ALIVE_METRIC;
import static com.yugabyte.yw.common.PlacementInfoUtil.getAzUuidToNumNodes;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.contentAsString;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;

import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Matchers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;

import org.yb.client.YBClient;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

public class UniverseControllerTest extends WithApplication {
  private static Commissioner mockCommissioner;
  private static MetricQueryHelper mockMetricQueryHelper;
  private Customer customer;
  private String authToken;
  private YBClientService mockService;
  private YBClient mockClient;

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockMetricQueryHelper = mock(MetricQueryHelper.class);
    mockClient = mock(YBClient.class);
    mockService = mock(YBClientService.class);
    return new GuiceApplicationBuilder()
      .configure((Map) Helpers.inMemoryDatabase())
      .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
      .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
      .build();
  }

  private PlacementInfo constructPlacementInfoObject(Map<UUID, Integer> azToNumNodesMap) {

    Map<UUID, PlacementInfo.PlacementCloud> placementCloudMap = new HashMap<>();
    Map<UUID, PlacementInfo.PlacementRegion> placementRegionMap = new HashMap<>();
    for (UUID azUUID : azToNumNodesMap.keySet()) {
      AvailabilityZone currentAz = AvailabilityZone.get(azUUID);

      // Get existing PlacementInfo Cloud or set up a new one.
      Provider currentProvider = currentAz.getProvider();
      PlacementInfo.PlacementCloud cloudItem = placementCloudMap.getOrDefault(currentProvider.uuid, null);
      if (cloudItem == null) {
        cloudItem = new PlacementInfo.PlacementCloud();
        cloudItem.uuid = currentProvider.uuid;
        cloudItem.code = currentProvider.code;
        cloudItem.regionList = new ArrayList<>();
        placementCloudMap.put(currentProvider.uuid, cloudItem);
      }

      // Get existing PlacementInfo Region or set up a new one.
      Region currentRegion = currentAz.region;
      PlacementInfo.PlacementRegion regionItem = placementRegionMap.getOrDefault(currentRegion.uuid, null);
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

  private static boolean areConfigObjectsEqual(ArrayNode nodeDetailSet, Map<UUID, Integer> azToNodeMap) {
    for (JsonNode nodeDetail : nodeDetailSet) {
      UUID azUUID = UUID.fromString(nodeDetail.get("azUuid").asText());
      azToNodeMap.put(azUUID, azToNodeMap.getOrDefault(azUUID, 0)-1);
    }
    return !azToNodeMap.values().removeIf(nodeDifference -> nodeDifference != 0);
  }

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    authToken = customer.createAuthToken();
  }

  @Test
  public void testEmptyUniverseListWithValidUUID() {
    Result result = doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/universes", authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
  }

  @Test
  public void testUniverseListWithValidUUID() {
    Universe u = Universe.create("Universe-1", UUID.randomUUID(), customer.getCustomerId());
    customer.addUniverseUUID(u.universeUUID);
    customer.save();

    Result result = doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/universes", authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertNotNull(json);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    assertValue(json.get(0), "universeUUID", u.universeUUID.toString());
  }

  @Test
  public void testUniverseListWithInvalidUUID() {
    UUID invalidUUID = UUID.randomUUID();
    Result result = doRequestWithAuthToken("GET", "/api/customers/" + invalidUUID + "/universes", authToken);
    assertBadRequest(result, "Invalid Customer UUID: " + invalidUUID);
  }

  @Test
  public void testUniverseGetWithInvalidCustomerUUID() {
    UUID invalidUUID = UUID.randomUUID();
    String url = "/api/customers/" + invalidUUID + "/universes/" + UUID.randomUUID();
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertBadRequest(result, "Invalid Customer UUID: " + invalidUUID);
  }

  @Test
  public void testUniverseGetWithInvalidUniverseUUID() {
    UUID invalidUUID = UUID.randomUUID();
    String url = "/api/customers/" + customer.uuid + "/universes/" + invalidUUID;
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertBadRequest(result, "Invalid Universe UUID: " + invalidUUID);
  }

  @Test
  public void testUniverseGetWithValidUniverseUUID() {
    UUID uUUID = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater(getDefaultUserIntent(customer)));

    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID;
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode userIntentJson = universeDetails.get("userIntent");
    assertNotNull(userIntentJson);
    assertThat(userIntentJson.get("replicationFactor").asInt(), allOf(notNullValue(), equalTo(3)));
    assertThat(userIntentJson.get("isMultiAZ").asBoolean(), allOf(notNullValue(), equalTo(true)));

    JsonNode nodeDetailsMap = universeDetails.get("nodeDetailsSet");
    assertNotNull(nodeDetailsMap);
    assertNotNull(json.get("resources"));
    Iterator<String> nodeNameIt = nodeDetailsMap.fieldNames();
    for (int i = 1; nodeNameIt.hasNext(); ++i) {
      assertEquals("host-n" + i, nodeNameIt.next());
    }
  }

  @Test
  public void testGetMasterLeaderWithValidParams() {
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    String host = "1.2.3.4";
    HostAndPort hostAndPort = HostAndPort.fromParts(host, 9000);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(hostAndPort);
    when(mockService.getClient(any(String.class))).thenReturn(mockClient);
    UniverseController universeController = new UniverseController(mockService);

    Result result = universeController.getMasterLeaderIP(customer.uuid, universe.universeUUID);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "privateIP", host);
  }

  @Test
  public void testGetMasterLeaderWithInvalidCustomerUUID() {
    UniverseController universeController = new UniverseController(mockService);
    UUID invalidUUID = UUID.randomUUID();
    Result result = universeController.getMasterLeaderIP(invalidUUID, UUID.randomUUID());
    assertBadRequest(result, "Invalid Customer UUID: " + invalidUUID);
  }

  @Test
  public void testGetMasterLeaderWithInvalidUniverseUUID() {
    UniverseController universeController = new UniverseController(mockService);
    UUID invalidUUID = UUID.randomUUID();
    Result result = universeController.getMasterLeaderIP(customer.uuid, invalidUUID);
    assertBadRequest(result, "No universe found with UUID: " + invalidUUID);
  }

  @Test
  public void testUniverseCreateWithInvalidParams() {
    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject());
    assertBadRequest(result, "userIntent: This field is required");
  }

  @Test
  public void testUniverseCreateWithoutAvailabilityZone() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject()
      .put("universeName", "Single UserUniverse")
      .put("isMultiAZ", false)
      .put("instanceType", "a-instance")
      .put("replicationFactor", 3)
      .put("numNodes", 3)
      .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    topJson.set("userIntent", bodyJson);

    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, topJson);
    assertInternalServerError(result, "No AZ found for region: " + r.uuid);
  }

  @Test
  public void testUniverseCreateWithSingleAvailabilityZones() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i = InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject()
      .put("universeName", "Single UserUniverse")
      .put("isMultiAZ", false)
      .put("instanceType", i.getInstanceTypeCode())
      .put("replicationFactor", 3)
      .put("numNodes", 3)
      .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    topJson.set("userIntent", bodyJson);

    String url = "/api/customers/" + customer.uuid + "/universes";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, topJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("universeDetails"));

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Single UserUniverse")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
  }

  @Test
  public void testUniverseUpdateWithInvalidParams() {
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, Json.newObject());
    assertBadRequest(result, "userIntent: This field is required");
  }

  @Test
  public void testUniverseUpdateWithValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    InstanceType i = InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject()
      .put("isMultiAZ", true)
      .put("universeName", u.name)
      .put("instanceType", i.getInstanceTypeCode())
      .put("replicationFactor", 3)
      .put("numNodes", 3)
      .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    topJson.set("userIntent", bodyJson);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, topJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    assertNotNull(json.get("universeDetails"));

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Update)));
  }

  @Test
  public void testUniverseCreateWithInvalidTServerJson() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    InstanceType i = InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject()
      .put("masterGFlags", "abcd")
      .put("isMultiAZ", true)
      .put("universeName", u.name)
      .put("instanceType", i.getInstanceTypeCode())
      .put("replicationFactor", 3)
      .put("numNodes", 3)
      .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    topJson.set("userIntent", bodyJson);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, topJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    assertNotNull(json.get("universeDetails"));

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Update)));
  }

  @Test
  public void testUniverseExpand() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    InstanceType i = InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject()
      .put("isMultiAZ", true)
      .put("universeName", u.name)
      .put("numNodes", 5)
      .put("instanceType", i.getInstanceTypeCode())
      .put("replicationFactor", 3)
      .put("provider", p.uuid.toString());
    ArrayNode regionList = Json.newArray().add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    topJson.set("userIntent", bodyJson);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, topJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    assertNotNull(universeDetails.get("userIntent"));

    // Try universe expand only, and re-check.
    bodyJson.put("numNodes", 9);
    result = doRequestWithAuthTokenAndBody("PUT", url, authToken, topJson);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.universeUUID.toString());
    universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    assertNotNull(universeDetails.get("userIntent"));
  }

  @Test
  public void testUniverseDestroyInvalidUUID() {
    UUID randomUUID = UUID.randomUUID();
    String url = "/api/customers/" + customer.uuid + "/universes/" + randomUUID;
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertBadRequest(result, "No universe found with UUID: " + randomUUID);
  }

  @Test
  public void testUniverseDestroyValidUUID() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails = new UniverseDefinitionTaskParams();
        universeDetails.userIntent = new UserIntent();
        universeDetails.userIntent.providerType = CloudType.aws;
        universe.setUniverseDetails(universeDetails);
      }
    };
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, updater);

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID;
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Delete)));

    assertTrue(customer.getUniverseUUIDs().isEmpty());
  }

  @Test
  public void testUniverseUpgradeWithEmptyParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    UUID uUUID = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId()).universeUUID;
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    String url = "/api/customers/" + customer.uuid + "/universes/" + uUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject());
    assertBadRequest(result, "task type is required");
    assertNull(CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique());
  }

  @Test
  public void testUniverseSoftwareUpgradeValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject()
      .put("universeUUID", u.universeUUID.toString())
      .put("taskType", "Software")
      .put("ybSoftwareVersion", "0.0.1");

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeSoftware)));
  }

  @Test
  public void testUniverseSoftwareUpgradeWithInvalidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject()
      .put("universeUUID", u.universeUUID.toString())
      .put("taskType", "Software");

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertBadRequest(result, "ybSoftwareVersion param is required for taskType: Software");
  }

  @Test
  public void testUniverseGFlagsUpgradeValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject()
      .put("universeUUID", u.universeUUID.toString())
      .put("taskType", "GFlags");

    bodyJson.set("masterGFlags", Json.parse("[{ \"name\": \"gflag1\", \"value\": \"123\"}]"));
    bodyJson.set("tserverGFlags", Json.parse("[{ \"name\": \"gflag1\", \"value\": \"123\"}]"));

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeGflags)));
  }

  @Test
  public void testUniverseGFlagsUpgradeWithInvalidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject()
      .put("universeUUID", u.universeUUID.toString())
      .put("taskType", "GFlags");
    bodyJson.set("masterGFlags", Json.parse("[\"gflag1\", \"123\"]"));
    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertBadRequest(result, "gflags param is required for taskType: GFlags");
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMissingGflags() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJsonMissingGFlags = Json.newObject()
      .put("universeUUID", u.universeUUID.toString())
      .put("taskType", "GFlags");


    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJsonMissingGFlags);

    assertBadRequest(result, "gflags param is required for taskType: GFlags");
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMalformedTServerFlags() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject()
      .put("universeUUID", u.universeUUID.toString())
      .put("taskType", "GFlags")
      .put("tserverGFlags", "abcd");

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertBadRequest(result, "gflags param is required for taskType: GFlags");
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMalformedMasterGFlags() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject()
      .put("universeUUID", u.universeUUID.toString())
      .put("taskType", "GFlags")
      .put("masterGFlags", "abcd");

    String url = "/api/customers/" + customer.uuid + "/universes/" + u.universeUUID + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertBadRequest(result, "gflags param is required for taskType: GFlags");
  }

  @Test
  public void testUniverseStatusSuccess() {
    JsonNode fakeReturn = Json.newObject().set(UNIVERSE_ALIVE_METRIC, Json.newObject());
    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(fakeReturn);
    UUID universeUUID = UUID.randomUUID();
    Universe.create("TestUniverse", universeUUID, customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/status";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
  }

  @Test
  public void testUniverseStatusError() {
    ObjectNode fakeReturn = Json.newObject().put("error", "foobar");
    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(fakeReturn);
    UUID universeUUID = UUID.randomUUID();
    Universe.create("TestUniverse", universeUUID, customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/status";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertBadRequest(result, "foobar");
  }

  @Test
  public void testUniverseStatusBadParams() {
    UUID universeUUID = UUID.randomUUID();
    String url = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/status";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertBadRequest(result, "No universe found with UUID: " + universeUUID);
  }

  @Test
  public void testFindByNameWithUniverseNameExists() {
    Universe u = Universe.create("TestUniverse", UUID.randomUUID(), customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/find/" + u.name;
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertBadRequest(result, "Universe already exists");
  }

  @Test
  public void testFindByNameWithUniverseDoesNotExist() {
    Universe.create("TestUniverse", UUID.randomUUID(), customer.getCustomerId());
    String url = "/api/customers/" + customer.uuid + "/universes/find/FakeUniverse";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
  }

  @Test
  public void testCustomConfigureCreateWithMultiAZMultiRegion() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i = InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.userIntent = getTestUserIntent(r, p, i, 5);
    taskParams.nodePrefix = "univConfCreate";
    taskParams.placementInfo = null;
    PlacementInfoUtil.updateUniverseDefinition(taskParams, customer.getCustomerId());
    List<PlacementInfo.PlacementAZ> azList = taskParams.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 2);
    PlacementInfo.PlacementAZ paz = azList.get(0);
    paz.numNodesInAZ += 2;
    taskParams.userIntent.numNodes += 2;
    Map<UUID, Integer> azUUIDToNumNodeMap = getAzUuidToNumNodes(taskParams.placementInfo);
    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);

    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, topJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(7, nodeDetailJson.size());
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUUIDToNumNodeMap));
  }

  @Test
  public void testCustomConfigureEditWithPureExpand() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class),
      Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe u = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i = InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    utd.userIntent = getTestUserIntent(r, p, i, 5);
    PlacementInfoUtil.updateUniverseDefinition(utd, customer.getCustomerId());
    u.setUniverseDetails(utd);
    u.save();
    int totalNumNodesAfterExpand = 0;
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(u.getUniverseDetails().nodeDetailsSet);
    for (Map.Entry<UUID, Integer> entry : azUuidToNumNodes.entrySet()) {
      totalNumNodesAfterExpand += entry.getValue() + 1;
      azUuidToNumNodes.put(entry.getKey(), entry.getValue() + 1);
    }
    UniverseDefinitionTaskParams editTestUTD = u.getUniverseDetails();
    editTestUTD.userIntent.numNodes = totalNumNodesAfterExpand;
    editTestUTD.placementInfo = constructPlacementInfoObject(azUuidToNumNodes);

    String url = "/api/customers/" + customer.uuid + "/universe_configure";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, Json.toJson(editTestUTD));
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(nodeDetailJson.size(), totalNumNodesAfterExpand);
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUuidToNumNodes));
  }
}
