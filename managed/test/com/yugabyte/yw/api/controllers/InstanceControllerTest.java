// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.UserUniverse;

import org.junit.Before;
import org.junit.Test;
import org.mockito.Matchers;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

import java.util.Iterator;
import java.util.Map;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.*;

public class InstanceControllerTest extends FakeDBApplication {
  private Customer customer;
	private ApiHelper mockApiHelper;

	@Override
	protected Application provideApplication() {
		mockApiHelper = mock(ApiHelper.class);
		return new GuiceApplicationBuilder()
			.configure((Map) Helpers.inMemoryDatabase())
			.overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
			.build();
	}

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
  }

  @Test
  public void testEmptyInstanceListWithValidUUID() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/instances").cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
  }

  @Test
  public void testInstanceListWithValidUUID() {
    ObjectNode placementInfo = Json.newObject();
    UserUniverse i1 = UserUniverse.create(customer, "instance-1", placementInfo);
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/instances").cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 1);
    assertThat(json.get(0).get("instanceId").asText(), allOf( notNullValue(), equalTo(i1.getInstanceId().toString())));
    assertThat(json.get(0).get("customerId").asText(), allOf( notNullValue(), equalTo(customer.uuid.toString())));
  }

  @Test
  public void testInstanceListWithInvalidUUID() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    UUID invalidUUID = UUID.randomUUID();
    Result result = route(fakeRequest("GET", "/api/customers/" + invalidUUID + "/instances").cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID: " + invalidUUID)));
  }

  @Test
  public void testInstanceCreateWithInvalidParams() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode emptyJson = Json.newObject();
    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/instances").cookie(validCookie).bodyJson(emptyJson));
    assertEquals(BAD_REQUEST, result.status());

    assertThat(contentAsString(result), is(containsString("\"regionUUID\":[\"This field is required\"]")));
    assertThat(contentAsString(result), is(containsString("\"isMultiAZ\":[\"This field is required\"]")));
    assertThat(contentAsString(result), is(containsString("\"name\":[\"This field is required\"]")));
  }

  @Test
  public void testInstanceCreateWithoutAvailabilityZone() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Provider p = Provider.create("Amazon");
    Region r = Region.create(p, "region-1", "PlacementRegion 1");

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("regionUUID", r.uuid.toString());
    bodyJson.put("name", "Single UserUniverse");
    bodyJson.put("isMultiAZ", false);

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/instances").cookie(validCookie).bodyJson(bodyJson));
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("Availability Zone not found for region: " + r.uuid)));
  }

  @Test
  public void testInstanceCreateWithSingleAvailabilityZones() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();

		ObjectNode postResponseJson = Json.newObject();
		UUID fakeTaskUUID = UUID.randomUUID();
		postResponseJson.put("taskUUID", fakeTaskUUID.toString());
		when(mockApiHelper.postRequest(Matchers.any(String.class), Matchers.any(JsonNode.class)))
			.thenReturn(postResponseJson);

		Provider p = Provider.create("Amazon");
    Region r = Region.create(p, "region-1", "PlacementRegion 1");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("regionUUID", r.uuid.toString());
    bodyJson.put("name", "Single UserUniverse");
    bodyJson.put("isMultiAZ", false);

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/instances").cookie(validCookie).bodyJson(bodyJson));
		assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(json.get("instanceId").asText(), is(notNullValue()));
    assertThat(json.get("customerId").asText(), allOf( notNullValue(), equalTo(customer.uuid.toString())));
    JsonNode placementInfo = json.get("placementInfo");
    assertThat(placementInfo, is(notNullValue()));

    assertThat(placementInfo.get("replicationFactor").asInt(), allOf(notNullValue(), equalTo(3)));
    assertTrue(placementInfo.get("subnets").isArray());
    assertEquals(3, placementInfo.get("subnets").size());
    for (Iterator<JsonNode> i = placementInfo.get("subnets").elements(); i.hasNext(); ) {
      JsonNode subnet = (JsonNode)i.next();
      assertThat(subnet.asText(), allOf(notNullValue(), equalTo(az1.subnet)));
    }

		CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
		assertNotNull(th);
		assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
		assertThat(th.getTarget().toString(), allOf(notNullValue(), equalTo("UserUniverse")));
  }

  @Test
  public void testInstanceCreateWithMultiAvailabilityZones() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Provider p = Provider.create("Amazon");
    Region r = Region.create(p, "region-1", "PlacementRegion 1");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");

		ObjectNode postResponseJson = Json.newObject();
		UUID fakeTaskUUID = UUID.randomUUID();
		postResponseJson.put("taskUUID", fakeTaskUUID.toString());
		when(mockApiHelper.postRequest(Matchers.any(String.class), Matchers.any(JsonNode.class)))
			.thenReturn(postResponseJson);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("regionUUID", r.uuid.toString());
    bodyJson.put("name", "Single UserUniverse");
    bodyJson.put("isMultiAZ", true);

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/instances").cookie(validCookie).bodyJson(bodyJson));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(json.get("instanceId").asText(), is(notNullValue()));
    assertThat(json.get("customerId").asText(), allOf( notNullValue(), equalTo(customer.uuid.toString())));
    JsonNode placementInfo = json.get("placementInfo");
    assertThat(placementInfo, is(notNullValue()));

    assertTrue(placementInfo.get("subnets").isArray());
    assertEquals(3, placementInfo.get("subnets").size());
    int idx = 1;
    for (Iterator<JsonNode> i = placementInfo.get("subnets").elements(); i.hasNext(); ) {
      JsonNode subnet = (JsonNode)i.next();
      assertThat(subnet.asText(), allOf(notNullValue(), equalTo("subnet-" + idx)));
      idx++;
    }

		CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
		assertNotNull(th);
		assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
		assertThat(th.getTarget().toString(), allOf(notNullValue(), equalTo("UserUniverse")));
  }
}
