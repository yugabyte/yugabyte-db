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

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static junit.framework.TestCase.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.libs.Files.singletonTemporaryFileCreator;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ha.PlatformInstanceClient;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Users;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.Random;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

public class InternalHAControllerTest extends FakeDBApplication {

  private static final String UPLOAD_ENDPOINT = "/api/settings/ha/internal/upload";
  private static final String SYNC_ENDPOINT = "/api/settings/ha/internal/config/sync/";
  private static final String DEMOTE_ENDPOINT = "/api/settings/ha/internal/config/demote/";
  private static final String GET_CONFIG_ENDPOINT = "/api/settings/ha/internal/config";

  private Users user;

  @Rule public TemporaryFolder folder = new TemporaryFolder();

  @Before
  public void setup() {
    Customer customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  @After
  public void tearDown() throws IOException {
    String storagePath = AppConfigHelper.getStoragePath();
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
    Result createClusterKeyResult =
        doRequestWithAuthToken("GET", "/api/settings/ha/generate_key", authToken);
    assertOk(createClusterKeyResult);

    return Json.parse(contentAsString(createClusterKeyResult)).get("cluster_key").asText();
  }

  private JsonNode createHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    String clusterKey = createClusterKey();
    JsonNode body = Json.newObject().put("cluster_key", clusterKey);
    Result createResult = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
    assertOk(createResult);

    return Json.parse(contentAsString(createResult));
  }

  private JsonNode getHAConfig() {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config";
    Result getResult = doRequestWithAuthToken("GET", uri, authToken);
    assertOk(getResult);
    return Json.parse(contentAsString(getResult));
  }

  private JsonNode createPlatformInstance(UUID configUUID, boolean isLocal, boolean isLeader) {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config/" + configUUID.toString() + "/instance";
    JsonNode body =
        Json.newObject()
            .put("address", "http://abc.com")
            .put("is_local", isLocal)
            .put("is_leader", isLeader);
    Result createResult = doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
    assertOk(createResult);

    return Json.parse(contentAsString(createResult));
  }

  @Test
  public void testGetHAConfigNoneExist() {
    String clusterKey = createClusterKey();
    Result getResult = doRequestWithHAToken("GET", GET_CONFIG_ENDPOINT, clusterKey);
    assertEquals(FORBIDDEN, getResult.status());
    assertEquals(contentAsString(getResult), "Unable to authenticate request");
  }

  @Test
  public void testGetHAConfigInvalidClusterKey() {
    createHAConfig();
    String incorrectClusterKey = createClusterKey();
    Result getResult = doRequestWithHAToken("GET", GET_CONFIG_ENDPOINT, incorrectClusterKey);
    assertEquals(FORBIDDEN, getResult.status());
    assertEquals(contentAsString(getResult), "Unable to authenticate request");
  }

  @Test
  public void testGetHAConfigValidClusterKey() {
    String correctClusterKey = createHAConfig().get("cluster_key").asText("");
    Result getResult = doRequestWithHAToken("GET", GET_CONFIG_ENDPOINT, correctClusterKey);
    assertOk(getResult);
    JsonNode getResponse = Json.parse(contentAsString(getResult));
    String getClusterKey = getResponse.get("cluster_key").asText();
    assertEquals(correctClusterKey, getClusterKey);
  }

  @Test
  public void testSyncInstancesNoLocalInstances() {
    JsonNode haConfigJson = createHAConfig();
    String clusterKey = haConfigJson.get("cluster_key").asText();
    String uri = SYNC_ENDPOINT + new Date().getTime();
    Result syncResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertBadRequest(syncResult, "No local instance configured");
  }

  @Test
  public void testSyncInstancesNoLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(config.getUuid(), true, true);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String uri = SYNC_ENDPOINT + config.getLastFailover().getTime();
    Result syncResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject());
    assertBadRequest(syncResult, "Cannot import instances for a leader");
  }

  @Test
  public void testSyncInstancesInvalidJson() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(config.getUuid(), true, false);
    String uri = SYNC_ENDPOINT + new Date().getTime();
    Result syncResult =
        assertPlatformException(
            () -> doRequestWithHATokenAndBody("PUT", uri, clusterKey, Json.newObject()));
    assertBadRequest(syncResult, "Failed to parse List<PlatformInstance> object: {}");
  }

  @Test
  public void testSyncInstancesEmptyArray() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(config.getUuid(), true, false);
    assertNoLocalInstanceErrorOnSync(clusterKey, new ArrayList<>());
  }

  public void assertNoLocalInstanceErrorOnSync(
      String clusterKey, List<PlatformInstance> instancesToImport) {
    JsonNode body = Json.toJson(instancesToImport);
    String uri = SYNC_ENDPOINT + new Date().getTime();
    Result syncResult =
        assertPlatformException(() -> doRequestWithHATokenAndBody("PUT", uri, clusterKey, body));
    List<String> addresses =
        instancesToImport.stream().map(PlatformInstance::getAddress).collect(Collectors.toList());
    assertBadRequest(syncResult, "(http://abc.com) not found in Sync request " + addresses);
  }

  @Test
  public void testSyncInstancesLeaderAlreadyExists() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    JsonNode localInstanceJson = createPlatformInstance(config.getUuid(), true, false);
    PlatformInstance localInstance = Json.fromJson(localInstanceJson, PlatformInstance.class);
    PlatformInstance previousLeader =
        PlatformInstance.create(config, "http://ghi.com", true, false);
    PlatformInstance newLeader = PlatformInstance.create(config, "http://jkl.com", false, false);
    previousLeader.setIsLeader(false);
    newLeader.setIsLeader(true);
    List<PlatformInstance> instancesToImport = new ArrayList<>();
    instancesToImport.add(newLeader);
    instancesToImport.add(previousLeader);
    instancesToImport.add(localInstance);
    JsonNode body = Json.toJson(instancesToImport);
    String uri = SYNC_ENDPOINT + new Date().getTime();
    Result syncResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(syncResult);
    Result getResult = doRequestWithHAToken("GET", GET_CONFIG_ENDPOINT, clusterKey);
    assertOk(getResult);
    JsonNode getResponse = Json.parse(contentAsString(getResult));
    ArrayNode instancesJson = (ArrayNode) getResponse.get("instances");
    assertEquals(instancesJson.size(), 3);
    config = Json.fromJson(getResponse, HighAvailabilityConfig.class);
    Optional<PlatformInstance> leader = config.getLeader();
    assertTrue(leader.isPresent());
    assertEquals(leader.get().getAddress(), "http://jkl.com");
  }

  @Test
  public void testSyncInstancesNoneMatchingLocalAddress() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    String clusterKey = haConfigJson.get("cluster_key").asText();
    PlatformInstance localInstance =
        Json.fromJson(
            createPlatformInstance(config.getUuid(), true, false), PlatformInstance.class);
    PlatformInstance leader = PlatformInstance.create(config, "http://ghi.com", true, false);

    localInstance.setAddress(localInstance.getAddress() + ":9000");
    List<PlatformInstance> instancesToImport = new ArrayList<>();
    instancesToImport.add(leader);
    instancesToImport.add(localInstance);
    assertNoLocalInstanceErrorOnSync(clusterKey, instancesToImport);
    Result getResult = doRequestWithHAToken("GET", GET_CONFIG_ENDPOINT, clusterKey);
    assertOk(getResult);
    JsonNode getResponse = Json.parse(contentAsString(getResult));
    ArrayNode instancesJson = (ArrayNode) getResponse.get("instances");
    assertEquals(2, instancesJson.size());
    int numLocalInstances = 0;
    for (JsonNode i : instancesJson) {
      PlatformInstance instance = Json.fromJson(i, PlatformInstance.class);
      if (instance.getIsLocal()) {
        numLocalInstances++;
      }
    }
    // we expect exactly one local instance
    assertEquals(1, numLocalInstances);
  }

  @Test
  public void testSyncInstancesSuccess() {
    JsonNode haConfigJson = createHAConfig();
    String leaderAddr = "http://abcdef.com";
    String clusterKey = createInstances(haConfigJson, leaderAddr);
    Result getResult = doRequestWithHAToken("GET", GET_CONFIG_ENDPOINT, clusterKey);
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
    i1.setUuid(UUID.randomUUID());
    i1.setConfig(config);
    i1.setAddress(leaderAddr);
    i1.setIsLeader(true);
    i1.setIsLocal(false);
    body.add(Json.toJson(i1));
    body.add(localInstance);
    String uri = SYNC_ENDPOINT + new Date().getTime();
    Result syncResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(syncResult);
    return clusterKey;
  }

  @Test
  public void testSyncBackups_no_known_leader_retained() throws IOException {
    String clusterKey = createHAConfig().get("cluster_key").asText("");
    // no instances set, so we have no valid leader
    // we always retain invalid all backups even when leader does not match local POV.

    String leaderAddr = "http://node0";
    File fakeDump = createFakeDump();
    Result result = sendBackupSyncRequest(clusterKey, leaderAddr, fakeDump, leaderAddr);
    assertOk(result);

    String storagePath = AppConfigHelper.getStoragePath();
    File uploadedFile =
        Paths.get(
                storagePath,
                PlatformReplicationHelper.REPLICATION_DIR,
                new URL(leaderAddr).getHost(),
                fakeDump.getName())
            .toFile();
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
    Result result =
        sendBackupSyncRequest(clusterKey, requestFromLeader, fakeDump, requestFromLeader);
    assertOk(result);

    String storagePath = AppConfigHelper.getStoragePath();
    File uploadedFile =
        Paths.get(
                storagePath,
                PlatformReplicationHelper.REPLICATION_DIR,
                new URL(requestFromLeader).getHost(),
                fakeDump.getName())
            .toFile();
    assertTrue(uploadedFile.getAbsolutePath(), uploadedFile.exists());

    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result listResult =
        doRequestWithAuthToken(
            "GET",
            "/api/settings/ha/config/" + configUUID + "/backup/list",
            user.createAuthToken());
    JsonNode jsonNode = Json.parse(contentAsString(listResult));
    assertEquals(0, jsonNode.size());

    listResult =
        doRequestWithAuthToken(
            "GET",
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

    String storagePath = AppConfigHelper.getStoragePath();
    File uploadedFile =
        Paths.get(
                storagePath,
                PlatformReplicationHelper.REPLICATION_DIR,
                new URL(leaderAddr).getHost(),
                fakeDump.getName())
            .toFile();
    assertTrue(uploadedFile.exists());
    assertTrue(FileUtils.contentEquals(uploadedFile, fakeDump));

    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result listResult =
        doRequestWithAuthToken(
            "GET",
            "/api/settings/ha/config/" + configUUID + "/backup/list",
            user.createAuthToken());
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
    Result result =
        sendBackupSyncRequest(clusterKey, leaderAddr, fakeDump, "http://different.sender");
    assertBadRequest(
        result, "Sender: http://different.sender does not match leader: http://leader.yw.com");

    String storagePath = AppConfigHelper.getStoragePath();
    File uploadedFile =
        Paths.get(
                storagePath,
                PlatformReplicationHelper.REPLICATION_DIR,
                leaderAddr,
                fakeDump.getName())
            .toFile();
    assertFalse(uploadedFile.getAbsolutePath(), uploadedFile.exists());
  }

  private Result sendBackupSyncRequest(
      String clusterKey, String leaderAddr, File file, String senderAddr) {

    return Helpers.route(
        app,
        fakeRequest()
            .method("POST")
            .uri(UPLOAD_ENDPOINT)
            .header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, clusterKey)
            .bodyMultipart(
                PlatformInstanceClient.buildPartsList(
                    file, ImmutableMap.of("leader", leaderAddr, "sender", senderAddr)),
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
    i1.setUuid(UUID.randomUUID());
    i1.setConfig(config);
    i1.setAddress("http://abcdef.com");
    i1.setIsLeader(true);
    i1.setIsLocal(false);
    body.add(Json.toJson(i1));
    body.add(localInstance);
    String uri = SYNC_ENDPOINT + 0;
    Result syncResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertBadRequest(syncResult, "Cannot import instances from stale leader");
  }

  @Test
  public void testDemoteLocalInstanceSuccess() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(configUUID, true, true);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertTrue(config.isLocalLeader());
    String uri = DEMOTE_ENDPOINT + new Date().getTime();
    JsonNode body = Json.newObject().put("leader_address", "http://1.2.3.4");
    Result demoteResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(demoteResult);
    JsonNode response = Json.parse(contentAsString(demoteResult));
    assertEquals("UNKNOWN", response.get("ybaVersion").asText());
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertFalse(config.isLocalLeader());
  }

  @Test
  public void testDemoteLocalInstanceStaleFailover() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    String clusterKey = haConfigJson.get("cluster_key").asText();
    Date staleFailover = new Date();
    createPlatformInstance(configUUID, true, true);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertTrue(config.isLocalLeader());
    String uri = DEMOTE_ENDPOINT + staleFailover.getTime();
    JsonNode body = Json.newObject().put("leader_address", "http://1.2.3.4");
    Result demoteResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertBadRequest(demoteResult, "Rejecting demote request from stale leader");
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertTrue(config.isLocalLeader());
  }

  @Test
  public void testDemoteLocalInstanceSuccessAlreadyFollower() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    String clusterKey = haConfigJson.get("cluster_key").asText();
    createPlatformInstance(configUUID, true, false);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertFalse(config.isLocalLeader());
    String uri = DEMOTE_ENDPOINT + new Date().getTime();
    JsonNode body = Json.newObject().put("leader_address", "http://1.2.3.4");
    Result demoteResult = doRequestWithHATokenAndBody("PUT", uri, clusterKey, body);
    assertOk(demoteResult);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertFalse(config.isLocalLeader());
  }
}
