// Copyright (c) YugaByte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.ser.impl.IteratorSerializer;
import helpers.FakeDBApplication;
import models.cloud.AvailabilityZone;
import models.cloud.Provider;
import models.cloud.Region;
import models.yb.Customer;
import models.yb.Instance;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

import java.util.Iterator;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

public class InstanceControllerTest extends FakeDBApplication {
	private Customer customer;
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
		Instance i1 = Instance.create(customer, "instance-1", Instance.ProvisioningState.Completed, placementInfo);
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
		JsonNode json = Json.parse(contentAsString(result));

		assertThat(contentAsString(result), is(containsString("\"regionUUID\":[\"This field is required\"]")));
		assertThat(contentAsString(result), is(containsString("\"multiAZ\":[\"This field is required\"]")));
		assertThat(contentAsString(result), is(containsString("\"name\":[\"This field is required\"]")));
	}

	@Test
	public void testInstanceCreateWithSingleAvailabilityZones() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Provider p = Provider.create("Amazon");
		Region r = Region.create(p, "region-1", "Region 1", true);
		AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "AZ 1", "subnet-1");
		AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "AZ 2", "subnet-2");

		ObjectNode bodyJson = Json.newObject();
		bodyJson.put("regionUUID", r.uuid.toString());
		bodyJson.put("name", "Single Instance");
		bodyJson.put("multiAZ", false);

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
	}

	@Test
	public void testInstanceCreateWithMultiAvailabilityZones() {
		String authToken = customer.createAuthToken();
		Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
		Provider p = Provider.create("Amazon");
		Region r = Region.create(p, "region-1", "Region 1", true);
		AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "AZ 1", "subnet-1");
		AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "AZ 2", "subnet-2");
		AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "AZ 3", "subnet-3");

		ObjectNode bodyJson = Json.newObject();
		bodyJson.put("regionUUID", r.uuid.toString());
		bodyJson.put("name", "Single Instance");
		bodyJson.put("multiAZ", true);

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
	}
}
