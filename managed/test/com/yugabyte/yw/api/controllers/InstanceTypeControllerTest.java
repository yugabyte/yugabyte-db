// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;


import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

import java.util.UUID;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.core.IsNull.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

public class InstanceTypeControllerTest extends FakeDBApplication {
  Provider provider;

  @Before
  public void setUp() {
    provider = Provider.create("aws", "Amazon");
  }

  @Test
  public void testListInstanceTypeWithInvalidProviderUUID() {
    Result result =
      FakeApiHelper.requestWithAuthToken("GET", "/api/providers/" + UUID.randomUUID() + "/instance_types");
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), CoreMatchers.containsString("Invalid Provider UUID"));
  }

  @Test
  public void testListEmptyInstanceTypeWithValidProviderUUID() {
    Result result =
      FakeApiHelper.requestWithAuthToken("GET", "/api/providers/" + provider.uuid + "/instance_types");
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(0, json.size());
  }

  @Test
  public void testListInstanceTypeWithValidProviderUUID() {
    InstanceType.create(provider.code, "test-i1", 2, 10.5, 100, InstanceType.VolumeType.EBS);
    InstanceType.create(provider.code, "test-i2", 3, 9.0, 80, InstanceType.VolumeType.EBS);

    Result result =
      FakeApiHelper.requestWithAuthToken("GET", "/api/providers/" + provider.uuid + "/instance_types");
    
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(2, json.size());

    int idx = 1;
    for (JsonNode instance : json) {
      assertThat(instance.get("instanceTypeCode").asText(), allOf(notNullValue(), equalTo("test-i" + idx)));
      idx++;
    }
  }

  @Test
  public void testCreateInstanceTypeWithInvalidProviderUUID() {
    ObjectNode instanceTypeJson = Json.newObject();

    Result result = FakeApiHelper.requestWithAuthToken(
      "POST",
      "/api/providers/" + UUID.randomUUID() + "/instance_types",
      instanceTypeJson);

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").toString(), CoreMatchers.containsString("Invalid Provider UUID"));
  }

  @Test
  public void testCreateInstanceTypeWithInvalidParams() {
    ObjectNode instanceTypeJson = Json.newObject();

    Result result = FakeApiHelper.requestWithAuthToken(
      "POST",
      "/api/providers/" + provider.uuid + "/instance_types",
      instanceTypeJson);

    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), CoreMatchers.containsString("\"idKey\":[\"This field is required\"]"));
    assertThat(contentAsString(result), CoreMatchers.containsString("\"memSize\":[\"This field is required\"]"));
    assertThat(contentAsString(result), CoreMatchers.containsString("\"volumeSize\":[\"This field is required\"]"));
    assertThat(contentAsString(result), CoreMatchers.containsString("\"volumeType\":[\"This field is required\"]"));
    assertThat(contentAsString(result), CoreMatchers.containsString("\"numCores\":[\"This field is required\"]"));
  }

  @Test
  public void testCreateInstanceTypeWithValidParams() {
    ObjectNode instanceTypeJson = Json.newObject();
    ObjectNode idKey = Json.newObject();
    idKey.put("instanceTypeCode", "test-i1");
    idKey.put("providerCode", "aws");
    instanceTypeJson.set("idKey", idKey);
    instanceTypeJson.put("memSize", 10.9);
    instanceTypeJson.put("volumeSize", 10);
    instanceTypeJson.put("volumeType", "EBS");
    instanceTypeJson.put("numCores", 3);

    Result result = FakeApiHelper.requestWithAuthToken(
      "POST",
      "/api/providers/" + provider.uuid + "/instance_types",
      instanceTypeJson);

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("instanceTypeCode").asText(), allOf(notNullValue(), equalTo("test-i1")));
    assertThat(json.get("volumeSize").asInt(), allOf(notNullValue(), equalTo(10)));
    assertThat(json.get("memSize").asDouble(), allOf(notNullValue(), equalTo(10.9)));
    assertThat(json.get("numCores").asInt(), allOf(notNullValue(), equalTo(3)));
    assertThat(json.get("volumeType").asText(), allOf(notNullValue(), equalTo("EBS")));
    assertTrue(json.get("active").asBoolean());
  }
}
