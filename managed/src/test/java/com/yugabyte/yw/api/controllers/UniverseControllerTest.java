// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import org.junit.Before;
import org.junit.Test;
import org.mockito.Matchers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class UniverseControllerTest extends FakeDBApplication {
  private Customer customer;
  private Commissioner mockCommissioner;
  private Http.Cookie validCookie;

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    return new GuiceApplicationBuilder()
      .configure((Map) Helpers.inMemoryDatabase())
      .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
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
    Universe u1 = Universe.create("Universe-1", customer.getCustomerId());
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();

    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(1, json.size());
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
    Universe universe = Universe.create("Test Universe", customer.getCustomerId());
    UniverseDefinitionTaskParams ud = universe.getUniverseDetails();

    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        if (universeDetails == null) {
          universeDetails = new UniverseDefinitionTaskParams();
        }
        universeDetails.userIntent = new UserIntent();
        universeDetails.userIntent.replicationFactor = 3;
        universeDetails.userIntent.isMultiAZ = true;

        List<String> subnets = new ArrayList<String>();
        subnets.add("subnet-1");
        subnets.add("subnet-2");
        subnets.add("subnet-3");

        // Add a desired number of nodes.
        universeDetails.userIntent.numNodes = 5;
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.subnet_id = subnets.get(idx % subnets.size());
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsSet.add(node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
    Universe.saveDetails(universe.universeUUID, updater);

    Result result =
      route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeDetails = json.get("universeDetails");
    assertThat(universeDetails, is(notNullValue()));
    JsonNode userIntent = universeDetails.get("userIntent");
    assertThat(userIntent, is(notNullValue()));
    assertThat(userIntent.get("replicationFactor").asInt(), allOf(notNullValue(), equalTo(3)));
    assertThat(userIntent.get("isMultiAZ").asBoolean(), allOf(notNullValue(), equalTo(true)));

    JsonNode nodeDetailsMap = universeDetails.get("nodeDetailsSet");
    assertThat(nodeDetailsMap, is(notNullValue()));

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
    assertThat(contentAsString(result), is(containsString("\"userIntent\":[\"This field is required\"]")));
  }

  @Test
  public void testUniverseCreateWithoutAvailabilityZone() {
    Provider p = Provider.create("aws", "Amazon");
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
    topJson.set("userIntent", bodyJson);

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universes")
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

    Provider p = Provider.create("aws", "Amazon");
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
    Universe universe = Universe.create("Test Universe", customer.getCustomerId());
    ObjectNode emptyJson = Json.newObject();
    Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID)
                            .cookie(validCookie).bodyJson(emptyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("\"userIntent\":[\"This field is required\"]")));
  }

  @Test
  public void testUniverseUpdateWithValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskInfo.Type.class), Matchers.any(UniverseDefinitionTaskParams.class)))
      .thenReturn(fakeTaskUUID);

    Provider p = Provider.create("aws", "Amazon");
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe universe = Universe.create("Test Universe", customer.getCustomerId());
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

    Provider p = Provider.create("aws", "Amazon");
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1a", "PlacementAZ 1a", "subnet-1a");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-1b", "PlacementAZ 1b", "subnet-1b");
    Region r2 = Region.create(p, "region-2", "PlacementRegion 2", "default-image");
    AvailabilityZone az3 = AvailabilityZone.create(r2, "az-2a", "PlacementAZ 2a", "subnet-2a");
    Universe universe = Universe.create("Test Universe", customer.getCustomerId());
    InstanceType i =
      InstanceType.upsert(p.code, "c3.xlarge", 10, 5.5, 1, 20, InstanceType.VolumeType.EBS, null);

    ObjectNode topJson = Json.newObject();
    ObjectNode bodyJson = Json.newObject();
    ArrayNode regionList = Json.newArray();
    regionList.add(r.uuid.toString());
    regionList.add(r2.uuid.toString());
    bodyJson.set("regionList", regionList);
    bodyJson.put("isMultiAZ", true);
    bodyJson.put("universeName", universe.name);
    bodyJson.put("numNodes", 11);
    bodyJson.put("instanceType", i.getInstanceTypeCode());
    bodyJson.put("replicationFactor", 3);
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

    List<UUID> azUuids = new ArrayList<UUID>();
    azUuids.add(az1.uuid);
    azUuids.add(az2.uuid);
    azUuids.add(az3.uuid);
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        universeDetails = new UniverseDefinitionTaskParams();
        universeDetails.userIntent = new UserIntent();
        universeDetails.userIntent.replicationFactor = 3;
        universeDetails.userIntent.isMultiAZ = true;
        universeDetails.userIntent.regionList = new ArrayList<UUID>();
        universeDetails.userIntent.regionList.add(r.uuid);
        universeDetails.userIntent.regionList.add(r2.uuid);

        universeDetails.placementInfo =
            PlacementInfoUtil.getPlacementInfo(universeDetails.userIntent);
        List<String> subnets = new ArrayList<String>();
        subnets.add("subnet-1a");
        subnets.add("subnet-1b");
        subnets.add("subnet-2a");

        // Add a desired number of nodes.
        universeDetails.userIntent.numNodes = 11;
        universeDetails.nodeDetailsSet = new HashSet<NodeDetails>();
        for (int idx = 1; idx <= universeDetails.userIntent.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.subnet_id = subnets.get(idx % subnets.size());
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          node.azUuid = azUuids.get(idx % 3);
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsSet.add(node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
    Universe.saveDetails(universe.universeUUID, updater);

    // Try universe expand only, and re-check.
    bodyJson.put("numNodes", 17);
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
    Universe universe = Universe.create("Test Universe", customer.getCustomerId());

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
}
