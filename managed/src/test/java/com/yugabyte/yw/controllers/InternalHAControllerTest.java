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

import akka.stream.javadsl.FileIO;
import akka.stream.javadsl.Source;
import akka.util.ByteString;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformReplicationManager;
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
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.time.Instant;
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
    String storagePath = app.config().getString(PlatformReplicationManager.STORAGE_PATH_KEY);
    File replicationDir =
      Paths.get(storagePath, PlatformReplicationManager.REPLICATION_DIR).toFile();
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
    String leaderAddr = "abcdef.com";
    String clusterKey = createInstances(haConfigJson, leaderAddr);
    String uri;
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
    assertEquals(remoteLeader.getAddress(), leaderAddr);
    assertTrue(remoteLeader.getIsLeader());
    assertFalse(remoteLeader.getIsLocal());
  }

  private String createInstances(JsonNode haConfigJson, String leaderAddr) {
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
    i1.setAddress(leaderAddr);
    i1.setIsLeader(true);
    i1.setIsLocal(false);
    body.add(Json.toJson(i1));
    body.add(localInstance);
    String uri = "/api/settings/ha/internal/config/" + configUUID;
    Result syncResult =
      FakeApiHelper.doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(syncResult);
    return clusterKey;
  }

  @Test
  public void testSyncBackups_no_known_leader_retained() throws IOException {
    String clusterKey = createHAConfig().get("cluster_key").asText("");
    // no instances set so we have no valid leader
    // we should retain invalid backups under "invalid" subfolder
    assertTrue(app.config().getBoolean(
      PlatformReplicationManager.RETAIN_DATA_FROM_INVALID_SRC_KEY));

    String leaderAddr = "node0";
    File fakeDump = createFakeDump();
    Result result = sendBackupSyncRequest(clusterKey, leaderAddr, fakeDump, leaderAddr);
    String content = Helpers.contentAsString(result);
    assertEquals(content, "File uploaded");

    String storagePath = app.config().getString(PlatformReplicationManager.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationManager.REPLICATION_DIR,
      leaderAddr,
      fakeDump.getName()
    ).toFile();
    assertTrue(uploadedFile.exists());
    assertTrue(FileUtils.contentEquals(uploadedFile, fakeDump));
  }

  @Test
  public void testSyncBackups_differentLeader_retained() throws IOException {
    JsonNode haConfigJson = createHAConfig();
    String knownLeaderAddr = "leader.yw.com";
    String clusterKey = createInstances(haConfigJson, knownLeaderAddr);
    File fakeDump = createFakeDump();
    String requestFromLeader = "different.leader";
    Result result = sendBackupSyncRequest(clusterKey, requestFromLeader, fakeDump,
      requestFromLeader);
    String content = Helpers.contentAsString(result);
    assertEquals(content, "File uploaded");

    String storagePath = app.config().getString(PlatformReplicationManager.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationManager.REPLICATION_DIR,
      requestFromLeader,
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
    String leaderAddr = "leader.yw.com";
    String clusterKey = createInstances(haConfigJson, leaderAddr);
    File fakeDump = createFakeDump();
    Result result = sendBackupSyncRequest(clusterKey, leaderAddr, fakeDump, leaderAddr);
    String content = Helpers.contentAsString(result);
    assertEquals(content, "File uploaded");

    String storagePath = app.config().getString(PlatformReplicationManager.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationManager.REPLICATION_DIR,
      leaderAddr,
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
    String leaderAddr = "leader.yw.com";
    String clusterKey = createInstances(haConfigJson, leaderAddr);
    File fakeDump = createFakeDump();
    Result result = sendBackupSyncRequest(clusterKey, leaderAddr, fakeDump, "different.sender");
    String content = Helpers.contentAsString(result);
    assertEquals(content, "{\"error\":\"Sender: different.sender does not " +
      "match leader: leader.yw.com\"}");

    String storagePath = app.config().getString(PlatformReplicationManager.STORAGE_PATH_KEY);
    File uploadedFile = Paths.get(storagePath,
      PlatformReplicationManager.REPLICATION_DIR,
      leaderAddr,
      fakeDump.getName()
    ).toFile();
    assertFalse(uploadedFile.getAbsolutePath(), uploadedFile.exists());
  }

  private Result sendBackupSyncRequest(String clusterKey, String leaderAddr, File file,
                                       String senderAddr) {
    Http.MultipartFormData.DataPart formData0 =
      new Http.MultipartFormData.DataPart(
        "leader",
        leaderAddr
      );
    Http.MultipartFormData.DataPart formData1 =
      new Http.MultipartFormData.DataPart(
        "sender",
        senderAddr
      );

    Http.MultipartFormData.FilePart<Source<ByteString, ?>> filePart =
      new Http.MultipartFormData.FilePart<>(
        "backup",
        file.getName(),
        "application/octet-stream",
        FileIO.fromFile(file, 1024));

    return Helpers.route(app,
      fakeRequest()
        .method("POST")
        .uri("/api/settings/ha/internal/upload")
        .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, clusterKey)
        .bodyMultipart(ImmutableList.of(formData0, formData1, filePart),
          singletonTemporaryFileCreator(),
          mat));
  }
}
