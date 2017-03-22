// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertErrorResponse;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.core.IsInstanceOf.instanceOf;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.YugawareProperty;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import org.mockito.Mockito;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

public class RegionControllerTest extends FakeDBApplication {
  Provider provider;
  Customer customer;

  NetworkManager networkManager;

  @Override
  protected Application provideApplication() {
    networkManager = mock(NetworkManager.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(NetworkManager.class).toInstance(networkManager))
        .build();
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
  }

  private Result listRegions(UUID providerUUID, boolean multiAZ) {
    String uri = "/api/customers/" + customer.uuid +
        "/providers/" + providerUUID + "/regions";
    if (multiAZ) {
      uri += "?isMultiAZ=true";
    }
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result listAllRegions() {
    String uri = "/api/customers/" + customer.uuid +
        "/regions";
    return FakeApiHelper.doRequest("GET", uri);
  }

  private Result createRegion(UUID providerUUID, JsonNode body) {
    String uri =  "/api/customers/" + customer.uuid +
        "/providers/" + providerUUID + "/regions";
    return FakeApiHelper.doRequestWithBody("POST", uri, body);
  }

  private Result deleteRegion(UUID providerUUID, UUID regionUUID) {
    String uri =  "/api/customers/" + customer.uuid +
        "/providers/" + providerUUID + "/regions/" + regionUUID;
    return FakeApiHelper.doRequest("DELETE", uri);
  }

  @Test
  public void testListRegionsWithInvalidProviderUUID() {
    Result result = listRegions(UUID.randomUUID(), false);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(0, json.size());
  }

  @Test
  public void testListEmptyRegionsWithValidProviderUUID() {
    Result result = listRegions(provider.uuid, false);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(0, json.size());
  }

  @Test
  public void testListAllRegionsWithValidRegion() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone az = AvailabilityZone.create(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    Result result = listAllRegions();
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    JsonNode regionJson = json.get(0);
    JsonNode providerJson = regionJson.get("provider");
    JsonNode zonesJson = regionJson.get("zones");
    assertNotNull(providerJson);
    assertEquals(1, zonesJson.size());
    assertValue(regionJson, "uuid", r.uuid.toString());
    assertValue(providerJson, "uuid", provider.uuid.toString());
    assertValue(zonesJson.get(0), "uuid", az.uuid.toString());
    assertValue(zonesJson.get(0), "code", az.code);
    assertValue(zonesJson.get(0), "subnet", az.subnet);
  }

  @Test
  public void testListAllRegionsWithNoRegion() {
    Result result = listAllRegions();
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListRegionWithoutZonesAndValidProviderUUID() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    Result result = listRegions(provider.uuid, false);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
  }

  @Test
  public void testListRegionsWithValidProviderUUID() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone.create(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    Result result = listRegions(provider.uuid, false);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r.uuid.toString());
    assertEquals(json.get(0).path("code").asText(), r.code);
    assertEquals(json.get(0).path("name").asText(), r.name);
    assertThat(json.get(0).path("zones"), allOf(notNullValue(), instanceOf(ArrayNode.class)));
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
    Result result = listRegions(provider.uuid, true);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r1.uuid.toString());
    assertEquals(json.get(0).path("code").asText(), r1.code);
    assertEquals(json.get(0).path("name").asText(), r1.name);
    assertEquals(3, json.get(0).path("zones").size());
  }


  @Test
  public void testCreateRegionsWithInvalidProviderUUID() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    UUID randomUUID = UUID.randomUUID();
    Result result = createRegion(randomUUID, regionJson);
    assertEquals(BAD_REQUEST, result.status());
    assertErrorResponse(result, "Invalid Provider UUID:" + randomUUID);
  }

  @Test
  public void testCreateRegionsWithoutRequiredParams() {
    Result result = createRegion(provider.uuid, Json.newObject());
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result),
            CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
  }

  @Test
  public void testCreateRegionsWithValidProviderUUID() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("name", "Foo PlacementRegion");
    Result result = createRegion(provider.uuid, regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), is(notNullValue()));
    assertValue(json, "code", "foo-region");
    assertValue(json, "name", "Foo PlacementRegion");
  }

  @Test
  public void testCreateRegionWithMetadataValidVPCInfo() {
    YugawareProperty.addConfigProperty(ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"foo-region\": {\"name\": \"Foo Region\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    JsonNode vpcInfo = Json.parse("{\"foo-region\": {\"zones\": {\"zone-1\": \"subnet-1\"}}}");
    Mockito.when(networkManager.bootstrap(any(UUID.class))).thenReturn(vpcInfo);
    Result result = createRegion(provider.uuid, regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), is(notNullValue()));
    assertValue(json, "code", "foo-region");
    assertValue(json, "name", "Foo Region");
    assertValue(json, "ybImage", "yb image");
    assertNotNull(json.get("zones"));
    Region r = Region.getByCode(provider, "foo-region");
    assertEquals(1, r.zones.size());
  }

  @Test
  public void testCreateRegionWithMetadataInvalidVPCInfo() {
    YugawareProperty.addConfigProperty(ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"foo-region\": {\"name\": \"Foo Region\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    ObjectNode vpcInfo = Json.newObject();
    vpcInfo.put("error", "Something went wrong!!.");
    Mockito.when(networkManager.bootstrap(any(UUID.class))).thenReturn(vpcInfo);
    Result result = createRegion(provider.uuid, regionJson);
    assertInternalServerError(result, "Region Bootstrap failed.");
    Region r = Region.getByCode(provider, "foo-region");
    assertNull(r);
  }

  @Test
  public void testDeleteRegionWithInValidParams() {
    UUID randomUUID = UUID.randomUUID();
    Result result = deleteRegion(provider.uuid, randomUUID);
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result),
            CoreMatchers.containsString("Invalid Provider/Region UUID:" + randomUUID));
  }

  @Test
  public void testDeleteRegionWithValidParams() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "AZ 2", "subnet-2");
    Result result = deleteRegion(provider.uuid, r.uuid);
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
