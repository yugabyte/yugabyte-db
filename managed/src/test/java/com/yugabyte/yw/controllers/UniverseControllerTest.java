// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.eq;
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

import java.util.Iterator;
import java.util.Map;
import java.util.UUID;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
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
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;

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

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockMetricQueryHelper = mock(MetricQueryHelper.class);
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
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = 3;
    userIntent.isMultiAZ = true;
    userIntent.numNodes = 5;
    userIntent.provider = "aws";

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
    when(mockCommissioner.submit(Matchers.any(TaskInfo.Type.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, 1, 20, InstanceType.VolumeType.EBS, null);

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
    when(mockCommissioner.submit(Matchers.any(TaskInfo.Type.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    InstanceType i =
        InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, 1, 20, InstanceType.VolumeType.EBS, null);

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
    when(mockCommissioner.submit(Matchers.any(TaskInfo.Type.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
    InstanceType i =
      InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, 1, 20, InstanceType.VolumeType.EBS, null);

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
    when(mockCommissioner.submit(Matchers.any(TaskInfo.Type.class), Matchers.any(UniverseDefinitionTaskParams.class)))
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
    when(mockCommissioner.submit(any(TaskInfo.Type.class), any(UniverseDefinitionTaskParams.class)))
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
    when(mockCommissioner.submit(any(TaskInfo.Type.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("taskType", "Software");
    bodyJson.put("nodeNames",  "[\"host-n1\",\"host-n3\",\"host-n2\"]");
    bodyJson.put("ybServerPackage", "awesomepkg-0123456");

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    verify(mockCommissioner).submit(eq(TaskInfo.Type.UpgradeUniverse), any(UniverseTaskParams.class));

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
    when(mockCommissioner.submit(any(TaskInfo.Type.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("taskType", "Software");
    bodyJson.put("nodeNames", "[\"host-n1\",\"host-n3\",\"host-n2\"]");

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), is(containsString("ybServerPackage param is required for taskType: Software")));
  }

  @Test
  public void testUniverseGFlagsUpgradeValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(TaskInfo.Type.class), any(UniverseDefinitionTaskParams.class)))
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

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    verify(mockCommissioner).submit(eq(TaskInfo.Type.UpgradeUniverse), any(UniverseTaskParams.class));
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
    when(mockCommissioner.submit(any(TaskInfo.Type.class), any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);
    Universe universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("taskType", "GFlags");
    bodyJson.put("nodeNames", "[\"host-n1\",\"host-n3\",\"host-n2\"]");

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid +
      "/universes/" + universe.universeUUID + "/upgrade")
                            .cookie(validCookie).bodyJson(bodyJson));

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), is(containsString("gflags param is required for taskType: GFlags")));
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
}
