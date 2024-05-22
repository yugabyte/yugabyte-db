// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ConfigureYCQLFormData;
import com.yugabyte.yw.forms.ConfigureYSQLFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class ConfigureDBApiLocalTest extends LocalProviderUniverseTestBase {

  private final String YCQL_PASSWORD = "Pass@123";

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
    taskInfo = CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
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
}
