// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.TlsConfigUpdateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class CertRotationLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(210, 240);
  }

  private Result updateTLSConfig(Universe universe, TlsConfigUpdateParams formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/update_tls",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  @Test
  public void testSelfSignedCACertRotateYCQL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent("universe-1", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.enableYSQL = false;
    userIntent.enableYCQLAuth = true;
    userIntent.ycqlPassword = "Pass@123";
    Universe universe = createUniverse(userIntent);
    initYCQL(universe, true, "Pass@123");

    userIntent = getDefaultUserIntent("universe-2", false);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.enableYSQL = false;
    userIntent.enableYCQLAuth = true;
    userIntent.ycqlPassword = "Pass@123";
    Universe universe2 = createUniverse(userIntent);
    initYCQL(universe2, true, "Pass@123");

    UUID oldRootCA = universe.getUniverseDetails().rootCA;
    TlsConfigUpdateParams formData = new TlsConfigUpdateParams();
    formData.rootCA = universe2.getUniverseDetails().rootCA;
    formData.enableClientToNodeEncrypt = true;
    formData.enableNodeToNodeEncrypt = true;
    formData.rootAndClientRootCASame = true;
    formData.createNewRootCA = false;
    formData.createNewClientRootCA = false;
    formData.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
    Result result = updateTLSConfig(universe, formData);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyYCQL(universe, true, "Pass@123");

    formData.rootCA = oldRootCA;
    result = updateTLSConfig(universe2, formData);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    verifyYCQL(universe2, true, "Pass@123");
  }

  @Test
  public void testSelfSignedCACertRotateYSQL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);

    UUID newCARootCert =
        certificateHelper.createRootCA(
            confGetter.getStaticConf(),
            universe.getUniverseDetails().nodePrefix,
            customer.getUuid());
    TlsConfigUpdateParams formData = new TlsConfigUpdateParams();
    formData.rootCA = newCARootCert;
    formData.enableClientToNodeEncrypt = true;
    formData.enableNodeToNodeEncrypt = true;
    formData.rootAndClientRootCASame = true;
    formData.createNewRootCA = false;
    formData.createNewClientRootCA = false;
    formData.upgradeOption = UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE;
    Result result = updateTLSConfig(universe, formData);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyYSQL(universe);
  }
}
