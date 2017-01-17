// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.api.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.IsInstanceOf.instanceOf;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.List;
import java.util.UUID;

import com.yugabyte.yw.common.FakeApiHelper;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import play.libs.Json;
import play.mvc.Result;

public class RegionControllerTest extends FakeDBApplication {
  Provider provider;

  @Before
  public void setUp() {
    provider = Provider.create("aws", "Amazon");
  }

  @Test
  public void testListRegionsWithInvalidProviderUUID() {
    String uri = "/api/providers/" + UUID.randomUUID() + "/regions";
    Result result = FakeApiHelper.requestWithAuthToken("GET", uri);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListEmptyRegionsWithValidProviderUUID() {
    String uri = "/api/providers/" + provider.uuid + "/regions";
    Result result = FakeApiHelper.requestWithAuthToken("GET", uri);

    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListAllRegionsWithValidRegion() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone.create(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    Result result = FakeApiHelper.requestWithAuthToken("GET", "/api/regions");
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get(0).path("uuid").asText(), r.uuid.toString());
    assertEquals(json.get(0).path("provider").get("uuid").asText(), provider.uuid.toString());
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
  }

  @Test
  public void testListAllRegionsWithNoRegion() {
    Result result = FakeApiHelper.requestWithAuthToken("GET", "/api/regions");
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListRegionWithoutZonesAndValidProviderUUID() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    String uri = "/api/providers/" + provider.uuid + "/regions";
    Result result = FakeApiHelper.requestWithAuthToken("GET", uri);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListRegionsWithValidProviderUUID() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone.create(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    String uri =  "/api/providers/" + provider.uuid + "/regions";
    Result result = FakeApiHelper.requestWithAuthToken("GET", uri);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());

    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r.uuid.toString());
    assertEquals(json.get(0).path("code").asText(), r.code);
    assertEquals(json.get(0).path("name").asText(), r.name);
  }

  @Test
  public void testListRegionsWithMultiAZOption() {
    Region r1 = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    Region r2 = Region.create(provider, "region-2", "PlacementRegion 2", "default-image");
    AvailabilityZone.create(r1, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    AvailabilityZone.create(r1, "PlacementAZ-1.2", "PlacementAZ 1.2", "Subnet - 1.2");
    AvailabilityZone.create(r1, "PlacementAZ-1.3", "PlacementAZ 1.3", "Subnet - 1.3");
    AvailabilityZone.create(r2, "PlacementAZ-2.1", "PlacementAZ 2.1", "Subnet - 2.1");
    AvailabilityZone.create(r2, "PlacementAZ-2.2", "PlacementAZ 2.2", "Subnet - 2.2");

    String uri =  "/api/providers/" + provider.uuid + "/regions?isMultiAZ=true";
    Result result = FakeApiHelper.requestWithAuthToken("GET", uri);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r1.uuid.toString());
    assertEquals(json.get(0).path("code").asText(), r1.code);
    assertEquals(json.get(0).path("name").asText(), r1.name);
  }

  @Test
  public void testCreateRegionsWithInvalidProviderUUID() {
    String uri =  "/api/providers/" + UUID.randomUUID() + "/regions";
    Result result = FakeApiHelper.requestWithAuthToken("POST", uri);
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), CoreMatchers.containsString("Invalid Provider UUID"));
  }

  @Test
  public void testCreateRegionsWithoutRequiredParams() {
    String uri =  "/api/providers/" + provider.uuid + "/regions";
    Result result = FakeApiHelper.requestWithAuthToken("POST", uri);
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result),
            CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
    assertThat(contentAsString(result),
            CoreMatchers.containsString("\"name\":[\"This field is required\"]"));
  }

  @Test
  public void testCreateRegionsWithValidProviderUUID() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("name", "Foo PlacementRegion");

    String uri =  "/api/providers/" + provider.uuid + "/regions";
    Result result = FakeApiHelper.requestWithAuthToken("POST", uri, regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    assertThat(json.get("uuid").toString(), is(notNullValue()));
    assertThat(json.get("code").asText(),
            is(allOf(notNullValue(), instanceOf(String.class), equalTo("foo-region"))));
    assertThat(json.get("name").asText(),
            is(allOf(notNullValue(), instanceOf(String.class), equalTo("Foo PlacementRegion"))));
  }

  @Test
  public void testDeleteRegionWithInValidParams() {
    UUID randomUUID = UUID.randomUUID();
    String uri =  "/api/providers/" + provider.uuid + "/regions/" + randomUUID;
    Result result = FakeApiHelper.requestWithAuthToken("DELETE", uri);
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result),
            CoreMatchers.containsString("Invalid Provider/Region UUID:" + randomUUID));
  }

  @Test
  public void testDeleteRegionWithValidParams() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "AZ 2", "subnet-2");

    String uri =  "/api/providers/" + provider.uuid + "/regions/" + r.uuid;
    Result result = FakeApiHelper.requestWithAuthToken("DELETE", uri);
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("success").asBoolean());

    r = Region.get(r.uuid);
    assertFalse(r.active);

    List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(r.uuid);
    for (AvailabilityZone az: zones) {
      assertFalse(az.active);
    }
  }
}
