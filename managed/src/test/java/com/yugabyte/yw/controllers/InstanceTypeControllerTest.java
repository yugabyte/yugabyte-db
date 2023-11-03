// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.core.IsNull.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.InstanceType.VolumeDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class InstanceTypeControllerTest extends FakeDBApplication {
  Customer customer;
  Users user;
  Provider awsProvider;
  Provider onPremProvider;
  Region defaultRegion;
  AvailabilityZone zone1, zone2;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    awsProvider = ModelFactory.awsProvider(customer);
    defaultRegion =
        Region.create(awsProvider, "default-region", "Default PlacementRegion", "default-image");
    zone1 = AvailabilityZone.createOrThrow(defaultRegion, "zone1", "Zone One", "Subnet 1");
    zone2 = AvailabilityZone.createOrThrow(defaultRegion, "zone2", "Zone Two", "Subnet 2");
    onPremProvider = ModelFactory.newProvider(customer, Common.CloudType.onprem);
  }

  private JsonNode doListInstanceTypesAndVerify(UUID providerUUID, int status) {
    return doListFilteredInstanceTypeAndVerify(providerUUID, status, null);
  }

  private JsonNode doListInstanceTypesAndVerify(UUID providerUUID, int status, Architecture arch) {
    return doListFilteredInstanceTypeAndVerify(providerUUID, status, arch);
  }

  private JsonNode doListFilteredInstanceTypeAndVerify(
      UUID providerUUID, int status, Architecture arch, String... zones) {
    String zoneParams = Arrays.stream(zones).collect(Collectors.joining("&zone=", "?zone=", ""));
    String archParam = "";
    if (arch != null) {
      archParam = String.format("&arch=%s", arch.toString());
    }
    Result result =
        doRequest(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + providerUUID
                + "/instance_types"
                + zoneParams
                + archParam);
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doCreateInstanceTypeAndVerify(UUID providerUUID, JsonNode bodyJson, int status) {
    Result result =
        doRequestWithBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + providerUUID
                + "/instance_types",
            bodyJson);

    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doGetInstanceTypeAndVerify(
      UUID providerUUID, String instanceTypeCode, int status) {
    Result result =
        doRequest(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + providerUUID
                + "/instance_types/"
                + instanceTypeCode);
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doDeleteInstanceTypeAndVerify(
      UUID providerUUID, String instanceTypeCode, int status) {
    Result result =
        doRequest(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/providers/"
                + providerUUID
                + "/instance_types/"
                + instanceTypeCode);
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private Map<String, InstanceType> setUpValidInstanceTypes(int numInstanceTypes) {
    return setUpValidInstanceTypes(numInstanceTypes, null);
  }

  private Map<String, InstanceType> setUpValidInstanceTypes(
      int numInstanceTypes, Architecture arch) {
    Map<String, InstanceType> instanceTypes = new HashMap<>(numInstanceTypes);
    for (int i = 0; i < numInstanceTypes; ++i) {
      InstanceType.VolumeDetails volDetails = new InstanceType.VolumeDetails();
      volDetails.volumeSizeGB = 100;
      volDetails.volumeType = InstanceType.VolumeType.EBS;
      InstanceTypeDetails instanceDetails = new InstanceTypeDetails();
      instanceDetails.volumeDetailsList.add(volDetails);
      instanceDetails.setDefaultMountPaths();
      String code = "c3.i" + i;
      if (arch != null) {
        instanceDetails.arch = arch;
        code = code + arch.toString();
      }
      instanceTypes.put(
          code, InstanceType.upsert(awsProvider.getUuid(), code, 2, 10.5, instanceDetails));
    }
    return instanceTypes;
  }

  @Test
  public void testListInstanceTypeWithInvalidProviderUUID() {
    UUID randomUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(() -> doListInstanceTypesAndVerify(randomUUID, BAD_REQUEST));
    assertErrorNodeValue(
        Json.parse(contentAsString(result)), "Invalid Provider UUID: " + randomUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListEmptyInstanceTypeWithValidProviderUUID() {
    JsonNode json = doListInstanceTypesAndVerify(awsProvider.getUuid(), OK);
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID() {
    Map<String, InstanceType> instanceTypes = setUpValidInstanceTypes(2);
    JsonNode json = doListInstanceTypesAndVerify(awsProvider.getUuid(), OK);
    checkListResponse(instanceTypes, json);
  }

  @Test
  public void testListInstanceTypeArchFilter() {
    Map<String, InstanceType> instanceTypes = setUpValidInstanceTypes(2, Architecture.x86_64);
    instanceTypes.putAll(setUpValidInstanceTypes(1, Architecture.aarch64));
    JsonNode json = doListInstanceTypesAndVerify(awsProvider.getUuid(), OK, Architecture.x86_64);
    assertEquals(2, json.size());
    json = doListInstanceTypesAndVerify(awsProvider.getUuid(), OK, Architecture.aarch64);
    assertEquals(1, json.size());
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID_filtered_ignoreNoCloud() {
    Map<String, InstanceType> instanceTypes = setUpValidInstanceTypes(2);
    when(mockCloudAPIFactory.get(any())).thenReturn(null);
    JsonNode json =
        doListFilteredInstanceTypeAndVerify(awsProvider.getUuid(), OK, null, "zone1", "zone2");
    checkListResponse(instanceTypes, json);
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID_filtered_ignoreCloudException() {
    Map<String, InstanceType> instanceTypes = setUpValidInstanceTypes(2);
    RuntimeException thrown = new RuntimeException("cloud exception");
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);
    when(mockCloudAPI.offeredZonesByInstanceType(any(), any(), any())).thenThrow(thrown);
    JsonNode json =
        doListFilteredInstanceTypeAndVerify(awsProvider.getUuid(), OK, null, "zone1", "zone2");
    checkListResponse(instanceTypes, json);
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID_filtered_allOffered() {
    Map<String, InstanceType> instanceTypes = setUpValidInstanceTypes(2);
    ImmutableSet<String> everywhere = ImmutableSet.of("zone1", "zone2");
    Map<String, Set<String>> allInstancesEverywhere =
        ImmutableMap.of(
            "c3.i0", everywhere,
            "c3.i1", everywhere);
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);

    when(mockCloudAPI.offeredZonesByInstanceType(any(), any(), any()))
        .thenReturn(allInstancesEverywhere);

    JsonNode json =
        doListFilteredInstanceTypeAndVerify(awsProvider.getUuid(), OK, null, "zone1", "zone2");
    checkListResponse(instanceTypes, json);

    verify(mockCloudAPI, times(1))
        .offeredZonesByInstanceType(
            eq(awsProvider),
            eq(Collections.singletonMap(defaultRegion, Sets.newHashSet("zone1", "zone2"))),
            eq(instanceTypes.keySet()));
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID_filtered_disjointOffered() {
    Map<String, InstanceType> instanceTypes = setUpValidInstanceTypes(2);
    Map<String, Set<String>> cloudResponse =
        ImmutableMap.of(
            "c3.i0", ImmutableSet.of("zone1"),
            "c3.i1", ImmutableSet.of("zone2"));
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);

    when(mockCloudAPI.offeredZonesByInstanceType(any(), any(), any())).thenReturn(cloudResponse);

    JsonNode json =
        doListFilteredInstanceTypeAndVerify(awsProvider.getUuid(), OK, null, "zone1", "zone2");
    assertEquals(0, json.size());

    verify(mockCloudAPI, times(1))
        .offeredZonesByInstanceType(
            eq(awsProvider),
            eq(Collections.singletonMap(defaultRegion, Sets.newHashSet("zone1", "zone2"))),
            eq(instanceTypes.keySet()));
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID_filtered_someNotOffered() {
    Map<String, InstanceType> instanceTypes = setUpValidInstanceTypes(2);
    Map<String, Set<String>> cloudResponse =
        ImmutableMap.of("c3.i1", ImmutableSet.of("zone2", "zone1"));
    CloudAPI mockCloudAPI = mock(CloudAPI.class);
    when(mockCloudAPIFactory.get(any())).thenReturn(mockCloudAPI);

    when(mockCloudAPI.offeredZonesByInstanceType(any(), any(), any())).thenReturn(cloudResponse);

    JsonNode json =
        doListFilteredInstanceTypeAndVerify(awsProvider.getUuid(), OK, null, "zone1", "zone2");
    assertEquals(1, json.size());
    InstanceType expectedInstanceType = instanceTypes.get("c3.i1");
    assertValue(json.path(0), "instanceTypeCode", expectedInstanceType.getInstanceTypeCode());

    verify(mockCloudAPI, times(1))
        .offeredZonesByInstanceType(
            eq(awsProvider),
            eq(Collections.singletonMap(defaultRegion, Sets.newHashSet("zone1", "zone2"))),
            eq(instanceTypes.keySet()));
  }

  private void checkListResponse(Map<String, InstanceType> instanceTypes, JsonNode json) {
    assertEquals(2, json.size());
    for (int idx = 0; idx < json.size(); ++idx) {
      JsonNode instance = json.path(idx);
      InstanceType expectedInstanceType =
          instanceTypes.get(instance.path("instanceTypeCode").asText());
      assertValue(instance, "instanceTypeCode", expectedInstanceType.getInstanceTypeCode());
      assertThat(
          instance.get("numCores").asDouble(),
          allOf(notNullValue(), equalTo(expectedInstanceType.getNumCores())));
      assertThat(
          instance.get("memSizeGB").asDouble(),
          allOf(notNullValue(), equalTo(expectedInstanceType.getMemSizeGB())));

      InstanceTypeDetails itd = expectedInstanceType.getInstanceTypeDetails();
      List<VolumeDetails> detailsList = itd.volumeDetailsList;
      VolumeDetails targetDetails = detailsList.get(0);
      JsonNode itdNode = instance.get("instanceTypeDetails");
      JsonNode detailsListNode = itdNode.get("volumeDetailsList");
      JsonNode jsonDetails = detailsListNode.get(0);
      assertThat(
          jsonDetails.get("volumeSizeGB").asInt(),
          allOf(notNullValue(), equalTo(targetDetails.volumeSizeGB)));
      assertValue(jsonDetails, "volumeType", targetDetails.volumeType.toString());
      assertValue(jsonDetails, "mountPath", targetDetails.mountPath);
    }
    List<String> expectedCodes = new ArrayList<>(instanceTypes.keySet());
    assertValues(json, "instanceTypeCode", expectedCodes);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateInstanceTypeWithInvalidProviderUUID() {
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", "test-i1");
    instanceTypeJson.put("memSizeGB", 10.9);
    instanceTypeJson.put("volumeCount", 1);
    instanceTypeJson.put("numCores", 3);
    instanceTypeJson.set("idKey", idKey);
    UUID randomUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> doCreateInstanceTypeAndVerify(randomUUID, instanceTypeJson, BAD_REQUEST));
    assertErrorNodeValue(
        Json.parse(contentAsString(result)), "Invalid Provider UUID: " + randomUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateInstanceTypeWithInvalidParams() {
    Result result =
        assertPlatformException(
            () ->
                doCreateInstanceTypeAndVerify(
                    awsProvider.getUuid(), Json.newObject(), BAD_REQUEST));
    assertErrorNodeValue(Json.parse(contentAsString(result)), "idKey", "This field is required");
    assertErrorNodeValue(
        Json.parse(contentAsString(result)), "memSizeGB", "This field is required");
    assertErrorNodeValue(Json.parse(contentAsString(result)), "numCores", "This field is required");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.EBS;
    volumeDetails.volumeSizeGB = 10;
    details.volumeDetailsList.add(volumeDetails);
    details.setDefaultMountPaths();
    details.arch = Architecture.x86_64;
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", "test-i1");
    instanceTypeJson.set("idKey", idKey);
    instanceTypeJson.put("memSizeGB", 10.9);
    instanceTypeJson.put("numCores", 3);
    instanceTypeJson.set("instanceTypeDetails", Json.toJson(details));
    JsonNode json = doCreateInstanceTypeAndVerify(awsProvider.getUuid(), instanceTypeJson, OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "memSizeGB", "10.9");
    assertValue(json, "numCores", "3.0");
    assertValue(json, "active", "true");
    JsonNode machineDetailsNode = json.get("instanceTypeDetails").get("volumeDetailsList").get(0);
    assertThat(machineDetailsNode, notNullValue());
    assertValue(machineDetailsNode, "volumeSizeGB", "10");
    assertValue(machineDetailsNode, "volumeType", "EBS");
    assertValue(machineDetailsNode, "mountPath", "/mnt/d0");
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateInstanceTypeWithSoftDeletedKeyCollision() {
    String sharedInstanceTypeCode = "test-i1";
    // Setup a soft deleted instance type
    InstanceType it =
        InstanceType.upsert(
            awsProvider.getUuid(),
            sharedInstanceTypeCode,
            3,
            5.0,
            new InstanceType.InstanceTypeDetails());
    JsonNode json =
        doDeleteInstanceTypeAndVerify(awsProvider.getUuid(), it.getInstanceTypeCode(), OK);
    it = InstanceType.get(awsProvider.getUuid(), it.getInstanceTypeCode());
    assertTrue(json.get("success").asBoolean());
    assertFalse(it.isActive());

    // Create and verify an instance type with the same instanceTypeCode `sharedInstanceTypeCode`.
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.EBS;
    volumeDetails.volumeSizeGB = 10;
    details.volumeDetailsList.add(volumeDetails);
    details.setDefaultMountPaths();
    details.arch = Architecture.x86_64;
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", sharedInstanceTypeCode);
    instanceTypeJson.set("idKey", idKey);
    instanceTypeJson.put("memSizeGB", 11.9);
    instanceTypeJson.put("numCores", 4);
    instanceTypeJson.set("instanceTypeDetails", Json.toJson(details));
    json = doCreateInstanceTypeAndVerify(awsProvider.getUuid(), instanceTypeJson, OK);
    assertValue(json, "instanceTypeCode", sharedInstanceTypeCode);
    assertValue(json, "memSizeGB", "11.9");
    assertValue(json, "numCores", "4.0");
    assertValue(json, "active", "true");
    JsonNode machineDetailsNode = json.get("instanceTypeDetails").get("volumeDetailsList").get(0);
    assertThat(machineDetailsNode, notNullValue());
    assertValue(machineDetailsNode, "volumeSizeGB", "10");
    assertValue(machineDetailsNode, "volumeType", "EBS");
    assertValue(machineDetailsNode, "mountPath", "/mnt/d0");
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testGetOnPremInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 20;
    volumeDetails.mountPath = "/tmp/path/";
    details.volumeDetailsList.add(volumeDetails);
    InstanceType it = InstanceType.upsert(onPremProvider.getUuid(), "test-i1", 3, 5.0, details);
    JsonNode json =
        doGetInstanceTypeAndVerify(onPremProvider.getUuid(), it.getInstanceTypeCode(), OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "memSizeGB", "5.0");
    assertValue(json, "numCores", "3.0");
    assertValue(json, "active", "true");
    JsonNode machineDetailsNode = json.get("instanceTypeDetails").get("volumeDetailsList").get(0);
    assertThat(machineDetailsNode, notNullValue());
    assertValue(machineDetailsNode, "volumeSizeGB", "20");
    assertValue(machineDetailsNode, "volumeType", "SSD");
    assertValue(machineDetailsNode, "mountPath", "/tmp/path/");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 20;
    details.volumeDetailsList.add(volumeDetails);
    details.volumeDetailsList.add(volumeDetails);
    InstanceType it = InstanceType.upsert(awsProvider.getUuid(), "test-i1", 3, 5.0, details);
    JsonNode json = doGetInstanceTypeAndVerify(awsProvider.getUuid(), it.getInstanceTypeCode(), OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "memSizeGB", "5.0");
    assertValue(json, "numCores", "3.0");
    assertValue(json, "active", "true");
    JsonNode volumeDetailsListNode = json.get("instanceTypeDetails").get("volumeDetailsList");
    assertNotNull(volumeDetailsListNode);
    for (int i = 0; i < 2; ++i) {
      JsonNode machineDetailsNode = volumeDetailsListNode.get(i);
      assertThat(machineDetailsNode, notNullValue());
      assertValue(machineDetailsNode, "volumeSizeGB", "20");
      assertValue(machineDetailsNode, "volumeType", "SSD");
      assertValue(machineDetailsNode, "mountPath", String.format("/mnt/d%d", i));
    }
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetInstanceTypeWithInvalidParams() {
    String fakeInstanceCode = "foo";
    Result result =
        assertPlatformException(
            () -> doGetInstanceTypeAndVerify(awsProvider.getUuid(), fakeInstanceCode, BAD_REQUEST));
    assertErrorNodeValue(
        Json.parse(contentAsString(result)), "Instance type not found: " + fakeInstanceCode);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetInstanceTypeWithInvalidProvider() {
    String fakeInstanceCode = "foo";
    UUID randomUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> doGetInstanceTypeAndVerify(randomUUID, fakeInstanceCode, BAD_REQUEST));
    assertErrorNodeValue(
        Json.parse(contentAsString(result)), "Invalid Provider UUID: " + randomUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteInstanceTypeWithValidParams() {
    InstanceType it =
        InstanceType.upsert(
            awsProvider.getUuid(), "test-i1", 3, 5.0, new InstanceType.InstanceTypeDetails());
    JsonNode json =
        doDeleteInstanceTypeAndVerify(awsProvider.getUuid(), it.getInstanceTypeCode(), OK);
    it = InstanceType.get(awsProvider.getUuid(), it.getInstanceTypeCode());
    assertTrue(json.get("success").asBoolean());
    assertFalse(it.isActive());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDeleteInstanceTypeWithInvalidParams() {
    String fakeInstanceCode = "foo";
    Result result =
        assertPlatformException(
            () ->
                doDeleteInstanceTypeAndVerify(
                    awsProvider.getUuid(), fakeInstanceCode, BAD_REQUEST));
    assertErrorNodeValue(
        Json.parse(contentAsString(result)), "Instance type not found: " + fakeInstanceCode);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteInstanceTypeWithInvalidProvider() {
    String fakeInstanceCode = "foo";
    UUID randomUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> doDeleteInstanceTypeAndVerify(randomUUID, fakeInstanceCode, BAD_REQUEST));
    assertErrorNodeValue(
        Json.parse(contentAsString(result)), "Invalid Provider UUID: " + randomUUID);
    assertAuditEntry(0, customer.getUuid());
  }
}
