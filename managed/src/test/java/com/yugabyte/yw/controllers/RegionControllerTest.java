// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertErrorResponse;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.hamcrest.CoreMatchers;
import org.hamcrest.core.AllOf;
import org.hamcrest.core.Is;
import org.hamcrest.core.IsInstanceOf;
import org.hamcrest.core.IsNull;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class RegionControllerTest extends FakeDBApplication {
  Provider provider;
  Customer customer;
  Users user;
  SettableRuntimeConfigFactory runtimeConfigFactory;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    provider = ModelFactory.awsProvider(customer);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.useLegacyPayloadForRegionAndAZs.getKey(), "true");
  }

  private Result listRegions(UUID providerUUID) {
    String uri =
        String.format("/api/customers/%s/providers/%s/regions", customer.getUuid(), providerUUID);
    return doRequest("GET", uri);
  }

  private Result listAllRegions() {
    String uri = String.format("/api/customers/%s/regions", customer.getUuid());
    return doRequest("GET", uri);
  }

  private Result createRegion(UUID providerUUID, JsonNode body) {
    String uri =
        String.format("/api/customers/%s/providers/%s/regions", customer.getUuid(), providerUUID);
    return doRequestWithBody("POST", uri, body);
  }

  private Result deleteRegion(UUID providerUUID, UUID regionUUID) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/regions/%s",
            customer.getUuid(), providerUUID, regionUUID);
    return doRequest("DELETE", uri);
  }

  private Result editRegion(UUID providerUUID, UUID regionUUID, JsonNode body) {
    String uri =
        String.format(
            "/api/customers/%s/providers/%s/regions/%s",
            customer.getUuid(), providerUUID, regionUUID);
    return doRequestWithBody("PUT", uri, body);
  }

  @Test
  public void testListRegionsWithInvalidProviderUUID() {
    Result result = listRegions(UUID.randomUUID());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListEmptyRegionsWithValidProviderUUID() {
    Result result = listRegions(provider.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListAllRegionsWithValidRegion() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    Result result = listAllRegions();
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    JsonNode regionJson = json.get(0);
    JsonNode providerJson = regionJson.get("provider");
    JsonNode zonesJson = regionJson.get("zones");
    assertNotNull(providerJson);
    assertEquals(1, zonesJson.size());
    assertValue(regionJson, "uuid", r.getUuid().toString());
    assertValue(providerJson, "uuid", provider.getUuid().toString());
    assertValue(zonesJson.get(0), "uuid", az.getUuid().toString());
    assertValue(zonesJson.get(0), "code", az.getCode());
    assertValue(zonesJson.get(0), "subnet", az.getSubnet());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListAllRegionsWithNoRegion() {
    Result result = listAllRegions();
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListRegionWithoutZonesAndValidProviderUUID() {
    Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    Result result = listRegions(provider.getUuid());
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertEquals("[]", json.toString());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListRegionsWithValidProviderUUID() {
    Region r = Region.create(provider, "foo-region", "Foo PlacementRegion", "default-image");
    AvailabilityZone.createOrThrow(r, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    Result result = listRegions(provider.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(1, json.size());
    assertEquals(json.get(0).path("uuid").asText(), r.getUuid().toString());
    assertEquals(json.get(0).path("code").asText(), r.getCode());
    assertEquals(json.get(0).path("name").asText(), r.getName());
    assertThat(
        json.get(0).path("zones"),
        AllOf.allOf(IsNull.notNullValue(), IsInstanceOf.instanceOf(JsonNode.class)));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListRegions() {
    Region r1 = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    Region r2 = Region.create(provider, "region-2", "PlacementRegion 2", "default-image");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ-1.1", "PlacementAZ 1.1", "Subnet - 1.1");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ-1.2", "PlacementAZ 1.2", "Subnet - 1.2");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ-1.3", "PlacementAZ 1.3", "Subnet - 1.3");
    AvailabilityZone.createOrThrow(r2, "PlacementAZ-2.1", "PlacementAZ 2.1", "Subnet - 2.1");
    AvailabilityZone.createOrThrow(r2, "PlacementAZ-2.2", "PlacementAZ 2.2", "Subnet - 2.2");
    Result result = listRegions(provider.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(2, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateRegionsWithInvalidProviderUUID() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    UUID randomUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> createRegion(randomUUID, regionJson));
    assertEquals(BAD_REQUEST, result.status());
    assertErrorResponse(result, "Invalid Provider UUID: " + randomUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateRegionsWithoutRequiredParams() {
    Result result =
        assertPlatformException(() -> createRegion(provider.getUuid(), Json.newObject()));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        contentAsString(result),
        CoreMatchers.containsString("\"code\":[\"This field is required\"]"));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateRegionsWithValidProviderUUID() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("name", "Foo PlacementRegion");
    Result result = createRegion(provider.getUuid(), regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), Is.is(IsNull.notNullValue()));
    assertValue(json, "code", "foo-region");
    assertValue(json, "name", "Foo PlacementRegion");
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateRegionsWithValidGCPRegion() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "us-west1");
    regionJson.put("name", "Gcp US West 1");
    Provider gcpProvider = ModelFactory.gcpProvider(customer);
    Result result = createRegion(gcpProvider.getUuid(), regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), Is.is(IsNull.notNullValue()));
    assertValue(json, "code", "us-west1");
    assertValue(json, "name", "Gcp US West 1");
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateRegionWithMetadataValidVPCInfo() {
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"foo-region\": {\"name\": \"Foo Region\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("hostVpcRegion", "host-vpc-region");
    regionJson.put("hostVpcId", "host-vpc-id");
    regionJson.put("destVpcId", "dest-vpc-id");
    JsonNode vpcInfo = Json.parse("{\"foo-region\": {\"zones\": {\"zone-1\": \"subnet-1\"}}}");
    // TODO:
    when(mockNetworkManager.bootstrap(any(), any(), any())).thenReturn(vpcInfo);
    Result result = createRegion(provider.getUuid(), regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode details = json.get("details");
    JsonNode cloudInfo = details.get("cloudInfo");
    JsonNode awsRegionCloudInfo = cloudInfo.get("aws");
    assertEquals(OK, result.status());
    assertThat(json.get("uuid").toString(), Is.is(IsNull.notNullValue()));
    assertValue(json, "code", "foo-region");
    assertValue(json, "name", "Foo Region");
    assertValue(awsRegionCloudInfo, "ybImage", "yb image");
    assertNotNull(json.get("zones"));
    Region r = Region.getByCode(provider, "foo-region");
    assertEquals(1, r.getZones().size());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateRegionWithMetadataInvalidVPCInfo() {
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"foo-region\": {\"name\": \"Foo Region\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "foo-region");
    regionJson.put("hostVpcRegion", "host-vpc-region");
    regionJson.put("hostVpcId", "host-vpc-id");
    regionJson.put("destVpcId", "dest-vpc-id");
    ObjectNode vpcInfo = Json.newObject();
    vpcInfo.put("error", "Something went wrong!!.");
    when(mockNetworkManager.bootstrap(any(), any(), any())).thenReturn(vpcInfo);
    Result result = assertPlatformException(() -> createRegion(provider.getUuid(), regionJson));
    assertInternalServerError(result, "Region Bootstrap failed.");
    Region r = Region.getByCode(provider, "foo-region");
    assertNull(r);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateRegionsWithLongRegionName() {
    ObjectNode regionJson = Json.newObject();
    regionJson.put("code", "datacenter-azure-washington");
    regionJson.put("name", "Gcp US West 1");
    Result result = assertPlatformException(() -> createRegion(provider.getUuid(), regionJson));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "success", "false");
    assertNotNull(json.get("error"));
    assertEquals(json.get("error").get("code").get(0).asText(), "Maximum length is 25");
  }

  @Test
  public void testDeleteRegionWithInvalidParams() {
    UUID randomUUID = UUID.randomUUID();
    Result result = assertPlatformException(() -> deleteRegion(provider.getUuid(), randomUUID));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        contentAsString(result), CoreMatchers.containsString("Invalid Provider/Region UUID"));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteRegionWithValidParams() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");

    AvailabilityZone.createOrThrow(r, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "AZ 2", "subnet-2");

    Region actualRegion = getFirstRegion();
    assertTrue(actualRegion.isActive());
    for (AvailabilityZone az : actualRegion.getZones()) {
      assertTrue(az.getActive());
    }

    Result result = deleteRegion(provider.getUuid(), r.getUuid());
    assertEquals(OK, result.status());

    assertFalse(Region.get(r.getUuid()).isActive());
  }

  @Test
  public void testDeleteRegionInUseByUniverses() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");

    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "AZ 2", "subnet-2");

    Universe universe = ModelFactory.createUniverse();
    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    Set<NodeDetails> nodeDetailsSet = ApiUtils.getDummyNodeDetailSet(UUID.randomUUID(), 3, 3);
    udtp.nodeDetailsSet = nodeDetailsSet;

    int i = 0;
    for (NodeDetails nd : nodeDetailsSet) {
      nd.azUuid = i++ % 2 == 0 ? az1.getUuid() : az2.getUuid();
    }

    universe.setUniverseDetails(udtp);
    universe.update();

    Result result = assertPlatformException(() -> deleteRegion(provider.getUuid(), r.getUuid()));
    assertEquals(FORBIDDEN, result.status());

    assertNotNull(Region.get(r.getUuid()));
  }

  @Test
  public void testEditRegion() {
    Region r = Region.create(provider, "region-1", "PlacementRegion 1", "default-image");
    ObjectNode regionJson = Json.newObject();
    String updatedYbImage = "another-image";
    String updatedSG = "sg-123456";
    regionJson.put("ybImage", updatedYbImage);
    regionJson.put("securityGroupId", updatedSG);

    Result result = editRegion(provider.getUuid(), r.getUuid(), regionJson);
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode details = json.get("details");
    JsonNode cloudInfo = details.get("cloudInfo");
    JsonNode awsRegionCloudInfo = cloudInfo.get("aws");

    assertEquals(OK, result.status());
    assertValue(awsRegionCloudInfo, "ybImage", updatedYbImage);
    assertValue(awsRegionCloudInfo, "securityGroupId", updatedSG);

    r.refresh();
    assertEquals(updatedSG, r.getSecurityGroupId());
    assertEquals(updatedYbImage, r.getYbImage());

    regionJson.put("securityGroupId", (String) null);
    result = editRegion(provider.getUuid(), r.getUuid(), regionJson);
    json = Json.parse(contentAsString(result));
    details = json.get("details");
    cloudInfo = details.get("cloudInfo");
    awsRegionCloudInfo = cloudInfo.get("aws");
    assertEquals(OK, result.status());
    assertValue(awsRegionCloudInfo, "ybImage", updatedYbImage);
    assertNull(awsRegionCloudInfo.get("securityGroupId"));

    r.refresh();
    assertNull(r.getSecurityGroupId());
    assertEquals(updatedYbImage, r.getYbImage());
  }

  @Test
  public void testCreateRegionV2Payload() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.aws, "provider-1");
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"us-west-2\": {\"name\": \"us-west-2\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    // use v2 APIs payload for region creation.
    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.useLegacyPayloadForRegionAndAZs.getKey(), "false");

    JsonNode regionBody = generateRegionRequestBody();
    Result result = createRegion(p.getUuid(), regionBody);
    Region region = Json.fromJson(Json.parse(contentAsString(result)), Region.class);
    assertEquals(OK, result.status());
    assertNotNull("Region not created, empty UUID.", region.getUuid());

    p = Provider.getOrBadRequest(customer.getUuid(), p.getUuid());
    assertEquals(1, p.getRegions().size());
    assertEquals(1, p.getRegions().get(0).getZones().size());
  }

  @Test
  public void testAddZoneV2Payload() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.aws, "provider-1");
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.AWSRegionMetadata.toString(),
        Json.parse("{\"us-west-2\": {\"name\": \"us-west-2\", \"ybImage\": \"yb image\"}}"),
        ConfigHelper.ConfigType.AWSRegionMetadata.getDescription());
    // use v2 APIs payload for region creation.
    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.useLegacyPayloadForRegionAndAZs.getKey(), "false");

    JsonNode regionBody = generateRegionRequestBody();
    Result result = createRegion(p.getUuid(), regionBody);
    Region region = Json.fromJson(Json.parse(contentAsString(result)), Region.class);
    assertEquals(OK, result.status());
    assertNotNull("Region not created, empty UUID.", region.getUuid());

    p = Provider.getOrBadRequest(customer.getUuid(), p.getUuid());
    assertEquals(1, p.getRegions().size());
    assertEquals(1, p.getRegions().get(0).getZones().size());

    List<AvailabilityZone> zones = region.getZones();
    AvailabilityZone zone = new AvailabilityZone();
    zone.setName("Zone 2");
    zone.setCode("zone-2");
    zones.add(zone);
    region.setZones(zones);
    region.getDetails().getCloudInfo().getAws().setSecurityGroupId("Modified group id");

    result = editRegion(p.getUuid(), region.getUuid(), Json.toJson(region));
    region = Json.fromJson(Json.parse(contentAsString(result)), Region.class);
    assertEquals(OK, result.status());
    assertEquals(2, region.getZones().size());
    assertEquals(
        "Modified group id", region.getDetails().getCloudInfo().getAws().getSecurityGroupId());
  }

  public JsonNode generateRegionRequestBody() {
    ObjectNode regionRequestBody = Json.newObject();
    regionRequestBody.put("code", "us-west-2");
    regionRequestBody.put("name", "us-west-2");

    ObjectNode details = Json.newObject();
    ObjectNode cloudInfo = Json.newObject();
    ObjectNode awsCloudInfo = Json.newObject();

    awsCloudInfo.put("vnet", "vnet");
    awsCloudInfo.put("securityGroupId", "sg");
    awsCloudInfo.put("ybImage", "yb_image");

    cloudInfo.set("aws", awsCloudInfo);
    details.set("cloudInfo", cloudInfo);
    regionRequestBody.set("details", details);

    ArrayNode zones = Json.newArray();
    ObjectNode zone = Json.newObject();
    zone.put("name", "Zone 1");
    zone.put("code", "zone-1");
    zone.put("subnet", "subnet");
    zones.add(zone);

    regionRequestBody.set("zones", zones);
    return regionRequestBody;
  }

  public Region getFirstRegion() {
    Result result;
    result = listAllRegions();
    assertEquals(OK, result.status());
    return Json.fromJson(Json.parse(contentAsString(result)).get(0), Region.class);
  }
}
