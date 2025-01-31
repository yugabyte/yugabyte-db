// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ConfigureYCQLFormData;
import com.yugabyte.yw.forms.ConfigureYSQLFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class ConfigureDBApiLocalTest extends LocalProviderUniverseTestBase {

  private final String YCQL_PASSWORD = "Pass@123";
  private final Map<String, String> connectionPoolingGflags =
      Map.of(
          "ysql_conn_mgr_max_client_connections",
          "10001",
          "ysql_conn_mgr_idle_time",
          "30",
          "ysql_conn_mgr_stats_interval",
          "20");

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair(150, 180);
  }

  private Result configureYSQL(ConfigureYSQLFormData formData, UUID universeUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/universes/" + universeUUID + "/configure/ysql",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  private Result configureYCQL(ConfigureYCQLFormData formData, UUID universeUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/universes/" + universeUUID + "/configure/ycql",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  private void enableConnectionPoolingFlag() {}

  @Test
  public void testConfigureYSQL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    // Enable YSQL Auth for the universe.
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    // Enable YSQL Auth for the universe.
    ConfigureYSQLFormData formData = new ConfigureYSQLFormData();
    formData.enableYSQL = true;
    formData.enableYSQLAuth = true;
    formData.ysqlPassword = "Pass@123";

    Result result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    initYSQL(universe, "", true);
    verifyYSQL(universe, false, YUGABYTE_DB, "", true);

    // Disable YSQL Auth for the universe.
    formData.enableYSQLAuth = false;

    result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe);

    int newYsqlServerRpcPort = 5678;
    int newYsqlServerHttpPort = 13001;
    formData.communicationPorts.ysqlServerRpcPort = newYsqlServerRpcPort;
    formData.communicationPorts.ysqlServerHttpPort = newYsqlServerHttpPort;
    formData.ysqlPassword = "";
    result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe);
    assertEquals(
        newYsqlServerRpcPort, universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort);
    assertEquals(
        newYsqlServerHttpPort, universe.getUniverseDetails().communicationPorts.ysqlServerHttpPort);
    for (NodeDetails node : universe.getNodes()) {
      assertEquals(newYsqlServerRpcPort, node.ysqlServerRpcPort);
      assertEquals(newYsqlServerHttpPort, node.ysqlServerHttpPort);
    }
  }

  @Test
  public void testConfigureYCQL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);

    // Enable YCQL Auth for the universe.
    ConfigureYCQLFormData formData = new ConfigureYCQLFormData();
    formData.enableYCQL = true;
    formData.enableYCQLAuth = true;
    formData.ycqlPassword = YCQL_PASSWORD;

    Result result = configureYCQL(formData, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    initYCQL(universe, true, YCQL_PASSWORD);
    verifyYCQL(universe, true, YCQL_PASSWORD);

    // Disable YCQL Auth for the universe.
    formData.enableYCQLAuth = false;

    result = configureYCQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYCQL(universe);

    int newYqlServerRpcPort = 9078;
    int newYqlServerHttpPort = 12001;
    formData.communicationPorts.yqlServerHttpPort = newYqlServerHttpPort;
    formData.communicationPorts.yqlServerRpcPort = newYqlServerRpcPort;
    formData.ycqlPassword = "";
    result = configureYCQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYCQL(universe);
    assertEquals(
        newYqlServerRpcPort, universe.getUniverseDetails().communicationPorts.yqlServerRpcPort);
    assertEquals(
        newYqlServerHttpPort, universe.getUniverseDetails().communicationPorts.yqlServerHttpPort);
    for (NodeDetails node : universe.getNodes()) {
      assertEquals(newYqlServerRpcPort, node.yqlServerRpcPort);
      assertEquals(newYqlServerHttpPort, node.yqlServerHttpPort);
    }
  }

  @Test
  public void testEnableConnectionPooling() throws InterruptedException {
    // Set the connection pooling flag to true.
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.allowConnectionPooling.getKey(),
        "true",
        true);

    // Create universe with YSQL Auth enabled.
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.enableYSQLAuth = true;
    userIntent.ysqlPassword = "Pass@123";
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);

    // Enable Connection Pooling for the universe.
    ConfigureYSQLFormData formData = new ConfigureYSQLFormData();
    formData.enableYSQL = true;
    formData.enableYSQLAuth = true;
    formData.enableConnectionPooling = true;
    formData.communicationPorts.internalYsqlServerRpcPort = 6434;
    formData.communicationPorts.ysqlServerRpcPort = 5434;
    formData.connectionPoolingGflags = connectionPoolingGflags;

    Result result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableConnectionPooling);
    assertEquals(5434, universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort);
    assertEquals(6434, universe.getUniverseDetails().communicationPorts.internalYsqlServerRpcPort);
    verifyYSQL(universe);
    verifyYSQL(
        universe,
        false,
        YUGABYTE_DB,
        "some_table" /* tableName */,
        true /* authEnabled */,
        true /* cpEnabled */);
    compareConnectionPoolingGFlags(universe);
  }

  private void compareConnectionPoolingGFlags(Universe universe) {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseTaskBase.ServerType serverType = UniverseTaskBase.ServerType.TSERVER;
    Map<String, String> gflags =
        userIntent.specificGFlags.getPerProcessFlags().value.get(serverType);
    for (NodeDetails node : universe.getNodes()) {
      UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
      if (node.isTserver) {
        Map<String, String> varz = getVarz(node, universe, serverType);
        log.info(
            "expected gflags for node {} server type {} are {}", node.nodeName, serverType, gflags);
        Map<String, String> gflagsOnDisk = getDiskFlags(node, universe, serverType);
        connectionPoolingGflags.forEach(
            (k, v) -> {
              String expectedValue = v;
              String actual = varz.getOrDefault(k, "?????");
              log.info("Actual gflag '{}', value in memory is '{}'.", k, actual);
              assertEquals("Compare in memory gflag " + k, expectedValue, actual);
              String onDisk = gflagsOnDisk.getOrDefault(k, "?????");
              log.info("Actual gflag '{}', value on disk is '{}'.", k, onDisk);
              assertEquals("Compare on disk gflag " + k, expectedValue, onDisk);
              String universeDetailsGflag = gflags.getOrDefault(k, "?????");
              log.info(
                  "Actual gflag '{}', value in universe details is '{}'.", k, universeDetailsGflag);
              assertEquals(
                  "Compare universe details gflag " + k, expectedValue, universeDetailsGflag);
            });
      }
    }
  }
}
