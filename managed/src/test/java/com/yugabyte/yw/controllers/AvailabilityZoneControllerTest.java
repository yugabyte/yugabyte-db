// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import java.util.UUID;
import org.hamcrest.CoreMatchers;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
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
    defaultRegion =
        Region.create(
            defaultProvider, "default-region", "Default PlacementRegion", "default-image");
  }

  @Test
  public void testListAvailabilityZonesWithInvalidProviderRegionUUID() {
    UUID providerUUID = UUID.randomUUID();
    UUID regionUUID = UUID.randomUUID();
    JsonNode json = doListAZAndVerifyResult(providerUUID, regionUUID, BAD_REQUEST, true);
    assertEquals("Invalid Provider/Region UUID", json.get("error").asText());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testListEmptyAvailabilityZonesWithValidProviderRegionUUID() {
    JsonNode json = doListAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid, OK, false);
    assertTrue(json.isArray());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testListAvailabilityZonesWithValidProviderRegionUUID() {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(
            defaultRegion, "PlacementAZ-1", "PlacementAZ One", "Subnet 1");
    JsonNode json = doListAZAndVerifyResult(defaultProvider.uuid, defaultRegion.uuid, OK, false);

    assertEquals(1, json.size());
    assertEquals(az.uuid.toString(), json.get(0).findValue("uuid").asText());
    assertEquals(az.code, json.get(0).findValue("code").asText());
    assertEquals(az.name, json.get(0).findValue("name").asText());
    assertTrue(json.get(0).findValue("active").asBoolean());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateAvailabilityZoneWithInvalidProviderRegionUUID() {
    UUID providerUUID = UUID.randomUUID();
    UUID regionUUID = UUID.randomUUID();
    JsonNode json = doCreateAZAndVerifyResult(providerUUID, regionUUID, null, BAD_REQUEST, true);
    assertEquals("Invalid Provider/Region UUID", json.get("error").asText());
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

    JsonNode json =
        doCreateAZAndVerifyResult(
            defaultProvider.uuid, defaultRegion.uuid, azRequestJson, OK, false);

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
    JsonNode json =
        doCreateAZAndVerifyResult(
            defaultProvider.uuid, defaultRegion.uuid, Json.newObject(), BAD_REQUEST, true);
    assertErrorNodeValue(json, "availabilityZones", "This field is required");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteAvailabilityZoneWithInvalidParams() {
    UUID randomUUID = UUID.randomUUID();
    JsonNode json =
        doDeleteAZAndVerify(
            defaultProvider.uuid, defaultRegion.uuid, randomUUID, BAD_REQUEST, true);
    Assert.assertThat(
        json.get("error").toString(),
        CoreMatchers.containsString("Invalid Region/AZ UUID:" + randomUUID));
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteAvailabilityZoneWithValidParams() {
    AvailabilityZone az = AvailabilityZone.createOrThrow(defaultRegion, "az-1", "AZ 1", "subnet-1");

    JsonNode json =
        doDeleteAZAndVerify(defaultProvider.uuid, defaultRegion.uuid, az.uuid, OK, false);
    az = AvailabilityZone.find.byId(az.uuid);
    assertTrue(json.get("success").asBoolean());
    assertAuditEntry(1, defaultCustomer.uuid);
    assertFalse(az.active);
  }

  private JsonNode doDeleteAZAndVerify(
      UUID providerUUID,
      UUID regionUUID,
      UUID zoneUUID,
      int expectedStatus,
      boolean isYWServiceException) {
    String uri =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/providers/"
            + providerUUID
            + "/regions/"
            + regionUUID
            + "/zones/"
            + zoneUUID;
    Result result;
    if (isYWServiceException) {
      result = assertPlatformException(() -> FakeApiHelper.doRequest("DELETE", uri));
    } else {
      result = FakeApiHelper.doRequest("DELETE", uri);
    }
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doListAZAndVerifyResult(
      UUID cloudProvider, UUID region, int expectedStatus, boolean isYWServiceException) {
    String uri =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/providers/"
            + cloudProvider
            + "/regions/"
            + region
            + "/zones";
    Result result;
    if (isYWServiceException) {
      result = assertPlatformException(() -> FakeApiHelper.doRequest("GET", uri));
    } else {
      result = FakeApiHelper.doRequest("GET", uri);
    }
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doCreateAZAndVerifyResult(
      UUID cloudProvider,
      UUID region,
      ObjectNode azRequestJson,
      int expectedStatus,
      boolean isYWServiceException) {

    String uri =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/providers/"
            + cloudProvider
            + "/regions/"
            + region
            + "/zones";

    Result result;
    if (azRequestJson != null) {
      if (isYWServiceException) {
        result =
            assertPlatformException(
                () -> FakeApiHelper.doRequestWithBody("POST", uri, azRequestJson));
      } else {
        result = FakeApiHelper.doRequestWithBody("POST", uri, azRequestJson);
      }
    } else {
      if (isYWServiceException) {
        result = assertPlatformException(() -> FakeApiHelper.doRequest("POST", uri));
      } else {
        result = FakeApiHelper.doRequest("POST", uri);
      }
    }
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }
}
