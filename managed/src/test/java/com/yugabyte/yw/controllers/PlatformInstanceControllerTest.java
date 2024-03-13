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
import static java.util.stream.Collectors.toList;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import ch.qos.logback.classic.spi.ILoggingEvent;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.ha.PlatformInstanceClient;
import com.yugabyte.yw.common.ha.PlatformInstanceClientFactory;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.Users;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import java.util.function.Predicate;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import play.inject.Bindings;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;

public class PlatformInstanceControllerTest extends FakeDBApplication {
  Customer customer;
  Users user;
  private PlatformInstanceClientFactory mockPlatformInstanceClientFactory =
      mock(PlatformInstanceClientFactory.class);

  @Override
  protected GuiceApplicationBuilder configureApplication(GuiceApplicationBuilder builder) {
    return super.configureApplication(
        builder.overrides(
            Bindings.bind(PlatformInstanceClientFactory.class)
                .toInstance(mockPlatformInstanceClientFactory)));
  }

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
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

  private Result createPlatformInstance(
      UUID configUUID, String address, boolean isLocal, boolean isLeader) {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config/" + configUUID.toString() + "/instance";
    JsonNode body =
        Json.newObject()
            .put("address", address)
            .put("is_local", isLocal)
            .put("is_leader", isLeader);
    return doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
  }

  private Result deletePlatformInstance(UUID configUUID, UUID instanceUUID) {
    String authToken = user.createAuthToken();
    String uri =
        "/api/settings/ha/config/" + configUUID.toString() + "/instance/" + instanceUUID.toString();
    return doRequestWithAuthToken("DELETE", uri, authToken);
  }

  private Result demotePlatformInstance(UUID configUUID, UUID instanceUUID) {
    String authToken = user.createAuthToken();
    String uri =
        "/api/settings/ha/config/"
            + configUUID.toString()
            + "/instance/"
            + instanceUUID.toString()
            + "/demote";
    return doRequestWithAuthToken("POST", uri, authToken);
  }

  @Test
  public void testCreatePlatformInstanceLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, false);
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
            () -> createPlatformInstance(configUUID, "http://abc.com/", false, true));
    assertBadRequest(
        createResult,
        "Cannot create a remote platform instance before creating local platform instance");
  }

  @Test
  public void testCreateRemotePlatformInstanceWithLocalFollower() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, false);
    assertOk(createResult);
    createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abcdef.com/", false, false));
    assertBadRequest(
        createResult, "Cannot create a remote platform instance on a follower platform instance");
  }

  @Test
  public void testCreatePlatformInstanceWithLocalLeader() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(true);
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://abcdef.com/", false, false);
    assertOk(createResult);
  }

  @Test
  public void testCreateRemotePlatformInstanceNoConnection() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(true);
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(false);
    createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abcdef.com/", false, false));
    assertBadRequest(
        createResult, "Standby YBA instance is unreachable or hasn't been configured yet");
  }

  @Test
  public void testCreateMultipleLocalPlatformInstances() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abcdef.com/", true, false));
    assertBadRequest(createResult, "Local platform instance already exists");
  }

  @Test
  public void testCreateMultipleLeaderPlatformInstances() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult =
        assertPlatformException(
            () -> createPlatformInstance(configUUID, "http://abcdef.com/", false, true));
    assertBadRequest(createResult, "Leader platform instance already exists");
  }

  @Test
  public void testDeleteLocalPlatformInstanceWithLocalLeader() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
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
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(true);
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://abcdef.com/", false, false);
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
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, false);
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
            () -> createPlatformInstance(configUUID, "http://abc.com::abc/", true, false));
    assertBadRequest(createResult, "");
    JsonNode node = Json.parse(contentAsString(createResult));
    assertErrorNodeValue(node, "address", "must be a valid URL");
  }

  @Test
  public void testLongAddress() {
    JsonNode haConfigJson = createHAConfig();
    UUID configUUID = UUID.fromString(haConfigJson.get("uuid").asText());

    // just within limits. DNS length 254 (which is <= 255)
    String shortAddress = "http://" + StringUtils.repeat("abcdefghi.", 25) + ".com/";
    Result createResult = createPlatformInstance(configUUID, shortAddress, true, true);
    assertOk(createResult);

    // Exceed dns length 264 (total address length is 272 > 263)
    final String expectedError = "Maximum length is 263";
    String longAddress = "http://" + StringUtils.repeat("abcdefghi.", 26) + ".com/";
    createResult =
        assertPlatformException(() -> createPlatformInstance(configUUID, longAddress, true, false));
    assertBadRequest(createResult, "");
    JsonNode node = Json.parse(contentAsString(createResult));
    assertErrorNodeValue(node, "address", expectedError);
  }

  @Test
  public void testPromoteInstanceNoConnection() {
    when(mockShellProcessHandler.run(anyList(), anyMap(), anyBoolean()))
        .thenReturn(new ShellResponse());
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(false);
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    PlatformInstance instance = Json.fromJson(instanceJson, PlatformInstance.class);
    UUID instanceUUID = instance.getUuid();
    PlatformInstance remoteLeader = PlatformInstance.create(config, "http://def.com/", true, false);
    remoteLeader.save();
    String uri =
        String.format(
            "/api/settings/ha/config/%s/instance/%s/promote?isForcePromote=false",
            configUUID.toString(), instanceUUID.toString());
    String authToken = user.createAuthToken();
    JsonNode body = Json.newObject().put("backup_file", "/foo/bar");
    Result promoteResult =
        assertPlatformException(() -> doRequestWithAuthTokenAndBody("POST", uri, authToken, body));
    assertBadRequest(
        promoteResult, "Could not connect to current leader and force parameter not set.");
  }

  @Test
  public void testPromoteInstanceBackupFileNonexistent() {
    when(mockShellProcessHandler.run(anyList(), anyMap(), anyBoolean()))
        .thenReturn(new ShellResponse());
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(true);
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    PlatformInstance instance = Json.fromJson(instanceJson, PlatformInstance.class);
    UUID instanceUUID = instance.getUuid();
    PlatformInstance remoteLeader = PlatformInstance.create(config, "http://def.com/", true, false);
    remoteLeader.save();
    String uri =
        String.format(
            "/api/settings/ha/config/%s/instance/%s/promote",
            configUUID.toString(), instanceUUID.toString());
    String authToken = user.createAuthToken();
    JsonNode body = Json.newObject().put("backup_file", "/foo/bar");
    Result promoteResult =
        assertPlatformException(() -> doRequestWithAuthTokenAndBody("POST", uri, authToken, body));
    assertBadRequest(promoteResult, "Could not find backup file");
  }

  @Test
  public void testPromoteInstanceNoLeader() {
    when(mockShellProcessHandler.run(anyList(), anyMap(), anyBoolean()))
        .thenReturn(new ShellResponse());
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    PlatformInstance instance = Json.fromJson(instanceJson, PlatformInstance.class);
    UUID instanceUUID = instance.getUuid();
    String uri =
        String.format(
            "/api/settings/ha/config/%s/instance/%s/promote",
            configUUID.toString(), instanceUUID.toString());
    String authToken = user.createAuthToken();
    JsonNode body = Json.newObject().put("backup_file", "/foo/bar");
    Result promoteResult =
        assertPlatformException(() -> doRequestWithAuthTokenAndBody("POST", uri, authToken, body));
    assertBadRequest(promoteResult, "Could not find leader instance");
  }

  @Test
  public void testPromoteLocalInstance() throws InterruptedException {

    List<ILoggingEvent> logsEvents =
        TestHelper.captureLogEventsFor(PlatformReplicationManager.class);

    when(mockShellProcessHandler.run(anyList(), anyMap(), anyBoolean()))
        .thenReturn(new ShellResponse());
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(true);

    PlatformReplicationManager platformReplicationManager =
        app.injector().instanceOf(PlatformReplicationManager.class);

    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://def.com/", false, false);
    assertOk(createResult);
    JsonNode instanceJson = Json.parse(contentAsString(createResult));
    PlatformInstance instance = Json.fromJson(instanceJson, PlatformInstance.class);

    platformReplicationManager.promoteLocalInstance(instance);

    platformReplicationManager.setFrequencyStartAndEnable(Duration.ofSeconds(1));

    Thread.sleep(1200);
    assertNotLogging(logsEvents, PlatformReplicationManager.NO_LOCAL_INSTANCE_MSG);
  }

  public static void assertNotLogging(List<ILoggingEvent> loggingEvents, String message) {
    final Predicate<ILoggingEvent> iLoggingEventPredicate =
        iLoggingEvent -> iLoggingEvent.getFormattedMessage().contains(message);
    assertFalse(
        "Found logs: "
            + loggingEvents.stream().filter(iLoggingEventPredicate).collect(toList()).toString(),
        loggingEvents.stream().anyMatch(iLoggingEventPredicate));
  }
}
