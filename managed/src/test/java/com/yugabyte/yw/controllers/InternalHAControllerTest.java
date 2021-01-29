/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Users;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

public class InternalHAControllerTest extends FakeDBApplication {
  Customer customer;
  Users user;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  private String createClusterKey() {
    String authToken = user.createAuthToken();
    Result createClusterKeyResult =  FakeApiHelper
      .doRequestWithAuthToken("GET", "/api/settings/ha/generate_key", authToken);
    assertOk(createClusterKeyResult);

    return Json.parse(contentAsString(createClusterKeyResult)).get("cluster_key").asText();
  }

  private JsonNode createHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = createClusterKey();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult =
      FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
    assertOk(createResult);

    return Json.parse(contentAsString(createResult));
  }

  private JsonNode createPlatformInstance(UUID configUUID, boolean isLocal, boolean isLeader) {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config/" + configUUID.toString() + "/instance";
    JsonNode body = Json.newObject()
      .put("address", "abc.com")
      .put("is_local", isLocal)
      .put("is_leader", isLeader);
    Result createResult =
      FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
    assertOk(createResult);

    return Json.parse(contentAsString(createResult));
  }

  @Test
  public void testGetHAConfigNoneExist() {
    String uri = "/api/settings/ha/internal/config";
    String clusterKey = createClusterKey();
    Result getResult = FakeApiHelper.doRequestWithHAToken("GET", uri, clusterKey);
    assertEquals(BAD_REQUEST, getResult.status());
    assertEquals(contentAsString(getResult), "Unable to authenticate provided cluster key");
  }

  @Test
  public void testGetHAConfigInvalidClusterKey() {
    String uri = "/api/settings/ha/internal/config";
    createHAConfig();
    String incorrectClusterKey = createClusterKey();
    Result getResult = FakeApiHelper.doRequestWithHAToken("GET", uri, incorrectClusterKey);
    assertEquals(BAD_REQUEST, getResult.status());
    assertEquals(contentAsString(getResult), "Unable to authenticate provided cluster key");
  }

  @Test
  public void testGetHAConfigValidClusterKey() {
    String uri = "/api/settings/ha/internal/config";
    String correctClusterKey = createHAConfig().get("cluster_key").asText("");
    Result getResult = FakeApiHelper.doRequestWithHAToken("GET", uri, correctClusterKey);
    assertOk(getResult);
    JsonNode getResponse = Json.parse(contentAsString(getResult));
    String getClusterKey = getResponse.get("cluster_key").asText();
    assertEquals(correctClusterKey, getClusterKey);
  }

  @Test
  public void testSyncInstancesNoLocalInstances() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    String clusterKey = haConfigJson.get("cluster_key").asText();
    String uri = "/api/settings/ha/internal/config/" + configUUID;
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertBadRequest(syncResult, "No local instance configured");
  }

  @Test
  public void testSyncInstancesNoLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(configUUID, true, true);
    String uri = "/api/settings/ha/internal/config/" + configUUID;
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertBadRequest(syncResult, "Cannot import instances for a leader");
  }

  @Test
  public void testSyncInstancesInvalidJson() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(configUUID, true, false);
    String uri = "/api/settings/ha/internal/config/" + configUUID;
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertInternalServerError(syncResult, "Error importing platform instances");
  }

  @Test
  public void testSyncInstancesEmptyArray() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(configUUID, true, false);
    String uri = "/api/settings/ha/internal/config/" + configUUID;
    ArrayNode body = Json.newArray();
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(syncResult);
  }

  @Test
  public void testSyncInstancesSuccess() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    String clusterKey = haConfigJson.get("cluster_key").asText();
    HighAvailabilityConfig config = new HighAvailabilityConfig();
    config.setUUID(configUUID);
    config.setClusterKey(clusterKey);
    JsonNode localInstance = createPlatformInstance(configUUID, true, false);
    ArrayNode body = Json.newArray();
    PlatformInstance i1 = new PlatformInstance();
    i1.setUUID(UUID.randomUUID());
    i1.setConfig(config);
    i1.setAddress("abcdef.com");
    i1.setIsLeader(true);
    i1.setIsLocal(false);
    body.add(Json.toJson(i1));
    body.add(localInstance);
    String uri = "/api/settings/ha/internal/config/" + configUUID;
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(syncResult);
    uri = "/api/settings/ha/internal/config";
    Result getResult = FakeApiHelper.doRequestWithHAToken("GET", uri, clusterKey);
    assertOk(getResult);
    JsonNode getResponse = Json.parse(contentAsString(getResult));
    ArrayNode instancesJson = (ArrayNode) getResponse.get("instances");
    assertEquals(instancesJson.size(), 2);
    PlatformInstance local = Json.fromJson(instancesJson.get(0), PlatformInstance.class);
    assertEquals(local.getAddress(), "abc.com");
    assertTrue(local.getIsLocal());
    assertFalse(local.getIsLeader());
    PlatformInstance remoteLeader = Json.fromJson(instancesJson.get(1), PlatformInstance.class);
    assertEquals(remoteLeader.getAddress(), "abcdef.com");
    assertTrue(remoteLeader.getIsLeader());
    assertFalse(remoteLeader.getIsLocal());
  }
}
