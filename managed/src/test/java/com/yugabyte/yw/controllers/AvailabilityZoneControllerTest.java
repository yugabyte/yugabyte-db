// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.IsInstanceOf.instanceOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.UUID;

import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import org.hamcrest.CoreMatchers;
import org.junit.Assert;
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

public class AvailabilityZoneControllerTest extends FakeDBApplication {
  Customer defaultCustomer;
  Provider defaultProvider;
  Region defaultRegion;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultRegion = Region.create(defaultProvider,
                                  "default-region",
                                  "Default PlacementRegion",
                                  "default-image");
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

    String uri =
        "/api/providers/" + defaultProvider.uuid + "/regions/" + defaultRegion.uuid + "/zones";
    Result result = FakeApiHelper.doRequestWithBody("POST", uri, azRequestJson);
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result),
                CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
    assertThat(contentAsString(result),
               CoreMatchers.containsString("\"name\":[\"This field is required\"]"));
    assertThat(contentAsString(result),
               CoreMatchers.containsString("\"subnet\":[\"This field is required\"]"));
  }

  @Test
  public void testDeleteAvailabilityZoneWithInValidParams() {
    UUID randomUUID = UUID.randomUUID();
    Result result =
            FakeApiHelper.doRequest("DELETE", "/api/providers/" + defaultProvider.uuid +
                    "/regions/" + defaultRegion.uuid + "/zones/" + randomUUID);

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    Assert.assertThat(json.get("error").toString(),
            CoreMatchers.containsString("Invalid Region/AZ UUID:" + randomUUID));
  }

  @Test
  public void testDeleteAvailabilityZoneWithValidParams() {
    AvailabilityZone az = AvailabilityZone.create(defaultRegion, "az-1", "AZ 1", "subnet-1");

    Result result =
            FakeApiHelper.doRequest("DELETE", "/api/providers/" + defaultProvider.uuid +
                    "/regions/" + defaultRegion.uuid + "/zones/" + az.uuid);

    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    az = AvailabilityZone.find.byId(az.uuid);
    assertTrue(json.get("success").asBoolean());
    assertFalse(az.active);
  }

  private JsonNode doListAZAndVerifyResult(UUID cloudProvider, UUID region, int expectedStatus) {
    String uri = "/api/providers/" + cloudProvider + "/regions/" + region + "/zones";
    Result result = FakeApiHelper.doRequest("GET", uri);
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doCreateAZAndVerifyResult(UUID cloudProvider,
                                             UUID region,
                                             ObjectNode azRequestJson,
                                             int expectedStatus) {

    String uri = "/api/providers/" + cloudProvider + "/regions/" + region + "/zones";

    Result result;
    if (azRequestJson != null) {
      result = FakeApiHelper.doRequestWithBody("POST", uri, azRequestJson);
    } else {
      result = FakeApiHelper.doRequest("POST", uri);
    }
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }
}
