// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.PlacementInfoUtil.UNIVERSE_ALIVE_METRIC;
import static com.yugabyte.yw.common.PlacementInfoUtil.getAzUuidToNumNodes;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
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
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
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
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

public class UniverseControllerTest extends WithApplication {
  private Customer customer;
  private static Commissioner mockCommissioner;
  private Http.Cookie validCookie;
  private static MetricQueryHelper mockMetricQueryHelper;
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

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    String authToken = customer.createAuthToken();
    validCookie = Http.Cookie.builder("authToken", authToken).build();
  }

  @Test
  public void testEmptyUniverseListWithValidUUID() {
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
  }

  @Test
  public void testUniverseListWithValidUUID() {
    Universe u1 = Universe.create("Universe-1", UUID.randomUUID(), customer.getCustomerId());
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();

    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    assertEquals(null, json.get("pricePerHour"));
    assertThat(json.get(0).get("universeUUID").asText(), allOf(notNullValue(), equalTo(u1.universeUUID.toString())));
  }

  @Test
  public void testUniverseListWithInvalidUUID() {
    UUID invalidUUID = UUID.randomUUID();
    Result result = route(fakeRequest("GET", "/api/customers/" + invalidUUID + "/universes").cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID: " + invalidUUID)));
  }

  @Test
  public void testUniverseGetWithInvalidCustomerUUID() {
    UUID invalidUUID = UUID.randomUUID();
    Result result =
      route(fakeRequest("GET", "/api/customers/" + invalidUUID + "/universes/" + UUID.randomUUID()).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID: " + invalidUUID)));
  }

  @Test
  public void testUniverseGetWithInvalidUniverseUUID() {
    UUID invalidUUID = UUID.randomUUID();
    Result result =
      route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/" + invalidUUID).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("Invalid Universe UUID: " + invalidUUID)));
  }

  @Test
  public void testUniverseGetWithValidUniverseUUID() {
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    UserIntent userIntent = getDefaultUserIntent();

    UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
    Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent));

    Result result =
      route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeDetails = json.get("universeDetails");
    assertThat(universeDetails, is(notNullValue()));
    JsonNode userIntentJson = universeDetails.get("userIntent");
    assertThat(userIntentJson, is(notNullValue()));
    assertThat(userIntentJson.get("replicationFactor").asInt(), allOf(notNullValue(), equalTo(3)));
    assertThat(userIntentJson.get("isMultiAZ").asBoolean(), allOf(notNullValue(), equalTo(true)));

    JsonNode nodeDetailsMap = universeDetails.get("nodeDetailsSet");
    assertThat(nodeDetailsMap, is(notNullValue()));
    JsonNode resources = json.get("resources");
    assertThat(resources, is(notNullValue()));
    int idx = 1;
    for (Iterator<Map.Entry<String, JsonNode>> nodeInfo = nodeDetailsMap.fields(); nodeInfo.hasNext(); ) {
      Map.Entry<String, JsonNode> node = nodeInfo.next();
      assertEquals(node.getKey(), "host-n" + idx);
      idx++;
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

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result)).get("privateIP");
    assertThat(json, notNullValue());
    assertThat(json.asText(), equalTo(host));
  }

  @Test
  public void testGetMasterLeaderWithInvalidCustomerUUID() {
    UniverseController universeController = new UniverseController(mockService);
    UUID invalidUUID = UUID.randomUUID();
    Result result = universeController.getMasterLeaderIP(invalidUUID, UUID.randomUUID());
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("Invalid Customer UUID: " + invalidUUID.toString())));
  }

  @Test
  public void testGetMasterLeaderWithInvalidUniverseUUID() {
    UniverseController universeController = new UniverseController(mockService);
    UUID invalidUUID = UUID.randomUUID();
    Result result = universeController.getMasterLeaderIP(customer.uuid, invalidUUID);
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("No universe found with UUID: " + invalidUUID.toString())));
  }

  @Test
  public void testUniverseCreateWithInvalidParams() {
    ObjectNode emptyJson = Json.newObject();
    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universes")
                      .cookie(validCookie).bodyJson(emptyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("userIntent: This field is required")));
  }

  @Test
  public void testUniverseCreateWithoutAvailabilityZone() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject();
    ArrayNode regionList = Json.newArray();
    regionList.add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    bodyJson.put("universeName", "Single UserUniverse");
    bodyJson.put("isMultiAZ", false);
    bodyJson.put("instanceType", "a-instance");
    bodyJson.put("replicationFactor", 3);
    bodyJson.put("numNodes", 3);
    bodyJson.put("provider", p.uuid.toString());
    topJson.set("userIntent", bodyJson);

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universe_configure")
                      .cookie(validCookie).bodyJson(topJson));
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("No AZ found for region: " + r.uuid)));
  }

  @Test
  public void testUniverseCreateWithSingleAvailabilityZones() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject();
    ArrayNode regionList = Json.newArray();
    regionList.add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    bodyJson.put("universeName", "Single UserUniverse");
    bodyJson.put("isMultiAZ", false);
    bodyJson.put("instanceType", i.getInstanceTypeCode());
    bodyJson.put("replicationFactor", 3);
    bodyJson.put("numNodes", 3);
    bodyJson.put("provider", p.uuid.toString());
    topJson.set("userIntent", bodyJson);

    AvailabilityZone az = AvailabilityZone.find.byId(az1.uuid);
    assertThat(az.region.name, is(notNullValue()));

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universes")
                            .cookie(validCookie).bodyJson(topJson));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("universeUUID").asText(), is(notNullValue()));
    JsonNode universeDetails = json.get("universeDetails");
    assertThat(universeDetails, is(notNullValue()));

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Single UserUniverse")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
  }

  @Test
  public void testUniverseUpdateWithInvalidParams() {
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    ObjectNode emptyJson = Json.newObject();
    Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID)
                            .cookie(validCookie).bodyJson(emptyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("userIntent: This field is required")));
  }

  @Test
  public void testUniverseUpdateWithValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    InstanceType i =
        InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject();
    ArrayNode regionList = Json.newArray();
    regionList.add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    bodyJson.put("isMultiAZ", true);
    bodyJson.put("universeName", universe.name);
    bodyJson.put("instanceType", i.getInstanceTypeCode());
    bodyJson.put("replicationFactor", 3);
    bodyJson.put("numNodes", 3);
    bodyJson.put("provider", p.uuid.toString());
    topJson.set("userIntent", bodyJson);

    AvailabilityZone az = AvailabilityZone.find.byId(az1.uuid);
    assertThat(az.region.name, is(notNullValue()));

    Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid +
                                      "/universes/" + universe.universeUUID)
                                      .cookie(validCookie).bodyJson(topJson));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("universeUUID").asText(), is(notNullValue()));
    JsonNode universeDetails = json.get("universeDetails");
    assertThat(universeDetails, is(notNullValue()));

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
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    InstanceType i =
      InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject();
    ArrayNode regionList = Json.newArray();
    regionList.add(r.uuid.toString());
    bodyJson.set("regionList", regionList);
    bodyJson.put("isMultiAZ", true);
    bodyJson.put("universeName", universe.name);
    bodyJson.put("numNodes", 5);
    bodyJson.put("instanceType", i.getInstanceTypeCode());
    bodyJson.put("replicationFactor", 3);
    bodyJson.put("provider", p.uuid.toString());
    topJson.set("userIntent", bodyJson);

    Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid +
                                             "/universes/" + universe.universeUUID)
                              .cookie(validCookie).bodyJson(topJson));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("universeUUID").asText(), is(notNullValue()));
    JsonNode universeDetails = json.get("universeDetails");
    assertThat(universeDetails, is(notNullValue()));
    JsonNode userIntent = universeDetails.get("userIntent");
    assertThat(userIntent, is(notNullValue()));

    // Try universe expand only, and re-check.
    bodyJson.put("numNodes", 9);
    result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid +
                                      "/universes/" + universe.universeUUID)
                       .cookie(validCookie).bodyJson(topJson));
    assertEquals(OK, result.status());
    json = Json.parse(contentAsString(result));
    assertThat(json.get("universeUUID").asText(), is(notNullValue()));
    universeDetails = json.get("universeDetails");
    assertThat(universeDetails, is(notNullValue()));
    userIntent = universeDetails.get("userIntent");
    assertThat(userIntent, is(notNullValue()));
  }

  @Test
  public void testUniverseDestroyInvalidUUID() {
    UUID randomUUID = UUID.randomUUID();
    Result result = route(fakeRequest("DELETE", "/api/customers/" + customer.uuid + "/universes/" + randomUUID)
                            .cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("No universe found with UUID: " + randomUUID)));
  }

  @Test
  public void testUniverseDestroyValidUUID() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

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
    Universe.saveDetails(universe.universeUUID, updater);


    Result result = route(fakeRequest("DELETE", "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID)
                            .cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("taskUUID").asText(), allOf(notNullValue(), equalTo(fakeTaskUUID.toString())));

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
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    universe = Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater());

    ObjectNode bodyJson = Json.newObject();

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));
    assertEquals(BAD_REQUEST, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), is(containsString("\"universeUUID\":[\"This field is required\"]")));
    assertThat(json.get("error").toString(), is(containsString("\"taskType\":[\"This field is required\"]")));
    assertThat(json.get("error").toString(), is(containsString("\"nodeNames\":[\"This field is required\"]")));

    CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNull(th);
  }

  @Test
  public void testUniverseSoftwareUpgradeValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("taskType", "Software");
    bodyJson.put("nodeNames",  "[\"host-n1\",\"host-n3\",\"host-n2\"]");
    bodyJson.put("ybSoftwareVersion", "0.0.1");
    UserIntent userIntent = getDefaultUserIntent();
    userIntent.universeName = universe.name;
    bodyJson.set("userIntent", Json.toJson(userIntent));

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("taskUUID").asText(), allOf(notNullValue(), equalTo(fakeTaskUUID.toString())));

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
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("taskType", "Software");
    bodyJson.put("nodeNames", "[\"host-n1\",\"host-n3\",\"host-n2\"]");
    UserIntent userIntent = getDefaultUserIntent();
    userIntent.universeName = universe.name;
    bodyJson.set("userIntent", Json.toJson(userIntent));

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), is(containsString("ybSoftwareVersion param is required for taskType: Software")));
  }

  @Test
  public void testUniverseGFlagsUpgradeValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ArrayNode nodes = Json.newArray();
    nodes.add("host-n1");
    nodes.add("host-n2");
    nodes.add("host-n3");

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("taskType", "GFlags");
    bodyJson.set("nodeNames", nodes);
    bodyJson.set("gflags", Json.parse("[{ \"name\": \"gflag1\", \"value\": \"123\"}]"));
    UserIntent userIntent = getDefaultUserIntent();
    userIntent.universeName = universe.name;
    bodyJson.set("userIntent", Json.toJson(userIntent));

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));
    assertThat(json.get("taskUUID").asText(), allOf(notNullValue(), equalTo(fakeTaskUUID.toString())));

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
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("taskType", "GFlags");
    bodyJson.put("nodeNames", "[\"host-n1\",\"host-n3\",\"host-n2\"]");
    UserIntent userIntent = getDefaultUserIntent();
    userIntent.universeName = universe.name;
    bodyJson.set("userIntent", Json.toJson(userIntent));

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), is(containsString("gflags param is required for taskType: GFlags")));
  }

  @Test
  public void testUniverseStatusSuccess() {
    ObjectNode fakeReturn = Json.newObject();
    fakeReturn.set(UNIVERSE_ALIVE_METRIC, Json.newObject());
    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(fakeReturn);
    UUID universeUUID = UUID.randomUUID();
    Universe.create("TestUniverse", universeUUID, customer.getCustomerId());
    String method = "GET";
    String uri = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/status";
    Result result = route(fakeRequest(method, uri).cookie(validCookie));
    assertEquals(OK, result.status());
  }

  @Test
  public void testUniverseStatusError() {
    ObjectNode fakeReturn = Json.newObject();
    fakeReturn.put("error", "foobar");
    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(fakeReturn);
    UUID universeUUID = UUID.randomUUID();
    Universe.create("TestUniverse", universeUUID, customer.getCustomerId());
    String method = "GET";
    String uri = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/status";
    Result result = route(fakeRequest(method, uri).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), containsString("foobar"));
  }

  @Test
  public void testUniverseStatusBadParams() {
    UUID universeUUID = UUID.randomUUID();
    String method = "GET";
    String uri = "/api/customers/" + customer.uuid + "/universes/" + universeUUID + "/status";
    Result result = route(fakeRequest(method, uri).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), containsString("No universe found with UUID: " + universeUUID));
  }

  @Test
  public void testFindByNameWithUniverseNameExists() {
    Universe.create("TestUniverse", UUID.randomUUID(), customer.getCustomerId());
    Result result =
      route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/find/TestUniverse").cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testFindByNameWithUniverseDoesNotExist() {
    Universe.create("TestUniverse", UUID.randomUUID(), customer.getCustomerId());
    Result result =
      route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/find/FakeUniverse").cookie(validCookie));
    assertEquals(OK, result.status());
  }

  @Test
  public void testCustomConfigureCreateWithMultiAZMultiRegion() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
      InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

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
    AvailabilityZone az = AvailabilityZone.find.byId(az1.uuid);
    assertThat(az.region.name, is(notNullValue()));
    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universe_configure")
      .cookie(validCookie).bodyJson(topJson));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");

    assertEquals(nodeDetailJson.size(), 7);
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUUIDToNumNodeMap));
  }

  @Test
  public void testCustomConfigureEditWithPureExpand() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class),
                                 Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
      InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    utd.userIntent = getTestUserIntent(r, p, i, 5);
    PlacementInfoUtil.updateUniverseDefinition(utd, customer.getCustomerId());
    universe.setUniverseDetails(utd);
    universe.save();
    int totalNumNodesAfterExpand = 0;
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(universe.getUniverseDetails().nodeDetailsSet);
    for(Map.Entry<UUID, Integer> entry : azUuidToNumNodes.entrySet()) {
      totalNumNodesAfterExpand += entry.getValue() + 1;
      azUuidToNumNodes.put(entry.getKey(), entry.getValue() + 1);
    }
    UniverseDefinitionTaskParams editTestUTD = universe.getUniverseDetails();
    editTestUTD.userIntent.numNodes = totalNumNodesAfterExpand;
    editTestUTD.placementInfo = constructPlacementInfoObject(azUuidToNumNodes);
    ObjectNode topJson = (ObjectNode) Json.toJson(editTestUTD);
    Result result = route(fakeRequest("POST", "/api/customers/" +
                          customer.uuid + "/universe_configure")
                    .cookie(validCookie) .bodyJson(topJson));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(nodeDetailJson.size(), totalNumNodesAfterExpand);
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUuidToNumNodes));
  }

  @Test
  public void testConfigureCreateOnPrem() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class),
      Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    Provider p = ModelFactory.newProvider(customer, CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
      InstanceType.upsert(p.code, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());
    NodeInstanceFormData.NodeInstanceData node1 = new NodeInstanceFormData.NodeInstanceData();
    NodeInstanceFormData.NodeInstanceData node2 = new NodeInstanceFormData.NodeInstanceData();
    NodeInstanceFormData.NodeInstanceData node3 = new NodeInstanceFormData.NodeInstanceData();
    node1.ip = "1.2.3.4";
    node1.instanceType = i.getInstanceTypeCode();
    node1.sshUser = "centos";
    node1.region = r.code;
    node1.zone = az1.code;
    node2 = node1;
    node3 = node1;
    node1.ip = "2.3.4.5";
    node2.zone = az1.code;
    node3.ip = "3.4.5.6";
    node3.zone = az1.code;
    NodeInstance.create(az1.uuid, node1);
    NodeInstance.create(az1.uuid, node2);
    NodeInstance.create(az1.uuid, node3);
    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    utd.userIntent = getTestUserIntent(r, p, i, 3);
    utd.userIntent.providerType = CloudType.onprem;
    PlacementInfoUtil.updateUniverseDefinition(utd, customer.getCustomerId());
    ArrayList<UUID> nodeList = new ArrayList<>();
    utd.nodeDetailsSet.stream().forEach(nodeDetail -> nodeList.add(nodeDetail.azUuid));
    assertTrue(nodeList.contains(az1.uuid));
    assertFalse(nodeList.contains(az2.uuid));
    assertFalse(nodeList.contains(az3.uuid));
  }

  @Test
  public void testConfigureEditOnPrem() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskType.class),
      Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    Provider p = ModelFactory.newProvider(customer, CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
      InstanceType.upsert(p.code, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());
    NodeInstanceFormData.NodeInstanceData node1 = new NodeInstanceFormData.NodeInstanceData();

    node1.ip = "1.2.3.4";
    node1.instanceType = i.getInstanceTypeCode();
    node1.sshUser = "centos";
    node1.region = r.code;
    node1.zone = az1.code;
    NodeInstanceFormData.NodeInstanceData node2 = node1;
    NodeInstanceFormData.NodeInstanceData node3 = node1;
    NodeInstanceFormData.NodeInstanceData node4 = node1;
    node1.ip = "2.3.4.5";
    node2.zone = az1.code;
    node3.ip = "3.4.5.6";
    node3.zone = az1.code;
    node4.ip = "9.6.5.4";
    node4.zone = az1.code;
    NodeInstance.create(az1.uuid, node1);
    NodeInstance.create(az1.uuid, node2);
    NodeInstance.create(az1.uuid, node3);
    NodeInstance.create(az1.uuid, node4);
    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    utd.userIntent = getTestUserIntent(r, p, i, 3);
    utd.userIntent.providerType = CloudType.onprem;
    PlacementInfoUtil.updateUniverseDefinition(utd, customer.getCustomerId());
    universe.setUniverseDetails(utd);
    universe.save();
    UniverseDefinitionTaskParams editTestUTD = universe.getUniverseDetails();
    editTestUTD.userIntent.numNodes = 4;

    PlacementInfoUtil.updateUniverseDefinition(editTestUTD, customer.getCustomerId());
    ArrayList<UUID> nodeList = new ArrayList<>();
    editTestUTD.nodeDetailsSet.stream().forEach(nodeDetail -> nodeList.add(nodeDetail.azUuid));
    assertTrue(nodeList.contains(az1.uuid));
    assertFalse(nodeList.contains(az2.uuid));
    assertFalse(nodeList.contains(az3.uuid));
  }


  private PlacementInfo constructPlacementInfoObject(Map<UUID, Integer> azToNumNodesMap) {
    PlacementInfo placementInfo = new PlacementInfo();
    placementInfo.cloudList = new ArrayList<>();
    Iterator it = azToNumNodesMap.entrySet().iterator();
    while (it.hasNext()) {
      Map.Entry pair = (Map.Entry)it.next();
      UUID azUUID = UUID.fromString(pair.getKey().toString());
      int azCount = Integer.parseInt(pair.getValue().toString());
      AvailabilityZone currentAz = AvailabilityZone.get(azUUID);
      Region currentRegion = currentAz.region;
      Provider currentProvider = currentAz.getProvider();
      boolean cloudItemFound = false;
      for (PlacementInfo.PlacementCloud cloudItem: placementInfo.cloudList) {
        if (cloudItem.uuid.equals(currentProvider.uuid)) {
          cloudItemFound = true;
          boolean regionItemFound = false;
          for (PlacementInfo.PlacementRegion regionItem: cloudItem.regionList) {
            if (regionItem.uuid.equals(currentRegion.uuid)) {
              PlacementInfo.PlacementAZ azItem = new PlacementInfo.PlacementAZ();
              azItem.name = currentAz.name;
              azItem.subnet = currentAz.subnet;
              azItem.replicationFactor = 1;
              azItem.uuid = currentAz.uuid;
              azItem.numNodesInAZ = azCount;
              regionItem.azList.add(azItem);
              regionItemFound = true;
            }
          }
          if (cloudItemFound && !regionItemFound) {
            PlacementInfo.PlacementRegion newRegion = new PlacementInfo.PlacementRegion();
            newRegion.uuid = currentRegion.uuid;
            newRegion.name = currentRegion.name;
            newRegion.code = currentRegion.code;
            newRegion.azList = new ArrayList<>();
            PlacementInfo.PlacementAZ azItem = new PlacementInfo.PlacementAZ();
            azItem.name = currentAz.name;
            azItem.subnet = currentAz.subnet;
            azItem.replicationFactor = 1;
            azItem.uuid = currentAz.uuid;
            azItem.numNodesInAZ = azCount;
            newRegion.azList.add(azItem);
          }
          }
        }
        if (!cloudItemFound) {
          PlacementInfo.PlacementCloud newCloud = new PlacementInfo.PlacementCloud();
          newCloud.uuid = currentProvider.uuid;
          newCloud.code = currentProvider.code;
          newCloud.regionList = new ArrayList<>();
          PlacementInfo.PlacementRegion newRegion = new PlacementInfo.PlacementRegion();
          newRegion.uuid = currentRegion.uuid;
          newRegion.name = currentRegion.name;
          newRegion.code = currentRegion.code;
          newRegion.azList = new ArrayList<>();
          PlacementInfo.PlacementAZ azItem = new PlacementInfo.PlacementAZ();
          azItem.name = currentAz.name;
          azItem.subnet = currentAz.subnet;
          azItem.replicationFactor = 1;
          azItem.uuid = currentAz.uuid;
          azItem.numNodesInAZ = azCount;
          newRegion.azList.add(azItem);
          newCloud.regionList.add(newRegion);
          placementInfo.cloudList.add(newCloud);
        }
      }
      return placementInfo;
    }

    private static ObjectNode getTestTaskParams(UUID fakeTaskUUID, Customer customer, Region r, Provider p, InstanceType i, int numNodes) {
      ObjectNode topJson = Json.newObject();
      ObjectNode bodyJson = Json.newObject();
      ArrayNode regionList = Json.newArray();
      regionList.add(r.uuid.toString());
      bodyJson.set("regionList", regionList);
      bodyJson.put("universeName", "Single UserUniverse");
      bodyJson.put("isMultiAZ", true);
      bodyJson.put("instanceType", i.getInstanceTypeCode());
      bodyJson.put("replicationFactor", 3);
      bodyJson.put("numNodes", numNodes);
      bodyJson.put("provider", p.uuid.toString());
      topJson.put("nodePrefix", "univContTest");
      topJson.set("userIntent", bodyJson);
      return  topJson;
    }

    private UserIntent getDefaultUserIntent() {
      Provider p = ModelFactory.awsProvider(customer);
      Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
      AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
      AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
      InstanceType i =
          InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
      int numNodes = 3;
      UserIntent ui = new UserIntent();
      ArrayList<UUID> regionList = new ArrayList<>();
      regionList.add(r.uuid);
      ui.replicationFactor = 3;
      ui.regionList = regionList;
      ui.provider = p.uuid.toString();
      ui.numNodes = numNodes;
      ui.instanceType = i.getInstanceTypeCode();
      ui.isMultiAZ = true;
      return ui;
    }

    private static UserIntent getTestUserIntent(Region r, Provider p, InstanceType i, int numNodes) {
      UserIntent ui = new UserIntent();
      ArrayList<UUID> regionList = new ArrayList<>();
      regionList.add(r.uuid);
      ui.regionList = regionList;
      ui.provider = p.uuid.toString();
      ui.numNodes = numNodes;
      ui.instanceType = i.getInstanceTypeCode();
      ui.isMultiAZ = true;
      return ui;
    }

    private static boolean areConfigObjectsEqual(ArrayNode nodeDetailSet,
                                                 Map<UUID, Integer> azToNodeMap) {
      boolean nodeNotFound = false;
      for (int nodeCounter = 0; nodeCounter < nodeDetailSet.size(); nodeCounter++) {
        String azUUID = nodeDetailSet.get(nodeCounter).get("azUuid").asText();
        if (azToNodeMap.get(UUID.fromString(azUUID)) != null) {
          azToNodeMap.put(UUID.fromString(azUUID), azToNodeMap.get(UUID.fromString(azUUID))-1);
        } else {
          nodeNotFound = true;
        }
      }
      boolean valueMatches = true;
      for (Integer nodeDifference : azToNodeMap.values()) {
        if (nodeDifference != 0) {
          valueMatches = false;
        }
      }
      if (valueMatches && !nodeNotFound) {
        return true;
      }
      return false;
    }
}
