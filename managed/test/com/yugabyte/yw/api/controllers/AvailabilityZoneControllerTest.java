// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.IsInstanceOf.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.route;

import java.util.UUID;

import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

public class AvailabilityZoneControllerTest extends FakeDBApplication {
	Customer customer;
	Provider defaultProvider;
	Region defaultRegion;

	@Before
	public void setUp() {
		customer = Customer.create("Valid Customer", "foo@bar.com", "password");
		defaultProvider = Provider.create("Amazon");
		defaultRegion = Region.create(defaultProvider, "default-region", "Default PlacementRegion");
	}

	@Test
	public void testListAvailabilityZonesWithInvalidProviderRegionUUID() {
		JsonNode json = doListAZAndVerifyResult(UUID.randomUUID(), UUID.randomUUID(), BAD_REQUEST);
		assertEquals("Invalid PlacementRegion/Provider UUID", json.get("error").asText());
	}

	@Test
	public void testListEmptyAvailabilityZonesWithValidProviderRegionUUID() {
    JsonNode json = doListAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid, OK);
		assertTrue(json.isArray());
	}

	@Test
	public void testListAvailabilityZonesWithValidProviderRegionUUID() {
    AvailabilityZone az = AvailabilityZone.create(defaultRegion, "PlacementAZ-1", "PlacementAZ One", "Subnet 1");
    JsonNode json = doListAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid, OK);

		assertEquals(1, json.size());
		assertEquals(az.uuid.toString(), json.get(0).findValue("uuid").asText());
		assertEquals(az.code, json.get(0).findValue("code").asText());
		assertEquals(az.name, json.get(0).findValue("name").asText());
		assertTrue(json.get(0).findValue("active").asBoolean());
	}

	@Test
	public void testCreateAvailabilityZoneWithInvalidProviderRegionUUID() {
	  JsonNode json =
	      doCreateAZAndVerifyResult(UUID.randomUUID(), UUID.randomUUID(), null, BAD_REQUEST);
		assertEquals("Invalid PlacementRegion/Provider UUID", json.get("error").asText());
	}

	@Test
	public void testCreateAvailabilityZoneWithValidParams() {
		ObjectNode azRequestJson = Json.newObject();
		azRequestJson.put("code", "foo-az-1");
		azRequestJson.put("name", "foo az 1");
		azRequestJson.put("subnet", "az subnet 1");

    JsonNode json =
        doCreateAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid, azRequestJson, OK);

		assertThat(json.get("uuid").toString(), is(notNullValue()));
		assertThat(json.get("code").asText(),
		           is(allOf(notNullValue(), instanceOf(String.class), equalTo("foo-az-1"))));
		assertThat(json.get("name").asText(),
		           is(allOf(notNullValue(), instanceOf(String.class), equalTo("foo az 1"))));
		assertThat(json.get("subnet").asText(),
		           is(allOf(notNullValue(), instanceOf(String.class), equalTo("az subnet 1"))));
	}

	@Test
	public void testCreateAvailabilityZoneWithInValidParams() {
		ObjectNode azRequestJson = Json.newObject();

    String authToken = customer.createAuthToken();
    String uri =
        "/api/providers/" + defaultProvider.uuid + "/regions/" + defaultRegion.uuid + "/zones";
    Http.RequestBuilder fr = play.test.Helpers.fakeRequest("POST",uri)
                             .header("X-AUTH-TOKEN", authToken)
                             .bodyJson(azRequestJson);

    Result result = route(fr);
    assertEquals(BAD_REQUEST, result.status());
   	assertThat(contentAsString(result),
   	           CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
		assertThat(contentAsString(result),
		           CoreMatchers.containsString("\"name\":[\"This field is required\"]"));
		assertThat(contentAsString(result),
		           CoreMatchers.containsString("\"subnet\":[\"This field is required\"]"));
	}

	private JsonNode doListAZAndVerifyResult(UUID cloudProvider, UUID region, int expectedStatus) {
    String authToken = customer.createAuthToken();
    String uri = "/api/providers/" + cloudProvider + "/regions/" + region + "/zones";
    Http.RequestBuilder fr =
        play.test.Helpers.fakeRequest("GET", uri).header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
	}

	private JsonNode doCreateAZAndVerifyResult(UUID cloudProvider,
	                                           UUID region,
	                                           ObjectNode azRequestJson,
	                                           int expectedStatus) {
	  String authToken = customer.createAuthToken();
	  Http.RequestBuilder fr = play.test.Helpers.fakeRequest(
	      "POST", "/api/providers/" + cloudProvider + "/regions/" + region + "/zones")
	      .header("X-AUTH-TOKEN", authToken);
	  if (azRequestJson != null) {
	    fr.bodyJson(azRequestJson);
	  }
	  Result result = route(fr);
	  assertEquals(expectedStatus, result.status());
	  return Json.parse(contentAsString(result));
	}
}
