// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.params.UniverseDefinitionTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.*;

import org.junit.Before;
import org.junit.Test;
import org.mockito.Matchers;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

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

public class UniverseControllerTest extends FakeDBApplication {
  private Customer customer;
	private Commissioner mockCommissioner;

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
  }

  @Test
  public void testEmptyUniverseListWithValidUUID() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
  }

  @Test
  public void testUniverseListWithValidUUID() {
		Universe u1 = Universe.create("Universe-1", customer.customerId);
		customer.addUniverseUUID(u1.universeUUID);

		String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 1);
    assertThat(json.get(0).get("universeUUID").asText(), allOf( notNullValue(), equalTo(u1.universeUUID.toString())));
    assertThat(json.get(0).get("customerId").asInt(), allOf( notNullValue(), equalTo(customer.customerId)));
  }

  @Test
  public void testUniverseListWithInvalidUUID() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    UUID invalidUUID = UUID.randomUUID();
    Result result = route(fakeRequest("GET", "/api/customers/" + invalidUUID + "/universes").cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID: " + invalidUUID)));
  }

  @Test
  public void testUniverseCreateWithInvalidParams() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode emptyJson = Json.newObject();
    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie).bodyJson(emptyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("\"regionUUID\":[\"This field is required\"]")));
    assertThat(contentAsString(result), is(containsString("\"isMultiAZ\":[\"This field is required\"]")));
    assertThat(contentAsString(result), is(containsString("\"universeName\":[\"This field is required\"]")));
  }

  @Test
  public void testUniverseCreateWithoutAvailabilityZone() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Provider p = Provider.create("Amazon");
    Region r = Region.create(p, "region-1", "PlacementRegion 1");

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("regionUUID", r.uuid.toString());
    bodyJson.put("universeName", "Single UserUniverse");
    bodyJson.put("isMultiAZ", false);

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie).bodyJson(bodyJson));
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("No AZ found for region: " + r.uuid)));
  }

  @Test
  public void testUniverseCreateWithSingleAvailabilityZones() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		UUID fakeTaskUUID = UUID.randomUUID();
		when(mockCommissioner.submit(Matchers.any(TaskInfo.Type.class), Matchers.any(UniverseDefinitionTaskParams.class)))
			.thenReturn(fakeTaskUUID);

		Provider p = Provider.create("Amazon");
    Region r = Region.create(p, "region-1", "PlacementRegion 1");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("regionUUID", r.uuid.toString());
    bodyJson.put("universeName", "Single UserUniverse");
    bodyJson.put("isMultiAZ", false);

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/universes").cookie(validCookie).bodyJson(bodyJson));
		assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("universeUUID").asText(), is(notNullValue()));
    assertThat(json.get("customerId").asInt(), allOf( notNullValue(), equalTo(customer.customerId)));
    assertEquals(json.get("version").asInt(), 1);
		JsonNode universeDetails = json.get("universeDetails");
    assertThat(universeDetails, is(notNullValue()));

		CustomerTask th = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
		assertNotNull(th);
		assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
		assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Single UserUniverse")));
  }
}
