// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;


import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.core.IsNull.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertFalse;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.List;
import java.util.UUID;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;

import play.libs.Json;
import play.mvc.Result;

public class InstanceTypeControllerTest extends FakeDBApplication {
  Provider provider;
  Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
  }

  private JsonNode doListInstanceTypesAndVerify(UUID providerUUID, int status) {
    Result result = FakeApiHelper.doRequest("GET", "/api/customers/" + customer.uuid
        + "/providers/" + providerUUID + "/instance_types");
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doCreateInstanceTypeAndVerify(UUID providerUUID, JsonNode bodyJson, int status) {
    Result result = FakeApiHelper.doRequestWithBody(
        "POST",
        "/api/customers/" + customer.uuid + "/providers/" + providerUUID + "/instance_types",
        bodyJson);

    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doGetInstanceTypeAndVerify(UUID providerUUID, String instanceTypeCode, int status) {
    Result result = FakeApiHelper.doRequest("GET", "/api/customers/" + customer.uuid
        + "/providers/" + providerUUID + "/instance_types/" + instanceTypeCode);
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  private JsonNode doDeleteInstanceTypeAndVerify(UUID providerUUID, String instanceTypeCode, int status) {
    Result result = FakeApiHelper.doRequest("DELETE", "/api/customers/" + customer.uuid
        + "/providers/" + providerUUID + "/instance_types/" + instanceTypeCode);
    assertEquals(status, result.status());
    return Json.parse(contentAsString(result));
  }

  @Test
  public void testListInstanceTypeWithInvalidProviderUUID() {
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doListInstanceTypesAndVerify(randomUUID, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }

  @Test
  public void testListEmptyInstanceTypeWithValidProviderUUID() {
    JsonNode json = doListInstanceTypesAndVerify(provider.uuid, OK);
    assertEquals(0, json.size());
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID() {
    InstanceType.upsert(provider.code, "test-i1", 2, 10.5, 1, 100,
                        InstanceType.VolumeType.EBS, null);
    InstanceType.upsert(provider.code, "test-i2", 3, 9.0, 1, 80, InstanceType.VolumeType.EBS, null);
    JsonNode json = doListInstanceTypesAndVerify(provider.uuid, OK);
    assertEquals(2, json.size());

    int idx = 1;
    for (JsonNode instance : json) {
      assertThat(instance.get("instanceTypeCode").asText(), allOf(notNullValue(), equalTo("test-i" + idx)));
      idx++;
    }
    List<String> expectedCodes = ImmutableList.of("test-i1", "test-i2");
    assertValues(json, "instanceTypeCode", expectedCodes);
  }

  @Test
  public void testCreateInstanceTypeWithInvalidProviderUUID() {
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", "test-i1");
    idKey.put("providerCode", "aws");
    instanceTypeJson.put("memSizeGB", 10.9);
    instanceTypeJson.put("volumeCount", 1);
    instanceTypeJson.put("volumeSizeGB", 10);
    instanceTypeJson.put("volumeType", "EBS");
    instanceTypeJson.put("numCores", 3);
    instanceTypeJson.set("idKey", idKey);
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doCreateInstanceTypeAndVerify(randomUUID, instanceTypeJson, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }

  @Test
  public void testCreateInstanceTypeWithInvalidParams() {
    JsonNode json = doCreateInstanceTypeAndVerify(provider.uuid, Json.newObject(), BAD_REQUEST);
    assertErrorNodeValue(json, "idKey", "This field is required");
    assertErrorNodeValue(json, "memSizeGB", "This field is required");
    assertErrorNodeValue(json, "volumeSizeGB", "This field is required");
    assertErrorNodeValue(json, "volumeType", "This field is required");
    assertErrorNodeValue(json, "numCores", "This field is required");
  }

  @Test
  public void testCreateInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.EBS;
    volumeDetails.volumeSizeGB = 10;
    volumeDetails.mountPath = "/tmp/path/";
    details.volumeDetailsList.add(volumeDetails);
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", "test-i1");
    idKey.put("providerCode", "aws");
    instanceTypeJson.set("idKey", idKey);
    instanceTypeJson.put("memSizeGB", 10.9);
    instanceTypeJson.put("volumeCount", 1);
    instanceTypeJson.put("volumeSizeGB", 10);
    instanceTypeJson.put("volumeType", "EBS");
    instanceTypeJson.put("numCores", 3);
    instanceTypeJson.set("instanceTypeDetails", Json.toJson(details));
    JsonNode json = doCreateInstanceTypeAndVerify(provider.uuid, instanceTypeJson, OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "volumeCount", "1");
    assertValue(json, "volumeSizeGB", "10");
    assertValue(json, "memSizeGB", "10.9");
    assertValue(json, "numCores", "3");
    assertValue(json, "volumeType", "EBS");
    assertValue(json, "active", "true");
    JsonNode machineDetailsNode = json.get("instanceTypeDetails").get("volumeDetailsList").get(0);
    assertThat(machineDetailsNode, notNullValue());
    assertValue(machineDetailsNode, "volumeSizeGB", "10");
    assertValue(machineDetailsNode, "volumeType", "EBS");
    assertValue(machineDetailsNode, "mountPath", "/tmp/path/");
  }

  @Test
  public void testGetInstanceTypeWithValidParams() {
    InstanceType.InstanceTypeDetails details = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.EBS;
    volumeDetails.volumeSizeGB = 20;
    volumeDetails.mountPath = "/tmp/path/";
    details.volumeDetailsList.add(volumeDetails);
    InstanceType it = InstanceType.upsert(provider.code, "test-i1", 3, 5.0, 1, 20,
            InstanceType.VolumeType.EBS, details);
    JsonNode json = doGetInstanceTypeAndVerify(provider.uuid, it.getInstanceTypeCode(), OK);
    assertValue(json, "instanceTypeCode", "test-i1");
    assertValue(json, "volumeCount", "1");
    assertValue(json, "volumeSizeGB", "20");
    assertValue(json, "memSizeGB", "5.0");
    assertValue(json, "numCores", "3");
    assertValue(json, "volumeType", "EBS");
    assertValue(json, "active", "true");
    JsonNode machineDetailsNode = json.get("instanceTypeDetails").get("volumeDetailsList").get(0);
    assertThat(machineDetailsNode, notNullValue());
    assertValue(machineDetailsNode, "volumeSizeGB", "20");
    assertValue(machineDetailsNode, "volumeType", "EBS");
    assertValue(machineDetailsNode, "mountPath", "/tmp/path/");
  }

  @Test
  public void testGetInstanceTypeWithInvalidParams() {
    String fakeInstanceCode = "foo";
    JsonNode json = doGetInstanceTypeAndVerify(provider.uuid, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Instance Type not found: " + fakeInstanceCode);
  }

  @Test
  public void testGetInstanceTypeWithInvalidProvider() {
    String fakeInstanceCode = "foo";
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doGetInstanceTypeAndVerify(randomUUID, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }

  @Test
  public void testDeleteInstanceTypeWithValidParams() {
    InstanceType it = InstanceType.upsert(provider.code, "test-i1", 3, 5.0, 1, 20,
            InstanceType.VolumeType.EBS, new InstanceType.InstanceTypeDetails());
    JsonNode json = doDeleteInstanceTypeAndVerify(provider.uuid, it.getInstanceTypeCode(), OK);
    it = InstanceType.get(provider.code, it.getInstanceTypeCode());
    assertTrue(json.get("success").asBoolean());
    assertFalse(it.isActive());
  }

  @Test
  public void testDeleteInstanceTypeWithInvalidParams() {
    String fakeInstanceCode = "foo";
    JsonNode json = doDeleteInstanceTypeAndVerify(provider.uuid, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Instance Type not found: " + fakeInstanceCode);
  }

  @Test
  public void testDeleteInstanceTypeWithInvalidProvider() {
    String fakeInstanceCode = "foo";
    UUID randomUUID = UUID.randomUUID();
    JsonNode json = doDeleteInstanceTypeAndVerify(randomUUID, fakeInstanceCode, BAD_REQUEST);
    assertErrorNodeValue(json, "Invalid Provider UUID: " + randomUUID);
  }
}
