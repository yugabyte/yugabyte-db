// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.UUID;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
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
  Users defaultUser;
  Provider defaultProvider;
  Region defaultRegion;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
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
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testListEmptyAvailabilityZonesWithValidProviderRegionUUID() {
    JsonNode json = doListAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid, OK);
    assertTrue(json.isArray());
    assertAuditEntry(0, defaultCustomer.uuid);
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
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateAvailabilityZoneWithInvalidProviderRegionUUID() {
    JsonNode json =
        doCreateAZAndVerifyResult(UUID.randomUUID(), UUID.randomUUID(), null, BAD_REQUEST);
    assertEquals("Invalid PlacementRegion/Provider UUID", json.get("error").asText());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateAvailabilityZoneWithValidParams() {
    ObjectNode azRequestJson = Json.newObject();
    ArrayNode azs = Json.newArray();
    ObjectNode az1 = Json.newObject();
    az1.put("code", "foo-az-1");
    az1.put("name", "foo az 1");
    az1.put("subnet", "az subnet 1");
    azs.add(az1);
    ObjectNode az2 = Json.newObject();
    az2.put("code", "foo-az-2");
    az2.put("name", "foo az 2");
    az2.put("subnet", "az subnet 2");
    azs.add(az2);
    azRequestJson.set("availabilityZones", azs);

    JsonNode json = doCreateAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid,
        azRequestJson, OK);

    assertEquals(2, json.size());
    assertAuditEntry(1, defaultCustomer.uuid);

    assertThat(json.get("foo-az-1"), is(notNullValue()));
    assertThat(json.get("foo-az-1").get("uuid").toString(), is(notNullValue()));
    assertValue(json.get("foo-az-1"), "code", "foo-az-1");
    assertValue(json.get("foo-az-1"), "name", "foo az 1");
    assertValue(json.get("foo-az-1"), "subnet", "az subnet 1");

    assertThat(json.get("foo-az-2"), is(notNullValue()));
    assertThat(json.get("foo-az-2").get("uuid").toString(), is(notNullValue()));
    assertValue(json.get("foo-az-2"), "code", "foo-az-2");
    assertValue(json.get("foo-az-2"), "name", "foo az 2");
    assertValue(json.get("foo-az-2"), "subnet", "az subnet 2");
  }

  @Test
  public void testCreateAvailabilityZoneWithInvalidTopFormParams() {
    JsonNode json = doCreateAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid,
        Json.newObject(), BAD_REQUEST);
    assertErrorNodeValue(json, "availabilityZones", "This field is required");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteAvailabilityZoneWithInvalidParams() {
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doDeleteAZAndVerify(defaultProvider.uuid, defaultRegion.uuid,
        randomUUID, BAD_REQUEST);
    Assert.assertThat(json.get("error").toString(),
            CoreMatchers.containsString("Invalid Region/AZ UUID:" + randomUUID));
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteAvailabilityZoneWithValidParams() {
    AvailabilityZone az = AvailabilityZone.create(defaultRegion, "az-1", "AZ 1", "subnet-1");

    JsonNode json = doDeleteAZAndVerify(defaultProvider.uuid, defaultRegion.uuid, az.uuid, OK);
    az = AvailabilityZone.find.byId(az.uuid);
    assertTrue(json.get("success").asBoolean());
    assertAuditEntry(1, defaultCustomer.uuid);
    assertFalse(az.active);
  }

  private JsonNode doDeleteAZAndVerify(UUID providerUUID, UUID regionUUID,
                                       UUID zoneUUID, int expectedStatus) {
    String uri = "/api/customers/" + defaultCustomer.uuid +
        "/providers/" + providerUUID + "/regions/" + regionUUID + "/zones/" + zoneUUID;
    Result result = FakeApiHelper.doRequest("DELETE", uri);
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doListAZAndVerifyResult(UUID cloudProvider, UUID region, int expectedStatus) {
    String uri = "/api/customers/" + defaultCustomer.uuid +
        "/providers/" + cloudProvider + "/regions/" + region + "/zones";
    Result result = FakeApiHelper.doRequest("GET", uri);
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doCreateAZAndVerifyResult(UUID cloudProvider,
                                             UUID region,
                                             ObjectNode azRequestJson,
                                             int expectedStatus) {

    String uri = "/api/customers/" + defaultCustomer.uuid +
        "/providers/" + cloudProvider + "/regions/" + region + "/zones";

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
