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

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Users;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class PlatformInstanceControllerTest extends FakeDBApplication {
  Customer customer;
  Users user;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  private String createClusterKey() {
    String authToken = user.createAuthToken();
    Result createClusterKeyResult =
        FakeApiHelper.doRequestWithAuthToken("GET", "/api/settings/ha/generate_key", authToken);
    assertOk(createClusterKeyResult);

    return Json.parse(contentAsString(createClusterKeyResult)).get("cluster_key").asText();
  }

  private JsonNode createHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = createClusterKey();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
    assertOk(createResult);

    return Json.parse(contentAsString(createResult));
  }

  private Result createPlatformInstance(
      UUID configUUID, String address, boolean isLocal, boolean isLeader) {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config/" + configUUID.toString() + "/instance";
    JsonNode body =
        Json.newObject()
            .put("address", address)
            .put("is_local", isLocal)
            .put("is_leader", isLeader);
    return FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
  }

  private Result deletePlatformInstance(UUID configUUID, UUID instanceUUID) {
    String authToken = user.createAuthToken();
    String uri =
        "/api/settings/ha/config/" + configUUID.toString() + "/instance/" + instanceUUID.toString();
    return FakeApiHelper.doRequestWithAuthToken("DELETE", uri, authToken);
  }

  private Result demotePlatformInstance(UUID configUUID, UUID instanceUUID) {
    String authToken = user.createAuthToken();
    String uri =
        "/api/settings/ha/config/"
            + configUUID.toString()
            + "/instance/"
            + instanceUUID.toString()
            + "/demote";
    return FakeApiHelper.doRequestWithAuthToken("POST", uri, authToken);
  }

  @Test
  public void testCreatePlatformInstanceLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    UUID instanceConfigUUID = UUID.fromString(instanceJson.get("config_uuid").asText());
    assertEquals(configUUID, instanceConfigUUID);
  }

  @Test
  public void testCreateRemotePlatformInstanceBeforeLocal() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abc.com", false, true));
    assertBadRequest(
        createResult,
        "Cannot create a remote platform instance before creating local platform instance");
  }

  @Test
  public void testCreateRemotePlatformInstanceWithLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, false);
    assertOk(createResult);
    createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abcdef.com", false, false));
    assertBadRequest(
        createResult, "Cannot create a remote platform instance on a follower platform instance");
  }

  @Test
  public void testCreatePlatformInstanceWithLocalLeader() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://abcdef.com", false, false);
    assertOk(createResult);
  }

  @Test
  public void testCreateMultipleLocalPlatformInstances() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, true);
    assertOk(createResult);
    createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abcdef.com", true, false));
    assertBadRequest(createResult, "Local platform instance already exists");
  }

  @Test
  public void testCreateMultipleLeaderPlatformInstances() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, true);
    assertOk(createResult);
    createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abcdef.com", false, true));
    assertBadRequest(createResult, "Leader platform instance already exists");
  }

  @Test
  public void testDeleteLocalPlatformInstanceWithLocalLeader() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, true);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    UUID instanceUUID = UUID.fromString(instanceJson.get("uuid").asText());
    Result deleteResult =
        assertPlatformException(() -> deletePlatformInstance(configUUID, instanceUUID));
    assertBadRequest(deleteResult, "Cannot delete local instance");
  }

  @Test
  public void testDeleteRemotePlatformInstanceWithLocalLeader() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://abcdef.com", false, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    UUID instanceUUID = UUID.fromString(instanceJson.get("uuid").asText());
    Result deleteResult = deletePlatformInstance(configUUID, instanceUUID);
    assertOk(deleteResult);
  }

  @Test
  public void testDeleteLocalPlatformInstanceWithLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    UUID instanceUUID = UUID.fromString(instanceJson.get("uuid").asText());
    Result deleteResult =
        assertPlatformException(() -> deletePlatformInstance(configUUID, instanceUUID));
    assertBadRequest(deleteResult, "Follower platform instance cannot delete platform instances");
  }

  @Test
  public void testInvalidAddress() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abc.com::abc", true, false));
    assertBadRequest(createResult, "");
    JsonNode node = Json.parse(contentAsString(createResult));
    assertErrorNodeValue(node, "address", "Invalid URL provided");
  }

  @Test
  public void testPromoteInstanceBackupFileNonexistent() {
    when(mockShellProcessHandler.run(anyList(), anyMap(), anyBoolean()))
        .thenReturn(new ShellResponse());
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUUID();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    PlatformInstance instance = Json.fromJson(instanceJson, PlatformInstance.class);
    UUID instanceUUID = instance.getUUID();
    PlatformInstance remoteLeader = PlatformInstance.create(config, "http://def.com", true, false);
    remoteLeader.save();
    String uri =
        String.format(
            "/api/settings/ha/config/%s/instance/%s/promote",
            configUUID.toString(), instanceUUID.toString());
    String authToken = user.createAuthToken();
    JsonNode body = Json.newObject().put("backup_file", "/foo/bar");
    Result promoteResult =
        assertPlatformException(
            () -> FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, authToken, body));
    assertBadRequest(promoteResult, "Could not find backup file");
  }

  @Test
  public void testPromoteInstanceNoLeader() {
    when(mockShellProcessHandler.run(anyList(), anyMap(), anyBoolean()))
        .thenReturn(new ShellResponse());
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUUID();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com", true, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    PlatformInstance instance = Json.fromJson(instanceJson, PlatformInstance.class);
    UUID instanceUUID = instance.getUUID();
    String uri =
        String.format(
            "/api/settings/ha/config/%s/instance/%s/promote",
            configUUID.toString(), instanceUUID.toString());
    String authToken = user.createAuthToken();
    JsonNode body = Json.newObject().put("backup_file", "/foo/bar");
    Result promoteResult =
        assertPlatformException(
            () -> FakeApiHelper.doRequestWithAuthTokenAndBody("POST", uri, authToken, body));
    assertBadRequest(promoteResult, "Could not find leader instance");
  }
}
