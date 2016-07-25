// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.api.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.IsInstanceOf.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
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

public class RegionControllerTest extends FakeDBApplication {
  Customer customer;
  Provider provider;

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    provider = Provider.create("Amazon");
  }

  @Test
  public void testListRegionsWithInvalidProviderUUID() {
    String authToken = customer.createAuthToken();
    Http.RequestBuilder fr =
        play.test.Helpers.fakeRequest("GET", "/api/providers/" + UUID.randomUUID() + "/regions")
                         .header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListEmptyRegionsWithValidProviderUUID() {
    String authToken = customer.createAuthToken();
    Http.RequestBuilder fr =
        play.test.Helpers.fakeRequest("GET", "/api/providers/" + provider.uuid + "/regions")
                         .header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListRegionWithoutZonesAndValidProviderUUID() {
    String authToken = customer.createAuthToken();
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion");
    Http.RequestBuilder fr = fakeRequest("GET", "/api/providers/" + provider.uuid + "/regions")
                             .header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListRegionsWithValidProviderUUID() {
    String authToken = customer.createAuthToken();
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion");
    AvailabilityZone.create(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");

    Http.RequestBuilder fr = fakeRequest("GET", "/api/providers/" + provider.uuid + "/regions")
                             .header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());

    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r.uuid.toString());
    assertEquals(json.get(0).path("code").asText(), r.code);
    assertEquals(json.get(0).path("name").asText(), r.name);
  }

  @Test
  public void testListRegionsWithMultiAZOption() {
    String authToken = customer.createAuthToken();
    Region r1 = Region.create(provider, "region-1", "PlacementRegion 1");
    Region r2 = Region.create(provider, "region-2", "PlacementRegion 2");
    AvailabilityZone.create(r1, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    AvailabilityZone.create(r1, "PlacementAZ-1.2", "PlacementAZ 1.2", "Subnet - 1.2");
    AvailabilityZone.create(r1, "PlacementAZ-1.3", "PlacementAZ 1.3", "Subnet - 1.3");
    AvailabilityZone.create(r2, "PlacementAZ-2.1", "PlacementAZ 2.1", "Subnet - 2.1");
    AvailabilityZone.create(r2, "PlacementAZ-2.2", "PlacementAZ 2.2", "Subnet - 2.2");

    Http.RequestBuilder fr =
        fakeRequest("GET", "/api/providers/" + provider.uuid + "/regions?isMultiAZ=true")
        .header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r1.uuid.toString());
    assertEquals(json.get(0).path("code").asText(), r1.code);
    assertEquals(json.get(0).path("name").asText(), r1.name);
  }

  @Test
  public void testCreateRegionsWithInvalidProviderUUID() {
    String authToken = customer.createAuthToken();
    Http.RequestBuilder fr = fakeRequest("POST", "/api/providers/" + UUID.randomUUID() + "/regions")
                             .header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);

    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), CoreMatchers.containsString("Invalid Provider UUID"));
  }

  @Test
  public void testCreateRegionsWithoutRequiredParams() {
    String authToken = customer.createAuthToken();
    Http.RequestBuilder fr = fakeRequest("POST", "/api/providers/" + provider.uuid + "/regions")
                             .header("X-AUTH-TOKEN", authToken);
    Result result = route(fr);

    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
    assertThat(contentAsString(result), CoreMatchers.containsString("\"name\":[\"This field is required\"]"));
  }

  @Test
  public void testCreateRegionsWithValidProviderUUID() {
    String authToken = customer.createAuthToken();

    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("name", "Foo PlacementRegion");
    Http.RequestBuilder fr = fakeRequest("POST", "/api/providers/" + provider.uuid + "/regions")
                             .header("X-AUTH-TOKEN", authToken)
                             .bodyJson(regionJson);
    Result result = route(fr);

    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    assertThat(json.get("uuid").toString(), is(notNullValue()));
    assertThat(json.get("code").asText(), is(allOf(notNullValue(), instanceOf(String.class), equalTo("foo-region"))));
    assertThat(json.get("name").asText(), is(allOf(notNullValue(), instanceOf(String.class), equalTo("Foo PlacementRegion"))));
  }
}
