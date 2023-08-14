// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertNoKey;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.AvailabilityZoneEditData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Set;
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

  private Result createZoneV2(UUID providerUUID, UUID regionUUID, JsonNode body) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/provider_regions/%s/region_zones",
            defaultCustomer.getUuid(), providerUUID, regionUUID);
    return doRequestWithBody("POST", uri, body);
  }

  @Test
  public void testListAvailabilityZonesWithInvalidProviderRegionUUID() {
    UUID providerUUID = UUID.randomUUID();
    UUID regionUUID = UUID.randomUUID();
    JsonNode json = doListAZAndVerifyResult(providerUUID, regionUUID, BAD_REQUEST, true);
    assertEquals("Invalid Provider/Region UUID", json.get("error").asText());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListEmptyAvailabilityZonesWithValidProviderRegionUUID() {
    JsonNode json =
        doListAZAndVerifyResult(defaultProvider.getUuid(), defaultRegion.getUuid(), OK, false);
    assertTrue(json.isArray());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListAvailabilityZonesWithValidProviderRegionUUID() {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(
            defaultRegion, "PlacementAZ-1", "PlacementAZ One", "Subnet 1");
    JsonNode json =
        doListAZAndVerifyResult(defaultProvider.getUuid(), defaultRegion.getUuid(), OK, false);

    assertEquals(1, json.size());
    assertEquals(az.getUuid().toString(), json.get(0).findValue("uuid").asText());
    assertEquals(az.getCode(), json.get(0).findValue("code").asText());
    assertEquals(az.getName(), json.get(0).findValue("name").asText());
    assertTrue(json.get(0).findValue("active").asBoolean());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateAvailabilityZoneWithInvalidProviderRegionUUID() {
    UUID providerUUID = UUID.randomUUID();
    UUID regionUUID = UUID.randomUUID();
    JsonNode json = doCreateAZAndVerifyResult(providerUUID, regionUUID, null, BAD_REQUEST, true);
    assertEquals("Invalid Provider/Region UUID", json.get("error").asText());
    assertAuditEntry(0, defaultCustomer.getUuid());
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
            defaultProvider.getUuid(), defaultRegion.getUuid(), azRequestJson, OK, false);

    assertEquals(2, json.size());
    assertAuditEntry(1, defaultCustomer.getUuid());

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
  public void testCreateAvailabilityZoneWithInValidParams() {
    Provider onPremProvider = ModelFactory.onpremProvider(defaultCustomer);
    Region onPremProviderRegion =
        Region.create(onPremProvider, "default-region", "Default PlacementRegion", null);
    ObjectNode azRequestJson = Json.newObject();
    ArrayNode azs = Json.newArray();
    ObjectNode az1 = Json.newObject();
    az1.put("code", "foo-az-1");
    az1.put("name", "foo az 1");
    azs.add(az1);
    ObjectNode az2 = Json.newObject();
    az2.put("code", "foo-az-2");
    az2.put("name", "foo az %%^&S 2");
    azs.add(az2);
    azRequestJson.set("availabilityZones", azs);

    JsonNode json =
        doCreateAZAndVerifyResult(
            onPremProvider.getUuid(),
            onPremProviderRegion.getUuid(),
            azRequestJson,
            BAD_REQUEST,
            true);

    assertEquals("providerValidation", json.get("error").get("errorSource").get(0).asText());
    assertEquals(
        "Zone name cannot contain any special characters except '-' and '_'.",
        json.get("error").get("data.ZONE").get(0).asText());
  }

  @Test
  public void testCreateAvailabilityZoneWithInvalidTopFormParams() {
    JsonNode json =
        doCreateAZAndVerifyResult(
            defaultProvider.getUuid(),
            defaultRegion.getUuid(),
            Json.newObject(),
            BAD_REQUEST,
            true);
    assertErrorNodeValue(json, "availabilityZones", "This field is required");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteAvailabilityZoneWithInvalidParams() {
    UUID randomUUID = UUID.randomUUID();
    JsonNode json =
        doDeleteAZAndVerify(
            defaultProvider.getUuid(), defaultRegion.getUuid(), randomUUID, BAD_REQUEST, true);
    Assert.assertThat(
        json.get("error").toString(),
        CoreMatchers.containsString("Invalid Region/AZ UUID:" + randomUUID));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteAvailabilityZoneWithValidParams() {
    AvailabilityZone az = AvailabilityZone.createOrThrow(defaultRegion, "az-1", "AZ 1", "subnet-1");

    JsonNode json =
        doDeleteAZAndVerify(
            defaultProvider.getUuid(), defaultRegion.getUuid(), az.getUuid(), OK, false);

    assertTrue(json.get("success").asBoolean());
    assertAuditEntry(1, defaultCustomer.getUuid());
    assertFalse(AvailabilityZone.find.byId(az.getUuid()).isActive());
  }

  private void createUniverseInAZ(UUID azUUID) {
    Universe universe =
        ModelFactory.createUniverse(Customer.get(defaultProvider.getCustomerUUID()).getId());
    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    Set<NodeDetails> nodeDetailsSet = ApiUtils.getDummyNodeDetailSet(UUID.randomUUID(), 3, 3);
    nodeDetailsSet.forEach(nd -> nd.azUuid = azUUID);
    udtp.nodeDetailsSet = nodeDetailsSet;
    universe.setUniverseDetails(udtp);
    universe.update();
  }

  @Test
  public void testDeleteAvailabilityZoneInUse() {
    AvailabilityZone az = AvailabilityZone.createOrThrow(defaultRegion, "az-1", "AZ 1", "subnet-1");
    UUID uuid = az.getUuid();

    createUniverseInAZ(uuid);

    doDeleteAZAndVerify(defaultProvider.getUuid(), defaultRegion.getUuid(), uuid, FORBIDDEN, true);

    assertNotNull(AvailabilityZone.find.byId(uuid));
  }

  @Test
  public void testEditAvailabilityZoneInUse() {
    AvailabilityZone az = AvailabilityZone.createOrThrow(defaultRegion, "az-1", "AZ 1", "subnet-1");
    UUID uuid = az.getUuid();

    createUniverseInAZ(uuid);

    AvailabilityZoneEditData editData = new AvailabilityZoneEditData();
    editData.subnet = "subnet-2";
    editData.secondarySubnet = "secondarySubnet";
    ObjectNode requestBody = (ObjectNode) Json.toJson(editData);

    doEditAZAndVerifyResult(
        defaultProvider.getUuid(), defaultRegion.getUuid(), uuid, requestBody, FORBIDDEN, true);
  }

  @Test
  public void testEditAvailabilityZone() {
    AvailabilityZone az = AvailabilityZone.createOrThrow(defaultRegion, "az-1", "AZ 1", "subnet-1");
    AvailabilityZoneEditData editData = new AvailabilityZoneEditData();

    editData.subnet = "subnet-2";
    editData.secondarySubnet = "secondarySubnet";
    ObjectNode requestBody = (ObjectNode) Json.toJson(editData);

    JsonNode response =
        doEditAZAndVerifyResult(
            defaultProvider.getUuid(), defaultRegion.getUuid(), az.getUuid(), requestBody);
    assertValue(response, "secondarySubnet", "secondarySubnet");
    assertValue(response, "subnet", "subnet-2");
    az.refresh();
    assertEquals("subnet-2", az.getSubnet());
    assertEquals("secondarySubnet", az.getSecondarySubnet());

    requestBody.put("subnet", "subnet-3");
    response =
        doEditAZAndVerifyResult(
            defaultProvider.getUuid(), defaultRegion.getUuid(), az.getUuid(), requestBody);
    assertValue(response, "secondarySubnet", "secondarySubnet");
    assertValue(response, "subnet", "subnet-3");
    az.refresh();
    assertEquals("subnet-3", az.getSubnet());
    assertEquals("secondarySubnet", az.getSecondarySubnet());

    requestBody.put("secondarySubnet", (String) null);
    response =
        doEditAZAndVerifyResult(
            defaultProvider.getUuid(), defaultRegion.getUuid(), az.getUuid(), requestBody);
    assertNoKey(response, "secondarySubnet");
    az.refresh();
    assertNull(az.getSecondarySubnet());
  }

  @Test
  public void testCreateZoneV2Payload() {
    JsonNode azBody = generateAZRequestBody(defaultRegion);
    Result result = createZoneV2(defaultProvider.getUuid(), defaultRegion.getUuid(), azBody);
    AvailabilityZone zone =
        Json.fromJson(Json.parse(contentAsString(result)), AvailabilityZone.class);
    assertEquals(OK, result.status());
    assertNotNull("Region not created, empty UUID.", zone.getUuid());

    Region region =
        Region.getOrBadRequest(
            defaultCustomer.getUuid(), defaultProvider.getUuid(), defaultRegion.getUuid());
    assertEquals(1, region.getZones().size());
  }

  public JsonNode generateAZRequestBody(Region region) {
    ObjectNode azRequestBody = Json.newObject();
    azRequestBody.put("code", "us-west-2a");
    azRequestBody.put("name", "us-west-2a");
    azRequestBody.put("subnet", "subnet");
    azRequestBody.set("region", Json.toJson(region));

    return azRequestBody;
  }

  private JsonNode doDeleteAZAndVerify(
      UUID providerUUID,
      UUID regionUUID,
      UUID zoneUUID,
      int expectedStatus,
      boolean isYWServiceException) {
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + providerUUID
            + "/regions/"
            + regionUUID
            + "/zones/"
            + zoneUUID;
    Result result;
    if (isYWServiceException) {
      result = assertPlatformException(() -> doRequest("DELETE", uri));
    } else {
      result = doRequest("DELETE", uri);
    }
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doListAZAndVerifyResult(
      UUID cloudProvider, UUID region, int expectedStatus, boolean isYWServiceException) {
    String uri =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/providers/"
            + cloudProvider
            + "/regions/"
            + region
            + "/zones";
    Result result;
    if (isYWServiceException) {
      result = assertPlatformException(() -> doRequest("GET", uri));
    } else {
      result = doRequest("GET", uri);
    }
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doEditAZAndVerifyResult(
      UUID cloudProvider, UUID region, UUID az, JsonNode azRequestJson) {
    return doEditAZAndVerifyResult(cloudProvider, region, az, azRequestJson, OK, false);
  }

  private JsonNode doEditAZAndVerifyResult(
      UUID cloudProvider,
      UUID region,
      UUID az,
      JsonNode azRequestJson,
      int expectedStatus,
      boolean isYWServiceException) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/regions/%s/zones/%s",
            defaultCustomer.getUuid(), cloudProvider, region, az);
    Result result;

    if (isYWServiceException) {
      result = assertPlatformException(() -> doRequestWithBody("PUT", uri, azRequestJson));
    } else {
      result = doRequestWithBody("PUT", uri, azRequestJson);
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
        String.format(
            "/api/customers/%s/providers/%s/regions/%s/zones",
            defaultCustomer.getUuid(), cloudProvider, region);

    Result result;
    if (azRequestJson != null) {
      if (isYWServiceException) {
        result = assertPlatformException(() -> doRequestWithBody("POST", uri, azRequestJson));
      } else {
        result = doRequestWithBody("POST", uri, azRequestJson);
      }
    } else {
      if (isYWServiceException) {
        result = assertPlatformException(() -> doRequest("POST", uri));
      } else {
        result = doRequest("POST", uri);
      }
    }
    assertEquals(expectedStatus, result.status());
    return Json.parse(contentAsString(result));
  }
}
