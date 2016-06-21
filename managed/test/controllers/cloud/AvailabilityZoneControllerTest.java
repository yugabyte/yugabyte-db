package controllers.cloud;// Copyright (c) Yugabyte, Inc.

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import helpers.FakeDBApplication;
import models.cloud.AvailabilityZone;
import models.cloud.Provider;
import models.cloud.Region;
import models.yb.Customer;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

import java.util.UUID;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.IsInstanceOf.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.route;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.CoreMatchers.allOf;

public class AvailabilityZoneControllerTest extends FakeDBApplication {
	Customer customer;
	Provider defaultProvider;
	Region defaultRegion;

	@Before
	public void setUp() {
		customer = Customer.create("Valid Customer", "foo@bar.com", "password");
		defaultProvider = Provider.create("Amazon");
		defaultRegion = Region.create(defaultProvider, "default-region", "Default Region", true);
	}

	@Test
	public void testListAvailabilityZonesWithInvalidProviderRegionUUID() {
		String authToken = customer.createAuthToken();

		Http.RequestBuilder fr = play.test.Helpers.fakeRequest(
				controllers.cloud.routes.AvailabilityZoneController.list(UUID.randomUUID(), UUID.randomUUID())
		).header("X-AUTH-TOKEN", authToken);

		Result result = route(fr);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(BAD_REQUEST, result.status());
		assertEquals("Invalid Region/Provider UUID", json.get("error").asText());
	}

	@Test
	public void testListEmptyAvailabilityZonesWithValidProviderRegionUUID() {
		String authToken = customer.createAuthToken();

		Http.RequestBuilder fr = play.test.Helpers.fakeRequest(
				controllers.cloud.routes.AvailabilityZoneController.list(defaultProvider.uuid, defaultRegion.uuid)
		).header("X-AUTH-TOKEN", authToken);

		Result result = route(fr);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(OK, result.status());
		assertTrue(json.isArray());
	}

	@Test
	public void testListAvailabilityZonesWithValidProviderRegionUUID() {
		String authToken = customer.createAuthToken();
		AvailabilityZone az = AvailabilityZone.create(defaultRegion, "AZ-1", "AZ One", "Subnet 1");
		Http.RequestBuilder fr = play.test.Helpers.fakeRequest(
				controllers.cloud.routes.AvailabilityZoneController.list(defaultProvider.uuid, defaultRegion.uuid)
		).header("X-AUTH-TOKEN", authToken);

		Result result = route(fr);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(OK, result.status());
		assertEquals(1, json.size());
		assertEquals(az.uuid.toString(), json.get(0).findValue("uuid").asText());
		assertEquals(az.code, json.get(0).findValue("code").asText());
		assertEquals(az.name, json.get(0).findValue("name").asText());
		assertTrue(json.get(0).findValue("active").asBoolean());
	}

	@Test
	public void testCreateAvailabilityZoneWithInvalidProviderRegionUUID() {
		String authToken = customer.createAuthToken();

		Http.RequestBuilder fr = play.test.Helpers.fakeRequest(
				controllers.cloud.routes.AvailabilityZoneController.create(UUID.randomUUID(), UUID.randomUUID())
		).header("X-AUTH-TOKEN", authToken);

		Result result = route(fr);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(BAD_REQUEST, result.status());
		assertEquals("Invalid Region/Provider UUID", json.get("error").asText());
	}

	@Test
	public void testCreateAvailabilityZoneWithValidParams() {
		String authToken = customer.createAuthToken();

		ObjectNode azRequestJson = Json.newObject();
		azRequestJson.put("code", "foo-az-1");
		azRequestJson.put("name", "foo az 1");
		azRequestJson.put("subnet", "az subnet 1");

		Http.RequestBuilder fr = play.test.Helpers.fakeRequest(
				controllers.cloud.routes.AvailabilityZoneController.create(defaultProvider.uuid, defaultRegion.uuid))
				.header("X-AUTH-TOKEN", authToken)
		    .bodyJson(azRequestJson);

		Result result = route(fr);
		JsonNode json = Json.parse(contentAsString(result));

		assertEquals(OK, result.status());
		assertThat(json.get("uuid").toString(), is(notNullValue()));
		assertThat(json.get("code").asText(), is(allOf(notNullValue(), instanceOf(String.class), equalTo("foo-az-1"))));
		assertThat(json.get("name").asText(), is(allOf(notNullValue(), instanceOf(String.class), equalTo("foo az 1"))));
		assertThat(json.get("subnet").asText(), is(allOf(notNullValue(), instanceOf(String.class), equalTo("az subnet 1"))));
	}

	@Test
	public void testCreateAvailabilityZoneWithInValidParams() {
		String authToken = customer.createAuthToken();

		ObjectNode azRequestJson = Json.newObject();
		azRequestJson.put("code", "foo-az-1");
		azRequestJson.put("name", "foo az 1");

		Http.RequestBuilder fr = play.test.Helpers.fakeRequest(
				controllers.cloud.routes.AvailabilityZoneController.create(defaultProvider.uuid, defaultRegion.uuid))
				.header("X-AUTH-TOKEN", authToken);
		Result result = route(fr);

		assertEquals(BAD_REQUEST, result.status());
   	assertThat(contentAsString(result), CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
		assertThat(contentAsString(result), CoreMatchers.containsString("\"name\":[\"This field is required\"]"));
		assertThat(contentAsString(result), CoreMatchers.containsString("\"subnet\":[\"This field is required\"]"));
	}
}
