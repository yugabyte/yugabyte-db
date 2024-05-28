// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.HaConfigStates.GlobalState;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ha.PlatformInstanceClient;
import com.yugabyte.yw.common.ha.PlatformInstanceClientFactory;
import java.util.Date;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import play.inject.Bindings;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class HighAvailabilityConfigTest extends FakeDBApplication {

  private static final String GET_CONFIG_ENDPOINT = "/api/settings/ha/internal/config";

  private Users user;
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
    Customer customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    PlatformInstanceClient mockPlatformInstanceClient = mock(PlatformInstanceClient.class);
    when(mockPlatformInstanceClientFactory.getClient(anyString(), anyString(), anyMap()))
        .thenReturn(mockPlatformInstanceClient);
    when(mockPlatformInstanceClient.testConnection()).thenReturn(true);
    RuntimeConfigEntry.upsertGlobal("yb.ha.replication_frequency", "1 minute");
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

  private Result createPlatformInstance(
      UUID configUUID, String address, boolean isLocal, boolean isLeader) {
    String authToken = user.createAuthToken();
    String uri = "/api/settings/ha/config/" + configUUID.toString() + "/instance";
    JsonNode body =
        Json.newObject()
            .put("address", address)
            .put("is_leader", isLeader)
            .put("is_local", isLocal);
    return doRequestWithAuthTokenAndBody("POST", uri, authToken, body);
  }

  @Test
  public void testLeaderHaGlobalConfigStateNoReplicas() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    haConfigJson = getHAConfig();
    config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    assertEquals(GlobalState.NoReplicas, config.computeGlobalState());
  }

  @Test
  public void testLeaderHaGlobalConfigStateAwaitingReplicas() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://def.com/", false, false);
    assertOk(createResult);
    config = HighAvailabilityConfig.get(configUUID).get();
    assertEquals(GlobalState.AwaitingReplicas, config.computeGlobalState());
  }

  @Test
  public void testLeaderHaGlobalConfigStateOperational() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://def.com/", false, false);
    assertOk(createResult);
    JsonNode remotePlatformJson = Json.parse(contentAsString(createResult));
    Optional<PlatformInstance> remote =
        PlatformInstance.get(UUID.fromString(remotePlatformJson.get("uuid").asText()));
    remote.get().updateLastBackup(new Date());
    config = HighAvailabilityConfig.get(configUUID).get();
    assertEquals(GlobalState.Operational, config.computeGlobalState());
  }

  @Test
  public void testLeaderHaGlobalConfigStateError() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);
    createResult = createPlatformInstance(configUUID, "http://def.com/", false, false);
    assertOk(createResult);
    JsonNode remotePlatformJson = Json.parse(contentAsString(createResult));
    Optional<PlatformInstance> remote =
        PlatformInstance.get(UUID.fromString(remotePlatformJson.get("uuid").asText()));
    remote.get().updateLastBackup(new Date(System.currentTimeMillis() - (30 * 60 * 1000)));
    config = HighAvailabilityConfig.get(configUUID).get();
    assertEquals(GlobalState.Error, config.computeGlobalState());
  }

  @Test
  public void testLeaderHaGlobalConfigStateWarning() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createResult);

    createResult = createPlatformInstance(configUUID, "http://def.com/", false, false);
    assertOk(createResult);
    JsonNode remotePlatformJson = Json.parse(contentAsString(createResult));
    Optional<PlatformInstance> remote =
        PlatformInstance.get(UUID.fromString(remotePlatformJson.get("uuid").asText()));
    remote.get().updateLastBackup(new Date(System.currentTimeMillis() - (30 * 60 * 1000)));

    createResult = createPlatformInstance(configUUID, "http://ghi.com/", false, false);
    assertOk(createResult);
    remotePlatformJson = Json.parse(contentAsString(createResult));
    remote = PlatformInstance.get(UUID.fromString(remotePlatformJson.get("uuid").asText()));
    remote.get().updateLastBackup(new Date());

    config = HighAvailabilityConfig.get(configUUID).get();
    assertEquals(GlobalState.Warning, config.computeGlobalState());
  }

  @Test
  public void testFollowerHaGlobalConfigStateAwaitingReplicas() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createResult = createPlatformInstance(configUUID, "http://abc.com/", true, false);
    assertOk(createResult);

    config = HighAvailabilityConfig.get(configUUID).get();
    assertEquals(GlobalState.AwaitingReplicas, config.computeGlobalState());
  }

  @Test
  public void testFollowerHaGlobalConfigStateOperational() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createLocalResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createLocalResult);

    Result createRemoteResult = createPlatformInstance(configUUID, "http://def.com/", false, false);
    assertOk(createRemoteResult);
    JsonNode remotePlatformJson = Json.parse(contentAsString(createRemoteResult));
    PlatformInstance remote =
        PlatformInstance.get(UUID.fromString(remotePlatformJson.get("uuid").asText())).get();
    JsonNode localPlatformJson = Json.parse(contentAsString(createLocalResult));
    PlatformInstance local =
        PlatformInstance.get(UUID.fromString(localPlatformJson.get("uuid").asText())).get();
    local.demote();
    remote.promote();

    local.updateLastBackup(new Date());
    config = HighAvailabilityConfig.get(configUUID).get();
    assertEquals(GlobalState.StandbyConnected, config.computeGlobalState());
  }

  @Test
  public void testFollowerHaGlobalConfigStateError() {
    JsonNode haConfigJson = createHAConfig();
    HighAvailabilityConfig config = Json.fromJson(haConfigJson, HighAvailabilityConfig.class);
    UUID configUUID = config.getUuid();
    Result createLocalResult = createPlatformInstance(configUUID, "http://abc.com/", true, true);
    assertOk(createLocalResult);

    Result createRemoteResult = createPlatformInstance(configUUID, "http://def.com/", false, false);
    assertOk(createRemoteResult);
    JsonNode remotePlatformJson = Json.parse(contentAsString(createRemoteResult));
    PlatformInstance remote =
        PlatformInstance.get(UUID.fromString(remotePlatformJson.get("uuid").asText())).get();
    JsonNode localPlatformJson = Json.parse(contentAsString(createLocalResult));
    PlatformInstance local =
        PlatformInstance.get(UUID.fromString(localPlatformJson.get("uuid").asText())).get();
    local.demote();
    remote.promote();

    local.updateLastBackup(new Date(System.currentTimeMillis() - (30 * 60 * 1000)));
    config = HighAvailabilityConfig.get(configUUID).get();
    assertEquals(GlobalState.StandbyDisconnected, config.computeGlobalState());
  }
}
