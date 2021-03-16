/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0
 * .txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Users;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.Date;
import java.util.Random;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.*;
import static junit.framework.TestCase.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.libs.Files.singletonTemporaryFileCreator;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

public class InternalHAControllerTest extends FakeDBApplication {
  public static final String UPLOAD_ENDPOINT = "/api/settings/ha/internal/upload";

  Customer customer;
  Users user;

  @Rule
  public TemporaryFolder folder = new TemporaryFolder();

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  @After
  public void tearDown() throws IOException {
    String storagePath = app.config().getString(PlatformReplicationHelper.STORAGE_PATH_KEY);
    File replicationDir =
      Paths.get(storagePath, PlatformReplicationHelper.REPLICATION_DIR).toFile();
    FileUtils.deleteDirectory(replicationDir);
  }

  private File createFakeDump() throws IOException {
    Random rng = new Random();
    File file = folder.newFile("backup_" + Instant.now().toEpochMilli() + ".tgz");
    try (FileWriter out = new FileWriter(file)) {
      int sz = 1024 * 1024;
      byte[] arr = new byte[sz];
      rng.nextBytes(arr);
      out.write(new String(arr, StandardCharsets.UTF_8));
    }
    return file;
  }

  private String createClusterKey() {
    String authToken = user.createAuthToken();
    Result createClusterKeyResult = FakeApiHelper
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

  private JsonNode getHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    Result getResult = FakeApiHelper.doRequestWithAuthToken("GET", uri, authToken);
    assertOk(getResult);
    return Json.parse(contentAsString(getResult));
  }

  private JsonNode createPlatformInstance(UUID configUUID, boolean isLocal, boolean isLeader) {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config/" + configUUID.toString() + "/instance";
    JsonNode body = Json.newObject()
      .put("address", "http://abc.com")
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
    assertEquals(contentAsString(getResult), "Unable to authenticate request");
  }

  @Test
  public void testGetHAConfigInvalidClusterKey() {
    String uri = "/api/settings/ha/internal/config";
    createHAConfig();
    String incorrectClusterKey = createClusterKey();
    Result getResult = FakeApiHelper.doRequestWithHAToken("GET", uri, incorrectClusterKey);
    assertEquals(BAD_REQUEST, getResult.status());
    assertEquals(contentAsString(getResult), "Unable to authenticate request");
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
    String clusterKey = haConfigJson.get("cluster_key").asText();
    String uri = "/api/settings/ha/internal/config/sync/" + new Date().getTime();
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertBadRequest(syncResult, "No local instance configured");
  }

  @Test
  public void testSyncInstancesNoLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(config.getUUID(), true, true);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String uri = "/api/settings/ha/internal/config/sync/" + config.getLastFailover().getTime();
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertBadRequest(syncResult, "Cannot import instances for a leader");
  }

  @Test
  public void testSyncInstancesInvalidJson() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(config.getUUID(), true, false);
    String uri = "/api/settings/ha/internal/config/sync/" + new Date().getTime();
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertInternalServerError(syncResult, "Error importing platform instances");
  }

  @Test
  public void testSyncInstancesEmptyArray() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(config.getUUID(), true, false);
    String uri = "/api/settings/ha/internal/config/sync/" + new Date().getTime();
    ArrayNode body = Json.newArray();
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(syncResult);
  }

  @Test
  public void testSyncInstancesSuccess() {
    JsonNode haConfigJson = createHAConfig();
    String leaderAddr = "http://abcdef.com";
    String clusterKey = createInstances(haConfigJson, leaderAddr);
    String uri;
    uri = "/api/settings/ha/internal/config";
    Result getResult = FakeApiHelper.doRequestWithHAToken("GET", uri, clusterKey);
    assertOk(getResult);
    JsonNode getResponse = Json.parse(contentAsString(getResult));
    ArrayNode instancesJson = (ArrayNode) getResponse.get("instances");
    assertEquals(instancesJson.size(), 2);
    PlatformInstance local = Json.fromJson(instancesJson.get(0), PlatformInstance.class);
    assertEquals(local.getAddress(), "http://abc.com");
    assertTrue(local.getIsLocal());
    assertFalse(local.getIsLeader());
    PlatformInstance remoteLeader = Json.fromJson(instancesJson.get(1), PlatformInstance.class);
    assertEquals(remoteLeader.getAddress(), leaderAddr);
    assertTrue(remoteLeader.getIsLeader());
    assertFalse(remoteLeader.getIsLocal());
  }

  private String createInstances(JsonNode haConfigJson, String leaderAddr) {
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    String clusterKey = haConfigJson.get("cluster_key").asText();
    JsonNode localInstance = createPlatformInstance(configUUID, true, false);
    ArrayNode body = Json.newArray();
    PlatformInstance i1 = new PlatformInstance();
    i1.setUUID(UUID.randomUUID());
    i1.setConfig(config);
    i1.setAddress(leaderAddr);
    i1.setIsLeader(true);
    i1.setIsLocal(false);
    body.add(Json.toJson(i1));
    body.add(localInstance);
    String uri = "/api/settings/ha/internal/config/sync/" + new Date().getTime();
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(syncResult);
    return clusterKey;
  }

  @Test
  public void testSyncBackups_no_known_leader_retained() throws IOException {
    String clusterKey = createHAConfig().get("cluster_key").asText("");
    // no instances set so we have no valid leader
    // we always retain invalid all backups even when leader does not match local POV.

    String leaderAddr = "http://node0";
    File fakeDump = createFakeDump();
    Result result = sendBackupSyncRequest(clusterKey, leaderAddr, fakeDump, leaderAddr);
    assertOk(result);

    String storagePath = app.config().getString(PlatformReplicationHelper.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationHelper.REPLICATION_DIR,
      new URL(leaderAddr).getHost(),
      fakeDump.getName()
    ).toFile();
    assertTrue(uploadedFile.exists());
    assertTrue(FileUtils.contentEquals(uploadedFile, fakeDump));
  }

  @Test
  public void testSyncBackups_differentLeader_retained() throws IOException {
    JsonNode haConfigJson = createHAConfig();
    String knownLeaderAddr = "http://leader.yw.com";
    String clusterKey = createInstances(haConfigJson, knownLeaderAddr);
    File fakeDump = createFakeDump();
    String requestFromLeader = "http://different.leader";
    Result result = sendBackupSyncRequest(clusterKey, requestFromLeader, fakeDump,
      requestFromLeader);
    assertOk(result);

    String storagePath = app.config().getString(PlatformReplicationHelper.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationHelper.REPLICATION_DIR,
      new URL(requestFromLeader).getHost(),
      fakeDump.getName()
    ).toFile();
    assertTrue(uploadedFile.getAbsolutePath(), uploadedFile.exists());

    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result listResult = FakeApiHelper.doRequestWithAuthToken("GET",
      "/api/settings/ha/config/" + configUUID + "/backup/list", user.createAuthToken());
    JsonNode jsonNode = Json.parse(contentAsString(listResult));
    assertEquals(0, jsonNode.size());

    listResult = FakeApiHelper.doRequestWithAuthToken("GET",
      "/api/settings/ha/config/" + configUUID + "/backup/list?leader=" + requestFromLeader,
      user.createAuthToken());
    jsonNode = Json.parse(contentAsString(listResult));
    assertEquals(1, jsonNode.size());
    assertEquals(fakeDump.getName(), jsonNode.get(0).asText());
  }

  @Test
  public void testSyncBackups_valid() throws IOException {
    JsonNode haConfigJson = createHAConfig();
    String leaderAddr = "http://leader.yw.com";
    String clusterKey = createInstances(haConfigJson, leaderAddr);
    File fakeDump = createFakeDump();
    Result result = sendBackupSyncRequest(clusterKey, leaderAddr, fakeDump, leaderAddr);
    assertOk(result);

    String storagePath = app.config().getString(PlatformReplicationHelper.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationHelper.REPLICATION_DIR,
      new URL(leaderAddr).getHost(),
      fakeDump.getName()
    ).toFile();
    assertTrue(uploadedFile.exists());
    assertTrue(FileUtils.contentEquals(uploadedFile, fakeDump));

    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result listResult = FakeApiHelper.doRequestWithAuthToken("GET",
      "/api/settings/ha/config/" + configUUID + "/backup/list", user.createAuthToken());
    JsonNode jsonNode = Json.parse(contentAsString(listResult));
    assertEquals(1, jsonNode.size());
    assertEquals(fakeDump.getName(), jsonNode.get(0).asText());
  }

  @Test
  public void testSyncBackups_badRequest() throws IOException {
    JsonNode haConfigJson = createHAConfig();
    String leaderAddr = "http://leader.yw.com";
    String clusterKey = createInstances(haConfigJson, leaderAddr);
    File fakeDump = createFakeDump();
    Result result = sendBackupSyncRequest(
      clusterKey,
      leaderAddr,
      fakeDump,
      "http://different.sender"
    );
    assertBadRequest(
      result,
      "Sender: http://different.sender does not match leader: http://leader.yw.com"
    );

    String storagePath = app.config().getString(PlatformReplicationHelper.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationHelper.REPLICATION_DIR,
      leaderAddr,
      fakeDump.getName()
    ).toFile();
    assertFalse(uploadedFile.getAbsolutePath(), uploadedFile.exists());
  }

  private Result sendBackupSyncRequest(String clusterKey, String leaderAddr, File file,
                                       String senderAddr) {

    return Helpers.route(app,
      fakeRequest()
        .method("POST")
        .uri(UPLOAD_ENDPOINT)
        .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, clusterKey)
        .bodyMultipart(
          PlatformInstanceClient.buildPartsList(
            file,
            ImmutableMap.of("leader", leaderAddr, "sender", senderAddr)),
          singletonTemporaryFileCreator(),
          mat));
  }

  @Test
  public void testSyncInstancesFromStaleLeader() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    config.updateLastFailover();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    String clusterKey = haConfigJson.get("cluster_key").asText();
    JsonNode localInstance = createPlatformInstance(configUUID, true, false);
    ArrayNode body = Json.newArray();
    PlatformInstance i1 = new PlatformInstance();
    i1.setUUID(UUID.randomUUID());
    i1.setConfig(config);
    i1.setAddress("http://abcdef.com");
    i1.setIsLeader(true);
    i1.setIsLocal(false);
    body.add(Json.toJson(i1));
    body.add(localInstance);
    String uri = "/api/settings/ha/internal/config/sync/" + 0;
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertBadRequest(syncResult, "Cannot import instances from stale leader");
  }

  @Test
  public void testDemoteLocalInstanceSuccess() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUUID();
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(configUUID, true, true);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertTrue(config.isLocalLeader());
    String uri = "/api/settings/ha/internal/config/demote/" + new Date().getTime();
    JsonNode body = Json.newObject().put("leader_address", "http://1.2.3.4");
    Result demoteResult = FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(demoteResult);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertFalse(config.isLocalLeader());
  }

  @Test
  public void testDemoteLocalInstanceStaleFailover() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUUID();
    String clusterKey = haConfigJson.get("cluster_key").asText();
    Date staleFailover = new Date();
    createPlatformInstance(configUUID, true, true);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertTrue(config.isLocalLeader());
    String uri = "/api/settings/ha/internal/config/demote/" + staleFailover.getTime();
    JsonNode body = Json.newObject().put("leader_address", "http://1.2.3.4");
    Result demoteResult = FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertBadRequest(demoteResult, "Rejecting demote request from stale leader");
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertTrue(config.isLocalLeader());
  }

  @Test
  public void testDemoteLocalInstanceSuccessAlreadyFollower() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUUID();
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(configUUID, true, false);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertFalse(config.isLocalLeader());
    String uri = "/api/settings/ha/internal/config/demote/" + new Date().getTime();
    JsonNode body = Json.newObject().put("leader_address", "http://1.2.3.4");
    Result demoteResult = FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(demoteResult);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertFalse(config.isLocalLeader());
  }
}
